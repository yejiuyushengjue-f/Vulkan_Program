#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

const std::vector validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

// Required device extensions for presenting rendered images through a swapchain
const std::vector requiredDeviceExtensions = {
    vk::KHRSwapchainExtensionName
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

class HelloTriangleApplication {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow* window = nullptr;
    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;
    vk::raii::Queue graphicsPresentQueue = nullptr;
    uint32_t graphicsPresentQueueFamilyIndex = 0;
    vk::raii::SwapchainKHR swapChain = nullptr;
    std::vector<vk::Image> swapChainImages;
    std::vector<vk::raii::ImageView> swapChainImageViews;
    vk::SurfaceFormatKHR swapChainSurfaceFormat;
    vk::Extent2D swapChainExtent;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline graphicsPipeline = nullptr;
    vk::raii::CommandPool commandPool = nullptr;
    vk::raii::CommandBuffer commandBuffer = nullptr;

    void initWindow()
    {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    }

    void initVulkan()
    {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createGraphicsPipeline();
        createCommandPool();
        createCommandBuffer();
    }

    void mainLoop()
    {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
        }
    }

    void cleanup()
    {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void createInstance()
    {
        constexpr vk::ApplicationInfo appInfo{
            .pApplicationName = "Hello Triangle",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "No Engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = vk::ApiVersion14,
        };

        // Get required validation layers
        std::vector<const char*> requiredLayers;
        if (enableValidationLayers) {
            requiredLayers.assign(validationLayers.begin(), validationLayers.end());
        }

        // Check if required validation layers are supported
        auto layerProperties = context.enumerateInstanceLayerProperties();
        auto unsupportedLayerIt = std::ranges::find_if(
            requiredLayers,
            [&layerProperties](auto const& requiredLayer) {
                return std::ranges::none_of(
                    layerProperties,
                    [requiredLayer](auto const& layerProperty) {
                        return std::strcmp(layerProperty.layerName, requiredLayer) == 0;
                    });
            });

        if (unsupportedLayerIt != requiredLayers.end()) {
            throw std::runtime_error(
                "Required layer not supported: " + std::string(*unsupportedLayerIt));
        }

        // Get required instance extensions
        auto requiredExtensions = getRequiredInstanceExtensions();
        auto extensionProperties = context.enumerateInstanceExtensionProperties();
        auto unsupportedExtensionIt = std::ranges::find_if(
            requiredExtensions,
            [&extensionProperties](auto const& requiredExtension) {
                return std::ranges::none_of(
                    extensionProperties,
                    [requiredExtension](auto const& extensionProperty) {
                        return std::strcmp(extensionProperty.extensionName, requiredExtension) == 0;
                    });
            });

        // Check if required instance extensions are supported
        if (unsupportedExtensionIt != requiredExtensions.end()) {
            throw std::runtime_error(
                "Required extension not supported: " + std::string(*unsupportedExtensionIt));
        }

        // Create Vulkan instance
        vk::InstanceCreateInfo createInfo{
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
            .ppEnabledLayerNames = requiredLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
            .ppEnabledExtensionNames = requiredExtensions.data(),
        };

        instance = vk::raii::Instance(context, createInfo);
    }
    
    // Get required instance extensions function
    std::vector<const char*> getRequiredInstanceExtensions()
    {
        uint32_t glfwExtensionCount = 0;
        auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        if (enableValidationLayers) {
            extensions.push_back(vk::EXTDebugUtilsExtensionName);
        }

        return extensions;
    }

    // Set up debug callback function
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback
    (
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
        vk::DebugUtilsMessageTypeFlagsEXT type,
        const vk::DebugUtilsMessengerCallbackDataEXT* callbackData,
        void*) {
        if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning ||
            severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
            std::cerr << "validation layer: type " << vk::to_string(type)
                      << " msg: " << callbackData->pMessage << std::endl;
        }

        return vk::False;
    }
    
    // Debug messenger setup
    void setupDebugMessenger() {
        if (!enableValidationLayers) {
            return;
        }

        // Set up debug messenger
        vk::DebugUtilsMessageSeverityFlagsEXT severityFlags{
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        };
        vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags{
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
        };

        // Create debug messenger
        vk::DebugUtilsMessengerCreateInfoEXT createInfo{
            .messageSeverity = severityFlags,
            .messageType = messageTypeFlags,
            .pfnUserCallback = &debugCallback,
        };

        debugMessenger = instance.createDebugUtilsMessengerEXT(createInfo);
    }

    // Create the platform-specific presentation surface through GLFW
    void createSurface()
    {
        VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
        if (glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface!");
        }

        surface = vk::raii::SurfaceKHR(instance, rawSurface);
    }

    // Check if a physical device is suitable for our needs
    bool isDeviceSuitable(const vk::raii::PhysicalDevice& device)
    {
        // Get the device type, supported API version, limits, and other properties
        const auto properties = device.getProperties();

        // Check if the device supports Vulkan 1.3 or higher
        if (properties.apiVersion < vk::ApiVersion13) {
            return false;
        }

        // Check if one queue family supports both graphics commands and presentation
        if (!findGraphicsAndPresentQueueFamily(device)) {
            return false;
        }

        // Check if the device supports the required extensions
        const auto availableExtensions = device.enumerateDeviceExtensionProperties();
        const bool supportsRequiredExtensions = std::ranges::all_of(
            requiredDeviceExtensions,
            [&availableExtensions](const char* requiredExtension) {
                return std::ranges::any_of(
                    availableExtensions,
                    [requiredExtension](const vk::ExtensionProperties& availableExtension) {
                        return std::strcmp(availableExtension.extensionName, requiredExtension) == 0;
                    });
            });
        if (!supportsRequiredExtensions) {
            return false;
        }

        // A supported extension must also provide formats and presentation modes for this surface
        const auto availableFormats = device.getSurfaceFormatsKHR(*surface);
        const auto availablePresentModes = device.getSurfacePresentModesKHR(*surface);
        if (availableFormats.empty() || availablePresentModes.empty()) {
            return false;
        }

        // Check if the device supports the required features
        const auto features = device.template getFeatures2<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();

        const bool supportsRequiredFeatures =
            features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
            features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

        return supportsRequiredFeatures;
    }

    // Pick the first physical device that satisfies every required capability
    void pickPhysicalDevice()
    {
        auto physicalDevices = instance.enumeratePhysicalDevices();
        if (physicalDevices.empty()) {
            throw std::runtime_error("Failed to find GPUs with Vulkan support!");
        }

        auto suitableDevice = std::ranges::find_if(
            physicalDevices,
            [this](const vk::raii::PhysicalDevice& device) {
                return isDeviceSuitable(device);
            });

        if (suitableDevice == physicalDevices.end()) {
            throw std::runtime_error("Failed to find a suitable GPU!");
        }

        physicalDevice = *suitableDevice;
        graphicsPresentQueueFamilyIndex =
            findGraphicsAndPresentQueueFamily(physicalDevice).value();
    }

    // Find the first queue family that can both draw and present to this window surface
    std::optional<uint32_t> findGraphicsAndPresentQueueFamily(const vk::raii::PhysicalDevice& device) const
    {
        const auto queueFamilies = device.getQueueFamilyProperties();
        const auto queueFamilyIndices =
            std::views::iota(uint32_t{0}, static_cast<uint32_t>(queueFamilies.size()));

        const auto suitableQueueFamily = std::ranges::find_if(
            queueFamilyIndices,
            [&device, &queueFamilies, this](uint32_t index) {
                const auto& queueFamily = queueFamilies[index];
                const bool supportsGraphics =
                    queueFamily.queueCount > 0 &&
                    static_cast<bool>(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics);

                return supportsGraphics &&
                       device.getSurfaceSupportKHR(index, *surface);
            });

        if (suitableQueueFamily == queueFamilyIndices.end()) {
            return std::nullopt;
        }

        return *suitableQueueFamily;
    }

    // Create a logical device from the selected physical device
    void createLogicalDevice()
    {
        // Create one queue that supports both graphics and presentation
        // Queue priorities must remain valid until device creation returns
        constexpr float queuePriority = 1.0F;
        const vk::DeviceQueueCreateInfo queueCreateInfo{
            .queueFamilyIndex = graphicsPresentQueueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };

        // Enable exactly the same features that were checked during physical device selection
        vk::StructureChain<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
            featureChain{
                {},                                     // No Vulkan 1.0 features are required yet
                {.shaderDrawParameters = true},         // Vulkan 1.1 shader draw parameters
                {
                    .synchronization2 = true,           // Vulkan 1.3 synchronization commands
                    .dynamicRendering = true,           // Vulkan 1.3 dynamic rendering
                },
                {.extendedDynamicState = true},         // Extended dynamic state feature
            };

        // Device extensions are separate from the instance extensions enabled earlier
        const vk::DeviceCreateInfo createInfo{
            .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            // Enable the required device extensions for swapchain support
            .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
            .ppEnabledExtensionNames = requiredDeviceExtensions.data(),
        };

        // Create the logical device, then retrieve queue 0 from the selected family
        // The queue is owned by the logical device and is destroyed with it
        device = vk::raii::Device(physicalDevice, createInfo);
        graphicsPresentQueue =
            vk::raii::Queue(device, graphicsPresentQueueFamilyIndex, 0);
    }

    // Prefer an sRGB format so colors are interpreted in the expected color space
    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
    {
        const auto preferredFormat = std::ranges::find_if(
            availableFormats,
            [](const vk::SurfaceFormatKHR& format) {
                return format.format == vk::Format::eB8G8R8A8Srgb &&
                       format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
            });

        return preferredFormat != availableFormats.end()
                   ? *preferredFormat
                   : availableFormats.front();
    }

    // Mailbox avoids tearing with low latency; FIFO is guaranteed by Vulkan
    static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes)
    {
        const bool supportsMailbox = std::ranges::any_of(
            availablePresentModes,
            [](vk::PresentModeKHR presentMode) {
                return presentMode == vk::PresentModeKHR::eMailbox;
            });

        return supportsMailbox
                   ? vk::PresentModeKHR::eMailbox
                   : vk::PresentModeKHR::eFifo;
    }

    // Use the surface-defined extent, or derive a pixel extent from GLFW when allowed
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) const
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }

        int width = 0;
        int height = 0;

        // Query the window size in pixels by glfw, which may differ from the window size in screen coordinates
        glfwGetFramebufferSize(window, &width, &height);

        return {
            std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
        };
    }

    // Prefer triple buffering without exceeding the implementation's maximum
    static uint32_t chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& capabilities)
    {
        uint32_t imageCount = std::max(3U, capabilities.minImageCount);
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
            imageCount = capabilities.maxImageCount;
        }

        return imageCount;
    }

    // Create the swapchain and retain its images for later rendering setup
    void createSwapChain()
    {
        const auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
        const auto availableFormats = physicalDevice.getSurfaceFormatsKHR(*surface);
        const auto availablePresentModes = physicalDevice.getSurfacePresentModesKHR(*surface);

        if (availableFormats.empty() || availablePresentModes.empty()) {
            throw std::runtime_error("The selected physical device has incomplete swapchain support!");
        }

        swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);
        swapChainExtent = chooseSwapExtent(capabilities);
        const uint32_t imageCount = chooseSwapMinImageCount(capabilities);
        const vk::PresentModeKHR presentMode = chooseSwapPresentMode(availablePresentModes);

        const vk::SwapchainCreateInfoKHR createInfo{
            .flags = vk::SwapchainCreateFlagsKHR{0},
            .surface = *surface,
            .minImageCount = imageCount,
            .imageFormat = swapChainSurfaceFormat.format,
            .imageColorSpace = swapChainSurfaceFormat.colorSpace,
            .imageExtent = swapChainExtent,
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .preTransform = capabilities.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = presentMode,
            .clipped = vk::True,
            .oldSwapchain = nullptr,
        };


        swapChain = vk::raii::SwapchainKHR(device, createInfo);
        swapChainImages = swapChain.getImages();
    }

    // Create a 2D color view for every image owned by the swapchain
    void createImageViews()
    {
        swapChainImageViews.clear();
        swapChainImageViews.reserve(swapChainImages.size());

        for (const vk::Image image : swapChainImages) {
            const vk::ImageViewCreateInfo createInfo{
                .flags = vk::ImageViewCreateFlags{0},
                .image = image,
                .viewType = vk::ImageViewType::e2D,
                .format = swapChainSurfaceFormat.format,
                // Use the default mapping of color channels to the image's format
                .components = {
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                },
                .subresourceRange = {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };

            swapChainImageViews.emplace_back(device, createInfo);
        }
    }

    // Read SPIR-V into aligned 32-bit words, as required by ShaderModuleCreateInfo
    static std::vector<uint32_t> readSpirvFile(const std::filesystem::path& filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error(
                "Failed to open SPIR-V shader: " + filename.string());
        }

        const std::streampos endPosition = file.tellg();
        if (endPosition <= std::streampos{0}) {
            throw std::runtime_error(
                "SPIR-V shader is empty: " + filename.string());
        }

        const auto byteCount = static_cast<std::size_t>(endPosition);
        if (byteCount % sizeof(uint32_t) != 0) {
            throw std::runtime_error(
                "SPIR-V shader size is not a multiple of four bytes: " +
                filename.string());
        }

        std::vector<uint32_t> code(byteCount / sizeof(uint32_t));
        file.seekg(0, std::ios::beg);
        if (!file.read(
                reinterpret_cast<char*>(code.data()),
                static_cast<std::streamsize>(byteCount))) {
            throw std::runtime_error(
                "Failed to read SPIR-V shader: " + filename.string());
        }

        constexpr uint32_t spirvMagicNumber = 0x07230203;
        if (code.front() != spirvMagicNumber) {
            throw std::runtime_error(
                "Shader file does not contain valid SPIR-V: " + filename.string());
        }

        return code;
    }

    // Wrap aligned SPIR-V words in a C++ RAII shader module
    [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<uint32_t>& code) const
    {
        const vk::ShaderModuleCreateInfo createInfo{
            .flags = vk::ShaderModuleCreateFlags{0},
            .codeSize = code.size() * sizeof(uint32_t),
            .pCode = code.data(),
        };

        return vk::raii::ShaderModule(device, createInfo);
    }

    // Prepare the programmable and fixed-function graphics pipeline state
    void createGraphicsPipeline()
    {
        const auto shaderCode = readSpirvFile(SHADER_SPIRV_PATH);
        const vk::raii::ShaderModule shaderModule = createShaderModule(shaderCode);

        const std::array shaderStages{
            vk::PipelineShaderStageCreateInfo{
                .flags = vk::PipelineShaderStageCreateFlags{0},
                .stage = vk::ShaderStageFlagBits::eVertex,
                .module = *shaderModule,
                .pName = "vertMain",
                .pSpecializationInfo = nullptr,
            },
            vk::PipelineShaderStageCreateInfo{
                .flags = vk::PipelineShaderStageCreateFlags{0},
                .stage = vk::ShaderStageFlagBits::eFragment,
                .module = *shaderModule,
                .pName = "fragMain",
                .pSpecializationInfo = nullptr,
            },
        };

        // Viewport and scissor dimensions will be supplied while recording commands
        constexpr std::array dynamicStates{
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
        };
        const vk::PipelineDynamicStateCreateInfo dynamicState{
            .flags = vk::PipelineDynamicStateCreateFlags{0},
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data(),
        };

        // Vertex positions and colors currently come from the vertex shader itself
        const vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .flags = vk::PipelineVertexInputStateCreateFlags{0},
            .vertexBindingDescriptionCount = 0,
            .pVertexBindingDescriptions = nullptr,
            .vertexAttributeDescriptionCount = 0,
            .pVertexAttributeDescriptions = nullptr,
        };

        // Interpret every three consecutive vertices as an independent triangle
        const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
            .flags = vk::PipelineInputAssemblyStateCreateFlags{0},
            .topology = vk::PrimitiveTopology::eTriangleList,
            .primitiveRestartEnable = vk::False,
        };

        // The dynamic viewport and scissor still require their counts at creation time
        const vk::PipelineViewportStateCreateInfo viewportState{
            .flags = vk::PipelineViewportStateCreateFlags{0},
            .viewportCount = 1,
            .pViewports = nullptr,
            .scissorCount = 1,
            .pScissors = nullptr,
        };

        // Fill clockwise front-facing triangles and cull their back faces
        const vk::PipelineRasterizationStateCreateInfo rasterizer{
            .flags = vk::PipelineRasterizationStateCreateFlags{0},
            .depthClampEnable = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eClockwise,
            .depthBiasEnable = vk::False,
            .depthBiasConstantFactor = 0.0F,
            .depthBiasClamp = 0.0F,
            .depthBiasSlopeFactor = 0.0F,
            .lineWidth = 1.0F,
        };

        // Use one sample per pixel; multisample antialiasing is introduced later
        const vk::PipelineMultisampleStateCreateInfo multisampling{
            .flags = vk::PipelineMultisampleStateCreateFlags{0},
            .rasterizationSamples = vk::SampleCountFlagBits::e1,
            .sampleShadingEnable = vk::False,
            .minSampleShading = 1.0F,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = vk::False,
            .alphaToOneEnable = vk::False,
        };

        // Write every color channel directly, without alpha or logical blending
        const vk::PipelineColorBlendAttachmentState colorBlendAttachment{
            .blendEnable = vk::False,
            .srcColorBlendFactor = vk::BlendFactor::eOne,
            .dstColorBlendFactor = vk::BlendFactor::eZero,
            .colorBlendOp = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
            .dstAlphaBlendFactor = vk::BlendFactor::eZero,
            .alphaBlendOp = vk::BlendOp::eAdd,
            .colorWriteMask = vk::ColorComponentFlagBits::eR |
                              vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB |
                              vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo colorBlending{
            .flags = vk::PipelineColorBlendStateCreateFlags{0},
            .logicOpEnable = vk::False,
            .logicOp = vk::LogicOp::eCopy,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment,
        };

        // No descriptor sets or push constants are used by the current shaders
        const vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
            .flags = vk::PipelineLayoutCreateFlags{0},
            .setLayoutCount = 0,
            .pSetLayouts = nullptr,
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr,
        };
        pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

        // Dynamic rendering describes attachment formats without a render pass object
        vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain{
                {
                    .flags = vk::PipelineCreateFlags{0},
                    .stageCount = static_cast<uint32_t>(shaderStages.size()),
                    .pStages = shaderStages.data(),
                    .pVertexInputState = &vertexInputInfo,
                    .pInputAssemblyState = &inputAssembly,
                    .pTessellationState = nullptr,
                    .pViewportState = &viewportState,
                    .pRasterizationState = &rasterizer,
                    .pMultisampleState = &multisampling,
                    .pDepthStencilState = nullptr,
                    .pColorBlendState = &colorBlending,
                    .pDynamicState = &dynamicState,
                    .layout = *pipelineLayout,
                    .renderPass = nullptr,
                    .subpass = 0,
                    .basePipelineHandle = nullptr,
                    .basePipelineIndex = -1,
                },
                {
                    .viewMask = 0,
                    .colorAttachmentCount = 1,
                    .pColorAttachmentFormats = &swapChainSurfaceFormat.format,
                    .depthAttachmentFormat = vk::Format::eUndefined,
                    .stencilAttachmentFormat = vk::Format::eUndefined,
                },
            };

        // A pipeline cache is optional; all state above now forms the graphics pipeline
        graphicsPipeline = vk::raii::Pipeline(device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
    }

    // Allocate command-buffer storage for the graphics/presentation queue family
    void createCommandPool()
    {
        const vk::CommandPoolCreateInfo poolInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = graphicsPresentQueueFamilyIndex,
        };

        commandPool = vk::raii::CommandPool(device, poolInfo);
    }

    // Allocate one primary command buffer; RAII returns it to the pool automatically
    void createCommandBuffer()
    {
        const vk::CommandBufferAllocateInfo allocateInfo{
            .commandPool = *commandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        };

        auto commandBuffers = vk::raii::CommandBuffers(device, allocateInfo);
        commandBuffer = std::move(commandBuffers.front());
    }

    // Record a Synchronization2 image-layout transition for one swapchain image
    void transitionImageLayout(
        uint32_t imageIndex,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::AccessFlags2 sourceAccessMask,
        vk::AccessFlags2 destinationAccessMask,
        vk::PipelineStageFlags2 sourceStageMask,
        vk::PipelineStageFlags2 destinationStageMask)
    {
        const vk::ImageMemoryBarrier2 barrier{
            .srcStageMask = sourceStageMask,
            .srcAccessMask = sourceAccessMask,
            .dstStageMask = destinationStageMask,
            .dstAccessMask = destinationAccessMask,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = swapChainImages.at(imageIndex),
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        const vk::DependencyInfo dependencyInfo{
            .dependencyFlags = vk::DependencyFlags{0},
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };

        commandBuffer.pipelineBarrier2(dependencyInfo);
    }

    // Record dynamic rendering commands for the acquired swapchain image
    void recordCommandBuffer(uint32_t imageIndex)
    {
        if (imageIndex >= swapChainImages.size()) {
            throw std::out_of_range("Swapchain image index is out of range!");
        }

        const vk::CommandBufferBeginInfo beginInfo{
            .flags = vk::CommandBufferUsageFlags{0},
            .pInheritanceInfo = nullptr,
        };
        commandBuffer.begin(beginInfo);

        // Discard old contents and make the image writable as a color attachment
        transitionImageLayout(
            imageIndex,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlags2{0},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput
        );

        // Clear the color attachment
        vk::ClearColorValue clearColor;
        clearColor.setFloat32(std::array{0.0F, 0.0F, 0.0F, 1.0F});
        vk::ClearValue clearValue;
        clearValue.setColor(clearColor);

        const vk::RenderingAttachmentInfo colorAttachmentInfo{
            .imageView = *swapChainImageViews.at(imageIndex),
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .resolveMode = vk::ResolveModeFlagBits::eNone,
            .resolveImageView = nullptr,
            .resolveImageLayout = vk::ImageLayout::eUndefined,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearValue,
        };
        const vk::RenderingInfo renderingInfo{
            .flags = vk::RenderingFlags{0},
            .renderArea = {
                .offset = {0, 0},
                .extent = swapChainExtent,
            },
            .layerCount = 1,
            .viewMask = 0,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo,
            .pDepthAttachment = nullptr,
            .pStencilAttachment = nullptr,
        };

        // Begin a dynamic rendering scope, which discards the color attachment contents
        commandBuffer.beginRendering(renderingInfo);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);

        const vk::Viewport viewport{
            .x = 0.0F,
            .y = 0.0F,
            .width = static_cast<float>(swapChainExtent.width),
            .height = static_cast<float>(swapChainExtent.height),
            .minDepth = 0.0F,
            .maxDepth = 1.0F,
        };
        const vk::Rect2D scissor{
            .offset = {0, 0},
            .extent = swapChainExtent,
        };

        // Set the dynamic viewport and scissor, then draw a single triangle
        commandBuffer.setViewport(0, viewport);
        commandBuffer.setScissor(0, scissor);

        // Draw three vertices, starting with vertex 0, in one instance
        commandBuffer.draw(3, 1, 0, 0);
        
        // End the dynamic rendering scope, which discards the color attachment contents
        commandBuffer.endRendering();

        // Make the rendered image ready for the presentation engine
        transitionImageLayout(
            imageIndex,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::AccessFlags2{0},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eBottomOfPipe
        );

        // End the command buffer recording, which makes it ready for submission
        commandBuffer.end();
    }
};

int main()
{
    try {
        HelloTriangleApplication app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

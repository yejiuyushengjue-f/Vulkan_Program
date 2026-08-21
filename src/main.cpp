#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

constexpr uint32_t WIDTH = 1600;
constexpr uint32_t HEIGHT = 1200;
constexpr vk::Format TEXTURE_FORMAT = vk::Format::eR8G8B8A8Srgb;

const std::vector validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

// Required device extensions for presenting rendered images through a swapchain
const std::vector requiredDeviceExtensions = {
    vk::KHRSwapchainExtensionName
};

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    // One Vertex object is consumed for each vertex from binding 0.
    [[nodiscard]] static constexpr vk::VertexInputBindingDescription getBindingDescription() noexcept
    {
        return {.binding = 0,
                .stride = sizeof(Vertex),
                .inputRate = vk::VertexInputRate::eVertex};
    }

    // Match Vertex::pos/color/texCoord with the shader's locations 0, 1, and 2.
    [[nodiscard]] static constexpr std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions() noexcept
    {
        return {{
            vk::VertexInputAttributeDescription{
                .location = 0,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(Vertex, pos),
            },
            vk::VertexInputAttributeDescription{
                .location = 1,
                .binding = 0,
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = offsetof(Vertex, color),
            },
            vk::VertexInputAttributeDescription{
                .location = 2,
                .binding = 0,
                .format = vk::Format::eR32G32Sfloat,
                .offset = offsetof(Vertex, texCoord),
            },
        }};
    }
};

// Keep this layout binary-compatible with UniformBuffer in shader.slang.
struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

const std::vector<Vertex> vertices = {
    {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},

    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}
};

const std::vector<uint16_t> indices = {
    0, 1, 2, 2, 3, 0,
    4, 5, 6, 6, 7, 4
};

class HelloTriangleApplication {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    // The memory member is declared first so the bound buffer is destroyed first.
    struct AllocatedBuffer {
        vk::raii::DeviceMemory memory = nullptr;
        vk::raii::Buffer buffer = nullptr;
    };

    // As with buffers, keep memory before the bound image for reverse RAII teardown.
    struct AllocatedImage {
        vk::raii::DeviceMemory memory = nullptr;
        vk::raii::Image image = nullptr;
    };

    GLFWwindow* window = nullptr;
    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;
    vk::raii::Queue graphicsPresentQueue = nullptr;
    vk::raii::Queue transferQueue = nullptr;
    uint32_t graphicsPresentQueueFamilyIndex = 0;
    uint32_t transferQueueFamilyIndex = 0;
    vk::raii::SwapchainKHR swapChain = nullptr;
    std::vector<vk::Image> swapChainImages;
    std::vector<vk::raii::ImageView> swapChainImageViews;
    vk::SurfaceFormatKHR swapChainSurfaceFormat;
    vk::Extent2D swapChainExtent;
    vk::Format depthFormat = vk::Format::eUndefined;
    // Memory precedes the bound image and its view for safe reverse-order teardown.
    vk::raii::DeviceMemory depthImageMemory = nullptr;
    vk::raii::Image depthImage = nullptr;
    vk::raii::ImageView depthImageView = nullptr;
    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::DescriptorPool descriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptorSets;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline graphicsPipeline = nullptr;
    vk::raii::CommandPool commandPool = nullptr;
    vk::raii::CommandPool transferCommandPool = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers;
    std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> drawFences;
    uint32_t currentFrame = 0;
    bool framebufferResized = false;
    // Declare memory before the buffer so reverse-order RAII destruction releases
    // the buffer before freeing the memory bound to it.
    vk::raii::DeviceMemory vertexBufferMemory = nullptr;
    vk::raii::Buffer vertexBuffer = nullptr;
    vk::raii::DeviceMemory indexBufferMemory = nullptr;
    vk::raii::Buffer indexBuffer = nullptr;
    // Keep the dependent view after the image so RAII destroys the view first.
    vk::raii::DeviceMemory textureImageMemory = nullptr;
    vk::raii::Image textureImage = nullptr;
    vk::raii::ImageView textureImageView = nullptr;
    vk::raii::Sampler textureSampler = nullptr;
    // Memory is declared before buffers so buffers are destroyed first by RAII.
    std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
    std::vector<vk::raii::Buffer> uniformBuffers;
    std::vector<void*> uniformBuffersMapped;

    void initWindow()
    {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
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
        createDepthResources();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createCommandPools();
        createTextureImage();
        createTextureImageView();
        createTextureSampler();
        createVertexBuffer();
        createIndexBuffer();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
    }

    void mainLoop()
    {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            drawFrame();
        }

        // Presentation is asynchronous, so all device work must finish before cleanup
        device.waitIdle();
    }

    void cleanup()
    {
        cleanupSwapChain();

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

        // Check graphics/presentation support and require a separate transfer family
        if (!findGraphicsAndPresentQueueFamily(device)) {
            return false;
        }
        if (!findDedicatedTransferQueueFamily(device)) {
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
            features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
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
        graphicsPresentQueueFamilyIndex = findGraphicsAndPresentQueueFamily(physicalDevice).value();
        transferQueueFamilyIndex = findDedicatedTransferQueueFamily(physicalDevice).value();
    }

    // Find the first queue family that can both draw and present to this window surface
    std::optional<uint32_t> findGraphicsAndPresentQueueFamily(const vk::raii::PhysicalDevice& device) const
    {
        const auto queueFamilies = device.getQueueFamilyProperties();
        const auto queueFamilyIndices = std::views::iota(uint32_t{0}, static_cast<uint32_t>(queueFamilies.size()));

        const auto suitableQueueFamily = std::ranges::find_if(
            queueFamilyIndices,
            [&device, &queueFamilies, this](uint32_t index) {
                const auto& queueFamily = queueFamilies[index];
                const bool supportsGraphics =
                    queueFamily.queueCount > 0 &&
                    static_cast<bool>(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics);

                return supportsGraphics && device.getSurfaceSupportKHR(index, *surface);
            });

        if (suitableQueueFamily == queueFamilyIndices.end()) {
            return std::nullopt;
        }

        return *suitableQueueFamily;
    }

    // Prefer a transfer-only family, then fall back to any non-graphics transfer family
    std::optional<uint32_t> findDedicatedTransferQueueFamily(const vk::raii::PhysicalDevice& candidateDevice) const
    {
        const auto queueFamilies = candidateDevice.getQueueFamilyProperties();
        const auto queueFamilyIndices = std::views::iota(uint32_t{0}, static_cast<uint32_t>(queueFamilies.size()));
        const auto supportsTransferWithoutGraphics =
            [&queueFamilies](uint32_t index) {
                const auto& queueFamily = queueFamilies[index];
                return queueFamily.queueCount > 0 &&
                       static_cast<bool>(queueFamily.queueFlags & vk::QueueFlagBits::eTransfer) &&
                       !static_cast<bool>(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics);
            };

        const auto transferOnlyQueueFamily = std::ranges::find_if(
            queueFamilyIndices,
            [&](uint32_t index) {
                // A transfer-only queue can let the driver schedule copies without
                // competing with graphics or compute work on the same queue family.
                return supportsTransferWithoutGraphics(index) &&
                       !static_cast<bool>(queueFamilies[index].queueFlags & vk::QueueFlagBits::eCompute);
            });
        if (transferOnlyQueueFamily != queueFamilyIndices.end()) {
            return *transferOnlyQueueFamily;
        }

        const auto nonGraphicsTransferQueueFamily = std::ranges::find_if(queueFamilyIndices, supportsTransferWithoutGraphics);
        if (nonGraphicsTransferQueueFamily == queueFamilyIndices.end()) {
            return std::nullopt;
        }

        return *nonGraphicsTransferQueueFamily;
    }

    // Create a logical device from the selected physical device
    void createLogicalDevice()
    {
        // Request one graphics/presentation queue and one dedicated transfer queue.
        // Queue priorities must remain valid until device creation returns.
        constexpr float queuePriority = 1.0F;
        const std::array<vk::DeviceQueueCreateInfo, 2> queueCreateInfos{{
            vk::DeviceQueueCreateInfo{
                .queueFamilyIndex = graphicsPresentQueueFamilyIndex,
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
            },
            vk::DeviceQueueCreateInfo{
                .queueFamilyIndex = transferQueueFamilyIndex,
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
            },
        }};

        // Enable exactly the same features that were checked during physical device selection
        vk::StructureChain<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
            featureChain{
                {.features = {.samplerAnisotropy = true}}, // Anisotropic texture filtering
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
            .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
            .pQueueCreateInfos = queueCreateInfos.data(),
            // Enable the required device extensions for swapchain support
            .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
            .ppEnabledExtensionNames = requiredDeviceExtensions.data(),
        };

        // Create the logical device, then retrieve queue 0 from the selected family
        // The queue is owned by the logical device and is destroyed with it
        device = vk::raii::Device(physicalDevice, createInfo);
        graphicsPresentQueue = vk::raii::Queue(device, graphicsPresentQueueFamilyIndex, 0);
        transferQueue = vk::raii::Queue(device, transferQueueFamilyIndex, 0);
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
            swapChainImageViews.push_back(createImageView(image, swapChainSurfaceFormat.format, vk::ImageAspectFlagBits::eColor));
        }
    }

    // Both swapchain images and owned texture images are raw VkImage handles at
    // the ImageView boundary, so one helper covers both without RAII conversions.
    [[nodiscard]] vk::raii::ImageView createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspectFlags) const
    {
        const vk::ImageViewCreateInfo createInfo{
            .flags = vk::ImageViewCreateFlags{0},
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            // Use the default mapping of color channels to the image's format
            .components = {
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
            },
            .subresourceRange = {
                .aspectMask = aspectFlags,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        return vk::raii::ImageView(device, createInfo);
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

    // Describe set 0: a vertex-stage UBO and a fragment-stage combined sampler.
    void createDescriptorSetLayout()
    {
        constexpr std::array bindings{
            vk::DescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eVertex,
                .pImmutableSamplers = nullptr,
            },
            vk::DescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
                .pImmutableSamplers = nullptr,
            },
        };
        const vk::DescriptorSetLayoutCreateInfo layoutInfo{
            .flags = vk::DescriptorSetLayoutCreateFlags{0},
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };

        descriptorSetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);
    }

    // Prepare the programmable and fixed-function graphics pipeline state
    void createGraphicsPipeline()
    {
        if (depthFormat == vk::Format::eUndefined) {
            throw std::logic_error("Depth resources must be created before the graphics pipeline!");
        }

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

        // Describe how the fixed Vertex layout is read by the vertex shader.
        constexpr auto bindingDescription = Vertex::getBindingDescription();
        constexpr auto attributeDescriptions = Vertex::getAttributeDescriptions();
        const vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .flags = vk::PipelineVertexInputStateCreateFlags{0},
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDescription,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
            .pVertexAttributeDescriptions = attributeDescriptions.data(),
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

        // Projection flips clip-space Y, so front-facing vertices become counter-clockwise.
        const vk::PipelineRasterizationStateCreateInfo rasterizer{
            .flags = vk::PipelineRasterizationStateCreateFlags{0},
            .depthClampEnable = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eCounterClockwise,
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

        // Set 0 must match the UniformBuffer declaration in the vertex shader.
        const std::array descriptorSetLayouts{*descriptorSetLayout};
        const vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
            .flags = vk::PipelineLayoutCreateFlags{0},
            .setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
            .pSetLayouts = descriptorSetLayouts.data(),
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr,
        };
        pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

        const vk::PipelineDepthStencilStateCreateInfo depthStencil{
            .flags = vk::PipelineDepthStencilStateCreateFlags{0},
            .depthTestEnable = vk::True,
            .depthWriteEnable = vk::True,
            .depthCompareOp = vk::CompareOp::eLess,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable = vk::False,
        };

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
                    .pDepthStencilState = &depthStencil,
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
                    .depthAttachmentFormat = depthFormat,
                    .stencilAttachmentFormat = vk::Format::eUndefined,
                },
            };

        // A pipeline cache is optional; all state above now forms the graphics pipeline
        graphicsPipeline = vk::raii::Pipeline(device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
    }

    // Create separate pools because command buffers must be submitted to a queue
    // from the same family as the pool that allocated them.
    void createCommandPools()
    {
        const vk::CommandPoolCreateInfo poolInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = graphicsPresentQueueFamilyIndex,
        };
        commandPool = vk::raii::CommandPool(device, poolInfo);

        const vk::CommandPoolCreateInfo transferPoolInfo{
            // Transfer command buffers are short-lived, so advertise that usage
            // pattern to the driver with eTransient.
            .flags = vk::CommandPoolCreateFlagBits::eTransient,
            .queueFamilyIndex = transferQueueFamilyIndex,
        };
        transferCommandPool = vk::raii::CommandPool(device, transferPoolInfo);
    }

    // Allocate one independently recordable primary command buffer per in-flight frame
    void createCommandBuffers()
    {
        const vk::CommandBufferAllocateInfo allocateInfo{
            .commandPool = *commandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
        };

        commandBuffers = vk::raii::CommandBuffers(device, allocateInfo);
    }

    // Record a Synchronization2 layout transition for any color or depth image.
    void transitionImageLayout(
        vk::raii::CommandBuffer& commandBuffer,
        vk::Image image,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::AccessFlags2 sourceAccessMask,
        vk::AccessFlags2 destinationAccessMask,
        vk::PipelineStageFlags2 sourceStageMask,
        vk::PipelineStageFlags2 destinationStageMask,
        vk::ImageAspectFlags aspectMask)
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
            .image = image,
            .subresourceRange = {
                .aspectMask = aspectMask,
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
    void recordCommandBuffer(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex, uint32_t frameIndex)
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
            commandBuffer,
            swapChainImages.at(imageIndex),
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlags2{0},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::ImageAspectFlagBits::eColor
        );

        // Previous depth contents are discarded, then the image becomes writable
        // by both early and late depth/stencil tests for this rendering pass.
        transitionImageLayout(
            commandBuffer,
            *depthImage,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::ImageAspectFlagBits::eDepth
        );

        // Clear the color attachment
        vk::ClearColorValue clearColor;
        clearColor.setFloat32(std::array{0.0F, 0.0F, 0.0F, 1.0F});
        vk::ClearValue clearValue;
        clearValue.setColor(clearColor);
        // Clear the depth attachment
        vk::ClearValue clearDepth;
        clearDepth.setDepthStencil(vk::ClearDepthStencilValue{1.0F, 0});

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
        const vk::RenderingAttachmentInfo depthAttachmentInfo{
            .imageView = *depthImageView,
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .resolveMode = vk::ResolveModeFlagBits::eNone,
            .resolveImageView = nullptr,
            .resolveImageLayout = vk::ImageLayout::eUndefined,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
            .clearValue = clearDepth,
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
            .pDepthAttachment = &depthAttachmentInfo,
            .pStencilAttachment = nullptr,
        };

        // Begin a dynamic rendering scope and clear the attachment
        commandBuffer.beginRendering(renderingInfo);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);

        // Binding 0 corresponds to Vertex::getBindingDescription().
        const std::array vertexBuffers{*vertexBuffer};
        constexpr std::array<vk::DeviceSize, 1> vertexBufferOffsets{0};
        commandBuffer.bindVertexBuffers(0, vertexBuffers, vertexBufferOffsets);
        commandBuffer.bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint16);

        // Set 0 selects this frame's UBO plus the shared texture view and sampler.
        const std::array currentDescriptorSets{*descriptorSets.at(frameIndex)};
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            *pipelineLayout, 0, currentDescriptorSets, {});

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

        // Set the dynamic viewport and scissor, then draw the indexed square.
        commandBuffer.setViewport(0, viewport);
        commandBuffer.setScissor(0, scissor);
        commandBuffer.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

        // End the dynamic rendering scope and retain the rendered attachment contents
        commandBuffer.endRendering();

        // Make the rendered image ready for the presentation engine
        transitionImageLayout(
            commandBuffer,
            swapChainImages.at(imageIndex),
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::AccessFlags2{0},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eBottomOfPipe,
            vk::ImageAspectFlagBits::eColor
        );

        // End recording so this frame's command buffer is ready for submission
        commandBuffer.end();
    }

    // Frame resources use currentFrame; presentation semaphores use swapchain imageIndex
    void createSyncObjects()
    {
        if (!presentCompleteSemaphores.empty() ||
            !renderFinishedSemaphores.empty() ||
            !drawFences.empty()) {
            throw std::logic_error("Synchronization objects have already been created!");
        }

        const vk::SemaphoreCreateInfo semaphoreInfo{
            .flags = vk::SemaphoreCreateFlags{0},
        };
        const vk::FenceCreateInfo fenceInfo{
            .flags = vk::FenceCreateFlagBits::eSignaled,
        };

        presentCompleteSemaphores.reserve(MAX_FRAMES_IN_FLIGHT);
        drawFences.reserve(MAX_FRAMES_IN_FLIGHT);
        createRenderFinishedSemaphores();

        std::ranges::generate_n(
            std::back_inserter(presentCompleteSemaphores),
            MAX_FRAMES_IN_FLIGHT,
            [this, &semaphoreInfo] {
                return vk::raii::Semaphore(device, semaphoreInfo);
            });
        std::ranges::generate_n(
            std::back_inserter(drawFences),
            MAX_FRAMES_IN_FLIGHT,
            [this, &fenceInfo] {
                return vk::raii::Fence(device, fenceInfo);
            });
    }

    // Present wait semaphores are indexed by swapchain image and must be recreated with it
    void createRenderFinishedSemaphores()
    {
        if (!renderFinishedSemaphores.empty()) {
            throw std::logic_error("Render-finished semaphores have already been created!");
        }

        const vk::SemaphoreCreateInfo semaphoreInfo{
            .flags = vk::SemaphoreCreateFlags{0},
        };
        renderFinishedSemaphores.reserve(swapChainImages.size());
        std::ranges::generate_n(
            std::back_inserter(renderFinishedSemaphores),
            swapChainImages.size(),
            [this, &semaphoreInfo] {
                return vk::raii::Semaphore(device, semaphoreInfo);
            });
    }

    void drawFrame()
    {
        auto& commandBuffer = commandBuffers.at(currentFrame);
        auto& presentCompleteSemaphore = presentCompleteSemaphores.at(currentFrame);
        auto& drawFence = drawFences.at(currentFrame);

        // Wait only when cycling back to resources belonging to this in-flight frame
        const vk::Result fenceResult = device.waitForFences(*drawFence, vk::True, std::numeric_limits<uint64_t>::max());
        if (fenceResult != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to wait for the draw fence!");
        }

        // Signal this frame's binary semaphore when an image becomes available
        const vk::ResultValue<uint32_t> acquireResult = swapChain.acquireNextImage(
            std::numeric_limits<uint64_t>::max(),
            *presentCompleteSemaphore, nullptr
        );

        if (acquireResult.result == vk::Result::eErrorOutOfDateKHR) {
            recreateSwapChain();
            return;
        }
        if (acquireResult.result != vk::Result::eSuccess && acquireResult.result != vk::Result::eSuboptimalKHR) {
            throw std::runtime_error("Failed to acquire next swapchain image!");
        }

        const uint32_t imageIndex = acquireResult.value;
        const vk::Semaphore renderFinishedSemaphore = *renderFinishedSemaphores.at(imageIndex);

        // The current frame's fence has completed, so its persistently mapped UBO
        // is no longer being read by the GPU and can be updated safely.
        updateUniformBuffer(currentFrame);

        // This frame's command buffer is idle now and can be reset and rerecorded
        commandBuffer.reset();
        recordCommandBuffer(commandBuffer, imageIndex, currentFrame);

        // Wait for acquisition before color output, then signal after all commands
        const vk::SemaphoreSubmitInfo waitSemaphoreInfo{
            .semaphore = *presentCompleteSemaphore,
            .value = 0,
            .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .deviceIndex = 0,
        };
        const vk::CommandBufferSubmitInfo commandBufferInfo{
            .commandBuffer = *commandBuffer,
            .deviceMask = 1,
        };
        const vk::SemaphoreSubmitInfo signalSemaphoreInfo{
            .semaphore = renderFinishedSemaphore,
            .value = 0,
            .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .deviceIndex = 0,
        };
        const vk::SubmitInfo2 submitInfo{
            .flags = vk::SubmitFlags{0},
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &waitSemaphoreInfo,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandBufferInfo,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &signalSemaphoreInfo,
        };

        // Reset only when a successful acquire guarantees that this frame will be submitted
        device.resetFences(*drawFence);
        graphicsPresentQueue.submit2(submitInfo, *drawFence);

        // Presentation waits on the semaphore associated with this swapchain image
        const vk::PresentInfoKHR presentInfo{
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderFinishedSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &*swapChain,
            .pImageIndices = &imageIndex,
            .pResults = nullptr,
        };

        const vk::Result presentResult = graphicsPresentQueue.presentKHR(presentInfo);
        if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR || framebufferResized) {
            framebufferResized = false;
            recreateSwapChain();
        } else if (presentResult != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to present the swapchain image!");
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void cleanupSwapChain()
    {
        // The pipeline bakes in the dynamic-rendering color and depth formats.
        graphicsPipeline = nullptr;
        pipelineLayout = nullptr;
        renderFinishedSemaphores.clear();
        // Destroy the view and image before releasing the memory bound to it.
        depthImageView = nullptr;
        depthImage = nullptr;
        depthImageMemory = nullptr;
        depthFormat = vk::Format::eUndefined;
        swapChainImageViews.clear();
        swapChainImages.clear();
        swapChain = nullptr;
    }

    void recreateSwapChain()
    {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        while (width == 0 || height == 0) {
            glfwWaitEvents();
            if (glfwWindowShouldClose(window)) {
                return;
            }
            glfwGetFramebufferSize(window, &width, &height);
        }

        device.waitIdle();

        cleanupSwapChain();

        createSwapChain();
        createImageViews();
        createDepthResources();
        createGraphicsPipeline();
        createRenderFinishedSemaphores();
        framebufferResized = false;
    }

    static void framebufferResizeCallback(GLFWwindow* window, int, int)
    {
        auto app = reinterpret_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }

    [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags requiredProperties) const
    {
        const vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();
        const auto memoryTypeIndices = std::views::iota(uint32_t{0}, memoryProperties.memoryTypeCount);

        const auto memoryTypeIt = std::ranges::find_if(
            memoryTypeIndices,
            [&](uint32_t index) {
                const bool supportedByBuffer = (typeFilter & (uint32_t{1} << index)) != 0;
                const vk::MemoryPropertyFlags availableProperties = memoryProperties.memoryTypes[index].propertyFlags;
                return supportedByBuffer && (availableProperties & requiredProperties) == requiredProperties;
            });

        if (memoryTypeIt == memoryTypeIndices.end()) {
            throw std::runtime_error("failed to find suitable memory type!");
        }

        return *memoryTypeIt;
    }

    void createVertexBuffer()
    {
        const vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

        // The CPU writes vertices into this temporary, host-visible source buffer.
        auto stagingBuffer = createBuffer(
            bufferSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

        // Host-coherent memory makes the copied vertex data visible without an explicit flushMappedMemoryRanges call.
        void* mappedMemory = stagingBuffer.memory.mapMemory(0, bufferSize);
        std::memcpy(mappedMemory, vertices.data(), static_cast<std::size_t>(bufferSize));
        stagingBuffer.memory.unmapMemory();

        // The final vertex buffer stays in device-local memory for efficient GPU reads.
        // eTransferDst is required because it receives data from the staging buffer.
        auto deviceLocalBuffer = createBuffer(
            bufferSize,
            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        vertexBufferMemory = std::move(deviceLocalBuffer.memory);
        vertexBuffer = std::move(deviceLocalBuffer.buffer);

        // copyBuffer waits for its fence, so the staging allocation remains alive
        // until the transfer has completed and can then be destroyed safely.
        copyBuffer(stagingBuffer.buffer, vertexBuffer, bufferSize);
    }

    void createIndexBuffer()
    {
        const vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

        // Upload the CPU-side uint16_t indices through a host-visible staging buffer.
        auto stagingBuffer = createBuffer(
            bufferSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

        void* mappedMemory = stagingBuffer.memory.mapMemory(0, bufferSize);
        std::memcpy(mappedMemory, indices.data(), static_cast<std::size_t>(bufferSize));
        stagingBuffer.memory.unmapMemory();

        // The final index buffer is device-local and receives the staged data by copy.
        auto deviceLocalBuffer = createBuffer(
            bufferSize,
            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        indexBufferMemory = std::move(deviceLocalBuffer.memory);
        indexBuffer = std::move(deviceLocalBuffer.buffer);

        copyBuffer(stagingBuffer.buffer, indexBuffer, bufferSize);
    }

    void createUniformBuffers()
    {
        if (!uniformBuffers.empty() || !uniformBuffersMemory.empty() || !uniformBuffersMapped.empty()) {
            throw std::logic_error("Uniform buffers have already been created!");
        }

        constexpr vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
        uniformBuffersMemory.reserve(MAX_FRAMES_IN_FLIGHT);
        uniformBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMapped.reserve(MAX_FRAMES_IN_FLIGHT);

        for (const uint32_t frameIndex : std::views::iota(uint32_t{0}, MAX_FRAMES_IN_FLIGHT)) {
            static_cast<void>(frameIndex);
            auto uniformBuffer = createBuffer(
                bufferSize,
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            );

            uniformBuffersMemory.emplace_back(std::move(uniformBuffer.memory));
            uniformBuffers.emplace_back(std::move(uniformBuffer.buffer));
            uniformBuffersMapped.emplace_back(uniformBuffersMemory.back().mapMemory(0, bufferSize));
        }
    }

    void updateUniformBuffer(uint32_t frameIndex)
    {
        static const auto startTime = std::chrono::steady_clock::now();
        const auto currentTime = std::chrono::steady_clock::now();
        const float elapsedSeconds = std::chrono::duration<float>(currentTime - startTime).count();

        UniformBufferObject ubo{};
        ubo.model = glm::rotate(
            glm::mat4{1.0F},
            elapsedSeconds * glm::radians(90.0F),
            glm::vec3{0.0F, 0.0F, 1.0F});
        ubo.view = glm::lookAt(
            glm::vec3{2.0F, 2.0F, 2.0F},
            glm::vec3{0.0F, 0.0F, 0.0F},
            glm::vec3{0.0F, 0.0F, 1.0F});
        ubo.proj = glm::perspective(
            glm::radians(45.0F),
            static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height),
            0.1F, 10.0F);
        // GLM uses OpenGL's inverted clip-space Y convention by default.
        ubo.proj[1][1] *= -1.0F;

        std::memcpy(uniformBuffersMapped.at(frameIndex), &ubo, sizeof(ubo));
    }

    [[nodiscard]] AllocatedImage createImage(
        uint32_t width,
        uint32_t height,
        vk::Format format,
        vk::ImageTiling tiling,
        vk::ImageUsageFlags usage,
        vk::MemoryPropertyFlags properties,
        bool shareBetweenTransferAndGraphics = false)
    {
        // Texture uploads use both queue families, whereas depth attachments remain
        // exclusive to the graphics family for lower ownership-management overhead.
        const std::array queueFamilyIndices{
            transferQueueFamilyIndex,
            graphicsPresentQueueFamilyIndex,
        };
        const vk::ImageCreateInfo imageInfo{
            .flags = vk::ImageCreateFlags{0},
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = tiling,
            .usage = usage,
            .sharingMode = shareBetweenTransferAndGraphics ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = shareBetweenTransferAndGraphics ? static_cast<uint32_t>(queueFamilyIndices.size()) : 0,
            .pQueueFamilyIndices = shareBetweenTransferAndGraphics ? queueFamilyIndices.data() : nullptr,
            .initialLayout = vk::ImageLayout::eUndefined,
        };

        AllocatedImage allocatedImage{
            .memory = nullptr,
            .image = vk::raii::Image(device, imageInfo),
        };
        const vk::MemoryRequirements memoryRequirements =
            allocatedImage.image.getMemoryRequirements();
        const vk::MemoryAllocateInfo allocateInfo{
            .allocationSize = memoryRequirements.size,
            .memoryTypeIndex = findMemoryType(
                memoryRequirements.memoryTypeBits,
                properties),
        };

        allocatedImage.memory = vk::raii::DeviceMemory(device, allocateInfo);
        allocatedImage.image.bindMemory(*allocatedImage.memory, 0);
        return allocatedImage;
    }

    [[nodiscard]] vk::raii::CommandBuffer beginSingleTimeCommands(
        vk::raii::CommandPool& pool)
    {
        const vk::CommandBufferAllocateInfo allocateInfo{
            .commandPool = *pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        };
        auto commandBuffers = vk::raii::CommandBuffers(device, allocateInfo);
        auto commandBuffer = std::move(commandBuffers.front());
        const vk::CommandBufferBeginInfo beginInfo{
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
            .pInheritanceInfo = nullptr,
        };
        commandBuffer.begin(beginInfo);
        return commandBuffer;
    }

    void endSingleTimeCommands(vk::raii::CommandBuffer&& commandBuffer, vk::raii::Queue& queue)
    {
        commandBuffer.end();
        const vk::CommandBufferSubmitInfo commandBufferInfo{
            .commandBuffer = *commandBuffer,
            .deviceMask = 1,
        };
        const vk::SubmitInfo2 submitInfo{
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandBufferInfo,
        };
        const vk::FenceCreateInfo fenceInfo{
            .flags = vk::FenceCreateFlags{0},
        };
        const vk::raii::Fence completionFence(device, fenceInfo);

        queue.submit2(submitInfo, *completionFence);
        const vk::Result waitResult = device.waitForFences(
            *completionFence,
            vk::True,
            std::numeric_limits<uint64_t>::max());
        if (waitResult != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to wait for a one-time command fence!");
        }
    }

    void createDepthResources()
    {
        if (*depthImage != nullptr || *depthImageMemory != nullptr || *depthImageView != nullptr) {
            throw std::logic_error("Depth resources have already been created!");
        }

        depthFormat = findDepthFormat();
        auto allocatedDepthImage = createImage(
            swapChainExtent.width,
            swapChainExtent.height,
            depthFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        depthImageMemory = std::move(allocatedDepthImage.memory);
        depthImage = std::move(allocatedDepthImage.image);
        depthImageView = createImageView(
            *depthImage,
            depthFormat,
            vk::ImageAspectFlagBits::eDepth);
    }

    [[nodiscard]] vk::Format findDepthFormat() const
    {
        constexpr std::array candidates{
            vk::Format::eD32Sfloat,
            vk::Format::eD32SfloatS8Uint,
            vk::Format::eD24UnormS8Uint,
        };
        return findSupportedFormat(
            candidates,
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eDepthStencilAttachment);
    }

    [[nodiscard]] vk::Format findSupportedFormat(std::span<const vk::Format> candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags requiredFeatures) const
    {
        const auto supportedFormat = std::ranges::find_if(
            candidates,
            [&](vk::Format candidate) {
                const vk::FormatProperties properties = physicalDevice.getFormatProperties(candidate);
                const vk::FormatFeatureFlags availableFeatures =
                    tiling == vk::ImageTiling::eLinear ? properties.linearTilingFeatures : properties.optimalTilingFeatures;
                return (availableFeatures & requiredFeatures) == requiredFeatures;
            });
        if (supportedFormat == candidates.end()) {
            throw std::runtime_error("Failed to find a supported image format!");
        }

        return *supportedFormat;
    }

    static void transitionTextureImageLayout(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Image& image,
        vk::ImageLayout oldLayout,vk::ImageLayout newLayout)
    {
        vk::PipelineStageFlags2 sourceStageMask;
        vk::AccessFlags2 sourceAccessMask;
        vk::PipelineStageFlags2 destinationStageMask;
        vk::AccessFlags2 destinationAccessMask;

        if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
            sourceStageMask = vk::PipelineStageFlagBits2::eNone;
            sourceAccessMask = vk::AccessFlags2{0};
            destinationStageMask = vk::PipelineStageFlagBits2::eTransfer;
            destinationAccessMask = vk::AccessFlagBits2::eTransferWrite;
        } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
            // The inter-queue semaphore supplies the transfer-write dependency;
            // this barrier runs on the graphics queue and makes it visible to shaders.
            sourceStageMask = vk::PipelineStageFlagBits2::eNone;
            sourceAccessMask = vk::AccessFlags2{0};
            destinationStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
            destinationAccessMask = vk::AccessFlagBits2::eShaderRead;
        } else {
            throw std::invalid_argument("Unsupported texture image layout transition!");
        }

        const vk::ImageMemoryBarrier2 barrier{
            .srcStageMask = sourceStageMask,
            .srcAccessMask = sourceAccessMask,
            .dstStageMask = destinationStageMask,
            .dstAccessMask = destinationAccessMask,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = *image,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        const vk::DependencyInfo dependencyInfo{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        commandBuffer.pipelineBarrier2(dependencyInfo);
    }

    static void copyBufferToImage(
        vk::raii::CommandBuffer& commandBuffer,
        const vk::raii::Buffer& buffer,
        const vk::raii::Image& image,
        uint32_t width,
        uint32_t height)
    {
        const vk::BufferImageCopy copyRegion{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, 1},
        };
        commandBuffer.copyBufferToImage(
            *buffer,
            *image,
            vk::ImageLayout::eTransferDstOptimal,
            copyRegion);
    }

    void uploadTextureImage(const vk::raii::Buffer& stagingBuffer, uint32_t width, uint32_t height)
    {
        auto transferCommandBuffer = beginSingleTimeCommands(transferCommandPool);
        transitionTextureImageLayout(
            transferCommandBuffer,
            textureImage,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal);
        copyBufferToImage(
            transferCommandBuffer,
            stagingBuffer,
            textureImage,
            width,
            height);
        transferCommandBuffer.end();

        const vk::SemaphoreCreateInfo semaphoreInfo{
            .flags = vk::SemaphoreCreateFlags{0},
        };
        const vk::raii::Semaphore transferCompleteSemaphore(device, semaphoreInfo);
        const vk::CommandBufferSubmitInfo transferCommandInfo{
            .commandBuffer = *transferCommandBuffer,
            .deviceMask = 1,
        };
        const vk::SemaphoreSubmitInfo signalSemaphoreInfo{
            .semaphore = *transferCompleteSemaphore,
            .value = 0,
            .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .deviceIndex = 0,
        };
        const vk::SubmitInfo2 transferSubmitInfo{
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &transferCommandInfo,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &signalSemaphoreInfo,
        };
        transferQueue.submit2(transferSubmitInfo, nullptr);

        // A pure transfer queue cannot use the fragment-shader pipeline stage.
        // Hand the dependency to the graphics queue before the final transition.
        auto graphicsCommandBuffer = beginSingleTimeCommands(commandPool);
        transitionTextureImageLayout(
            graphicsCommandBuffer,
            textureImage,
            vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal);
        graphicsCommandBuffer.end();

        const vk::SemaphoreSubmitInfo waitSemaphoreInfo{
            .semaphore = *transferCompleteSemaphore,
            .value = 0,
            .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .deviceIndex = 0,
        };
        const vk::CommandBufferSubmitInfo graphicsCommandInfo{
            .commandBuffer = *graphicsCommandBuffer,
            .deviceMask = 1,
        };
        const vk::SubmitInfo2 graphicsSubmitInfo{
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &waitSemaphoreInfo,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &graphicsCommandInfo,
        };
        const vk::FenceCreateInfo fenceInfo{
            .flags = vk::FenceCreateFlags{0},
        };
        const vk::raii::Fence uploadCompleteFence(device, fenceInfo);
        graphicsPresentQueue.submit2(graphicsSubmitInfo, *uploadCompleteFence);

        const vk::Result waitResult = device.waitForFences(
            *uploadCompleteFence,
            vk::True,
            std::numeric_limits<uint64_t>::max());
        if (waitResult != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to wait for the texture upload fence!");
        }
    }

    void createTextureImage()
    {
        int textureWidth = 0; int textureHeight = 0; int textureChannels = 0;

        using StbiPixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
        StbiPixels pixels{
            stbi_load(TEXTURE_PATH, &textureWidth, &textureHeight, &textureChannels, STBI_rgb_alpha),
            &stbi_image_free,
        };
        if (!pixels || textureWidth <= 0 || textureHeight <= 0) {
            const char* failureReason = stbi_failure_reason();
            throw std::runtime_error(
                std::string{"Failed to load texture image: "} +
                (failureReason ? failureReason : "unknown stb_image error"));
        }
        const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(textureWidth) * static_cast<vk::DeviceSize>(textureHeight) * STBI_rgb_alpha;
        
        auto stagingBuffer = createBuffer(
            imageSize, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        void* mappedMemory = stagingBuffer.memory.mapMemory(0, imageSize);
        std::memcpy(mappedMemory, pixels.get(), static_cast<std::size_t>(imageSize));
        stagingBuffer.memory.unmapMemory();
        pixels.reset();

        auto allocatedTexture = createImage(
            static_cast<uint32_t>(textureWidth),
            static_cast<uint32_t>(textureHeight),
            TEXTURE_FORMAT,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            true);
        textureImageMemory = std::move(allocatedTexture.memory);
        textureImage = std::move(allocatedTexture.image);

        uploadTextureImage(
            stagingBuffer.buffer,
            static_cast<uint32_t>(textureWidth),
            static_cast<uint32_t>(textureHeight));
    }

    void createTextureImageView()
    {
        // Images are accessed by shaders through a view, not through VkImage directly.
        textureImageView = createImageView(*textureImage, TEXTURE_FORMAT, vk::ImageAspectFlagBits::eColor);
    }

    void createTextureSampler()
    {
        const vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
        const vk::SamplerCreateInfo samplerInfo{
            .flags = vk::SamplerCreateFlags{0},
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .mipLodBias = 0.0F,
            // Use the highest anisotropy supported by the selected physical device.
            .anisotropyEnable = vk::True,
            .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
            .compareEnable = vk::False,
            .compareOp = vk::CompareOp::eAlways,
            .minLod = 0.0F,
            .maxLod = 0.0F,
            .borderColor = vk::BorderColor::eIntOpaqueBlack,
            // Normalized UV coordinates keep sampling independent of image dimensions.
            .unnormalizedCoordinates = vk::False,
        };

        textureSampler = vk::raii::Sampler(device, samplerInfo);
    }

    [[nodiscard]] AllocatedBuffer createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
    {
        const std::array queueFamilyIndices{
            graphicsPresentQueueFamilyIndex,
            transferQueueFamilyIndex,
        };
        // Concurrent sharing lets the transfer family write the buffer and the
        // graphics family read it without explicit ownership-release/acquire barriers.
        const vk::BufferCreateInfo bufferInfo{
            .flags = vk::BufferCreateFlags{0},
            .size = size,
            .usage = usage,
            .sharingMode = vk::SharingMode::eConcurrent,
            .queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size()),
            .pQueueFamilyIndices = queueFamilyIndices.data(),
        };

        AllocatedBuffer allocatedBuffer{
            .memory = nullptr,
            .buffer = vk::raii::Buffer(device, bufferInfo),
        };

        // A Vulkan buffer does not own storage; query its size/alignment/type mask,
        // then allocate a compatible memory type and bind it at offset zero.
        const vk::MemoryRequirements memoryRequirements =
            allocatedBuffer.buffer.getMemoryRequirements();
        const vk::MemoryAllocateInfo allocateInfo{
            .allocationSize = memoryRequirements.size,
            .memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties),
        };

        allocatedBuffer.memory = vk::raii::DeviceMemory(device, allocateInfo);
        allocatedBuffer.buffer.bindMemory(*allocatedBuffer.memory, 0);

        return allocatedBuffer;
    }

    // Copy data from a host-visible buffer to a device-local buffer using a one-time command buffer
    void copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size)
    {
        auto commandBuffer = beginSingleTimeCommands(transferCommandPool);

        const vk::BufferCopy copyRegion{
            .srcOffset = 0,
            .dstOffset = 0,
            .size = size,
        };
        commandBuffer.copyBuffer(*srcBuffer, *dstBuffer, copyRegion);
        endSingleTimeCommands(std::move(commandBuffer), transferQueue);
    }

    void createDescriptorPool()
    {
        if (*descriptorPool != nullptr) {
            throw std::logic_error("Descriptor pool has already been created!");
        }

        // Each frame owns one UBO descriptor and one combined image sampler.
        constexpr std::array poolSizes{
            vk::DescriptorPoolSize{
                .type = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = MAX_FRAMES_IN_FLIGHT,
            },
            vk::DescriptorPoolSize{
                .type = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = MAX_FRAMES_IN_FLIGHT,
            },
        };
        const vk::DescriptorPoolCreateInfo poolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = MAX_FRAMES_IN_FLIGHT,
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.data(),
        };

        descriptorPool = vk::raii::DescriptorPool(device, poolInfo);
    }

    void createDescriptorSets()
    {
        if (*descriptorPool == nullptr) {
            throw std::logic_error("Descriptor pool must be created before descriptor sets!");
        }
        if (*descriptorSetLayout == nullptr) {
            throw std::logic_error("Descriptor set layout must be created before descriptor sets!");
        }
        if (uniformBuffers.size() != MAX_FRAMES_IN_FLIGHT) {
            throw std::logic_error("One uniform buffer is required for every frame in flight!");
        }
        if (*textureImageView == nullptr || *textureSampler == nullptr) {
            throw std::logic_error("Texture image view and sampler must be created before descriptor sets!");
        }
        if (!descriptorSets.empty()) {
            throw std::logic_error("Descriptor sets have already been created!");
        }

        std::array<vk::DescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> descriptorSetLayouts;
        std::ranges::fill(descriptorSetLayouts, *descriptorSetLayout);
        const vk::DescriptorSetAllocateInfo allocateInfo{
            .descriptorPool = *descriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
            .pSetLayouts = descriptorSetLayouts.data(),
        };

        descriptorSets = vk::raii::DescriptorSets(device, allocateInfo);

        for (const uint32_t frameIndex : std::views::iota(uint32_t{0}, MAX_FRAMES_IN_FLIGHT)) {
            const vk::DescriptorBufferInfo bufferInfo{
                .buffer = *uniformBuffers.at(frameIndex),
                .offset = 0,
                .range = sizeof(UniformBufferObject),
            };
            const vk::DescriptorImageInfo imageInfo{
                .sampler = *textureSampler,
                .imageView = *textureImageView,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            };
            const std::array descriptorWrites{
                vk::WriteDescriptorSet{
                    .dstSet = *descriptorSets.at(frameIndex),
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                    .pImageInfo = nullptr,
                    .pBufferInfo = &bufferInfo,
                    .pTexelBufferView = nullptr,
                },
                vk::WriteDescriptorSet{
                    .dstSet = *descriptorSets.at(frameIndex),
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .pImageInfo = &imageInfo,
                    .pBufferInfo = nullptr,
                    .pTexelBufferView = nullptr,
                },
            };

            device.updateDescriptorSets(descriptorWrites, {});
        }
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

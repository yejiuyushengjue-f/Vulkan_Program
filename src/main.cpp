#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
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
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;
    vk::raii::Queue graphicsQueue = nullptr;
    uint32_t graphicsQueueFamilyIndex = 0;

    void initWindow() {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    }

    void initVulkan() {
        createInstance();
        setupDebugMessenger();
        pickPhysicalDevice();
        createLogicalDevice();
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
        }
    }

    void cleanup() {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void createInstance() {
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
    std::vector<const char*> getRequiredInstanceExtensions() {
        uint32_t glfwExtensionCount = 0;
        auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        if (enableValidationLayers) {
            extensions.push_back(vk::EXTDebugUtilsExtensionName);
        }

        return extensions;
    }

    // Set up debug callback function
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
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

    bool isDeviceSuitable(const vk::raii::PhysicalDevice& device) {
        // Get the device type, supported API version, limits, and other properties
        const auto properties = device.getProperties();

        // Check if the device supports Vulkan 1.3 or higher
        if (properties.apiVersion < vk::ApiVersion13) {
            return false;
        }

        // Check if the device has a graphics queue
        if (!findGraphicsQueueFamily(device)) {
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

        // Check if the device supports the required features
        const auto features = device.template getFeatures2<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();

        const bool supportsRequiredFeatures =
            features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
            features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

        return supportsRequiredFeatures;
    }

    // Pick the first physical device that satisfies every required capability
    void pickPhysicalDevice() {
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
        graphicsQueueFamilyIndex = findGraphicsQueueFamily(physicalDevice).value();
    }

    // Find the first queue family that can execute graphics commands
    std::optional<uint32_t> findGraphicsQueueFamily(const vk::raii::PhysicalDevice& device) const {
        const auto queueFamilies = device.getQueueFamilyProperties();
        const auto graphicsQueueFamily = std::ranges::find_if(
            queueFamilies,
            [](const vk::QueueFamilyProperties& queueFamily) {
                return queueFamily.queueCount > 0 &&
                       static_cast<bool>(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics);
            });

        if (graphicsQueueFamily == queueFamilies.end()) {
            return std::nullopt;
        }

        return static_cast<uint32_t>(std::distance(queueFamilies.begin(), graphicsQueueFamily));
    }

    void createLogicalDevice() {
        // Create one graphics queue; priorities must remain valid until device creation returns
        constexpr float queuePriority = 1.0F;
        const vk::DeviceQueueCreateInfo queueCreateInfo{
            .queueFamilyIndex = graphicsQueueFamilyIndex,
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
                {.dynamicRendering = true},             // Vulkan 1.3 dynamic rendering
                {.extendedDynamicState = true},         // Extended dynamic state feature
            };

        // Device extensions are separate from the instance extensions enabled earlier
        const vk::DeviceCreateInfo createInfo{
            .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
            .ppEnabledExtensionNames = requiredDeviceExtensions.data(),
        };

        // Create the logical device, then retrieve queue 0 from the selected graphics family
        // The queue is owned by the logical device and is destroyed with it
        device = vk::raii::Device(physicalDevice, createInfo);
        graphicsQueue = vk::raii::Queue(device, graphicsQueueFamilyIndex, 0);
    }
};

int main() {
    try {
        HelloTriangleApplication app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

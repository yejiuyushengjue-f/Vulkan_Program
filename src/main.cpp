#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>

const int WIDTH = 1200;
const int HEIGHT = 900;

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
    VkInstance instance = VK_NULL_HANDLE;

    void initWindow() {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    }

    void initVulkan() {
        createInstance();
        // Vulkan initialization code goes here
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
        }
        // Main rendering loop code goes here
    }

    void cleanup() {
        vkDestroyInstance(instance, nullptr);
        
        glfwDestroyWindow(window);

        glfwTerminate();
        // Cleanup code goes here
    }

    void createInstance() {
        // Vulkan Application Info
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Hello Triangle";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        // Vulkan Instance Create Info
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        
        // Get required extensions from GLFW
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        createInfo.enabledExtensionCount = glfwExtensionCount;
        createInfo.ppEnabledExtensionNames = glfwExtensions;

        // Check for extension support
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

        std::cout << "Available Vulkan extensions:\n";
        for (const auto& extension : availableExtensions) {
            std::cout << "\t" << extension.extensionName << "\n";
        }

        if (checkExtensionSupport(glfwExtensions, glfwExtensionCount, availableExtensions)) {
            std::cout << "All required extensions are supported.\n";
        } else {
            throw std::runtime_error("Not all required extensions are supported.");
        }

        // No validation layers for now
        createInfo.enabledLayerCount = 0;

        // Create Vulkan Instance
        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Vulkan instance");
        }
    }

    bool checkExtensionSupport(const char** requiredExtensions, uint32_t requiredCount, const std::vector<VkExtensionProperties>& availableExtensions) {
        for (uint32_t i = 0; i < requiredCount; ++i) {
            bool found = false;
            for (const auto& extension : availableExtensions) {
                if (strcmp(requiredExtensions[i], extension.extensionName) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cerr << "Required extension not found: " << requiredExtensions[i] << "\n";
                return false;
            }
        }
        return true;
    }
};

int main() {
    HelloTriangleApplication app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

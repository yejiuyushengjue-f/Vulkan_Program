cmake_minimum_required(VERSION 3.29)

project(vulkan LANGUAGES C CXX)

option(ENABLE_CPP20_MODULE "Enable C++20 module support for Vulkan" OFF)

if(ENABLE_CPP20_MODULE)
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
else()
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
endif()

find_package(Vulkan 1.4.335 REQUIRED)
find_program(SLANGC_EXECUTABLE
    NAMES slangc slangc.exe
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "${Vulkan_INCLUDE_DIR}/../Bin"
    REQUIRED
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(deps/glfw)
add_subdirectory(deps/glm)

if(ENABLE_CPP20_MODULE)
    add_library(VulkanCppModule)
    add_library(Vulkan::cppm ALIAS VulkanCppModule)

    target_compile_definitions(VulkanCppModule PUBLIC
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS=1
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
    )
    target_include_directories(VulkanCppModule PUBLIC "${Vulkan_INCLUDE_DIR}")
    target_link_libraries(VulkanCppModule PUBLIC Vulkan::Vulkan)
    set_target_properties(VulkanCppModule PROPERTIES CXX_STANDARD 20)

    target_sources(VulkanCppModule PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${Vulkan_INCLUDE_DIR}"
        FILES "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
    )
else()
    add_library(VulkanCppModule INTERFACE)
    add_library(Vulkan::cppm ALIAS VulkanCppModule)

    target_link_libraries(VulkanCppModule INTERFACE Vulkan::Vulkan)
    target_compile_definitions(VulkanCppModule INTERFACE
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS=1
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
    )
endif()

add_executable(${PROJECT_NAME} src/main.cpp)

set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.slang")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(SHADER_SPIRV "${SHADER_OUTPUT_DIR}/slang.spv")

add_custom_command(
    OUTPUT "${SHADER_SPIRV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUTPUT_DIR}"
    COMMAND "${SLANGC_EXECUTABLE}"
        "${SHADER_SOURCE}"
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -entry vertMain
        -entry fragMain
        -o "${SHADER_SPIRV}"
    DEPENDS "${SHADER_SOURCE}"
    COMMENT "Compiling Slang shader to SPIR-V"
    VERBATIM
)

add_custom_target(compile_shaders DEPENDS "${SHADER_SPIRV}")
add_dependencies(${PROJECT_NAME} compile_shaders)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    SHADER_SPIRV_PATH="${SHADER_SPIRV}"
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::cppm
    glfw
    glm::glm
)

if(ENABLE_CPP20_MODULE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_CPP20_MODULES=1)
endif()

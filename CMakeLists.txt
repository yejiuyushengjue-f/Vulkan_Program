cmake_minimum_required(VERSION 3.20)

project(vulkan)

# cpp 20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---
# ---: Dependences    [C0301] Line too long (92/80)
# ---
# ---: Targets    [C0301] Line too long (88/80)

file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS "src/*.cpp")    # source files
file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS "include/*.h")    # header files

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE
    include
)
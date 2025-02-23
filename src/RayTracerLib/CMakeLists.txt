cmake_minimum_required(VERSION 3.14)
project(RayTracerLib)

add_subdirectory(lib)

set(sourceFiles
    BaseApp.cpp
)

add_library(RayTracerLib ${sourceFiles})

find_package(OpenGL REQUIRED)

target_include_directories(RayTracerLib PUBLIC include)

target_link_libraries(RayTracerLib PRIVATE glfw glad glm TracyClient spdlog imgui)

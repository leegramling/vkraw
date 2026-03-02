cmake_minimum_required(VERSION 3.15)

project(vkcornell)
file(GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp *.hpp)

set(RECIPE_NAME "vkcornell")

add_executable(${RECIPE_NAME} ${sources})

target_compile_features(${RECIPE_NAME} PUBLIC cxx_std_20)
set_property(TARGET ${RECIPE_NAME} PROPERTY CMAKE_CXX_STANDARD 20)

include_directories(${PROJECT_SOURCE_DIR}/../)

target_link_libraries(${RECIPE_NAME} PUBLIC VulkanCore EngineCore glfw ${RequiredVulkanSDKLIBS} spirv-reflect-static TracyClient meshoptimizer)

file(COPY resources DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

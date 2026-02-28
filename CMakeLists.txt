cmake_minimum_required(VERSION 3.22)

project(vkRaw LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(VK_BOOTSTRAP_VULKAN_HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan-Headers/include" CACHE FILEPATH "" FORCE)

add_subdirectory(external/vk-bootstrap)
add_subdirectory(external/glfw)
add_subdirectory(external/glm)

find_package(Vulkan REQUIRED)

set(_VKRAW_USE_SYSTEM_IMAGE_LIBS_DEFAULT ON)
if(WIN32)
    set(_VKRAW_USE_SYSTEM_IMAGE_LIBS_DEFAULT OFF)
endif()
option(VKRAW_USE_SYSTEM_IMAGE_LIBS "Enable optional TIFF file texture decoding in vkraw via system libraries" ${_VKRAW_USE_SYSTEM_IMAGE_LIBS_DEFAULT})

set(VKRAW_HAS_SYSTEM_IMAGE_LIBS OFF)
if(VKRAW_USE_SYSTEM_IMAGE_LIBS)
    find_package(TIFF QUIET)
    if(TIFF_FOUND)
        set(VKRAW_HAS_SYSTEM_IMAGE_LIBS ON)
    else()
        message(WARNING "TIFF library not found. vkraw .tif/.tiff texture loading will be disabled; .jpg/.jpeg/.png via stb_image remain enabled.")
    endif()
endif()

set(VSG_DEPS_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vsg_deps/install" CACHE PATH "Path to prebuilt VSG dependencies install prefix")
option(VKRAW_BUILD_VKVSG "Build VSG-based renderer target (vkvsg)" ON)

if(VKRAW_BUILD_VKVSG)
    if(EXISTS "${VSG_DEPS_INSTALL_DIR}")
        list(APPEND CMAKE_PREFIX_PATH "${VSG_DEPS_INSTALL_DIR}")
    endif()
    find_package(vsg CONFIG REQUIRED)
    find_package(vsgImGui CONFIG REQUIRED)
    find_package(vsgXchange CONFIG QUIET)
endif()

add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    external/imgui
    external/imgui/backends
)

target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

find_program(GLSLC_EXECUTABLE NAMES glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)
find_program(GLSLANG_VALIDATOR_EXECUTABLE NAMES glslangValidator HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin bin)
option(VKRAW_ALLOW_PLACEHOLDER_SHADERS "Allow build without shader compiler by generating empty shader binaries" OFF)

if(GLSLC_EXECUTABLE)
    set(VK_SHADER_COMPILER "${GLSLC_EXECUTABLE}")
    set(VK_SHADER_ARGS "")
elseif(GLSLANG_VALIDATOR_EXECUTABLE)
    set(VK_SHADER_COMPILER "${GLSLANG_VALIDATOR_EXECUTABLE}")
    set(VK_SHADER_ARGS -V)
else()
    if(VKRAW_ALLOW_PLACEHOLDER_SHADERS)
        message(WARNING "No shader compiler found. Building with placeholder shader binaries; runtime rendering will fail.")
    else()
        message(FATAL_ERROR "No shader compiler found. Install Vulkan SDK (glslc) or glslangValidator.")
    endif()
endif()

set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

set(VKRAW_SHADER_SOURCES
    cube.vert
    cube.frag
    equator_line.vert
    equator_line.frag
)
set(VKRAW_SHADER_OUTPUTS "")

if(VK_SHADER_COMPILER)
    foreach(shader_src ${VKRAW_SHADER_SOURCES})
        set(shader_in ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader_src})
        set(shader_out ${SHADER_OUTPUT_DIR}/${shader_src}.spv)
        list(APPEND VKRAW_SHADER_OUTPUTS ${shader_out})

        add_custom_command(
            OUTPUT ${shader_out}
            COMMAND ${VK_SHADER_COMPILER} ${VK_SHADER_ARGS} ${shader_in} -o ${shader_out}
            DEPENDS ${shader_in}
            VERBATIM
        )
    endforeach()
else()
    foreach(shader_src ${VKRAW_SHADER_SOURCES})
        set(shader_in ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader_src})
        set(shader_out ${SHADER_OUTPUT_DIR}/${shader_src}.spv)
        list(APPEND VKRAW_SHADER_OUTPUTS ${shader_out})

        add_custom_command(
            OUTPUT ${shader_out}
            COMMAND ${CMAKE_COMMAND} -E touch ${shader_out}
            DEPENDS ${shader_in}
            VERBATIM
        )
    endforeach()
endif()

add_custom_target(shaders ALL
    DEPENDS ${VKRAW_SHADER_OUTPUTS}
)

add_executable(vkraw
    src/vkraw/main.cpp
    src/vkraw/VkVisualizerLifecycle.cpp
    src/vkraw/VkVisualizerDevice.cpp
    src/vkraw/VkVisualizerResources.cpp
    src/vkraw/VkVisualizerImGui.cpp
    src/vkraw/VkVisualizerFrame.cpp
    src/vkraw/setup/SwapchainSetup.cpp
    src/vkraw/setup/RenderPassSetup.cpp
    src/vkraw/setup/FramebufferSetup.cpp
    src/vkraw/setup/PipelineSetup.cpp
)

add_dependencies(vkraw shaders)

target_include_directories(vkraw PRIVATE src external/glfw/deps external/stb)

target_link_libraries(vkraw PRIVATE
    vk-bootstrap::vk-bootstrap
    glfw
    glm::glm
    imgui
    Vulkan::Vulkan
)

if(VKRAW_HAS_SYSTEM_IMAGE_LIBS)
    target_link_libraries(vkraw PRIVATE TIFF::TIFF)
    target_compile_definitions(vkraw PRIVATE VKRAW_ENABLE_IMAGE_FILE_IO=1)
endif()

target_compile_definitions(vkraw PRIVATE GLFW_INCLUDE_NONE)
target_compile_definitions(vkraw PRIVATE VKRAW_SHADER_DIR="${SHADER_OUTPUT_DIR}")

if(WIN32)
    target_compile_definitions(vkraw PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

if(VKRAW_BUILD_VKVSG)
    add_executable(vkvsg
        src/vkvsg/main.cpp
        src/vkvsg/VsgVisualizer.cpp
    )

    target_include_directories(vkvsg PRIVATE src)

    target_link_libraries(vkvsg PRIVATE
        vsg::vsg
        vsgImGui::vsgImGui
    )

    if(vsgXchange_FOUND)
        target_link_libraries(vkvsg PRIVATE vsgXchange::vsgXchange)
        target_compile_definitions(vkvsg PRIVATE VKVSG_HAS_VSGXCHANGE)
    endif()

    if(WIN32)
        target_compile_definitions(vkvsg PRIVATE VK_USE_PLATFORM_WIN32_KHR)
    endif()
    target_compile_definitions(vkvsg PRIVATE VKVSG_EXAMPLES_DIR="${VSG_DEPS_INSTALL_DIR}/share/vsgExamples")
    target_compile_definitions(vkvsg PRIVATE VKVSG_SHADER_DIR="${SHADER_OUTPUT_DIR}")
    add_dependencies(vkvsg shaders)

    add_executable(vkglobe
        src/vkglobe/main.cpp
        src/vkglobe/VsgVisualizer.cpp
        src/vkglobe/GlobeTileLayer.cpp
        src/vkglobe/OsmProjection.cpp
        src/vkglobe/OsmTileFetcher.cpp
        src/vkglobe/OsmTileManager.cpp
    )

    target_include_directories(vkglobe PRIVATE src)

    target_link_libraries(vkglobe PRIVATE
        vsg::vsg
        vsgImGui::vsgImGui
    )

    if(vsgXchange_FOUND)
        target_link_libraries(vkglobe PRIVATE vsgXchange::vsgXchange)
        target_compile_definitions(vkglobe PRIVATE VKVSG_HAS_VSGXCHANGE)
    endif()

    if(WIN32)
        target_compile_definitions(vkglobe PRIVATE VK_USE_PLATFORM_WIN32_KHR)
    endif()
    target_compile_definitions(vkglobe PRIVATE VKVSG_EXAMPLES_DIR="${VSG_DEPS_INSTALL_DIR}/share/vsgExamples")
    target_compile_definitions(vkglobe PRIVATE VKVSG_SHADER_DIR="${SHADER_OUTPUT_DIR}")
    add_dependencies(vkglobe shaders)
endif()

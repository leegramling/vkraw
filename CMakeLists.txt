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

option(VKRAW_BUILD_VKCORNELL "Build vkcornell app using copied vulkancore/enginecore under src/" ON)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(VK_BOOTSTRAP_VULKAN_HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan-Headers/include" CACHE FILEPATH "" FORCE)

add_subdirectory(external/vk-bootstrap)
add_subdirectory(external/glfw)
add_subdirectory(external/glm)

find_package(Vulkan REQUIRED)

if(VKRAW_BUILD_VKCORNELL)
    include(FetchContent)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

    FetchContent_Declare(
        volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG 1.3.270
    )
    FetchContent_Declare(
        vma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.0.1
    )
    FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.9.1
        GIT_SHALLOW TRUE
    )
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
    )
    FetchContent_MakeAvailable(volk vma tracy stb)
endif()

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
option(VKRAW_BUILD_VSGDOCK "Build docking-experiment VSG app (vsgdock) with isolated ImGui/VSGImGui dependencies" ON)

if(VKRAW_BUILD_VKVSG)
    if(EXISTS "${VSG_DEPS_INSTALL_DIR}")
        list(APPEND CMAKE_PREFIX_PATH "${VSG_DEPS_INSTALL_DIR}")
    endif()
    find_package(vsg CONFIG REQUIRED)
    find_package(vsgImGui CONFIG REQUIRED)
    find_package(vsgXchange CONFIG QUIET)
endif()

set(VSGDOCK_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui-docking")
set(VSGDOCK_VSGIMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vsgImGui-docking")

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

if(WIN32)
    add_custom_target(docs
        COMMAND cmd /c "${CMAKE_CURRENT_SOURCE_DIR}/docs/build_docs.bat"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Natural Docs HTML output"
    )
else()
    add_custom_target(docs
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/docs/build_docs.sh"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Natural Docs HTML output"
    )
endif()

if(VKRAW_BUILD_VKCORNELL)
    set(VKCORNELL_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/vkcornell_shaders)
    file(MAKE_DIRECTORY ${VKCORNELL_SHADER_OUTPUT_DIR})
    set(VKCORNELL_SHADER_SOURCES
        cornell.vert
        cornell.frag
    )
    set(VKCORNELL_SHADER_OUTPUTS "")
    foreach(shader_src ${VKCORNELL_SHADER_SOURCES})
        set(shader_in ${CMAKE_CURRENT_SOURCE_DIR}/src/vkcornell/resources/shaders/${shader_src})
        set(shader_out ${VKCORNELL_SHADER_OUTPUT_DIR}/${shader_src}.spv)
        list(APPEND VKCORNELL_SHADER_OUTPUTS ${shader_out})

        if(VK_SHADER_COMPILER)
            add_custom_command(
                OUTPUT ${shader_out}
                COMMAND ${VK_SHADER_COMPILER} ${VK_SHADER_ARGS} ${shader_in} -o ${shader_out}
                DEPENDS ${shader_in}
                VERBATIM
            )
        else()
            add_custom_command(
                OUTPUT ${shader_out}
                COMMAND ${CMAKE_COMMAND} -E touch ${shader_out}
                DEPENDS ${shader_in}
                VERBATIM
            )
        endif()
    endforeach()

    add_custom_target(vkcornell_shaders ALL DEPENDS ${VKCORNELL_SHADER_OUTPUTS})
endif()

set(VKRAW_APP_SOURCES
    src/core/AppRunner.cpp
    src/core/vulkan/SwapchainSetup.cpp
    src/core/vulkan/RenderPassSetup.cpp
    src/core/vulkan/FramebufferSetup.cpp
    src/core/vulkan/PipelineSetup.cpp
    src/core/runtime/VkVisualizerLifecycle.cpp
    src/core/runtime/VkVisualizerDevice.cpp
    src/core/runtime/VkVisualizerResources.cpp
    src/core/runtime/VkVisualizerImGui.cpp
    src/core/runtime/VkVisualizerFrame.cpp
    src/vkscene/GltfModelObject.cpp
)

add_executable(vkraw
    src/vkraw/main.cpp
    ${VKRAW_APP_SOURCES}
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

add_executable(vkScene
    src/vkscene/main.cpp
    ${VKRAW_APP_SOURCES}
)

add_dependencies(vkScene shaders)

target_include_directories(vkScene PRIVATE src external/glfw/deps external/stb)

target_link_libraries(vkScene PRIVATE
    vk-bootstrap::vk-bootstrap
    glfw
    glm::glm
    imgui
    Vulkan::Vulkan
)

if(VKRAW_HAS_SYSTEM_IMAGE_LIBS)
    target_link_libraries(vkScene PRIVATE TIFF::TIFF)
    target_compile_definitions(vkScene PRIVATE VKRAW_ENABLE_IMAGE_FILE_IO=1)
endif()

target_compile_definitions(vkScene PRIVATE GLFW_INCLUDE_NONE)
target_compile_definitions(vkScene PRIVATE VKRAW_SHADER_DIR="${SHADER_OUTPUT_DIR}")

if(WIN32)
    target_compile_definitions(vkScene PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

add_executable(procRhai
    src/procRhai/main.cpp
)
target_compile_features(procRhai PRIVATE cxx_std_20)
if(UNIX AND NOT APPLE)
    target_link_libraries(procRhai PRIVATE ${CMAKE_DL_LIBS})
endif()

if(WIN32)
    add_custom_target(procRhai_bundle
        COMMAND cmd /c "${CMAKE_CURRENT_SOURCE_DIR}/src/procRhai/copy_animvm.bat ${CMAKE_CURRENT_BINARY_DIR}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS procRhai
        COMMENT "Building and copying animvm.dll next to procRhai"
    )
else()
    add_custom_target(procRhai_bundle
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/src/procRhai/copy_animvm.sh" "${CMAKE_CURRENT_BINARY_DIR}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS procRhai
        COMMENT "Building and copying libanimvm next to procRhai"
    )
endif()

if(VKRAW_BUILD_VKVSG)
    add_executable(vkvsg
        src/vkvsg/main.cpp
        src/vkvsg/VsgVisualizer.cpp
        src/vkvsg/LineObject.cpp
        src/vkvsg/TileGeo.cpp
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

if(VKRAW_BUILD_VSGDOCK)
    if(EXISTS "${VSGDOCK_IMGUI_DIR}" AND EXISTS "${VSGDOCK_VSGIMGUI_DIR}")
        add_library(vsgImGuiDocking STATIC
            ${VSGDOCK_VSGIMGUI_DIR}/include/vsgImGui/imgui.h
            ${VSGDOCK_VSGIMGUI_DIR}/include/vsgImGui/SendEventsToImGui.h
            ${VSGDOCK_VSGIMGUI_DIR}/include/vsgImGui/RenderImGui.h
            ${VSGDOCK_VSGIMGUI_DIR}/include/vsgImGui/Texture.h
            ${VSGDOCK_VSGIMGUI_DIR}/src/vsgImGui/RenderImGui.cpp
            ${VSGDOCK_VSGIMGUI_DIR}/src/vsgImGui/SendEventsToImGui.cpp
            ${VSGDOCK_VSGIMGUI_DIR}/src/vsgImGui/Texture.cpp
            ${VSGDOCK_VSGIMGUI_DIR}/src/imgui/imgui.cpp
            ${VSGDOCK_VSGIMGUI_DIR}/src/imgui/imgui_draw.cpp
            ${VSGDOCK_VSGIMGUI_DIR}/src/imgui/imgui_tables.cpp
            ${VSGDOCK_VSGIMGUI_DIR}/src/imgui/imgui_widgets.cpp
            ${VSGDOCK_VSGIMGUI_DIR}/src/imgui/imgui_demo.cpp
            ${VSGDOCK_VSGIMGUI_DIR}/src/imgui/backends/imgui_impl_vulkan.cpp
            ${VSGDOCK_VSGIMGUI_DIR}/src/imgui/misc/cpp/imgui_stdlib.cpp
            ${VSGDOCK_VSGIMGUI_DIR}/src/implot/implot.cpp
            ${VSGDOCK_VSGIMGUI_DIR}/src/implot/implot_items.cpp
            ${VSGDOCK_VSGIMGUI_DIR}/src/implot/implot_demo.cpp
        )

        target_compile_definitions(vsgImGuiDocking PRIVATE IMGUI_USER_CONFIG=<vsgImGui/Export.h>)

        target_include_directories(vsgImGuiDocking PUBLIC
            ${VSGDOCK_VSGIMGUI_DIR}/include
            ${VSGDOCK_VSGIMGUI_DIR}/include/vsgImGui
            ${VSGDOCK_VSGIMGUI_DIR}/src
            ${VSGDOCK_VSGIMGUI_DIR}/src/imgui
            ${VSGDOCK_VSGIMGUI_DIR}/src/implot
        )

        target_link_libraries(vsgImGuiDocking PUBLIC vsg::vsg)

        add_executable(vsgdock
            src/vsgdock/main.cpp
            src/vsgdock/VsgVisualizer.cpp
            src/vsgdock/LineObject.cpp
            src/vsgdock/TileGeo.cpp
        )

        target_include_directories(vsgdock PRIVATE src)

        target_link_libraries(vsgdock PRIVATE
            vsg::vsg
            vsgImGuiDocking
        )

        if(vsgXchange_FOUND)
            target_link_libraries(vsgdock PRIVATE vsgXchange::vsgXchange)
            target_compile_definitions(vsgdock PRIVATE VKVSG_HAS_VSGXCHANGE)
        endif()

        if(WIN32)
            target_compile_definitions(vsgdock PRIVATE VK_USE_PLATFORM_WIN32_KHR)
        endif()
        target_compile_definitions(vsgdock PRIVATE VKVSG_EXAMPLES_DIR="${VSG_DEPS_INSTALL_DIR}/share/vsgExamples")
        target_compile_definitions(vsgdock PRIVATE VKVSG_SHADER_DIR="${SHADER_OUTPUT_DIR}")
        add_dependencies(vsgdock shaders)
    else()
        message(WARNING "VKRAW_BUILD_VSGDOCK is ON but docking dependencies were not found at ${VSGDOCK_IMGUI_DIR} and ${VSGDOCK_VSGIMGUI_DIR}. Skipping vsgdock target.")
    endif()
endif()

if(VKRAW_BUILD_VKCORNELL)
    add_library(vulkancore STATIC
        src/vulkancore/Buffer.cpp
        src/vulkancore/CommandQueueManager.cpp
        src/vulkancore/Common.cpp
        src/vulkancore/Context.cpp
        src/vulkancore/DynamicRendering.cpp
        src/vulkancore/Framebuffer.cpp
        src/vulkancore/PhysicalDevice.cpp
        src/vulkancore/Pipeline.cpp
        src/vulkancore/RenderPass.cpp
        src/vulkancore/Sampler.cpp
        src/vulkancore/ShaderModule.cpp
        src/vulkancore/Swapchain.cpp
        src/vulkancore/Texture.cpp
        src/vulkancore/Utility.cpp
        src/vulkancore/vma.cpp
    )
    target_include_directories(vulkancore PUBLIC
        src
        external/glfw/include
        ${vma_SOURCE_DIR}/include
    )
    target_link_libraries(vulkancore PUBLIC
        Vulkan::Vulkan
        volk
        TracyClient
    )

    add_library(enginecore STATIC
        src/enginecore/Camera.cpp
        src/enginecore/Model.cpp
    )
    target_include_directories(enginecore PUBLIC
        src
        external/glfw/deps
        ${stb_SOURCE_DIR}
    )
    target_link_libraries(enginecore PUBLIC
        vulkancore
        glm::glm
    )

    add_executable(vkcornell_vc
        src/vkcornell/main.cpp
    )
    target_include_directories(vkcornell_vc PRIVATE src)
    target_link_libraries(vkcornell_vc PRIVATE
        vulkancore
        enginecore
        glfw
        glm::glm
        Vulkan::Vulkan
    )
    target_compile_definitions(vkcornell_vc PRIVATE VKCORNELL_SHADER_DIR="${VKCORNELL_SHADER_OUTPUT_DIR}")
    add_dependencies(vkcornell_vc vkcornell_shaders)
endif()

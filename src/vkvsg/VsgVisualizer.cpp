#include "vkvsg/VsgVisualizer.h"
#include "vkvsg/CubeObject.h"
#include "vkvsg/UIObject.h"

#include <vsg/all.h>
#include <vsgImGui/RenderImGui.h>
#include <vsgImGui/SendEventsToImGui.h>
#include <vsgImGui/imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <vector>

struct AppState : public vsg::Inherit<vsg::Object, AppState>
{
    vkvsg::CubeObject cube;
    vkvsg::UIObject ui;
};

class RotationInputHandler : public vsg::Inherit<vsg::Visitor, RotationInputHandler>
{
public:
    explicit RotationInputHandler(vsg::ref_ptr<AppState> inState) :
        state(std::move(inState))
    {
    }

    void apply(vsg::KeyPressEvent& keyPress) override { setKey(keyPress.keyBase, true); }
    void apply(vsg::KeyReleaseEvent& keyRelease) override { setKey(keyRelease.keyBase, false); }

    void update(float dt)
    {
        state->cube.applyInput(left, right, up, down, dt);
    }

private:
    void setKey(vsg::KeySymbol key, bool pressed)
    {
        switch (key)
        {
            case vsg::KEY_Left:
                left = pressed;
                break;
            case vsg::KEY_Right:
                right = pressed;
                break;
            case vsg::KEY_Up:
                up = pressed;
                break;
            case vsg::KEY_Down:
                down = pressed;
                break;
            default:
                break;
        }
    }

    vsg::ref_ptr<AppState> state;
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
};

class CubeGui : public vsg::Inherit<vsg::Command, CubeGui>
{
public:
    explicit CubeGui(vsg::ref_ptr<AppState> inState) :
        state(std::move(inState))
    {
    }

    void record(vsg::CommandBuffer&) const override
    {
        state->ui.draw(state->cube);
    }

private:
    vsg::ref_ptr<AppState> state;
};

vsg::Paths shaderSearchPaths()
{
    vsg::Paths paths = vsg::getEnvPaths("VSG_FILE_PATH");
#ifdef VKVSG_EXAMPLES_DIR
    paths.push_back(vsg::Path(VKVSG_EXAMPLES_DIR));
#endif
    paths.push_back("../vsg_deps/install/share/vsgExamples");
    return paths;
}

vsg::ref_ptr<vsg::Data> createCheckerTexture()
{
    auto tex = vsg::ubvec4Array2D::create(2, 2, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
    tex->set(0, 0, vsg::ubvec4(255, 255, 255, 255));
    tex->set(1, 0, vsg::ubvec4(30, 30, 30, 255));
    tex->set(0, 1, vsg::ubvec4(30, 30, 30, 255));
    tex->set(1, 1, vsg::ubvec4(255, 255, 255, 255));
    tex->dirty();
    return tex;
}

vsg::ref_ptr<vsg::Node> createCubePrototype()
{
    auto searchPaths = shaderSearchPaths();

    auto vertexShader = vsg::ShaderStage::read(
        VK_SHADER_STAGE_VERTEX_BIT, "main", vsg::findFile("shaders/vert_PushConstants.spv", searchPaths));
    auto fragmentShader = vsg::ShaderStage::read(
        VK_SHADER_STAGE_FRAGMENT_BIT, "main", vsg::findFile("shaders/frag_PushConstants.spv", searchPaths));

    if (!vertexShader || !fragmentShader)
    {
        std::cerr << "Could not load precompiled VSG shaders (vert_PushConstants.spv/frag_PushConstants.spv)." << std::endl;
        return {};
    }

    vsg::DescriptorSetLayoutBindings descriptorBindings{{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

    vsg::PushConstantRanges pushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 128}};

    vsg::VertexInputState::Bindings vertexBindings{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},
        VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},
        VkVertexInputBindingDescription{2, sizeof(vsg::vec2), VK_VERTEX_INPUT_RATE_VERTEX},
    };

    vsg::VertexInputState::Attributes vertexAttributes{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0},
        VkVertexInputAttributeDescription{2, 2, VK_FORMAT_R32G32_SFLOAT, 0},
    };

    auto rasterizationState = vsg::RasterizationState::create();
    rasterizationState->cullMode = VK_CULL_MODE_NONE;

    vsg::GraphicsPipelineStates pipelineStates{
        vsg::VertexInputState::create(vertexBindings, vertexAttributes),
        vsg::InputAssemblyState::create(),
        rasterizationState,
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        vsg::DepthStencilState::create()};

    auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstantRanges);
    auto graphicsPipeline = vsg::GraphicsPipeline::create(pipelineLayout, vsg::ShaderStages{vertexShader, fragmentShader}, pipelineStates);

    auto texture = vsg::DescriptorImage::create(vsg::Sampler::create(), createCheckerTexture(), 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    auto descriptorSet = vsg::DescriptorSet::create(descriptorSetLayout, vsg::Descriptors{texture});

    auto vertices = vsg::vec3Array::create({
        {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
        {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}
    });

    auto colors = vsg::vec3Array::create({
        {1.0f, 0.2f, 0.2f}, {0.2f, 1.0f, 0.2f}, {0.2f, 0.2f, 1.0f}, {1.0f, 1.0f, 0.2f},
        {1.0f, 0.2f, 1.0f}, {0.2f, 1.0f, 1.0f}, {0.9f, 0.9f, 0.9f}, {0.5f, 0.5f, 0.9f}
    });

    auto texcoords = vsg::vec2Array::create({
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}
    });

    auto indices = vsg::ushortArray::create({
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        0, 4, 7, 7, 3, 0,
        1, 5, 6, 6, 2, 1,
        3, 2, 6, 6, 7, 3,
        0, 1, 5, 5, 4, 0,
    });

    auto geometry = vsg::Geometry::create();
    geometry->assignArrays(vsg::DataList{vertices, colors, texcoords});
    geometry->assignIndices(indices);
    geometry->commands.push_back(vsg::DrawIndexed::create(36, 1, 0, 0, 0));

    auto stateGroup = vsg::StateGroup::create();
    stateGroup->add(vsg::BindGraphicsPipeline::create(graphicsPipeline));
    stateGroup->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, descriptorSet));
    stateGroup->addChild(geometry);

    return stateGroup;
}

void rebuildCubeInstances(vsg::Group& targetGroup, const vsg::ref_ptr<vsg::Node>& cubeNode, int cubeCount)
{
    targetGroup.children.clear();
    targetGroup.children.reserve(static_cast<size_t>(cubeCount));

    const int side = std::max(1, static_cast<int>(std::ceil(std::cbrt(static_cast<float>(cubeCount)))));
    const double spacing = 2.8;
    const vsg::dvec3 centerOffset(
        0.5 * static_cast<double>(side - 1),
        0.5 * static_cast<double>(side - 1),
        0.5 * static_cast<double>(side - 1));

    for (int i = 0; i < cubeCount; ++i)
    {
        const int x = i % side;
        const int y = (i / side) % side;
        const int z = i / (side * side);
        const vsg::dvec3 gridPos(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));

        auto transform = vsg::MatrixTransform::create();
        transform->matrix = vsg::translate((gridPos - centerOffset) * spacing);
        transform->addChild(cubeNode);
        targetGroup.addChild(transform);
    }
}

double latestVsgGpuFrameMs(const vsg::Profiler& profiler)
{
    if (!profiler.log || profiler.log->frameIndices.empty())
    {
        return 0.0;
    }

    auto frameGpuMs = [&](uint64_t frameRef) {
        uint64_t begin = frameRef;
        uint64_t end = profiler.log->entry(begin).reference;
        if (begin > end) std::swap(begin, end);

        double totalMs = 0.0;
        for (uint64_t i = begin; i <= end; ++i)
        {
            auto& entry = profiler.log->entry(i);
            if (!entry.enter || entry.type != vsg::ProfileLog::COMMAND_BUFFER) continue;

            auto& pair = profiler.log->entry(entry.reference);
            if (entry.gpuTime == 0 || pair.gpuTime == 0) continue;

            const uint64_t minTime = std::min(entry.gpuTime, pair.gpuTime);
            const uint64_t maxTime = std::max(entry.gpuTime, pair.gpuTime);
            totalMs += static_cast<double>(maxTime - minTime) * profiler.log->timestampScaleToMilliseconds;
        }
        return totalMs;
    };

    for (auto it = profiler.log->frameIndices.rbegin(); it != profiler.log->frameIndices.rend(); ++it)
    {
        const double ms = frameGpuMs(*it);
        if (ms > 0.0) return ms;
    }
    return 0.0;
}

int vkvsg::VsgVisualizer::run(int argc, char** argv)
{
    try
    {
        vsg::CommandLine arguments(&argc, argv);

        auto windowTraits = vsg::WindowTraits::create(arguments);
        windowTraits->windowTitle = "vkvsg";
        windowTraits->width = 1280;
        windowTraits->height = 720;
        windowTraits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        float runDurationSeconds = 0.0f;
        arguments.read("--seconds", runDurationSeconds);
        arguments.read("--duration", runDurationSeconds);

        if (arguments.errors()) return arguments.writeErrorMessages(std::cerr);

        auto viewer = vsg::Viewer::create();
        auto window = vsg::Window::create(windowTraits);
        if (!window)
        {
            std::cerr << "Could not create VSG window." << std::endl;
            return 1;
        }
        viewer->addWindow(window);

        auto cubeNode = createCubePrototype();
        if (!cubeNode)
        {
            return 1;
        }

        auto cubesGroup = vsg::Group::create();
        auto cubeTransform = vsg::MatrixTransform::create();
        cubeTransform->addChild(cubesGroup);

        auto scene = vsg::Group::create();
        scene->addChild(cubeTransform);

        const double aspect = static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height);
        auto perspective = vsg::Perspective::create(60.0, aspect, 0.1, 2000.0);
        auto lookAt = vsg::LookAt::create(vsg::dvec3(0.0, -220.0, 80.0), vsg::dvec3(0.0, 0.0, 0.0), vsg::dvec3(0.0, 0.0, 1.0));
        auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(window->extent2D()));

        auto commandGraph = vsg::CommandGraph::create(window);
        auto renderGraph = vsg::RenderGraph::create(window);
        commandGraph->addChild(renderGraph);

        auto view = vsg::View::create(camera);
        view->addChild(scene);
        renderGraph->addChild(view);

        auto appState = AppState::create();
        rebuildCubeInstances(*cubesGroup, cubeNode, appState->cube.cubeCount);
        int renderedCubeCount = appState->cube.cubeCount;
        uint64_t frameCount = 0;
        float runSeconds = 0.0f;
        float cpuFrameMs = 0.0f;

        std::cout << "[START] vkvsg cubes=" << appState->cube.cubeCount
                  << " present_mode=" << appState->ui.presentModeName
                  << " gpu_profiler=on" << std::endl;

        auto renderImGui = vsgImGui::RenderImGui::create(window, CubeGui::create(appState));
        renderGraph->addChild(renderImGui);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.MouseDrawCursor = true;

        auto inputHandler = RotationInputHandler::create(appState);

        auto profilerSettings = vsg::Profiler::Settings::create();
        profilerSettings->cpu_instrumentation_level = 0;
        profilerSettings->gpu_instrumentation_level = 1;
        auto profiler = vsg::Profiler::create(profilerSettings);
        viewer->assignInstrumentation(profiler);

        viewer->addEventHandler(vsgImGui::SendEventsToImGui::create());
        viewer->addEventHandler(vsg::CloseHandler::create(viewer));
        viewer->addEventHandler(inputHandler);

        viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});
        viewer->compile();

        const auto start = std::chrono::steady_clock::now();
        auto last = start;

        while (viewer->advanceToNextFrame())
        {
            const auto now = std::chrono::steady_clock::now();
            const float delta = std::chrono::duration<float>(now - last).count();
            const float elapsed = std::chrono::duration<float>(now - start).count();
            last = now;
            ++frameCount;
            runSeconds = elapsed;
            cpuFrameMs = 1000.0f * delta;

            if (runDurationSeconds > 0.0f && runSeconds >= runDurationSeconds)
            {
                break;
            }

            viewer->handleEvents();

            inputHandler->update(delta);
            appState->ui.deltaTimeMs = 1000.0f * delta;
            appState->ui.fps = (delta > 0.0f) ? (1.0f / delta) : 0.0f;
            appState->ui.gpuFrameMs = static_cast<float>(latestVsgGpuFrameMs(*profiler));

            if (appState->cube.cubeCount != renderedCubeCount)
            {
                renderedCubeCount = appState->cube.cubeCount;
                rebuildCubeInstances(*cubesGroup, cubeNode, renderedCubeCount);
            }

            cubeTransform->matrix = appState->cube.computeRotation(elapsed);

            viewer->update();
            viewer->recordAndSubmit();
            viewer->present();
        }

        profiler->finish();
        appState->ui.gpuFrameMs = static_cast<float>(latestVsgGpuFrameMs(*profiler));

        const uint64_t triangles = appState->cube.triangles();
        const uint64_t vertices = appState->cube.vertices();
        std::cout << "[EXIT] vkvsg status=OK code=0"
                  << " frames=" << frameCount
                  << " seconds=" << runSeconds
                  << " cubes=" << appState->cube.cubeCount
                  << " triangles=" << triangles
                  << " vertices=" << vertices
                  << " fps=" << appState->ui.fps
                  << " cpu_ms=" << cpuFrameMs
                  << " gpu_ms=" << appState->ui.gpuFrameMs
                  << " present_mode=" << appState->ui.presentModeName
                  << std::endl;
    }
    catch (const vsg::Exception& e)
    {
        std::cout << "[EXIT] vkvsg status=FAIL code=1 reason=\"" << e.message << "\"" << std::endl;
        std::cerr << "[vsg::Exception] " << e.message << std::endl;
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cout << "[EXIT] vkvsg status=FAIL code=1 reason=\"" << e.what() << "\"" << std::endl;
        std::cerr << "[Exception] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

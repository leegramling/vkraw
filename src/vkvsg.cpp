#include <vsg/all.h>
#include <vsgImGui/RenderImGui.h>
#include <vsgImGui/SendEventsToImGui.h>
#include <vsgImGui/imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

struct UiState : public vsg::Inherit<vsg::Object, UiState>
{
    float yaw = 30.0f;
    float pitch = 20.0f;
    float autoSpinDegPerSec = 22.5f;
    int cubeCount = 4096;
    bool showDemoWindow = true;
    float deltaTimeMs = 0.0f;
    float fps = 0.0f;
};

class RotationInputHandler : public vsg::Inherit<vsg::Visitor, RotationInputHandler>
{
public:
    explicit RotationInputHandler(vsg::ref_ptr<UiState> inUiState) :
        uiState(std::move(inUiState))
    {
    }

    void apply(vsg::KeyPressEvent& keyPress) override { setKey(keyPress.keyBase, true); }
    void apply(vsg::KeyReleaseEvent& keyRelease) override { setKey(keyRelease.keyBase, false); }

    void update(float dt)
    {
        constexpr float rotationSpeed = 90.0f;
        if (left) uiState->yaw -= rotationSpeed * dt;
        if (right) uiState->yaw += rotationSpeed * dt;
        if (up) uiState->pitch += rotationSpeed * dt;
        if (down) uiState->pitch -= rotationSpeed * dt;
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

    vsg::ref_ptr<UiState> uiState;
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
};

class CubeGui : public vsg::Inherit<vsg::Command, CubeGui>
{
public:
    explicit CubeGui(vsg::ref_ptr<UiState> inUiState) :
        uiState(std::move(inUiState))
    {
    }

    void record(vsg::CommandBuffer&) const override
    {
        ImGui::Begin("Cube Controls");
        ImGui::Text("Arrow keys rotate the cube");
        ImGui::SliderFloat("Yaw", &uiState->yaw, -180.0f, 180.0f);
        ImGui::SliderFloat("Pitch", &uiState->pitch, -89.0f, 89.0f);
        ImGui::SliderFloat("Auto spin (deg/s)", &uiState->autoSpinDegPerSec, -180.0f, 180.0f);
        ImGui::SliderInt("Cube count", &uiState->cubeCount, 1, 20000);
        ImGui::Text("FPS %.1f", uiState->fps);
        ImGui::Text("Frame time %.3f ms", uiState->deltaTimeMs);
        ImGui::End();

        ImGui::ShowDemoWindow(&uiState->showDemoWindow);
    }

private:
    vsg::ref_ptr<UiState> uiState;
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

int main(int argc, char** argv)
{
    try
    {
        vsg::CommandLine arguments(&argc, argv);

        auto windowTraits = vsg::WindowTraits::create(arguments);
        windowTraits->windowTitle = "vkvsg";
        windowTraits->width = 1280;
        windowTraits->height = 720;

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
        auto perspective = vsg::Perspective::create(45.0, aspect, 0.1, 1000.0);
        auto lookAt = vsg::LookAt::create(vsg::dvec3(0.0, -120.0, 40.0), vsg::dvec3(0.0, 0.0, 0.0), vsg::dvec3(0.0, 0.0, 1.0));
        auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(window->extent2D()));

        auto commandGraph = vsg::CommandGraph::create(window);
        auto renderGraph = vsg::RenderGraph::create(window);
        commandGraph->addChild(renderGraph);

        auto view = vsg::View::create(camera);
        view->addChild(scene);
        renderGraph->addChild(view);

        auto uiState = UiState::create();
        rebuildCubeInstances(*cubesGroup, cubeNode, uiState->cubeCount);
        int renderedCubeCount = uiState->cubeCount;

        auto renderImGui = vsgImGui::RenderImGui::create(window, CubeGui::create(uiState));
        renderGraph->addChild(renderImGui);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.MouseDrawCursor = true;

        auto inputHandler = RotationInputHandler::create(uiState);

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

            viewer->handleEvents();

            inputHandler->update(delta);
            uiState->deltaTimeMs = 1000.0f * delta;
            uiState->fps = (delta > 0.0f) ? (1.0f / delta) : 0.0f;

            if (uiState->cubeCount != renderedCubeCount)
            {
                renderedCubeCount = uiState->cubeCount;
                rebuildCubeInstances(*cubesGroup, cubeNode, renderedCubeCount);
            }

            const double yaw = vsg::radians(static_cast<double>(uiState->yaw + uiState->autoSpinDegPerSec * elapsed));
            const double pitch = vsg::radians(static_cast<double>(uiState->pitch));
            cubeTransform->matrix = vsg::rotate(yaw, 0.0, 0.0, 1.0) * vsg::rotate(pitch, 1.0, 0.0, 0.0);

            viewer->update();
            viewer->recordAndSubmit();
            viewer->present();
        }
    }
    catch (const vsg::Exception& e)
    {
        std::cerr << "[vsg::Exception] " << e.message << std::endl;
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Exception] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

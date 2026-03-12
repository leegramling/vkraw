#include "vsgdock/LineObject.h"

#include <vsg/all.h>

#include <cmath>
#include <string>
#include <utility>

namespace
{

class EquatorRenderObject : public vsg::Inherit<vsg::Group, EquatorRenderObject>
{
};

class EquatorLineDraw : public vsg::Inherit<vsg::VertexIndexDraw, EquatorLineDraw>
{
};

class DebugLabelBegin : public vsg::Inherit<vsg::Command, DebugLabelBegin>
{
public:
    DebugLabelBegin(std::string inLabel, vsg::vec4 inColor = vsg::vec4(1.0f, 1.0f, 1.0f, 1.0f)) :
        label(std::move(inLabel)),
        color(inColor)
    {
    }

    void record(vsg::CommandBuffer& commandBuffer) const override
    {
        auto extensions = commandBuffer.getDevice()->getInstance()->getExtensions();
        if (!extensions || !extensions->vkCmdBeginDebugUtilsLabelEXT) return;

        VkDebugUtilsLabelEXT markerInfo{};
        markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        markerInfo.pLabelName = label.c_str();
        markerInfo.color[0] = color.r;
        markerInfo.color[1] = color.g;
        markerInfo.color[2] = color.b;
        markerInfo.color[3] = color.a;

        extensions->vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &markerInfo);
    }

private:
    std::string label;
    vsg::vec4 color;
};

class DebugLabelEnd : public vsg::Inherit<vsg::Command, DebugLabelEnd>
{
public:
    void record(vsg::CommandBuffer& commandBuffer) const override
    {
        auto extensions = commandBuffer.getDevice()->getInstance()->getExtensions();
        if (!extensions || !extensions->vkCmdEndDebugUtilsLabelEXT) return;
        extensions->vkCmdEndDebugUtilsLabelEXT(commandBuffer);
    }
};

} // namespace

vsg::ref_ptr<vsg::Node> vkvsg::createEquatorLineNode(double equatorialRadiusFeet)
{
    auto stateGroup = vsg::StateGroup::create();

#ifndef VKVSG_SHADER_DIR
#define VKVSG_SHADER_DIR ""
#endif

    const std::string vertPath = std::string(VKVSG_SHADER_DIR) + "/equator_line.vert.spv";
    const std::string fragPath = std::string(VKVSG_SHADER_DIR) + "/equator_line.frag.spv";
    auto vertexShader = vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT, "main", vertPath);
    auto fragmentShader = vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT, "main", fragPath);
    if (!vertexShader || !fragmentShader) return {};

    vsg::VertexInputState::Bindings bindings{
        VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX},
        VkVertexInputBindingDescription{1, sizeof(vsg::vec4), VK_VERTEX_INPUT_RATE_VERTEX}};

    vsg::VertexInputState::Attributes attributes{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0}};

    auto rasterizationState = vsg::RasterizationState::create();
    rasterizationState->cullMode = VK_CULL_MODE_NONE;

    auto depthStencilState = vsg::DepthStencilState::create();
    depthStencilState->depthTestEnable = VK_FALSE;
    depthStencilState->depthWriteEnable = VK_FALSE;

    auto dynamicState = vsg::DynamicState::create();
    dynamicState->dynamicStates = {VK_DYNAMIC_STATE_LINE_WIDTH};

    vsg::GraphicsPipelineStates pipelineStates{
        vsg::VertexInputState::create(bindings, attributes),
        vsg::InputAssemblyState::create(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_FALSE),
        rasterizationState,
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(),
        depthStencilState,
        dynamicState};

    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128}};

    auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{}, pushConstantRanges);
    auto graphicsPipeline = vsg::GraphicsPipeline::create(pipelineLayout, vsg::ShaderStages{vertexShader, fragmentShader}, pipelineStates);
    stateGroup->add(vsg::BindGraphicsPipeline::create(graphicsPipeline));

    constexpr uint32_t segmentCount = 256;
    auto vertices = vsg::vec3Array::create(segmentCount);
    auto colors = vsg::vec4Array::create(segmentCount);

    const double radius = equatorialRadiusFeet * 1.002;
    for (uint32_t i = 0; i < segmentCount; ++i)
    {
        const double t = (static_cast<double>(i) / static_cast<double>(segmentCount)) * (2.0 * vsg::PI);
        const float x = static_cast<float>(-std::sin(t) * radius);
        const float y = static_cast<float>(std::cos(t) * radius);
        (*vertices)[i] = vsg::vec3(x, y, 0.0f);
        (*colors)[i] = vsg::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    auto indices = vsg::ushortArray::create(segmentCount * 2);
    uint32_t write = 0;
    for (uint32_t i = 0; i < segmentCount; ++i)
    {
        const uint16_t i0 = static_cast<uint16_t>(i);
        const uint16_t i1 = static_cast<uint16_t>((i + 1) % segmentCount);
        (*indices)[write++] = i0;
        (*indices)[write++] = i1;
    }

    auto equatorDraw = EquatorLineDraw::create();
    equatorDraw->assignArrays(vsg::DataList{vertices, colors});
    equatorDraw->assignIndices(indices);
    equatorDraw->indexCount = static_cast<uint32_t>(indices->size());
    equatorDraw->instanceCount = 1;

    auto equatorCommands = vsg::Commands::create();
    equatorCommands->addChild(DebugLabelBegin::create("EquatorLineDraw", vsg::vec4(1.0f, 1.0f, 1.0f, 1.0f)));
    equatorCommands->addChild(vsg::SetLineWidth::create(3.0f));
    equatorCommands->addChild(equatorDraw);
    equatorCommands->addChild(DebugLabelEnd::create());

    stateGroup->addChild(equatorCommands);
    auto equatorRenderObject = EquatorRenderObject::create();
    equatorRenderObject->addChild(stateGroup);
    return equatorRenderObject;
}

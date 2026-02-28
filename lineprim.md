# VSG Line Primitive Guide (`LINE_LIST`)

This document shows how to create a line primitive in VulkanSceneGraph (VSG), with a minimal path that is easy to inspect in RenderDoc.

## 1. Choose Primitive Topology

For independent line segments, use:

- `VK_PRIMITIVE_TOPOLOGY_LINE_LIST`

Each line consumes 2 indices (`i0, i1`).

For one continuous loop (like an equator), build segment pairs:

- `(0,1), (1,2), ... (N-1,0)`

## 2. Create Vertex Data

Typical line vertex data:

- positions: `vsg::vec3Array`
- colors: `vsg::vec4Array` (flat white or per-vertex color)

Example:

```cpp
auto vertices = vsg::vec3Array::create(segmentCount);
auto colors = vsg::vec4Array::create(segmentCount);

for (uint32_t i = 0; i < segmentCount; ++i)
{
    // Fill positions here
    (*vertices)[i] = vsg::vec3(x, y, z);
    (*colors)[i] = vsg::vec4(1.0f, 1.0f, 1.0f, 1.0f);
}
```

## 3. Create Indices for `LINE_LIST`

```cpp
auto indices = vsg::ushortArray::create(segmentCount * 2);
uint32_t write = 0;
for (uint32_t i = 0; i < segmentCount; ++i)
{
    const uint16_t i0 = static_cast<uint16_t>(i);
    const uint16_t i1 = static_cast<uint16_t>((i + 1) % segmentCount);
    (*indices)[write++] = i0;
    (*indices)[write++] = i1;
}
```

## 4. Shader Interface (Flat Color)

Use a simple vertex+fragment shader pair:

- vertex input location 0: `vec3 position`
- vertex input location 1: `vec4 color`
- output color directly in fragment shader

No texture is required for a solid white line.

## 5. Build Graphics Pipeline State

Critical states:

- `VertexInputState` for position/color bindings + attributes
- `InputAssemblyState(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_FALSE)`
- `RasterizationState` with `cullMode = VK_CULL_MODE_NONE`
- optional `DepthStencilState`:
  - disable depth test/write if you always want the line visible
  - enable depth test if you want proper occlusion by globe geometry
- `DynamicState` including `VK_DYNAMIC_STATE_LINE_WIDTH`

## 6. Record Draw Commands

Use `vsg::VertexIndexDraw`:

```cpp
auto draw = vsg::VertexIndexDraw::create();
draw->assignArrays(vsg::DataList{vertices, colors});
draw->assignIndices(indices);
draw->indexCount = static_cast<uint32_t>(indices->size());
draw->instanceCount = 1;
```

Set line width before draw:

```cpp
auto commands = vsg::Commands::create();
commands->addChild(vsg::SetLineWidth::create(3.0f));
commands->addChild(draw);
```

Attach under a `vsg::StateGroup` with `BindGraphicsPipeline`.

## 7. RenderDoc Annotation

To find the line draw quickly in RenderDoc:

- wrap the draw node in a custom class (for object identity in captures)
- add GPU annotation around command buffer entry/exit (if your app has annotation plumbing)

Practical naming pattern:

- `EquatorRenderObject`
- `EquatorLineDraw`
- annotation text: `"EquatorLineDraw"`

This makes the `vkCmdDrawIndexed` event easy to locate in the event browser.

## 8. Common Issues

- No line visible:
  - wrong shader path / SPIR-V not built
  - missing `LINE_LIST` topology
  - depth test hiding line inside surface
  - line too thin (increase width)
  - geometry at exact same depth as globe (offset radius slightly outward)

- Too many lines:
  - index generation accidentally creates grid/lat-lon loops
  - ensure you only emit one ring’s index pairs

## 9. Minimal Equator Recipe

For one equator circle only:

1. Generate one ring of vertices at `z = 0`.
2. Build `LINE_LIST` indices that connect each vertex to the next and close the loop.
3. Use flat white color shader.
4. Draw once with one `VertexIndexDraw`.


# vkvsg Geometry Notes

This note is based on local VSG source in `../vsg_deps/VulkanSceneGraph` and the `vkvsg` geometry path.

## Core VSG Types Used In Globe Geometry

- `vsg::Group`
  - General scene graph container of child nodes.
  - `addChild()` appends renderable or container children.
  - Source: `include/vsg/nodes/Group.h`

- `vsg::StateGroup`
  - Group plus `stateCommands` (pipeline/state bindings) that apply to its subgraph.
  - Typical place to bind graphics pipeline and descriptor sets.
  - Source: `include/vsg/nodes/StateGroup.h`

- `vsg::VertexIndexDraw`
  - Leaf command node that binds vertex/index buffers and records `vkCmdDrawIndexed`.
  - Higher-level convenience over separate bind/draw commands.
  - Source: `include/vsg/nodes/VertexIndexDraw.h`, `src/vsg/nodes/VertexIndexDraw.cpp`

- `vsg::Builder`
  - Utility that creates state groups and primitive geometry helpers.
  - In this project we use it to produce the globe `StateGroup`.
  - Source: `include/vsg/utils/Builder.h`, `src/vsg/utils/Builder.cpp`

- `vsg::Data` arrays (`vec3Array`, `vec2Array`, `ushortArray`, etc.)
  - CPU-side geometry/storage objects used to populate buffers.
  - Source: `include/vsg/core/Data.h`, `include/vsg/core/Array*.h`

## How `vkvsg` Globe Geometry Is Added To Scene Graph

Current file split:

- `src/vkvsg/TileGeo.cpp`
  - Builds textured/wireframe globe node (`createGlobeNode`).
  - Creates vertex/normal/uv arrays and index array.
  - Creates `VertexIndexDraw` and attaches it to a `StateGroup` from `Builder`.

- `src/vkvsg/VsgVisualizer.cpp`
  - Creates scene root/group/transform.
  - Calls `createGlobeNode(...)`.
  - Adds returned node under `globeTransform` via `addChild(...)`.

Logical flow:

1. Build geometry arrays (`vertices`, `normals`, `texcoords`) and `indices`.
2. Fill `VertexIndexDraw` with arrays/indices and draw counts.
3. Create `StateGroup` (pipeline + descriptors) via `Builder`.
4. Add `VertexIndexDraw` as child of `StateGroup`.
5. Add resulting globe node into scene `Group/Transform`.

## When Objects Need Compile

`CompileTraversal` visits graph objects and calls their `compile(context)` methods to create Vulkan resources.

- Source: `include/vsg/app/CompileTraversal.h`, `src/vsg/app/CompileTraversal.cpp`

For `VertexIndexDraw`/`Geometry`, compile handles:

- Buffer creation/assignment if needed
- Data transfer setup when modified counts indicate updates
- Vulkan buffer binding cache population

- Source:
  - `src/vsg/nodes/VertexIndexDraw.cpp`
  - `src/vsg/nodes/Geometry.cpp`
  - `src/vsg/state/BufferInfo.cpp`

In practice:

- New node/pipeline/descriptor objects: must be compiled before first render in a context.
- Structural resource changes (new buffer layouts, changed descriptor set layout, different pipeline state): require compile path.

## Uniform Buffer Updates: Recompile Or Not?

VSG tracks data mutation via `vsg::Data::dirty()` and modified counts.

- Source: `include/vsg/core/Data.h`

Uniform/storage descriptor path:

- `DescriptorBuffer` compiles buffers and assigns transfer handling.
- `TransferTask` uploads changed dynamic data each frame when modified counts differ.

- Source:
  - `src/vsg/state/DescriptorBuffer.cpp`
  - `include/vsg/app/TransferTask.h`
  - `src/vsg/app/TransferTask.cpp`

Rules from source behavior:

- Changing values in existing uniform buffer data:
  - Call `data->dirty()`.
  - Usually does not require rebuilding pipeline objects.
  - Transfer/update path handles upload.

- Changing descriptor set structure/layout (bindings/types/counts):
  - Requires descriptor/pipeline layout recompile path.

- Changing graphics pipeline state (e.g., topology baked in `InputAssemblyState`):
  - Requires graphics pipeline compile path.

## Dynamic Data Variance And Transfer Timing

`DataVariance` in VSG:

- `STATIC_DATA`
- `STATIC_DATA_UNREF_AFTER_TRANSFER`
- `DYNAMIC_DATA`
- `DYNAMIC_DATA_TRANSFER_AFTER_RECORD`

Transfer timing:

- Before record traversal (`TRANSFER_BEFORE_RECORD_TRAVERSAL`)
- After record traversal (`TRANSFER_AFTER_RECORD_TRAVERSAL`)

The selected path depends on variance and where data is expected to change.

- Source:
  - `include/vsg/core/Data.h`
  - `include/vsg/app/TransferTask.h`
  - `src/vsg/app/TransferTask.cpp`
  - `src/vsg/app/RecordAndSubmitTask.cpp`


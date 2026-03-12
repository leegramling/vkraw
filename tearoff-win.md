# Tear-Off Globe Controls In `vkvsg`

## Goal

Allow the `Globe Controls` ImGui panel to detach from the main `vkvsg` window when the user drags the panel far enough outside the main client area. The trigger is when less than 50% of the panel area still overlaps the main window.

The detached panel lives in a second VSG window. The main globe render stays in the original window.

## Why This Is Not Just "Open Another Window"

The local `vsgImGui` integration is built around a single Dear ImGui context and the non-docking single-viewport model. That means:

- Dear ImGui is not creating native platform windows for us.
- `vsgImGui::SendEventsToImGui` routes input to one current ImGui context.
- `vsgImGui::RenderImGui` renders whichever ImGui context is current when it records.

So the tear-off behavior has to be application-managed:

1. `vkvsg` creates the second `vsg::Window` itself.
2. `vkvsg` owns one ImGui context per VSG window.
3. `vkvsg` routes events to the correct ImGui context based on `event.window`.
4. `vkvsg` decides which window owns the `Globe Controls` panel.

## VSG Side

The second window follows the same basic pattern as the example in:

`../vsg_deps/vsgExamples/examples/app/vsgmultiwindowmultiview/vsgmultiwindowmultiview.cpp`

The important VSG pieces are:

- Create a second `vsg::Window` with shared device settings from the main window.
- Create a second `vsg::CommandGraph`.
- Create a second `vsg::RenderGraph`.
- Add that command graph to the viewer with `addRecordAndSubmitTaskAndPresentation`.
- Recompile once after the new window is attached.

In `vkvsg`, the tear-off window is UI-only. It does not render the globe scene. Its render graph only contains a `RenderImGui` node.

## ImGui Side

Each VSG window gets its own Dear ImGui context:

- Main window context: demo window plus `Globe Controls` before detach.
- Tear-off context: `Globe Controls` after detach.

The existing `vsgImGui::RenderImGui` implementation does not switch contexts for us, so `vkvsg` wraps each ImGui render node with a context-switching node. That wrapper calls `ImGui::SetCurrentContext()` before record traversal reaches `RenderImGui`.

Input routing uses a custom visitor instead of `vsgImGui::SendEventsToImGui`. The custom handler:

- Looks at `event.window`.
- Switches to that window's ImGui context.
- Pushes pointer and keyboard events into that context's `ImGuiIO`.
- Updates `DisplaySize` on `ConfigureWindowEvent`.
- Updates `DeltaTime` for every ImGui context on each frame.

## Tear-Off Trigger

While `Globe Controls` is still in the main window, `vkvsg` records:

- panel position
- panel size
- whether ImGui reports the panel as moving

Each frame it computes:

- panel area
- overlap rectangle between the panel and the main window client rect
- overlap ratio = overlap area / panel area

If:

- the panel is moving
- and overlap ratio drops below `0.5`

then `vkvsg` opens the second VSG window and transfers panel ownership to that window.

## Panel Ownership Model

`Globe Controls` exists in exactly one window at a time.

- Attached state:
  - main window draws `Globe Controls`
  - tear-off window does not exist
- Detached state:
  - main window no longer draws `Globe Controls`
  - tear-off window draws `Globe Controls`

The demo window remains in the main window.

## Close Behavior

The tear-off window uses a custom close handler. Closing that window:

- does not close the whole app
- removes the tear-off command graph from the viewer
- destroys the tear-off ImGui context
- returns `Globe Controls` to the main window

Closing the main window still closes the viewer.

## Low-Level Constraints

This feature needs a few low-level details that are easy to miss:

- One `RenderImGui` instance cannot safely serve multiple native windows without explicit context switching.
- ImGui Vulkan backend state is stored per ImGui context, so each context must be current during backend init, record, and shutdown.
- VSG window events carry `event.window`, which is enough to route input without OS-specific hooks.
- Viewer task setup has to be updated when the tear-off window is added or removed, otherwise the extra window will not be recorded or presented.

## Why We Did Not Use Dear ImGui Docking/Viewports

The current repo does not have a docking-enabled `vsgImGui` stack wired in. Even if Dear ImGui itself is swapped to a docking branch, `vsgImGui` still needs backend work to support multiple platform windows cleanly.

For `vkvsg`, app-managed multi-window is the smaller and safer change.

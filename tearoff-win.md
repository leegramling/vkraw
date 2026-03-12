# Tear-Off Globe Controls In `vkvsg`

## Goal

Allow the `Globe Controls` ImGui panel to detach from the main `vkvsg` window when the user drags the panel far enough outside the main client area. The trigger is when less than 50% of the panel area still overlaps the main window.

The detached panel lives in a second VSG window. The main globe render stays in the original window.

## Why This Is Not Just "Open Another Window"

The local `vsgImGui` integration is built around a single Dear ImGui context and the non-docking single-viewport model. That means:

- Dear ImGui is not creating native platform windows for us.
- `vsgImGui::SendEventsToImGui` is not enough once more than one native VSG window is involved.
- `vsgImGui::RenderImGui` renders whichever ImGui context is current when it records.

So the tear-off behavior has to be application-managed:

1. `vkvsg` creates the second `vsg::Window` itself.
2. `vkvsg` owns one ImGui context per VSG window, including the main window.
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

This is used for both windows, not just the tear-off window. That keeps the main window from inheriting the tear-off window's ImGui display size or viewport state.

Input routing uses a custom visitor instead of `vsgImGui::SendEventsToImGui`. The custom handler:

- Looks at `event.window`.
- Switches to that window's ImGui context.
- Pushes pointer and keyboard events into that context's `ImGuiIO`.
- Updates `DisplaySize` on `ConfigureWindowEvent`.
- Updates `DeltaTime` for every ImGui context on each frame.

Native OS cursor rendering is used. ImGui software cursor drawing is disabled in both contexts so the app does not show two cursors.

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

The actual VSG window/task mutation is deferred until after `viewer->present()`. This matters because changing the viewer's window list or command graphs in the middle of an active frame proved unstable.

## Panel Ownership Model

`Globe Controls` exists in exactly one window at a time.

- Attached state:
  - main window draws `Globe Controls`
  - tear-off window does not exist
- Detached state:
  - main window no longer draws `Globe Controls`
  - tear-off window draws `Globe Controls`

The demo window remains in the main window.

## Dock Back And Close Behavior

The detached panel exposes an explicit `Dock Back` button. Pressing it:

- marks the tear-off window for removal
- removes only that window from the viewer
- destroys the tear-off ImGui resources
- returns `Globe Controls` to the main window
- resets the main-window panel to a sane default position and size

After a dock-back, tear-off is suppressed until the left mouse button is released. That prevents the panel from immediately tearing off again while the same drag is still active.

The tear-off window also uses a custom close handler. Closing that native window is treated the same way as `Dock Back`:

- it does not close the whole app
- it removes only the tear-off window from the viewer
- it returns `Globe Controls` to the main window

Closing the main window still closes the viewer.

## Low-Level Constraints

This feature needs a few low-level details that are easy to miss:

- One `RenderImGui` instance cannot safely serve multiple native windows without explicit context switching.
- ImGui Vulkan backend state is stored per ImGui context, so each context must be current during backend init, record, and shutdown.
- VSG window events carry `event.window`, which is enough to route input without OS-specific hooks.
- Viewer task setup has to be updated when the tear-off window is added or removed, otherwise the extra window will not be recorded or presented.
- Tear-off window removal is done with `viewer->removeWindow(...)` plus task reassignment, following the VSG multiwindow example's pattern for secondary-window closure.
- Tear-off create/remove has to happen outside the active submit path. Deferring those transitions until after `present()` avoided crashes seen when mutating viewer state mid-frame.

## Why We Did Not Use Dear ImGui Docking/Viewports

The current repo does not have a docking-enabled `vsgImGui` stack wired in. Even if Dear ImGui itself is swapped to a docking branch, `vsgImGui` still needs backend work to support multiple platform windows cleanly.

For `vkvsg`, app-managed multi-window is the smaller and safer change.

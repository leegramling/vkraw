# Tear-Off Globe Controls In `vkvsg` And `vsgdock`

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

In `vsgdock`, this logic now lives in a dedicated `VsgInputManager` rather than a narrow ImGui-only event bridge. That manager:

- owns the per-window ImGui context map
- handles window events such as `CloseWindowEvent` and `ConfigureWindowEvent`
- arbitrates whether the main-window globe manipulator should receive mouse input
- converts `W` key presses into app-side wireframe toggle requests
- exposes tear-off close requests back to the app loop so window removal still happens after `present()`

That is closer to the pattern used in `../vsgWorld/vsgBox/VsgInputManager.cpp/h`: the app owns event policy, and ImGui input routing is just one part of that policy.

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

The original `vkvsg` path does not have a docking-enabled `vsgImGui` stack wired in. `vsgdock` now does use docking-capable Dear ImGui dependencies, but that still does not provide native ImGui-managed platform windows in this repo because the `vsgImGui` backend layer does not implement the platform-window callbacks.

So for both `vkvsg` and `vsgdock`, tear-off is still application-managed and still follows the VSG multiwindow pattern.

## ImGui Docking Changes

If this project moved to a docking-enabled Dear ImGui build, the design space would change, but not as much as it may first appear.

What docking would likely improve:

- `Globe Controls` could become a dockable ImGui window instead of an app-managed ownership toggle.
- The user interaction model would be more natural because ImGui could manage undocking and redocking semantics directly.
- The explicit `Dock Back` button could potentially be removed if docking behavior proved reliable enough.
- The app would no longer need to infer user intent from the panel overlap ratio if ImGui itself could report or drive dock/undock transitions.

What would still need work in this repo:

- `vsgImGui` would still need backend support for multiple platform windows or multiple ImGui viewports.
- VSG would still need a way to create, track, resize, and destroy native windows that correspond to ImGui platform windows.
- Input routing would still need to be correct per native window.
- Vulkan renderer backend state would still need to be initialized and shut down in a way that matches however many ImGui contexts or viewports are active.

The main architectural difference would be this:

- Current implementation:
  - one application-owned ImGui context per VSG window
  - one application-owned secondary VSG window
  - explicit panel transfer between windows
- Docking-enabled target:
  - ideally one ImGui context with docking and multi-viewport support
  - ImGui requests platform windows as needed
  - VSG backend layer creates and services those windows

If docking support were added all the way through `vsgImGui`, some current code in `vkvsg` would probably disappear:

- overlap-based tear-off detection
- explicit `Dock Back` state
- explicit suppression of immediate re-tear-off after redock
- some of the per-window panel placement reset logic

But some low-level VSG integration would remain necessary:

- native window lifecycle management
- per-window swapchain/render graph handling
- per-window event delivery
- backend-safe shutdown ordering

So the docking branch would change the feature from "application-managed tear-off" to "backend-supported ImGui-managed undock/dock", but it would not be a drop-in switch in the current codebase. The main work would move from `vkvsg` into the `vsgImGui` integration layer.

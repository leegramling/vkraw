#include "vsgdock/VsgInputManager.h"

namespace vkvsg {

VsgInputManager::VsgInputManager(vsg::ref_ptr<vsg::Viewer> inViewer) :
    viewer(std::move(inViewer))
{
}

void VsgInputManager::setMainWindow(vsg::ref_ptr<vsg::Window> window)
{
    mainWindow = std::move(window);
}

void VsgInputManager::setTearOffWindow(vsg::ref_ptr<vsg::Window> window)
{
    tearOffWindow = std::move(window);
}

void VsgInputManager::addWindow(vsg::ref_ptr<vsg::Window> window, ImGuiContext* context)
{
    if (!window || !context) return;

    auto& state = windows[window.get()];
    state.window = window;
    state.context = context;
    state.draggingScene = false;
    state.lastFrameTime = std::chrono::high_resolution_clock::now();

    ScopedImGuiContext scoped(context);
    ImGuiIO& io = ImGui::GetIO();
    const auto extent = window->extent2D();
    io.DisplaySize = ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
    io.DeltaTime = 1.0f / 60.0f;
}

void VsgInputManager::removeWindow(vsg::ref_ptr<vsg::Window> window)
{
    if (!window) return;
    windows.erase(window.get());
    if (tearOffWindow == window) tearOffWindow = {};
}

void VsgInputManager::apply(vsg::MoveEvent& moveEvent)
{
    processEvent(moveEvent);
}

void VsgInputManager::apply(vsg::ButtonPressEvent& buttonPress)
{
    processEvent(buttonPress);
}

void VsgInputManager::apply(vsg::ButtonReleaseEvent& buttonRelease)
{
    processEvent(buttonRelease);
}

void VsgInputManager::apply(vsg::ScrollWheelEvent& scrollWheel)
{
    processEvent(scrollWheel);
}

void VsgInputManager::apply(vsg::KeyPressEvent& keyPress)
{
    processEvent(keyPress);
}

void VsgInputManager::apply(vsg::KeyReleaseEvent& keyRelease)
{
    processEvent(keyRelease);
}

void VsgInputManager::apply(vsg::ConfigureWindowEvent& configureWindow)
{
    processEvent(configureWindow);
}

void VsgInputManager::apply(vsg::FrameEvent& frameEvent)
{
    processEvent(frameEvent);
}

void VsgInputManager::apply(vsg::CloseWindowEvent& closeWindowEvent)
{
    if (closeWindowEvent.window == tearOffWindow.get())
    {
        tearOffCloseRequested = true;
        closeWindowEvent.handled = true;
        return;
    }

    if (closeWindowEvent.window == mainWindow.get())
    {
        if (viewer) viewer->close();
        closeWindowEvent.handled = true;
    }
}

void VsgInputManager::processQueuedEvents()
{
}

bool VsgInputManager::consumeWireframeToggleRequest()
{
    const bool requested = wireframeToggleRequested;
    wireframeToggleRequested = false;
    return requested;
}

bool VsgInputManager::consumeTearOffCloseRequest()
{
    const bool requested = tearOffCloseRequested;
    tearOffCloseRequested = false;
    return requested;
}

bool VsgInputManager::leftMouseButtonDown() const
{
    return leftButtonDown;
}

VsgInputManager::ScopedImGuiContext::ScopedImGuiContext(ImGuiContext* next) :
    previous(ImGui::GetCurrentContext())
{
    ImGui::SetCurrentContext(next);
}

VsgInputManager::ScopedImGuiContext::~ScopedImGuiContext()
{
    ImGui::SetCurrentContext(previous);
}

VsgInputManager::ImGuiWindowState* VsgInputManager::getState(vsg::Window* window)
{
    if (!window) return nullptr;
    auto it = windows.find(window);
    return it == windows.end() ? nullptr : &it->second;
}

void VsgInputManager::processEvent(vsg::UIEvent& event)
{
    auto getWindowState = [&](vsg::Window* window) -> ImGuiWindowState* {
        return getState(window);
    };

    if (auto frameEvent = event.cast<vsg::FrameEvent>(); frameEvent)
    {
        const auto now = std::chrono::high_resolution_clock::now();
        for (auto& [_, state] : windows)
        {
            if (!state.context) continue;
            ScopedImGuiContext scoped(state.context);
            const float deltaTime = std::chrono::duration_cast<std::chrono::duration<float>>(now - state.lastFrameTime).count();
            ImGui::GetIO().DeltaTime = std::max(deltaTime, 1.0e-6f);
            state.lastFrameTime = now;
        }
        return;
    }

    if (auto configure = event.cast<vsg::ConfigureWindowEvent>(); configure)
    {
        auto* state = getWindowState(vsg::ref_ptr<vsg::Window>(configure->window).get());
        if (!state || !state->context) return;

        ScopedImGuiContext scoped(state->context);
        ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(configure->width), static_cast<float>(configure->height));
        return;
    }

    if (auto buttonPress = event.cast<vsg::ButtonPressEvent>(); buttonPress)
    {
        auto* state = getWindowState(vsg::ref_ptr<vsg::Window>(buttonPress->window).get());
        if (!state || !state->context) return;

        if (buttonPress->button == 1) leftButtonDown = true;

        ScopedImGuiContext scoped(state->context);
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(static_cast<float>(buttonPress->x), static_cast<float>(buttonPress->y));
        if (io.WantCaptureMouse)
        {
            io.AddMouseButtonEvent(convertButton(buttonPress->button), true);
            buttonPress->handled = true;
            state->draggingScene = false;
        }
        else
        {
            state->draggingScene = true;
        }
        return;
    }

    if (auto buttonRelease = event.cast<vsg::ButtonReleaseEvent>(); buttonRelease)
    {
        auto* state = getWindowState(vsg::ref_ptr<vsg::Window>(buttonRelease->window).get());
        if (!state || !state->context) return;

        if (buttonRelease->button == 1) leftButtonDown = false;

        ScopedImGuiContext scoped(state->context);
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(static_cast<float>(buttonRelease->x), static_cast<float>(buttonRelease->y));
        if (!state->draggingScene)
        {
            io.AddMouseButtonEvent(convertButton(buttonRelease->button), false);
            buttonRelease->handled = io.WantCaptureMouse;
        }
        state->draggingScene = false;
        return;
    }

    if (auto moveEvent = event.cast<vsg::MoveEvent>(); moveEvent)
    {
        auto* state = getWindowState(vsg::ref_ptr<vsg::Window>(moveEvent->window).get());
        if (!state || !state->context) return;

        ScopedImGuiContext scoped(state->context);
        ImGuiIO& io = ImGui::GetIO();
        if (!state->draggingScene)
        {
            io.AddMousePosEvent(static_cast<float>(moveEvent->x), static_cast<float>(moveEvent->y));
            moveEvent->handled = io.WantCaptureMouse;
        }
        return;
    }

    if (auto scrollWheel = event.cast<vsg::ScrollWheelEvent>(); scrollWheel)
    {
        auto* state = getWindowState(vsg::ref_ptr<vsg::Window>(scrollWheel->window).get());
        if (!state || !state->context) return;

        ScopedImGuiContext scoped(state->context);
        ImGuiIO& io = ImGui::GetIO();
        if (!state->draggingScene)
        {
            io.AddMouseWheelEvent(scrollWheel->delta[0], scrollWheel->delta[1]);
            scrollWheel->handled = io.WantCaptureMouse;
        }
        return;
    }

    if (auto keyPress = event.cast<vsg::KeyPressEvent>(); keyPress)
    {
        auto* state = getWindowState(vsg::ref_ptr<vsg::Window>(keyPress->window).get());
        if (!state || !state->context) return;

        if (keyPress->window == mainWindow.get() && (keyPress->keyBase == 'w' || keyPress->keyBase == 'W'))
        {
            wireframeToggleRequested = true;
        }

        ScopedImGuiContext scoped(state->context);
        ImGuiIO& io = ImGui::GetIO();
        updateModifier(io, keyPress->keyModifier, true);
        const auto imguiKey = convertKey(keyPress->keyBase, keyPress->keyModified);
        if (imguiKey != ImGuiKey_None) io.AddKeyEvent(imguiKey, true);
        if (uint16_t c = keyPress->keyModified; c > 0 && c < 255) io.AddInputCharacter(c);
        keyPress->handled = io.WantCaptureKeyboard;
        return;
    }

    if (auto keyRelease = event.cast<vsg::KeyReleaseEvent>(); keyRelease)
    {
        auto* state = getWindowState(vsg::ref_ptr<vsg::Window>(keyRelease->window).get());
        if (!state || !state->context) return;

        ScopedImGuiContext scoped(state->context);
        ImGuiIO& io = ImGui::GetIO();
        updateModifier(io, keyRelease->keyModifier, false);
        const auto imguiKey = convertKey(keyRelease->keyBase, keyRelease->keyModified);
        if (imguiKey != ImGuiKey_None) io.AddKeyEvent(imguiKey, false);
        keyRelease->handled = io.WantCaptureKeyboard;
    }
}

uint32_t VsgInputManager::convertButton(uint32_t button)
{
    return button == 1 ? 0 : button == 3 ? 1 : button;
}

void VsgInputManager::updateModifier(ImGuiIO& io, vsg::KeyModifier& modifier, bool pressed)
{
    if (modifier & vsg::MODKEY_Control) io.AddKeyEvent(ImGuiMod_Ctrl, pressed);
    if (modifier & vsg::MODKEY_Shift) io.AddKeyEvent(ImGuiMod_Shift, pressed);
    if (modifier & vsg::MODKEY_Alt) io.AddKeyEvent(ImGuiMod_Alt, pressed);
    if (modifier & vsg::MODKEY_Meta) io.AddKeyEvent(ImGuiMod_Super, pressed);
}

ImGuiKey VsgInputManager::convertKey(vsg::KeySymbol keyBase, vsg::KeySymbol keyModified)
{
    if (keyModified >= vsg::KEY_KP_0 && keyModified <= vsg::KEY_KP_9) keyBase = keyModified;
    if (keyBase >= vsg::KEY_a && keyBase <= vsg::KEY_z)
    {
        return static_cast<ImGuiKey>(ImGuiKey_A + (keyBase - vsg::KEY_a));
    }
    if (keyBase >= vsg::KEY_0 && keyBase <= vsg::KEY_9)
    {
        return static_cast<ImGuiKey>(ImGuiKey_0 + (keyBase - vsg::KEY_0));
    }

    switch (keyBase)
    {
        case vsg::KEY_Space: return ImGuiKey_Space;
        case vsg::KEY_Tab: return ImGuiKey_Tab;
        case vsg::KEY_Return: return ImGuiKey_Enter;
        case vsg::KEY_Escape: return ImGuiKey_Escape;
        case vsg::KEY_Delete: return ImGuiKey_Delete;
        case vsg::KEY_BackSpace: return ImGuiKey_Backspace;
        case vsg::KEY_Left: return ImGuiKey_LeftArrow;
        case vsg::KEY_Right: return ImGuiKey_RightArrow;
        case vsg::KEY_Up: return ImGuiKey_UpArrow;
        case vsg::KEY_Down: return ImGuiKey_DownArrow;
        case vsg::KEY_Page_Up: return ImGuiKey_PageUp;
        case vsg::KEY_Page_Down: return ImGuiKey_PageDown;
        case vsg::KEY_Home: return ImGuiKey_Home;
        case vsg::KEY_End: return ImGuiKey_End;
        case vsg::KEY_Insert: return ImGuiKey_Insert;
        case vsg::KEY_F1: return ImGuiKey_F1;
        case vsg::KEY_F2: return ImGuiKey_F2;
        case vsg::KEY_F3: return ImGuiKey_F3;
        case vsg::KEY_F4: return ImGuiKey_F4;
        case vsg::KEY_F5: return ImGuiKey_F5;
        case vsg::KEY_F6: return ImGuiKey_F6;
        case vsg::KEY_F7: return ImGuiKey_F7;
        case vsg::KEY_F8: return ImGuiKey_F8;
        case vsg::KEY_F9: return ImGuiKey_F9;
        case vsg::KEY_F10: return ImGuiKey_F10;
        case vsg::KEY_F11: return ImGuiKey_F11;
        case vsg::KEY_F12: return ImGuiKey_F12;
        case vsg::KEY_Shift_L: return ImGuiKey_LeftShift;
        case vsg::KEY_Shift_R: return ImGuiKey_RightShift;
        case vsg::KEY_Control_L: return ImGuiKey_LeftCtrl;
        case vsg::KEY_Control_R: return ImGuiKey_RightCtrl;
        case vsg::KEY_Alt_L: return ImGuiKey_LeftAlt;
        case vsg::KEY_Alt_R: return ImGuiKey_RightAlt;
        case vsg::KEY_Super_L: return ImGuiKey_LeftSuper;
        case vsg::KEY_Super_R: return ImGuiKey_RightSuper;
        default: return ImGuiKey_None;
    }
}

} // namespace vkvsg

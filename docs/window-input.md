# Window & Input

## Window API (`modules/api/include/sonnet/api/window/IWindow.h`)

`IWindow` is the platform-independent windowing contract. It exposes:

- Title, framebuffer size, visibility, close-request flag, focus state.
- `pollEvents()` and `swapBuffers()` for the per-frame loop.
- Cursor management (`captureCursor`, `releaseCursor`).
- `toggleFullscreen()`.
- `WindowState` enum (`Normal | Focused | Unfocused | Minimized | Maximized | Fullscreen | Hidden | Closed`).

## GLFW implementation (`modules/window`)

`GLFWWindow` is the only concrete `IWindow`. It owns a `GLFWwindow*`, registers the standard set of callbacks, and forwards key/mouse events to an attached `GLFWInputAdapter`.

When `SONNET_USE_VULKAN` is defined:

- `GLFW_INCLUDE_VULKAN` is set as a PUBLIC compile definition by `modules/window/CMakeLists.txt`, so `<GLFW/glfw3.h>` pulls in `<vulkan/vulkan.h>` everywhere.
- `GLFWWindow::createVulkanSurface(VkInstance)` returns a `VkSurfaceKHR` for the backend.
- `GLFWWindow::requiredVulkanInstanceExtensions()` returns the GLFW-reported list of required instance extensions.

## Input API (`modules/api/include/sonnet/api/input`)

Two interfaces:

- `IInputSink` — receives raw `KeyEvent` / `MouseEvent` from a platform adapter.
- `IInput` — query-style API for game code: `isKeyDown / JustPressed / JustReleased`, `isMouseDown / JustPressed / JustReleased`, `mouseDelta()`, `nextFrame()`.

`Key` is a renderer-agnostic enum (letters, digits, function keys, modifiers, arrows, numpad, etc.). `MouseButton` is `Left | Right | Middle | Mouse4 | Mouse5`.

## InputSystem (`modules/input`)

`InputSystem` implements both interfaces. Internally it tracks per-key/per-button `(current, previous)` pressed states and accumulates mouse motion. Each frame the application calls `input.nextFrame()` to roll `current` into `previous` and reset `mouseDelta`.

The GLFW callbacks plumbed through `GLFWInputAdapter` translate GLFW key/button codes into the engine's `Key` / `MouseButton` enums and forward them to `InputSystem` as `IInputSink`.

## Typical wire-up

```cpp
sonnet::window::GLFWWindow window{{1280, 720, "Sonnet"}};
sonnet::input::InputSystem input;
sonnet::window::GLFWInputAdapter adapter{input};
window.setInputAdapter(&adapter);

while (!window.shouldClose()) {
    window.pollEvents();
    if (input.isKeyJustPressed(Key::Escape)) window.requestClose();
    // ...
    input.nextFrame();
}
```

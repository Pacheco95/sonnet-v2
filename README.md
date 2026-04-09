# Sonnet
Sonnet is a modern cross-platform 3D game engine developed with C++ 23 and CMake.

Sonnet is implemented in a modular way using one-way dependencies.
It uses free cross-platform open-source dependencies as much as possible.
The engine is decoupled and should be easy to change the rendering library (Vulkan, OpenGL, etc.) as needed.
The dependencies are fetched using the CMake FetchContent feature as much as possible in order to make the project setup automatic and simpler.

The engine defines clear APIs such as:
- Window API
- Low level renderer API (that must be implemented by Vulkan, OpenGL, etc.)
- High level renderer API (uses the low level API)
- Input system

Tests are implemented using `Catch2`.
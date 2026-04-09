# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Sonnet is a modern cross-platform 3D game engine built with C++23 and CMake. It uses a modular architecture with strict one-way dependencies between modules. Dependencies are managed via CMake `FetchContent` for automatic setup.

## Build System

CMake is the build system. Dependencies are fetched automatically via `FetchContent` — no manual dependency installation required beyond CMake itself.

## Architecture

The engine is organized around clear, decoupled APIs:

- **Window API** — platform-independent windowing
- **Low-level Renderer API** — abstraction over graphics backends (Vulkan, OpenGL, etc.)
- **High-level Renderer API** — built on top of the low-level API
- **Input System** — cross-platform input handling

The low-level renderer is designed to be swappable; changing the graphics backend should not require changes to the high-level renderer or game logic.

Modules follow **one-way dependency** rules — no circular dependencies between layers.

## Testing

Tests use [Catch2](https://github.com/catchorg/Catch2), fetched via CMake `FetchContent`.

# SDL3 GPU for Beginners

A small C++20 tutorial: open a window, create an SDL GPU device, and draw a spinning textured quad. The same code runs on Vulkan, Direct3D 12, and Metal. SDL3 core only. No engine, no SDL_mixer, no image library.

![The finished sample: a spinning 8x8 checkerboard quad](docs/images/screenshot-lavapipe.png)

**New here?** Let an AI assistant do the build setup, then check its work against a short list. The [guide](docs/guide.md) has paste-ready prompts and that checklist. Hand-written CMake is an appendix, for when you want to see what the assistant did.

**Stay current.** Assistants often suggest an old SDL tag, SDL2 packages, or stale CMake. Before you build, paste: `Check that this project uses the latest stable SDL3 release and current CMake setup; update anything outdated before building.` Ask it to confirm SDL_shadercross, the CMake minimum, and the install steps as well.

## Build and run

You need CMake 3.24 or newer, a C++20 compiler, and a GPU (Lavapipe is enough on Linux). The default build downloads [SDL 3.4.16](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16) and checks its SHA256. Nothing else is fetched.

```sh
cmake --preset debug
cmake --build --preset debug
```

The build produces two programs. `quad` is Part 1. `text` is Part 2. With Makefiles or Ninja they are `./out/debug/quad` and `./out/debug/text`. With Visual Studio they are `out\debug\Debug\quad.exe` and `out\debug\Debug\text.exe`. Release uses `release` in place of `debug`.

```sh
GUIDE_FRAMES=120 ./out/debug/quad   # quit after 120 frames
GUIDE_FRAMES=120 ./out/debug/text
```

The log line `GPU driver: vulkan` (or `direct3d12`, or `metal`) means SDL picked a backend. `quad` shows the spinning checkerboard. `text` shows a title and a live frame-time line. Press **T** to switch between the font atlas and the hand-drawn bitmap font. The walkthrough is [Part 2: text](docs/part-02-text.md).

Already have SDL3 installed? Configure with `-DUSE_SYSTEM_SDL3=ON`.

Linux needs the usual X11/Wayland and Vulkan headers before SDL will configure. The [guide's appendix](docs/guide.md#appendix-a-under-the-hood-diy-build-setup) lists them, including `libxtst-dev` on Debian.

## Shaders

`shaders/` and `parts/02-text/shaders/` hold the HLSL source and precompiled SPIR-V, DXIL, and MSL, so Windows and macOS can run without extra tools. If `shadercross` from [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross) is on your `PATH`, the build recompiles the HLSL (that is the `COMPILE_SHADERS` option, on by default). Pass `-DCOMPILE_SHADERS=OFF` to always use the committed bytecode.

## What's next

Part 1 is this quad. [Part 2](docs/part-02-text.md) draws text: a glyph is just that quad, it is satisfying to see your own words early, and score, lives, and a debug overlay all reuse it. Part 3 is palettes. The checkerboard and a 1-bit glyph are already indexed images, and a small color table is still how you get team colors, hit flashes, and quick programmer art. After that, a classic vector Asteroids game on the same setup: a line-list ship, a fixed timestep, rocks that split, and sound through SDL3's own audio streams. The first ship movement uses plain key checks so it flies quickly. A later part turns those checks into named actions (thrust, turn, fire, pause) bound to keys and gamepad buttons in a table, with rebinding and hot-plug. Every later part is **planned**. They will be released gradually, one part at a time. There are no dates.

| Part | Title | Status |
| --- | --- | --- |
| 1 | Window, device, pipeline, textured quad | this repo |
| 2 | Text: a font atlas | [this repo](docs/part-02-text.md) |
| 3 | Palettes | planned |
| 4 | Vector ship as a line list | planned |
| 5 | Fixed timestep, thrust, rotation, wrap (plain keys) | planned |
| 6 | Dynamic vertex buffer | planned |
| 7 | Bullets | planned |
| 8 | Actions: keys and gamepad | planned |
| 9 | Asteroids and splitting | planned |
| 10 | Collisions | planned |
| 11 | Score, lives, waves, UI | planned |
| 12 | Audio with core audio streams | planned |
| 13 | Juice: debris, shake, glow | planned |
| 14 | Polish and a shareable build | planned |

The per-part notes are in the [series outline](docs/series-outline.md).

## License

[MIT](LICENSE). The sample, the guide, the shaders, and the pictures in this repo are original.

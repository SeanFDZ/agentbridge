# AgentBridge — Classic Mac Source

This directory contains the C source for the Classic Mac OS side of
AgentBridge (the `AgentBridge.app` that runs on Mac OS 7 / 8 / 9).

The MCP server and protocol documentation live at the repository root.

## Building

Built with [Retro68](https://github.com/autc04/Retro68) via CMake:

```
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=<path-to-Retro68>/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake ..
make
```

Outputs (`.APPL`, `.bin`, `.dsk`) are gitignored.

## Layout

- `src/` — C source
- `include/` — headers
- `rsrc/` — Rez resource definitions (`AgentBridge.r`)
- `CMakeLists.txt` — build configuration

## License

Source in this directory is licensed under a modified
PolyForm Noncommercial 1.0.0 — see [LICENSE](LICENSE).
This is **different from the GPL-3.0 license** that covers the
MCP server in the repository root.

Copyright (c) 2026 Falling Data Zone, LLC.

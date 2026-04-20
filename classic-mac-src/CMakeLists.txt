cmake_minimum_required(VERSION 3.9)

# Retro68 cross-compiler toolchain
# Build with:
#   mkdir build && cd build
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
#   make
#
# For PowerPC:
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake

project(AgentBridge LANGUAGES C)

add_application(AgentBridge
    src/main.c
    src/protocol.c
    src/commands.c
    src/introspect.c
    src/input.c
    src/files.c
    src/clipboard.c
    src/appleevent_cmds.c
    src/heartbeat.c
    src/bridge_utils.c
    rsrc/AgentBridge.r
)

target_include_directories(AgentBridge PRIVATE include)

# Retro68 handles linking against the Mac Toolbox automatically

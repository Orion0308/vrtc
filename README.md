# vrtc

First-person sandbox in an endless, fully destructible voxel world. `Plan.txt` is the spec; `docs/GUIDE.md`
explains what is built, how it works and how to use it; `DECISIONS.md`
records choices made where it is silent. Current state: milestone 2 (static streaming world with a fly camera).

## Build

C++23, OpenGL 4.6 core, CMake 3.28+, vcpkg packages `glfw3 glad glm imgui[glfw-binding,opengl3-binding] stb`.
FastNoiseLite is vendored in `external/`. Open the folder in CLion with the vcpkg toolchain file set, or:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
    cmake --build build
    build/vrtc [--seed N] [--workers N] [--run-seconds S] [--autofly M/S] [--name NAME]

`--autofly` moves the camera forward without input and `--run-seconds` quits after a while; together they
give a repeatable streaming and memory test, e.g. `vrtc --autofly 30 --run-seconds 600`.

Use RelWithDebInfo or Release to measure anything: generation and meshing are many times slower unoptimised.

## Controls

Click the world to capture the mouse, Esc to release. WASD moves, Space/Shift up and down, Ctrl sprints,
scroll changes speed, F1 hides the debug panel.

## Layout

    src/core     jobs, time, log, queue, assertions (depends on nothing)
    src/shared   Config.h, coordinates, BlockRegistry, Chunk, protocol, serialisation, LocalConnection
    src/server   World, WorldGen, Server tick loop, ECS core
    src/client   Window, Input, Camera, ClientWorld replica, Mesher, ChunkRenderer, Renderer, DebugUI
    src/main.cpp links server and client through a LocalConnection
    assets/      shaders (data files arrive with M7)

## Editing the world (test tool)

Left mouse digs, right mouse places, at whatever the white outline marks; the orange box is the edit's extent.
The "Edit tool" section of the debug panel (or the keys below) sets:

- mode: whole blocks (sphere of block centres, radius 0 = one block) or micro-voxels (exact 1/16-block carve/fill)
- reach in metres, sphere radius in metres, strength (hardness tiers it can cut), block to place
- keys while flying: Tab mode, `[` `]` radius, `-` `=` reach, R next block

Command-line defaults: `--reach M --radius M --strength N --mode blocks|micro --block NAME`. For automated
tests, `--spawn X Y Z --yaw D --pitch D` set the camera and `--autodig N` / `--autoplace N` fire N edits at the
target without input. Server-side limits live in `shared/Config.h`
(`TOOL_MAX_REACH`, `TOOL_MAX_RADIUS`, `TOOL_COOLDOWN_TICKS`).

## Orientation: where things happen

Read `Plan.txt` sections 2 to 5 first; `DECISIONS.md` lists every deviation. Then:

| Want to... | Look at |
|---|---|
| Change a tunable number | `src/shared/Config.h` (everything lives there) |
| Add a block type | `RegisterDefaultBlocks` in `src/shared/BlockRegistry.cpp` and a tile in `src/client/TerrainTextures.cpp` |
| Add a message | `src/shared/Protocol.h/.cpp` (struct + Encode/Decode), handle it in `Server::HandleMessage` and `Engine::HandleMessage` |
| Change the world on the server | only through `WorldEdit` (`src/server/WorldEdit.h`); it records changes and the server sends BlockDelta |
| Add a server system | `Server::Tick` runs the section 11 order; systems are plain functions taking server state |
| Change terrain | `src/server/WorldGen.cpp`, a pure function of (seed, chunk position) |
| Change what the client does per frame | `Engine::Run` in `src/client/Engine.cpp`: input, drain messages, mesh jobs, render, debug UI |
| Change how blocks look | `assets/shaders/chunk.vert/.frag` and `src/client/Mesher.cpp` |
| Add a debug counter | `ClientStats` / `ServerStats`, drawn in `src/client/DebugUI.cpp` |

Rules that the build enforces or the code relies on: server/ never includes GL or client headers (checked by
`cmake/CheckSplit.cmake`); workers only see snapshots and report back through `ResultQueue`; block coordinates
use shift and mask (`Coords.h`), never division; no per-block virtual calls or hash lookups (look a chunk up
once, then index).

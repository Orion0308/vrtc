# vrtc: what exists, how it works, how to use it

State as of 17 Nov 2026. `Plan.txt` is the spec; this guide describes what has been built against it so far,
how the pieces fit, and how to run and probe them. `DECISIONS.md` records every place the code deviates from
the spec and why. Section numbers below ("spec 9") refer to Plan.txt.

---

## 1. Status at a glance

| Milestone | State | Notes |
|---|---|---|
| M1 Foundation | Done | Four libraries, window, input, fly camera, job system, Config.h, logging, debug panel, GPU profiler |
| M2 Static streaming world | Done | Server thread, LocalConnection, noise terrain with caves, streaming in and out, worker meshing, per-chunk drawing |
| M3 Player | Mostly | Walking player with gravity, jump, sprint and auto-step, exact micro-voxel collision, raycast, Dig/Place. No client prediction or hotbar yet |
| M4 Micro-destruction | Partly | MicroBlock and SlopeBlock storage, Carve and Fill, micro meshing with LOD and a per-block cache, micro raycast, impact carving. Collision is sub-stepped overlap rather than the swept box; no projectiles or palette compression |
| M5 Performance | Partly | Binary greedy meshing, 12-byte instanced quads, face-direction and frustum culling, sky light, sun, fog, upload and dispatch budgets. No shared vertex buffer, multi-draw, cave culling or block light |
| M6 Persistence | Not started | Edited chunks are regenerated from the seed if they unload |
| M7 Survival | Not started | Blocks are defined in code, not `blocks.json` |
| M8 | Started | ECS components, `ForEach`, a physics system, one chaser mob type. No spatial index, flow fields, hordes, combat or defences |
| M9, M10 | Not started | |

Measured on the development machine (six-core laptop, RTX 5060, **Debug build**): about 1200 FPS at an 8-chunk
radius, 30 Hz server tick well under 1 ms, zero frames over 20 ms during a 30 s flight at 30 m/s, one 27 ms
frame at start-up (first draw). Numbers in a RelWithDebInfo build are several times better for generation and
meshing.

---

## 2. Build and run

### Requirements

- C++23 compiler. Developed with the MinGW GCC 15.2 bundled with CLion; MSVC and Clang should work but are untested.
- CMake 3.28 or newer, Ninja.
- vcpkg packages: `glfw3 glad glm imgui[glfw-binding,opengl3-binding] stb`.
- OpenGL 4.6 core. The code uses direct state access (GL 4.5) and `glMultiDraw` is not used yet.
- FastNoiseLite is vendored in `external/`; nothing to install.

### Configure and build

Open the folder in CLion with the vcpkg toolchain file set in the CMake profile, or from a shell:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --target vrtc
```

Every build of `vrtc` also runs `cmake/CheckSplit.cmake`, which fails the build if a server, shared or core
file includes a GL, GLFW, ImGui, stb or client header, or a client file includes a server header (hard rule 1).

On Windows, running `vrtc.exe` outside CLion needs the MinGW runtime DLLs on `PATH`
(`<CLion>/bin/mingw/bin`); without them the process exits silently with code 127.

### Command line

```
vrtc [--seed N] [--workers N] [--name NAME]
     [--run-seconds S] [--autofly M/S] [--autodig N] [--autoplace N] [--spawn X Y Z] [--yaw DEG] [--pitch DEG]
     [--fly] [--enemies N]
     [--reach M] [--radius M] [--strength 0-15] [--mode blocks|micro] [--block NAME]
```

| Flag | Meaning |
|---|---|
| `--seed N` | World seed. Random when omitted; shown in the debug panel |
| `--workers N` | Worker threads. Default is hardware threads minus two, at least two |
| `--run-seconds S` | Quit after S seconds (smoke tests) |
| `--autofly M/S` | Fly forward at this speed without input |
| `--autodig N`, `--autoplace N` | Fire N digs or places at whatever the camera looks at, as if the button were held |
| `--spawn X Y Z`, `--yaw`, `--pitch` | Initial camera position and look direction |
| `--fly` | Start with the free fly camera instead of walking (`--autofly` implies it) |
| `--enemies N` | Chasers spawned around the player when it appears (default 1) |
| `--reach`, `--radius`, `--strength`, `--mode`, `--block` | Initial edit tool settings (section 10) |

Useful recipes:

```
vrtc --autofly 30 --run-seconds 600                              # memory and streaming soak
vrtc --spawn 0.5 72 -16 --pitch -89 --reach 60 --radius 1.5 --autodig 10 --run-seconds 10   # carve test
vrtc --mode blocks --radius 0 --reach 8                          # Minecraft-style single block editing
```

The exit log line reports replica size, GPU mesh bytes, meshes built, edits sent and long-frame count.

### Controls

| Input | Action |
|---|---|
| Left click on the world | Capture the mouse and fly |
| Esc | Release the mouse (use the debug panel) |
| W A S D | Walk (or fly, in fly mode) |
| Space | Jump (up, in fly mode) |
| Shift | Down, in fly mode |
| F3 | Toggle walking as the player / free fly camera |
| Ctrl | Sprint |
| Scroll wheel | Change fly speed (fly mode) |
| Left mouse (captured) | Dig at the white outline |
| Right mouse (captured) | Place at the white outline |
| Tab | Toggle edit mode: whole blocks / micro-voxels |
| `[` `]` | Edit radius down / up |
| `-` `=` | Reach down / up |
| R | Next block to place |
| F1 | Hide or show the debug panel |

---

## 3. Architecture

### Libraries and the dependency rule

```
src/core     JobSystem, Time, Log, ResultQueue, FlatMap, VRTC_CHECK     depends on nothing
src/shared   Config.h, Coords.h, Faces.h, BlockRegistry, Chunk, MicroBlock,
             Shapes.h, Raycast.h, Protocol, Serialise.h, ChunkSerial,
             Connection (IConnection, LocalConnection), DebugCounters.h      depends on core, glm
src/server   World, WorldGen, WorldEdit, Server, ecs/                   depends on shared, core, FastNoiseLite
src/client   Window, Input, Camera, Engine, ClientWorld, Mesher,
             ChunkRenderer, Renderer, Shader, Texture, Material,
             TerrainTextures, DebugUI, GpuProfiler, Frustum              depends on shared, core, glfw, glad, imgui, stb
src/main.cpp wires one Server and one Engine together through a LocalConnection
```

Each library is a CMake static target whose include directories are only the directories it may depend on.
Headers are included by bare name (`#include "Chunk.h"`), so a server file literally cannot find `Window.h`.
Keep header names unique across `src/`, and never name a header like a standard header: `Time.h` and
`Assert.h` shadowed `<time.h>` and `<assert.h>` on Windows, which is why they are `Timing.h` and `Check.h`.

### Threads

| Thread | Owns | Runs |
|---|---|---|
| Main | Window, GL context, ClientWorld replica, ChunkRenderer, DebugUI | `Engine::Run`, one iteration per rendered frame |
| Server | World, WorldEdit, sessions | `Server::Run`, a fixed 30 Hz tick (`TICK_RATE`) |
| Workers (N) | nothing | Terrain generation jobs (submitted by the server thread) and mesh jobs (submitted by the main thread) |

The one `JobSystem` is shared. Jobs are `std::move_only_function`s in a priority queue (higher priority first,
FIFO within a priority) served by `std::jthread` workers. Hard rule 5 is kept by construction: a generation job
receives `(seed, ChunkPos)` and returns a new `Chunk`; a mesh job receives a `MeshInput` snapshot and returns a
`MeshOutput`. Results go into a `ResultQueue<T>` (mutex plus vector, drained by swap) that the owning thread
empties once per tick or frame. `JobSystem::WaitIdle()` is called by `Server::Stop` and `~Engine` so no job
outlives the object it reports to.

The server and client never share mutable state. In single-player they share two immutable things:
the `BlockRegistry` (loaded once in `main`) and a `ServerStats` struct of atomics that the debug panel reads.

### The frame and the tick

Client frame (`Engine::Run`), in the order of spec section 11:

1. `Time::Tick`, poll events, update `Input`, camera, raycast the target, handle tool keys and clicks.
   Send `PlayerInput` once per fixed step (30 Hz).
2. Drain server messages for at most `MESSAGE_DRAIN_BUDGET_MS` (1 ms).
3. Collect finished mesh jobs, upload up to `MESH_UPLOADS_PER_FRAME` or `MESH_UPLOAD_BUDGET_MS`, then dispatch
   new mesh jobs up to `MESH_DISPATCH_PER_FRAME` or `MESH_DISPATCH_BUDGET_MS`.
4. Render: clear, terrain (frustum culled, front to back), outlines, then the debug panel. GPU timer queries
   bracket terrain and UI.
5. Swap. Frame phase timings land in `FrameStats`; a frame over `LONG_FRAME_MS` logs its breakdown (first ten).

Server tick (`Server::Tick`), in the order of spec section 11:

1. Drain messages from every session and apply them (inputs update the player's chunk; Dig/Place call `WorldEdit`).
2. Systems: none yet.
3. `FlushEdits`: one `BlockDelta` per edited chunk to every session holding that chunk.
4. Streaming: finish generation jobs, refresh tickets, unload stale chunks, dispatch loads, refresh each
   session's view.
5. Replication: send up to `CHUNK_SENDS_PER_TICK` ChunkData per session, nearest first.

---

## 4. Units, coordinates and conventions

- One block is one metre. A chunk is 32 x 32 x 32 blocks (`CHUNK_BITS` = 5). A micro-voxel is 1/16 block.
- The world is 12 chunks tall: chunk Y from `WORLD_MIN_CHUNK_Y` = -4 (inclusive) to `WORLD_MAX_CHUNK_Y` = 8
  (exclusive), blocks -128 to 255. Horizontal extent is unbounded within int32 chunk coordinates.
- Axes: right handed, +Y up, +X east, -Z north. Face order everywhere is `+Z, -Z, +X, -X, +Y, -Y` (indices 0 to 5,
  `Faces.h`). Front faces wind counter-clockwise.
- `BlockPos` and `ChunkPos` are int32 triples. `ToChunkPos` is an arithmetic shift, `LocalCoord` a mask, so
  negative coordinates work. `ToBlockPos(dvec3)` floors; never truncate a float.
- Block index inside a chunk: `x | z << 5 | y << 10`, so x-runs are contiguous in memory.
- Positions are `glm::dvec3` metres. The client renders relative to the camera's chunk origin: the view matrix is
  built from `camera.position - chunkOrigin` as a float, and each chunk is drawn with a float origin relative
  to that same chunk, so precision never degrades far from the origin.
- Everything is in `namespace vrtc`. Types and functions are PascalCase, variables camelCase, constants
  UPPER_SNAKE. All tunables are in `shared/Config.h`.

---

## 5. World data model

### BlockRegistry (`shared/BlockRegistry.h`)

`BlockId` is a `uint16_t`; 0 is air. A `BlockDef` has a name, `solid`, `opaque`, six face texture names,
hardness (0 to 15), light emission, drop name and flags (`hasBlockEntity`, `replaceable`, `unbreakable`).
The registry is a vector indexed by ID plus packed side arrays `Opaque()` and `Solid()` that hot loops read.
`Find(name)` returns air for unknown names; `Require(name)` stops the program.

`RegisterDefaultBlocks` defines stone, dirt, grass and bedrock in code. It is the stand-in for `blocks.json`
(M7); nothing else in the engine knows block IDs by number. Grass uses the grass tile on its sides as well as
its top, because slopes are made of sixteenth-high risers.

### Chunk (`shared/Chunk.h`)

```
std::unique_ptr<std::array<BlockId, 32768>> blocks;   // null while uniform
BlockId uniformId;                                     // the block everything is, while uniform
std::vector<MicroBlock> micro;                         // masks of edited blocks
std::vector<uint16_t>   microOwner;                    // slot -> block index
FlatMap<uint16_t, uint16_t> microSlot;                 // block index -> slot
std::vector<SlopeBlock> slopes;                        // generated partial blocks, sorted by block index
std::array<Chunk*, 6> neighbours;  uint32_t version;  uint8_t dirty;
```

A block is in one of four states:

| State | Stored as | Mask |
|---|---|---|
| Air | ID 0 | none |
| Whole | its ID | none |
| Generated slope | its ID + a `SlopeBlock` (18 bytes) | computed from eight corner values on demand |
| Edited | its ID + a `MicroBlock` (512 bytes) | stored |

- A chunk whose blocks are all the same and has no partial blocks stores no array. `Compact()` collapses back
  to that when possible. Palette packing (spec 6) is later work behind the same `Get`/`Set`.
- `SetIndex(index, id)` drops any mask or slope: changing a block's ID makes it whole.
- `Micro(index)` returns the stored mask of an edited block only. Use `GetMask(index, out)` for the mask of any
  partial block, `IsDamaged(index)` to test for either kind, and `Slopes()` to iterate slope blocks.
- `Materialise(index)` converts a slope into an edited mask; `GetOrCreateMicro` does it automatically, and
  `WorldEdit` does it before every edit, so editing code only ever deals with `MicroBlock`.
- Invariants (spec 6): a partial block keeps its ID; all-ones is never stored (the block is whole); all-zeros
  is never stored (the block is air).

### MicroBlock and SlopeBlock (`shared/MicroBlock.h`)

```
struct MicroBlock { uint16_t rows[16][16]; };             // rows[y][z], bit x; 512 bytes
struct SlopeBlock { uint16_t index; int16_t corner[8]; }; // density * 256 at the block corners; 18 bytes
```

`ExpandSlope(corner, mask)` fills a mask: a micro-voxel is solid where the trilinear interpolation of the eight
corners is positive at its centre (corner index: bit 0 x, bit 1 z, bit 2 y). Server and clients run the same
function on the same quantised values, so they agree bit for bit. Storing slopes this way is what makes
micro-voxel terrain affordable: about 8 million partial blocks at a 16 x 8 chunk radius cost 4 GB as masks and
about 150 MB as corners.

### World (server, `server/World.h`)

`std::unordered_map<ChunkPos, LoadedChunk>` where `LoadedChunk` holds the chunk and `lastTicketTick`.
`Insert` links the six face neighbours both ways, `Remove` unlinks. `GetBlock(BlockPos)` exists for one-off
queries; per-block loops look the chunk up once and index.

---

## 6. Terrain generation (`server/WorldGen.cpp`)

`WorldGen::Generate(ChunkPos)` is a pure function of the seed and position, run on a worker. Terrain is voxels
and micro-voxels throughout; slopes are smooth because surface blocks are partial.

1. **Early out above the surface.** A chunk whose bottom is above `WORLDGEN_MAX_SURFACE_Y` is uniform air.
2. **One solidity field on a lattice.** Sampled every 4 blocks (`WORLDGEN_LATTICE`): 9 x 9 points across the
   chunk and 10 layers up, the extra layer so the top rows know what is above them. The value is
   `density = 3D noise x WORLDGEN_DENSITY_AMPLITUDE + (height(x, z) - y)`, forced solid below
   `WORLDGEN_FLOOR_START_Y`, then cut by caves with `min`: tunnels where the sum of squares of two noise fields
   falls below `WORLDGEN_TUNNEL_RADIUS` squared, chambers where a low-frequency field exceeds
   `WORLDGEN_CHAMBER_THRESHOLD` (no caves below `WORLDGEN_CAVE_MIN_Y`). Positive is solid.
3. **Uniform early outs.** If no lattice point is positive the chunk is air; if all are (and this is not the
   bedrock row) it is stone. Neither fills an array.
4. **Interpolation onto block corners.** The lattice is interpolated trilinearly onto a 33 x 37 x 33 grid: every
   corner of every block in the chunk and the 4 rows above.
5. **Classification from corners.** Each block's eight corner values are quantised to 1/256. All positive is a
   whole block, none is air, a mix is a block the surface cuts through. With `WORLDGEN_SMOOTH_SLOPES` such a
   block becomes a `SlopeBlock` (unless its expanded mask turns out full or empty); otherwise it is whole when
   at least four corners are positive.
6. **Materials per column, top down.** The first solid block under air is grass, the next `WORLDGEN_DIRT_DEPTH`
   are dirt, the rest stone. A surface slope block does not count as cover, so the whole block beneath it is
   grass too. The lowest row of the world is bedrock.

Adjacent chunks sample the same world positions on their shared faces, so slopes are continuous across chunk
borders. Ores, trees and boulders are not implemented; they slot in after step 6.

---

## 7. Streaming (server side, `server/Server.cpp`)

Each session (one per connection) tracks the player's position and chunk, the set of chunks it `held` (has
been sent), and a `pendingSend` list.

- **Tickets.** Every player holds a ticket of `LOAD_RADIUS_H` x `LOAD_RADIUS_V` chunks (8 x 4, clamped to the
  world's height). `RefreshTickets` runs when a player changes chunk or every `TICKET_REFRESH_TICKS`: loaded
  chunks in range get `lastTicketTick = now`; missing ones go into `loadQueue` sorted nearest first. Chunks in
  the wider ring (`UNLOAD_RADIUS_EXTRA` = 2 more) also keep their ticket, so the edge does not thrash.
- **Loading.** `DispatchLoads` submits generation jobs up to `workers x LOADS_IN_FLIGHT_PER_WORKER`, with job
  priority = -distance so a moving player's new neighbourhood jumps the queue. `FinishLoads` drains results,
  drops any that no ticket covers any more, inserts the rest into `World` and appends them to the
  `pendingSend` of every session in range.
- **Unloading.** `UnloadStale` removes chunks whose ticket is older than `UNLOAD_GRACE_TICKS` (3 s), sending
  `ChunkUnload` to sessions that held them. Edited chunks are lost here until persistence exists.
- **Replication.** `RefreshView` (on chunk change) unloads held chunks outside the ring and rebuilds
  `pendingSend` as "loaded, in radius, not held", nearest last. `SendReplication` pops up to
  `CHUNK_SENDS_PER_TICK` (64) per tick and serialises each chunk with `WriteChunkRecord`.

Memory stays bounded by the unload ring: about 21 x 21 x 9 chunks per player at most, and only non-uniform
chunks hold the 64 KB array.

---

## 8. The protocol (`shared/Protocol.h`)

Every message is one type byte followed by little-endian fields; counts are varints. `Encode` writes the type
byte and body; `Decode` expects the type byte to have been consumed (callers `PeekType` first). `ByteReader`
never throws: a short buffer sets `Ok()` false.

| Message | Direction | Body |
|---|---|---|
| Join | client to server | protocol version, player name |
| PlayerInput | client to server | sequence, position (fly mode only), yaw, pitch, buttons (jump, sprint, fly), move axes |
| Dig | client to server | sphere centre, radius, strength, mode |
| Place | client to server | sphere centre, radius, mode, block ID |
| Welcome | server to client | player entity (0 for now), seed, chunk bits, load radii |
| ChunkData | server to client | chunk position, version, chunk record bytes |
| ChunkUnload | server to client | chunk position |
| BlockDelta | server to client | chunk position, version, list of (block index, ID, has mask, 512-byte mask) |
| PlayerState | server to client | last input sequence, position, velocity, on ground (every tick, to the owner) |
| EntitySpawn, EntityDespawn | server to client | entity ID, visual ID, position, box half extents |
| EntitySnapshot | server to client | tick, list of entity ID + position + yaw (every tick) |

`PROTOCOL_VERSION` is checked on Join. The other message types from spec 11 are enumerated but have no body yet.

**Chunk record** (`shared/ChunkSerial.cpp`, format 2): version byte, mode byte (0 uniform, 16 direct), then
either one ID or 32768 IDs followed by a varint count of (index, mask) pairs. The same bytes will go into region
files in M6. No compression yet.

**Connection.** `IConnection` has `Send(span, Channel)` and `Poll(vector&)`. `LocalConnection` is two
mutex-guarded deques; both channels are reliable in memory. ENet arrives in M10 as a second implementation.

---

## 9. The client replica, meshing and drawing

### ClientWorld (`client/ClientWorld.cpp`)

The replica is a map of `ClientChunk { chunk, version, dirty, inFlight, gpu, microCache, microStamp, meshLod,
cover }`.

- `ApplyChunkData` decodes the record, links neighbours, bumps `version` (a client-wide counter, so stale mesh
  results are recognised), clears the quad cache and marks the chunk and its 26 neighbours dirty. If the chunk is
  not air it also marks the chunks below it in the surrounding columns, whose sky light it may change.
- `ApplyBlockDelta` sets IDs and masks, erases the touched blocks from the quad cache, bumps `version` and
  `microStamp`, and marks dirty; a change on local coordinate 0 or 31 also marks the neighbour on that side.
- `ApplyUnload` unlinks neighbours and returns the GPU handle for `ChunkRenderer::Free`.
- `Block(pos)` and `Micro(pos)` serve the raycast; `Micro` expands slopes into a per-thread scratch mask.
- `RefreshLod(cameraChunk)` marks chunks with partial blocks whose micro LOD no longer matches their distance.

### Choosing what to mesh (`Engine::ScheduleMeshing`)

One pass over the dirty list each frame drops stale entries, resolves chunks that are `KnownEmpty` without a
job, and scores the rest that are `Meshable` (all six face neighbours present, or outside the world's height)
by distance, with out-of-frustum chunks after every visible one. The best are dispatched, bounded by in-flight
jobs (`workers x MESH_JOBS_IN_FLIGHT_PER_WORKER`), `MESH_DISPATCH_PER_FRAME` and `MESH_DISPATCH_BUDGET_MS`.

### The snapshot (`ClientWorld::BuildMeshInput`)

A `MeshInput` holds:

- a 34 x 34 x 34 array of block IDs: the chunk plus a one-block border from up to 26 neighbours (air above the
  world, bedrock below it);
- a parallel `whole` array, 0 where a block is partial;
- one small `MicroEntry` per partial block in the chunk: a slope's eight corners inline, or an index into
  `masks` for an edited block, plus any cached quads;
- `coveredAbove`, 34 x 34 bits: whether any opaque block lies above the snapshot in that column, gathered from
  the per-chunk `cover` bitmaps of the loaded chunks overhead;
- the micro LOD for this chunk.

At LOD 3 partial blocks are not sent at all: one that is at least half solid stays in the ID array as a whole
block, anything less becomes air.

### The mesher (`client/Mesher.cpp`, worker thread)

1. **Occluders.** `occluder[i] = opaque[id] && whole[i]`: only opaque-whole blocks hide faces or stop light.
2. **Sky light.** Each column is scanned top down: cells are lit 15 until the first occluder, unless
   `coveredAbove` says the column is already shaded. If any open cell is dark, light floods from the lit cells
   into them losing one level per step, confined to the snapshot.
3. **Pass 1, whole blocks, binary greedy.** Per axis, 64-bit column masks of occluders and of visible whole
   blocks give each column's visible faces with one shift and mask. Those bits are scattered into per-slice
   32 x 32 rows. Within a slice a run of bits grows along the row and then downward while every cell has the
   same key: texture layer, four 2-bit corner AO values, and the sky light of the cell the face looks into.
   Keys are computed lazily, so cost follows visible faces rather than chunk volume.
4. **Pass 2, partial blocks.** For each entry the worker computes six boundary bits (is the neighbour on that
   side an occluder) and takes the sky light of the block's own cell. If cached quads were built with the same
   bits and light they are reused. Otherwise the mask is taken from `masks` or expanded from the slope corners,
   reduced to 8 or 4 cells per side for LOD 1 and 2 (a coarse cell is solid when at least half its micro-voxels
   are), and meshed: per row, visibility bits for all six faces come from bit operations against the
   neighbouring rows, and each slice's rows are merged by the same binary greedy routine.
5. **Output.** Quads grouped by face direction with the six range starts, plus the freshly built partial-block
   meshes for the cache (LOD 0 chunks only).

Back on the main thread `CollectMeshes` stores cache entries if the chunk's `microStamp` is unchanged, drops
results whose `version` no longer matches, and queues the rest for upload within the per-frame budget.

Micro LOD by Chebyshev chunk distance from the camera (`MicroLodFor`): 16 cells per block side within
`MICRO_LOD0_CHUNKS`, 8 within `MICRO_LOD1_CHUNKS`, 4 within `MICRO_LOD2_CHUNKS`, whole-or-nothing beyond.

### GPU format and drawing (`client/ChunkVertex.h`, `client/ChunkRenderer.cpp`, `assets/shaders/chunk.*`)

One `ChunkQuad` per greedy quad, 12 bytes:

```
a:  x:10 | y:10 | z:10           min corner on the face's plane, 1/16 block steps, chunk-local
b:  w:10 | h:10 | face:3 | ao:8  extent along the face's two axes; 2 bits of AO per corner
c:  layer:12 | sky:4 | blockLight:4
```

Each chunk has a VAO and a buffer of quads bound with an instance divisor of one. The vertex shader expands a
quad to four corners from `gl_VertexID` (a 4-vertex strip), choosing the diagonal from the AO values, and
derives texture coordinates from position and face (one tile per block, repeat wrapping).

`ChunkRenderer::Draw` frustum-culls chunk boxes, sorts survivors front to back, and for each chunk skips the
face directions the camera cannot see (a +X face is only visible from beyond the chunk's -X side, and so on),
merging adjacent visible directions into one `glDrawArraysInstancedBaseInstance`. Chunk origins are relative
to the camera's chunk, so precision holds far from the world origin. Lighting is described in section 15.

Textures: one 16 x 16 `GL_TEXTURE_2D_ARRAY`, nearest filtering with mipmaps, sRGB. Layer 0 is a magenta missing
tile; `TerrainTextures.cpp` generates placeholder tiles by name and builds `MeshTables` (`opaque[id]`,
`visible[id]`, `faceLayer[id * 6 + face]`) from the registry.

---

## 10. Editing the world

### Client side

Every frame `Engine::UpdateTarget` raycasts from the camera along its forward vector up to the tool's reach,
against the replica. `Raycast` (`shared/Raycast.h`) is a 3D DDA over blocks; when it enters a damaged block it
runs a second DDA over that block's 16^3 grid and continues if it passes through a gap. The result gives the
block, the face entered (its normal points back toward the ray), the micro cell if any, the distance and the hit
point. The white outline marks the block or micro cell; the orange box marks the extent of the edit a click
would apply.

Clicks (or `--autodig`/`--autoplace`) send `Dig` or `Place`, repeating every `TOOL_REPEAT_SECONDS` while held.
The sphere centre depends on the mode:

| Mode | Dig centre | Place centre |
|---|---|---|
| Micro | hit point nudged 1 mm into the block (digs a bowl) | hit point nudged 1 mm out (adds a bump) |
| Whole blocks | centre of the hit block | centre of the block in front of the hit face |

The client never changes its replica itself; it waits for the BlockDelta (hard rule 3).

### Server side

`Server::ValidateEdit` rejects edits unless the session has joined and has a position, the radius is within
`TOOL_MAX_RADIUS`, the centre is within `TOOL_MAX_REACH` (+ radius) of the player, and `TOOL_COOLDOWN_TICKS`
have passed since the player's last edit. Rejections are counted.

`WorldEdit` (`server/WorldEdit.cpp`) is the only writer of loaded chunks:

- **Carve(shape, strength)**, spec 7: walk the shape's block bounding box, chunk looked up once per chunk; skip
  air, unbreakable, and hardness above strength; a block wholly inside the shape becomes air; otherwise the mask
  is created (all ones) and every bit whose micro centre is inside is cleared, one 16-bit row at a time using
  `RowBitsInside` (an analytic x-range per row for spheres). An all-zero mask becomes air. Bits removed are
  counted per block ID in `CarveResult` for the drop system (M7).
- **Fill(shape, id)**: fills air (whole block if fully inside, else a mask of the inside bits) and repairs
  damaged blocks of the same material; an all-ones mask becomes a whole block. A different material is never
  placed into a damaged block's gaps.
- **RemoveBlocks / PlaceBlocks**: the whole-block mode, every block whose centre is inside the shape.
- **SetBlock(pos, id)** for later systems.

Every change bumps the chunk's `version`, sets its dirty flags and records the block index. `FlushEdits` at
step 3 of the tick emits one `BlockDelta` per edited chunk (sorted, de-duplicated indices, with current ID and
mask) to each session that holds the chunk. Navigation, persistence and block-entity listeners attach at the
same point later.

---

## 11. The debug panel

Press F1 to toggle. Sections:

- **Edit tool**: mode, reach, radius (also shown in micro-voxels), strength, block to place, presets; the current
  target (block, position, distance, face, micro cell); edits sent and deltas applied; server edits applied and
  rejected with the last edit's cost; micro-voxels carved and filled, damaged block count and their memory;
  the last edit remesh (time, quads, damaged blocks rebuilt vs cached) and the last mesh job.
- **Frame**: FPS, frame time, worst frame in the last second, count of frames over 20 ms, CPU vs GPU bottleneck,
  two 240-frame graphs, per-phase CPU bars (events, drain, mesh, draw, UI, swap) and GPU timer sections.
- **World and streaming**: camera position and chunk, seed; replica size, chunks needing a mesh (and how many are
  meshable now, the rest wait for neighbours), jobs in flight, pending uploads, mesh job timings against the
  3 ms budget, message rate; job system counters; server tick time, loaded/generating/queued chunks, sent bytes.
- **Rendering**: wireframe toggle, chunks drawn and culled, quads and triangles, entity draws, GPU pipeline
  statistics (primitives, shader invocations, overdraw).
- **Memory**: GPU mesh bytes, entity meshes, textures, VRAM (NVIDIA or AMD extensions), device strings.

Add a counter by extending `ClientStats` or `ServerStats` and drawing it in `DebugUI.cpp` (hard rule 10).

---

## 12. Carried-over pieces not yet in use

- `client/Renderer` is the old sort-key renderer with an unpacked `EntityVertex` and per-material colour and
  texture. It draws the debug outlines today and is meant for entities and models.
- `server/ecs/` holds `Entity` (20-bit index, 12-bit generation), `EntityManager` (rejects double free, retires
  an index instead of wrapping its generation), `ComponentArray` (sparse set with `Has`, `TryGet`, checked `Get`,
  no-op `Remove`) and `Registry` (`Destroy` removes from every pool then bumps the generation). Views and the
  deferred command buffer come with M8.
- `GpuProfiler` keeps timer and pipeline-statistics queries four frames deep so reading them never stalls.

---

## 13. Known limitations

- No client-side prediction: walking lags input by one to two server ticks. The fly camera (F3) ignores collision.
- Collision uses sub-stepped overlap tests, not the swept box of spec 13; body-body contacts test all pairs.
- Edited chunks revert to generated terrain when they unload (no persistence yet).
- Edited masks are run-length encoded on the wire; LZ4 waits for M6.
- Per-chunk draw calls and per-chunk buffer uploads; occasional multi-millisecond swap stalls in Debug builds
  when a large mesh replaces a buffer the GPU is still reading. The M5 shared buffer removes both.
- Chunk meshes take about 300 MiB of GPU memory at a 16 x 8 radius; lower the `MICRO_LOD*_CHUNKS` values to trade distant detail for memory.
- Terrain has no ores, trees, block light or cave culling. Sky light is not re-flooded across chunks after a distant edit.
- Only one connection is wired up; the server code loops over sessions but nothing creates a second one.
- Debug builds are several times slower in generation and meshing; measure with RelWithDebInfo.

---

## 14. File index

```
CMakeLists.txt            four libraries, the executable, the include-split check
cmake/CheckSplit.cmake    hard rule 1 enforced on every build
assets/shaders/           chunk.vert chunk.frag (terrain), entity.vert entity.frag (entities, outlines)
external/FastNoiseLite.h  vendored noise library (MIT)

src/core/Check.h          VRTC_CHECK (always) and VRTC_ASSERT (debug) that stop with a message
src/core/FlatMap.h        sorted-vector map used for block index -> micro slot
src/core/Jobs.h/.cpp      JobSystem
src/core/Log.h/.cpp       Info/Warn/Error/Fatal with std::format
src/core/Queue.h          ResultQueue<T>
src/core/Timing.h         Time: frame clock and fixed-step accumulator

src/shared/BlockRegistry  block definitions, side arrays, RegisterDefaultBlocks
src/shared/Chunk          block array, uniform storage, micro masks
src/shared/ChunkSerial    chunk record read/write
src/shared/Config.h       every tunable
src/shared/Connection     IConnection, MessageQueue, LocalConnection
src/shared/Collision.h    box against whole blocks and micro-voxels
src/shared/Coords.h       BlockPos, ChunkPos, conversions, hash
src/shared/DebugCounters.h ServerStats atomics
src/shared/Faces.h        face order, normals, corner tables
src/shared/MicroBlock.h   the 16^3 mask
src/shared/Protocol       message structs, Encode/Decode
src/shared/Raycast.h      block and micro DDA (template over a world query)
src/shared/Serialise.h    ByteWriter/ByteReader
src/shared/Shapes.h       Sphere, Box, containment and row tests

src/server/Server         tick loop, sessions, streaming, replication, Dig/Place handling
src/server/World          loaded chunk map with neighbour linking
src/server/WorldEdit      SetBlock, Carve, Fill, whole-block variants, change recording
src/server/WorldGen       terrain
src/server/Components.h   Transform, Collider, RigidBody, Health, PlayerControl, MobAI, Replicated
src/server/Systems        PlayerMovementSystem, MobAISystem, PhysicsSystem
src/server/ecs/           Entity, ComponentArray, Registry

src/client/Camera         fly camera, relative-to-chunk view matrix
src/client/ChunkRenderer  per-chunk VAO/VBO, shared index buffer, culled front-to-back draw
src/client/ChunkVertex.h  the 8-byte packed vertex
src/client/ClientStats.h  ToolSettings and client counters
src/client/ClientWorld    the replica, delta application, snapshots, micro cache
src/client/DebugUI        ImGui panel
src/client/Engine         the frame loop, targeting, tool input, mesh scheduling
src/client/FrameStats.h   per-phase frame times
src/client/Frustum.h      frustum planes and box test
src/client/GpuProfiler    GPU timer and statistics queries
src/client/Input          polled keyboard/mouse state, cursor capture
src/client/Material.h     shader + texture + colour
src/client/Mesher         greedy meshing, both passes
src/client/Renderer       sort-key renderer for entities and debug lines
src/client/Shader         program loading with cached uniform locations
src/client/TerrainTextures placeholder tiles and MeshTables from the registry
src/client/Texture        texture arrays with the missing tile at layer 0
src/client/Window         GLFW window and GL 4.6 context
src/main.cpp              argument parsing, wiring server and client
```

---

## 15. Lighting

Three cooperating parts, all client-side:

- **Sky light, baked per quad** (see the mesher, section 9). Open sky is 15; light loses one level per block as
  it floods under overhangs and into caves and tunnels. Opaque whole blocks stop it; partial blocks let it
  through. The flood stays inside one chunk's snapshot, so light that would detour through a neighbouring
  chunk is missed, which only dims a few cells near chunk borders. Chunks are re-lit when they are re-meshed:
  on edits to them or their neighbours, and when a chunk arrives above them. A hole dug far above an already
  meshed cave does not brighten it until that cave chunk is next re-meshed.
- **A sun**, the light source. `LightSettings` (elevation, azimuth) becomes a direction, a colour and an
  ambient level in `Engine::UpdateLighting`: full day above about 15 degrees, night below the horizon, a warm
  tint while low. The vertex shader lights each face with `ambient x skyAmount + sun x max(N.L, 0)`, where the
  direct term only reaches cells that are nearly open to the sky, then multiplies by ambient occlusion.
  Because the sun is a uniform, changing it costs nothing.
- **Distance fog** toward the sky colour, from `FOG_START_FRACTION` of the load radius to just inside it, which
  hides chunks appearing at the edge.

Controls are in the debug panel under "Light": sun elevation and azimuth sliders, a day/night cycle
(`SUN_CYCLE_SECONDS` per day) and a fog toggle. Defaults are `SUN_ELEVATION_DEGREES` and `SUN_AZIMUTH_DEGREES`.
There are no cast shadows and block light (torches) is not implemented; the 4-bit block light field exists in
the quad format.

---

## 16. Entities, the player and physics

Everything here is simulated on the server (hard rule 3). The client sends input and interpolates what it is told.

### Components (`server/Components.h`)

| Component | Fields |
|---|---|
| `Transform` | position (centre of the box, dvec3 metres), yaw, pitch |
| `Collider` | half extents of the axis-aligned box |
| `RigidBody` | velocity, acceleration requested this tick, force accumulated this tick (all dvec3); mass (kg), gravity scale, drag, friction, restitution, step height, hardness; flags `onGround`, `blockedHorizontally`, `frozen` |
| `Health` | current, maximum (not used yet) |
| `PlayerControl` | session index, move axes, jump, sprint, fly, last input sequence |
| `MobAI` | type, target entity |
| `Replicated` | visual ID, the only link to rendering |

`Registry::ForEach<A, B, ...>(fn)` iterates pool A and calls `fn(entity, a, b, ...)` for entities that have all
of them. Entity handles are a 20-bit index and a 12-bit generation; `Destroy` removes every component.

### Systems (`server/Systems.cpp`), run by `Server::RunSystems` each tick

1. **PlayerMovementSystem.** Turns input into a wished horizontal velocity along the player's yaw (walk
   `WALK_SPEED`, sprint `SPRINT_SPEED`) and requests acceleration `(wish - velocity) x GROUND_ACCEL` (or
   `AIR_ACCEL` in the air). Jump sets the upward velocity for `JUMP_HEIGHT` under `GRAVITY`. Fly mode freezes
   the body and takes the client's position as is.
2. **MobAISystem.** A chaser steers the same way toward the nearest player at `ENEMY_SPEED`, holds position at
   `ENEMY_STOP_DISTANCE`, and hops when a wall taller than its step height stops it.
3. **PhysicsSystem.**
   - Integration, semi-implicit Euler: `a = requested + force / mass - gravity`, `v += a dt`, drag, ground
     friction, then the move.
   - The move is split into sub-steps of at most `PHYSICS_SUBSTEP` (5 cm, less than a micro-voxel), each tried
     per axis, so no speed carries a body through a thin wall.
   - Collision is `BoxOverlapsSolid` (`shared/Collision.h`): whole solid blocks, and for partial blocks the
     exact micro-voxels under the box, tested a row of bits at a time. Slope masks are expanded on demand and
     kept in a small cache for the tick. Unloaded space counts as solid, and a body whose own chunk is not
     loaded is held in place, so nothing falls through the world while it streams in.
   - Auto-step: a grounded body blocked sideways is lifted one micro-voxel at a time up to its `stepHeight`
     (0.5 m) if there is headroom. This is what lets bodies walk up sixteenth-high slope stairs.
   - A blocked axis stops (or bounces by `restitution` above `BOUNCE_MIN_SPEED`). Landing sets `onGround`.
   - Impact damage: a body with `hardness > 0` that stops faster than `IMPACT_CARVE_SPEED` carves a sphere at
     the contact, sized from its kinetic energy `0.5 m v^2`, cutting blocks up to its hardness.
   - Body against body: overlapping boxes are separated along the axis of least penetration, the push shared by
     inverse mass, with an impulse exchange along that axis. All pairs are tested; the spatial hash from spec
     section 12 replaces that loop when hordes arrive.

To push a body from gameplay code, add to `RigidBody::force` (newtons, for this tick) or `acceleration`; both
are cleared after integration. To launch it, set `velocity` directly.

### The player and replication

- The player entity is created on the first `PlayerInput`, at the position the client reports, with a
  0.6 x 1.8 x 0.6 m box and `PLAYER_MASS`. `--enemies N` chasers spawn in a ring around it (default 1): 1 m cubes
  with `ENEMY_MASS` and `ENEMY_HARDNESS`.
- `PlayerInput` carries move axes, jump, sprint, fly, yaw and pitch at 30 Hz. The server answers every tick with
  `PlayerState` (position, velocity, on ground) to the owner and one `EntitySnapshot` of everything else that
  is `Replicated`; `EntitySpawn` announces an entity with its visual ID and box size.
- The client keeps the last two positions of the player and of each entity and interpolates across one tick.
  Walking, the camera sits `PLAYER_EYE_ABOVE_CENTRE` above the interpolated player position; there is no
  client-side prediction yet, so movement lags input by one to two ticks. F3 (or `--fly`) switches to the free
  camera, during which the server moves the body to wherever the camera is.
- Entities are drawn as boxes through the entity `Renderer`; the enemy visual is a cyan cube.
- Streaming follows the player entity's position (`Server::SyncSessions`).

# Decisions

Choices made where Plan.txt is silent or where a milestone needed a stand-in (spec section 1).

## M1 / M2 (14 Nov 2026)

- **Load radius is 8 x 4 for M2.** Section 5 lists 12 x 4 as the initial value, but M2 is accepted at an 8-chunk
  radius and M5 is where the multi-draw path makes 12 affordable. `LOAD_RADIUS_H` is the only place to change it.
- **PlayerInput carries the camera position in M2.** There is no player entity or shared movement code until
  M3, so the fly camera's position rides in `PlayerInputMsg` and the server uses it to place the player's ticket.
  M3 replaces this with move axes consumed by `shared/PlayerMovement` on both sides. No other message changes.
- **Blocks are defined in code until M7.** `RegisterDefaultBlocks` registers air, stone, dirt, grass and bedrock
  through the same `BlockRegistry` API that `blocks.json` loading will use. Engine code only ever reads
  registry side arrays or resolves names (`Require("stone")`), so swapping in the data file touches one function.
  No JSON library is in the spec's library list; one is chosen when M7 starts.
- **One `BlockRegistry` instance is shared by server and client in single-player.** It is immutable after
  start-up and both would otherwise load the identical data file. Networked clients (M10) load their own copy.
- **Server debug counters live in `shared/DebugCounters.h` as atomics.** They carry no game state and the debug
  UI needs them without the client including a server header (hard rule 1). Replication stays messages-only.
- **Generation and deserialisation write chunk data directly, before insertion into a World.** Hard rule 4
  covers edits to loaded chunks; a chunk being built on a worker or decoded from a message is not yet in any
  world. `Chunk::Set` and `ExpandForWrite` are the only writers, and `WorldEdit` (M3) will use the same ones.
- **The chunk border below the world is treated as solid, above as air.** Section 9 says chunks at the world's
  height limits are meshable; this choice hides the underside of bedrock and shows the top of the highest row.
- **Frustum culling and front-to-back chunk sorting are in M2.** They are listed under M5 but are pure client
  optimisations of a few dozen lines that do not change any interface, and per-chunk draws at an 8-chunk radius
  benefit from them immediately. Cave culling, the shared vertex buffer and multi-draw wait for M5.
- **A new ChunkData marks the 26 surrounding chunks dirty, not 6.** Ambient occlusion at a chunk edge reads
  the diagonal neighbour, so the 6-neighbour rule leaves shading seams at chunk corners until a later remesh.
  Duplicate marks coalesce in the dirty list, so the cost during streaming is bounded.
- **The job system is shared by server and client.** One pool sized "hardware threads minus two" serves
  generation (server thread submits) and meshing (main thread submits); priorities interleave them.
- **Ores and features are not in M2.** Section 8 lists them under generation, but M2's build list is terrain
  with 3D noise and caves. They slot into `WorldGen::Generate` after the surface pass.
- **Vendored FastNoiseLite.** The single header lives in `external/` (MIT) rather than coming from vcpkg,
  which has no port for it in this environment.
- **The include split is checked by a script as well as by include directories.** vcpkg installs every package
  under one include root, so the server library can technically see `<GLFW/glfw3.h>`. `cmake/CheckSplit.cmake`
  runs on every build of `vrtc` and fails it if server/, shared/ or core/ include a forbidden header, or
  client/ includes a server one.
- **Core header names avoid system header names.** `Time.h` and `Assert.h` would shadow `<time.h>` and
  `<assert.h>` on Windows' case-insensitive filesystem once `src/core` is an include directory, so the files are
  `Timing.h` and `Check.h`. Keep every header name in `src/` distinct from any C or C++ standard header.

## Edit test tool (14 Nov 2026)

- **Dig and Place carry the tool shape until items exist.** Section 11 lists Dig as "target block, hit point,
  hotbar slot" and Place as "target block, face, hotbar slot". There is no inventory before M7, so the messages
  carry what the hotbar item would supply: sphere radius, strength, an edit mode and (for Place) a block ID. The
  server still owns the result: it checks reach (`TOOL_MAX_REACH`), radius (`TOOL_MAX_RADIUS`) and a per-player
  cooldown, then calls `WorldEdit`. M7 swaps the shape fields for a hotbar slot without touching `WorldEdit`.
- **Two edit modes.** `Micro` is the real path (`Carve`/`Fill` on 16^3 masks). `WholeBlocks` sets every block
  whose centre is inside the sphere, radius 0 meaning one block; it is the "voxel" counterpart for testing and
  what M3's whole-block dig/place would do before micro-destruction.
- **Whole-block Place requires air; Fill only repairs the same material.** Per section 13: a damaged block
  cannot have a different block placed into its gaps. Entity-overlap checks wait for entities.
- **Micro meshing is per face with x-run merging, no per-block quad cache yet.** Section 9's cache of each
  damaged block's quads is a later optimisation; the per-chunk remesh on every delta is measured in the panel.
- **Edited chunks are lost on unload.** Persistence is M6; until then an edit far enough from the player is
  regenerated from the seed when it reloads.
- **BlockDelta masks are sent raw (512 bytes each).** LZ4 compression of deltas is M6/M10 work.

## Meshing optimisations (14 Nov 2026)

- **Binary greedy meshing for whole blocks.** Per axis, column bitmasks of "opaque-whole" and "visible-whole"
  over the padded 34^3 grid give every face's visibility with one shift and mask per column, so meshing cost
  follows visible faces rather than chunk volume. Faces merge only when texture layer and all four corner AO
  values match (section 9); light joins the key in M5.
- **Damaged blocks are greedy-meshed straight from the 16-bit rows** and their quads are cached per block on
  the main thread (`ClientChunk::microCache`), keyed by the block index and the six boundary-culling bits. A
  BlockDelta erases the entries it touches; a mesh job rebuilds only blocks without a valid entry and returns
  the new quads for the cache. A `microStamp` per chunk discards results built before a later delta.
- **Mesh dispatch is budgeted per frame** (`MESH_DISPATCH_PER_FRAME`, `MESH_DISPATCH_BUDGET_MS`) because the
  snapshot copy runs on the main thread; without it a burst of dirty chunks cost 10 to 18 ms in one frame.
- **Not done yet, by measurement:** the shared vertex buffer and multi-draw (M5) and LZ4 for 512-byte masks
  (M6). Per-chunk `glNamedBufferData` uploads occasionally stall the swap for a few ms in Debug builds.

## Micro-voxel slopes and rendering at scale (16 Nov 2026)

- **Terrain slopes are shaped with micro-voxels at generation** (`WORLDGEN_SMOOTH_SLOPES`). The density lattice is
  interpolated onto every block corner; a block whose corners disagree in sign becomes partial, and a
  micro-voxel is solid where the trilinear interpolation of the corners is positive. Geometry stays voxels and
  micro-voxels throughout: slopes step by 1/16 block instead of whole blocks. Caves are folded into the same
  field (`min` with the scaled tunnel and chamber fields), so cave walls are smooth too.
- **Generated slopes are stored as `SlopeBlock`, not as masks.** This departs from spec section 6, which keeps a
  512-byte mask per damaged block. With slopes, a 16 x 8 radius world held about 8 million partial blocks: 4 GB
  per side as masks. A slope block is fully determined by its eight corner values, so the chunk keeps those
  (18 bytes, quantised to 1/256) in a sorted array and `ExpandSlope` rebuilds the mask on demand for meshing,
  ray casts and edits. The same replica dropped to 500 MiB. Corners are quantised before anything is decided,
  so server and clients derive identical micro-voxels.
- **An edit materialises the slope.** `WorldEdit` calls `Chunk::Materialise` before touching a block, turning it
  into an ordinary `MicroBlock`, so Carve, Fill, BlockDelta and the invariants are unchanged. `Chunk::GetMask`
  and `IsDamaged` answer for both kinds.
- **Surface material follows slope blocks.** A whole block under a surface slope block is also grass, and grass
  uses the grass tile on its sides; otherwise every sixteenth-high riser showed dirt and hillsides striped brown.
- **12-byte instanced quads** replace four 8-byte vertices per quad (spec section 10's format), drawn as a
  4-vertex strip per instance with corners derived in the vertex shader. No index buffer is needed.
- **Face-direction culling.** Each chunk's quads are grouped by face; a direction is skipped when the camera is
  on the wrong side of the chunk to see it, typically removing a third to a half of submitted quads.
- **Micro LOD by distance** (`MICRO_LOD0/1/2_CHUNKS`): partial blocks mesh at 16^3, 8^3, 4^3, and beyond that as
  a whole block if at least half remains. Chunks re-mesh when the camera crosses a band.
- **The per-block quad cache covers only LOD 0 chunks.** Caching every slope block in view cost more memory
  than it saved time; edits happen near the camera, where the cache still makes a remesh rebuild only what
  changed. Boundary-culling bits are now computed on the worker from the snapshot.
- **Masks are run-length encoded on the wire** (chunk record format 4, protocol 4).
- **A surface-nets "smooth" render mode was built and removed** the same day: it was a misreading of the
  request, hid micro-voxel detail and duplicated the terrain in a second representation.

## Lighting, player, enemy and physics (17 Nov 2026)

- **Sky light is flooded per mesh snapshot, not by separate light jobs over 27 chunks** (spec section 10). The
  mesher scans columns for open sky, using a per-chunk cover bitmap for what lies above the snapshot, and floods
  within the 34^3 snapshot. It costs no extra job type and no stored light arrays; the price is that light
  cannot detour through a neighbouring chunk, and a chunk is only re-lit when it is re-meshed.
- **Light is per quad.** It joins the greedy merge key for whole blocks and the cache key for partial blocks,
  so cave gradients show as one-block bands rather than smooth ramps.
- **The sun is a shader uniform** driven by `LightSettings`; nothing is re-meshed when it moves. No shadow maps
  (a spec non-goal for version 1).
- **PlayerInput now carries move axes and buttons; the server simulates the player.** The fly-camera position
  field remains for fly mode only (F3), where the server moves the body to the camera so switching back to
  walking is continuous. There is no client prediction yet: the client interpolates PlayerState.
- **Physics is a system over `RigidBody` in the existing ECS** with mass, force, acceleration, velocity,
  restitution, friction, drag and hardness. Movement is sub-stepped (5 cm per test) with exact micro-voxel
  overlap instead of the swept box of spec section 13: simpler, still tunnelling-proof at any speed because a
  sub-step is smaller than a micro-voxel, but its cost grows with speed. The sweep can replace `MoveBody`
  without touching components or callers.
- **Auto-step works in micro-voxel increments** up to `STEP_HEIGHT`, which is what makes sixteenth-high slope
  stairs walkable.
- **Hardness on a body means what it can chip on impact.** A body stopping faster than `IMPACT_CARVE_SPEED`
  carves a sphere sized from its kinetic energy with its hardness as tool strength. The player has hardness 0.
- **Body-body contacts test all pairs** until the spatial hash (spec section 12) exists.
- **Entity IDs on the wire** are the 20-bit index and 12-bit generation packed into a uint32. Protocol 5.

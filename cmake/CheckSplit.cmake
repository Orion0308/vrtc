# Enforces hard rule 1 at build time (spec section 3): server/ never includes OpenGL, GLFW, ImGui or client/
# headers, and client/ never includes server/ headers. The library include directories already keep the source
# trees apart, but vcpkg installs every package under one include root, so a stray <GLFW/glfw3.h> in server/
# would otherwise compile. Run with: cmake -DSOURCE_DIR=<repo> -P cmake/CheckSplit.cmake
if (NOT SOURCE_DIR)
    message(FATAL_ERROR "CheckSplit: SOURCE_DIR not set")
endif ()

function(check_tree tree forbidden_regex what)
    file(GLOB_RECURSE files "${SOURCE_DIR}/src/${tree}/*.h" "${SOURCE_DIR}/src/${tree}/*.cpp")
    foreach (file IN LISTS files)
        file(STRINGS "${file}" lines REGEX "^[ \t]*#[ \t]*include")
        foreach (line IN LISTS lines)
            if (line MATCHES "${forbidden_regex}")
                message(FATAL_ERROR "hard rule 1: ${file} includes ${what}:\n    ${line}")
            endif ()
        endforeach ()
    endforeach ()
endfunction()

# Headers that only exist in src/client/, so including them from server/ is the forbidden dependency.
set(CLIENT_ONLY_HEADERS "Window|Input|Camera|Engine|ClientWorld|Mesher|ChunkRenderer|ChunkVertex|Renderer|Shader|Texture|Material|DebugUI|GpuProfiler|FrameStats|ClientStats|Frustum|TerrainTextures")
set(SERVER_ONLY_HEADERS "World|WorldGen|Server|ecs/")

check_tree(server "(glad|GLFW|GL/|imgui|stb_)" "OpenGL, GLFW, ImGui or stb")
check_tree(server "\"(${CLIENT_ONLY_HEADERS})\\.h\"" "a client header")
check_tree(client "\"(${SERVER_ONLY_HEADERS})" "a server header")
check_tree(shared "(glad|GLFW|GL/|imgui|stb_|\"(${CLIENT_ONLY_HEADERS}|${SERVER_ONLY_HEADERS})\\.h\")" "a client, server or GL header")
check_tree(core "(glm|glad|GLFW|GL/|imgui|stb_|\"(${CLIENT_ONLY_HEADERS}|${SERVER_ONLY_HEADERS}|Config|Coords|Chunk|Protocol|Connection|Serialise|BlockRegistry|Faces|DebugCounters|ChunkSerial)\\.h\")" "anything outside core")
message(STATUS "CheckSplit: server/client include rules hold")

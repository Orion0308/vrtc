cmake_minimum_required(VERSION 3.28)
project(VRTC CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(glm CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED) # vcpkg install imgui[glfw-binding,opengl3-binding]
find_package(Stb REQUIRED)          # vcpkg install stb

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif ()

# Four libraries whose include directories enforce the server/client split (spec section 4). Headers are
# included by bare file name, so a library only sees the directories it is allowed to depend on.

# core: depends on nothing.
add_library(vrtc_core STATIC
        src/core/Jobs.cpp
        src/core/Log.cpp)
target_include_directories(vrtc_core PUBLIC src/core)

# shared: core + glm.
add_library(vrtc_shared STATIC
        src/shared/BlockRegistry.cpp
        src/shared/Chunk.cpp
        src/shared/ChunkSerial.cpp
        src/shared/Connection.cpp
        src/shared/Protocol.cpp)
target_include_directories(vrtc_shared PUBLIC src/shared)
target_link_libraries(vrtc_shared PUBLIC vrtc_core glm::glm)

# server: shared + core. No OpenGL, GLFW or ImGui include paths.
add_library(vrtc_server STATIC
        src/server/Server.cpp
        src/server/Systems.cpp
        src/server/WorldEdit.cpp
        src/server/World.cpp
        src/server/WorldGen.cpp)
target_include_directories(vrtc_server PUBLIC src/server)
target_include_directories(vrtc_server SYSTEM PRIVATE external) # vendored: no warnings from it
target_link_libraries(vrtc_server PUBLIC vrtc_shared)

# client: shared + core + the windowing and rendering libraries.
add_library(vrtc_client STATIC
        src/client/Camera.cpp
        src/client/ChunkRenderer.cpp
        src/client/ClientWorld.cpp
        src/client/DebugUI.cpp
        src/client/Engine.cpp
        src/client/GpuProfiler.cpp
        src/client/Input.cpp
        src/client/Mesher.cpp
        src/client/Renderer.cpp
        src/client/Shader.cpp
        src/client/TerrainTextures.cpp
        src/client/Texture.cpp
        src/client/Window.cpp)
target_include_directories(vrtc_client PUBLIC src/client PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(vrtc_client PUBLIC vrtc_shared glfw glad::glad imgui::imgui)
target_compile_definitions(vrtc_client PUBLIC GLFW_INCLUDE_NONE) # glad provides the GL headers
# Absolute path so assets load regardless of the working directory.
target_compile_definitions(vrtc_client PRIVATE VRTC_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")

add_executable(vrtc src/main.cpp)
target_link_libraries(vrtc PRIVATE vrtc_server vrtc_client)

# Hard rule 1, checked on every build: see cmake/CheckSplit.cmake.
add_custom_target(vrtc_check_split
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/CheckSplit.cmake
        COMMENT "Checking the server/client include split")
add_dependencies(vrtc vrtc_check_split)

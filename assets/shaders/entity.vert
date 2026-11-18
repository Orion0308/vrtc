#version 460 core

// Unpacked entity vertex: see EntityVertex in client/Renderer.h.
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in float aLayer;

out vec3 vNormal;
out vec2 vUV;
flat out float vLayer;

uniform mat4 uMVP;

void main()
{
    vNormal = aNormal;
    vUV = aUV;
    vLayer = aLayer;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}

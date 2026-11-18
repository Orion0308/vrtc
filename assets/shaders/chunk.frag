#version 460 core

in vec2 vUV;
in vec3 vLight;     // sun + ambient, already scaled by ambient occlusion
in float vDistance; // metres from the camera
flat in uint vLayer;

out vec4 FragColor;

uniform sampler2DArray uTextures;
uniform vec3 uFogColour;
uniform vec2 uFogRange; // start, end in metres

void main()
{
    vec3 colour = texture(uTextures, vec3(vUV, float(vLayer))).rgb * vLight;
    float fog = smoothstep(uFogRange.x, uFogRange.y, vDistance);
    FragColor = vec4(mix(colour, uFogColour, fog), 1.0);
}

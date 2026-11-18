#version 460 core

in vec3 vNormal;
in vec2 vUV;
flat in float vLayer;

out vec4 FragColor;

uniform sampler2DArray uTextures;
uniform int uUseTexture; // 0: flat uColour
uniform vec4 uColour;

const vec3 LIGHT_DIR = normalize(vec3(0.4, 1.0, 0.6));

void main()
{
    // Lines and outlines carry no normal and stay unlit; solid shapes get simple directional shading.
    float lit = dot(vNormal, vNormal) > 0.0 ? 0.55 + 0.45 * max(dot(normalize(vNormal), LIGHT_DIR), 0.0) : 1.0;
    vec4 base = uUseTexture != 0 ? texture(uTextures, vec3(vUV, vLayer)) : vec4(1.0);
    FragColor = vec4(base.rgb * uColour.rgb * lit, base.a * uColour.a);
}

#version 460 core

// One instance per greedy quad; the four corners come from gl_VertexID (triangle strip).
// Must mirror PackChunkQuad() in client/ChunkVertex.h:
//   a: x:10 | y:10 | z:10           min corner on the face's plane, 1/16 block steps, chunk-local
//   b: w:10 | h:10 | face:3 | ao:8  extent along the face's u and v axes; 2 bits of AO per corner
//   c: layer:12 | sky:4 | blockLight:4
layout (location = 0) in uvec3 aQuad;

out vec2 vUV;
out vec3 vLight;     // sun + ambient, already scaled by ambient occlusion
out float vDistance; // metres from the camera
flat out uint vLayer;

uniform mat4 uViewProj;
uniform vec3 uChunkOrigin; // chunk origin relative to the camera's chunk origin
uniform vec3 uEye;         // camera position in the same space
uniform vec3 uSunDir;      // unit vector toward the sun
uniform vec3 uSunColour;   // direct light, zero at night
uniform vec3 uAmbient;     // sky ambient reaching fully open cells

const float STEPS_PER_BLOCK = 16.0;

// Face order: +Z, -Z, +X, -X, +Y, -Y.
const vec3 FACE_NORMAL[6] = vec3[6](vec3(0, 0, 1), vec3(0, 0, -1), vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0));
// A little fixed contrast between faces so shapes still read where no direct light reaches.
const float FACE_SHADE[6] = float[6](0.90, 0.90, 0.82, 0.82, 1.00, 0.70);

// Corners of each face as (u, v) in {0,1}, counter-clockwise from outside: Faces.h FACE_CORNERS projected onto
// the face's axes (X -> u = z, v = y;  Y -> u = x, v = z;  Z -> u = x, v = y).
const ivec2 CORNER[24] = ivec2[24](
    ivec2(0, 0), ivec2(1, 0), ivec2(1, 1), ivec2(0, 1),   // +Z
    ivec2(1, 0), ivec2(0, 0), ivec2(0, 1), ivec2(1, 1),   // -Z
    ivec2(1, 0), ivec2(0, 0), ivec2(0, 1), ivec2(1, 1),   // +X
    ivec2(0, 0), ivec2(1, 0), ivec2(1, 1), ivec2(0, 1),   // -X
    ivec2(0, 0), ivec2(0, 1), ivec2(1, 1), ivec2(1, 0),   // +Y
    ivec2(0, 0), ivec2(1, 0), ivec2(1, 1), ivec2(0, 1));  // -Y

// Strip order 0,1,3,2 draws triangles (0,1,3) and (1,2,3): the quad splits along the 1-3 diagonal.
const int STRIP[4] = int[4](0, 1, 3, 2);

// One tile per block, projected from the position onto the face's plane. Signs keep the image upright and
// unmirrored seen from outside on every face.
vec2 FaceUV(vec3 p, uint face)
{
    switch (face) {
        case 0u: return vec2(p.x, p.y);   // +Z
        case 1u: return vec2(-p.x, p.y);  // -Z
        case 2u: return vec2(-p.z, p.y);  // +X
        case 3u: return vec2(p.z, p.y);   // -X
        case 4u: return vec2(p.x, -p.z);  // +Y
        default: return vec2(p.x, p.z);   // -Y
    }
}

void main()
{
    vec3 p = vec3(aQuad.x & 0x3FFu, (aQuad.x >> 10) & 0x3FFu, (aQuad.x >> 20) & 0x3FFu);
    float w = float(aQuad.y & 0x3FFu), h = float((aQuad.y >> 10) & 0x3FFu);
    uint face = (aQuad.y >> 20) & 0x7u;
    uint aoBits = aQuad.y >> 23;
    uint ao0 = aoBits & 3u, ao1 = (aoBits >> 2) & 3u, ao2 = (aoBits >> 4) & 3u, ao3 = (aoBits >> 6) & 3u;

    // Rotate the corners by one when the 0-2 diagonal is the darker pair, so interpolation follows it.
    int rotation = (ao0 + ao2 > ao1 + ao3) ? 0 : 1;
    int k = (STRIP[gl_VertexID] + rotation) & 3;
    ivec2 c = CORNER[int(face) * 4 + k];
    if (face < 2u)      { p.x += float(c.x) * w; p.y += float(c.y) * h; }   // Z faces: u = x, v = y
    else if (face < 4u) { p.z += float(c.x) * w; p.y += float(c.y) * h; }   // X faces: u = z, v = y
    else                { p.x += float(c.x) * w; p.z += float(c.y) * h; }   // Y faces: u = x, v = z
    vec3 local = p / STEPS_PER_BLOCK;

    float ao = float((aoBits >> (2 * k)) & 3u) / 3.0;
    vLayer = aQuad.z & 0xFFFu;
    float sky = float((aQuad.z >> 12) & 0xFu) / 15.0;
    float block = float((aQuad.z >> 16) & 0xFu) / 15.0;

    // Sky light floods in from open sky and fades with every block of cover: it scales the ambient term, and
    // direct sun only reaches cells that are (nearly) open to the sky.
    float open = max(sky, block);
    float skyAmount = open * open;
    float direct = max(dot(FACE_NORMAL[face], uSunDir), 0.0) * smoothstep(0.75, 1.0, sky);
    vec3 light = uAmbient * FACE_SHADE[face] * mix(0.06, 1.0, skyAmount) + uSunColour * direct;
    vLight = light * mix(0.45, 1.0, ao);
    vDistance = length(uChunkOrigin + local - uEye);
    vUV = FaceUV(local, face);
    gl_Position = uViewProj * vec4(uChunkOrigin + local, 1.0);
}

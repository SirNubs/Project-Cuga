#version 450

layout(location = 0) in vec2 frag_texcoord;
layout(location = 1) in vec4 frag_color;
layout(location = 2) in vec3 frag_specular;
layout(location = 3) in float frag_fog;
layout(location = 4) flat in vec4 frag_uv_rect;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D guest_texture;

// False for pipelines of draws without an alpha test (texture_params.w 0):
// the discard below is then compiled out, so tiled GPUs keep their early
// depth test and hidden surface removal for them.
#ifdef GE_NO_SPECIALIZATION
// kGeFragmentShaderPlain, for GPU compatibility mode: the same shader with no
// specialization constant at all, for drivers that fail to build one. Every
// draw keeps the test, which passes where nothing is tested.
const bool kAlphaTest = true;
#else
layout(constant_id = 0) const bool kAlphaTest = true;
#endif

layout(push_constant) uniform Push {
    mat4 transform;
    vec4 viewport;
    vec4 texture_params; // x: texture enabled, y: texture function, z: alpha ref, w: alpha func
    vec4 uv_transform;
    vec4 view_z;
} push;

// Only the fog colour is read here; the block is described in ge.vert.
layout(set = 1, binding = 0) uniform Environment {
    vec4 ambient;
    vec4 fog;
    vec4 fog_color;
} lighting;

// Sharp bilinear, for the 2D interface (texture_params.x 2, issue #164): a
// texel magnified to several pixels keeps its colour across them and blends
// with its neighbour only over the last pixel at its edge, so pixel art stays
// crisp at any scale, whole or not, without the blur of plain bilinear. The
// coordinate is moved towards the nearest texel centre, and the bilinear
// sampler does the one pixel of blending that is left.
vec2 sharp_bilinear(vec2 uv) {
    vec2 size = vec2(textureSize(guest_texture, 0));
    vec2 texel = uv * size - 0.5;
    vec2 base = floor(texel);
    vec2 pixels_per_texel = max(1.0 / max(fwidth(texel), vec2(1e-6)), vec2(1.0));
    vec2 f = clamp((texel - base - 0.5) * pixels_per_texel + 0.5, 0.0, 1.0);
    return (base + f + 0.5) / size;
}

void main() {
    vec4 color = frag_color;
    if (push.texture_params.x > 0.5) {
        vec2 uv = push.texture_params.x > 1.5 ? sharp_bilinear(frag_texcoord) : frag_texcoord;
        vec4 texel = texture(guest_texture, clamp(uv, frag_uv_rect.xy, frag_uv_rect.zw));
        int function = int(push.texture_params.y + 0.5);
        if (function == 0) {          // modulate
            color *= texel;
        } else if (function == 1) {   // decal
            color = vec4(mix(color.rgb, texel.rgb, texel.a), color.a);
        } else if (function == 2) {   // blend
            color = vec4(mix(color.rgb, texel.rgb, texel.rgb), color.a * texel.a);
        } else {                      // replace and everything else
            color = texel;
        }
    }

    // A separate specular term is added after texturing, then fog blends
    // towards its colour; neither touches alpha.
    color.rgb = min(color.rgb + frag_specular, vec3(1.0));
    if ((int(push.viewport.w + 0.5) & 1) != 0) color.rgb = mix(lighting.fog_color.rgb, color.rgb, clamp(frag_fog, 0.0, 1.0));

    // PSP alpha test, evaluated per fragment.
    if (!kAlphaTest) {
        out_color = color;
        return;
    }
    int alpha_function = int(push.texture_params.w + 0.5);
    float reference = push.texture_params.z / 255.0;
    float alpha = color.a;
    bool passed = true;
    if (alpha_function == 1) passed = false;                     // never
    else if (alpha_function == 2) passed = abs(alpha - reference) < 0.002;
    else if (alpha_function == 3) passed = abs(alpha - reference) >= 0.002;
    else if (alpha_function == 4) passed = alpha < reference;
    else if (alpha_function == 5) passed = alpha <= reference;
    else if (alpha_function == 6) passed = alpha > reference;
    else if (alpha_function == 7) passed = alpha >= reference;
    if (!passed) discard;

    out_color = color;
}

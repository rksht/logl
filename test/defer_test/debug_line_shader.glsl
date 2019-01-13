#version 430 core

#if defined(VERTEX_SHADER)

// ----- Vertex shader

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 st;

layout(binding = PER_CAMERA_UBLOCK, std140) uniform EyeBlock {
    mat4 view_from_world;
    mat4 clip_from_world;
};

out VsOut { vec2 st; }
vs_out;

#if !defined(NDC_Z)
#define NDC_Z -1.0
#endif

void main() {
    gl_Position = clip_from_world * view_from_world * vec4(position.xy, -1.0, 1.0);
    // gl_Position = clip_from_world * view_from_world * vec4(st.xy, -1.0, 1.0);

    vs_out.st = st;
}

#elif defined(FRAGMENT_SHADER)

// ----- Fragment shader

in VsOut { vec2 st; }
fs_in;

layout(binding = FONT_ATLAS_TEXTURE_UNIT) uniform sampler2D atlas_sampler;

#ifndef COLOR
#define COLOR vec3(1.0, 1.0, 1.0)
#endif

#ifndef BG_COLOR
#define BG_COLOR vec3(0.0f, 0.0f, 0.0f)
#endif

out vec4 fc;

void main() {
    if (fs_in.st.x >= 0.0) {
        float alpha = texture(atlas_sampler, fs_in.st).r;
        fc = vec4(COLOR, alpha);
    } else {
        fc = vec4(BG_COLOR, 0.5);
    }
}

#endif

#version 430 core

#if defined(PICK_VERTEX_SHADER)

uniform mat4 proj_mat;

in vec2 position;
in vec2 st;
in vec4 color;

out vec2 frag_st;
out vec4 frag_color;

void main() {
    frag_st = st;
    frag_color = color;
    gl_Position = proj_mat * vec4(position.xy, 0, 1);
}

#elif defined(PICK_FRAGMENT_SHADER)

#version 430 core

uniform sampler2D sampler;

in vec2 frag_st;
in vec4 frag_color;

out vec4 out_color;

void main() { out_color = frag_color * texture(sampler, frag_st); }

#else

#error "Need to define one of those two macros"

#endif


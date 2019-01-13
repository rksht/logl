#version 430 core

in Pnuv_VSOut {
    vec4 pos_w;
    vec4 normal_w;
    vec2 uv;
} in_fs;

out vec4 o_fragcolor;

void main() { o_fragcolor = vec4(1.0); }

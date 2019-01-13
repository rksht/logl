#version 410
#extension GL_ARB_shading_language_420pack: enable

in vec2 st;

layout(binding = 0) uniform sampler2D tex;

out vec4 frag_color;

subroutine vec4 calc_color_sub();

subroutine(calc_color_sub) vec4 invert() {
    return vec4(1.0 - texture(tex, st).rgb, 1.0);
}

subroutine(calc_color_sub) vec4 gray() {
    vec3 color = texture(tex, st).rgb;
    float average = 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
    return vec4(average, average, average, 1.0);
}

subroutine uniform calc_color_sub calc_color;

void main() {
    /*
    vec3 color;
    if (st.s >= 0.5) {
        color = 1.0 - texture(tex, st).rgb;
    } else {
        color = texture(tex, st).rgb;
    }
    */
    frag_color = calc_color();
}
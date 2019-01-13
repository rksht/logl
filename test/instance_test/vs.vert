#version 410 core

layout(location = 0) in vec3 u_i; // initial velocity
layout(location = 1) in float start_time;

uniform mat4 mat_view, mat_proj;
uniform vec3 emitter_pos_wor;      // emitter position in world coordinates
uniform float current_time; // system time in seconds

out float opacity;

void main() {
    float t = current_time - start_time;
    t = mod(t, 3.0);
    opacity = 0.0;

    vec3 p = emitter_pos_wor;

    vec3 a = vec3(0.0, -1.0, 0.0);
    p += u_i * t + 0.5 * a * t * t; // pos = ut + (1/2) a*t^2
    opacity = 1.0 - (t / 3.0);

    vec3 eye_pos = -mat_view[3].xyz;
    float particle_dist = length(p - eye_pos);

    // Point size depends on distance (commented out)
    // 1 units --- 4 pixels
    gl_PointSize = clamp(20.0 / particle_dist, 0.0, 50.0);

    gl_Position = mat_proj * mat_view * vec4(p, 1.0);
}

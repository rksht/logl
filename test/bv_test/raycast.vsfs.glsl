#version 430 core

layout(std140, binding = 0) uniform ViewProjEtc {
    mat4 view_from_world;
    mat4 clip_from_view;
    mat4 view_from_clip; // Inverse of perspective projection matrix
    mat4 model_to_world;
    vec4 eye_position;
};

#if defined(DO_VS)

    layout(location = 0) in vec3 pos;

    out VsOut {
        vec4 object_space_corner_pos; // This are points in [-1, -1, -1] x [1, 1, 1]
    } vs_out;

    void main()
    {
        gl_Position = clip_from_view * view_from_world * vec4(pos, 1.0);
        vs_out.object_space_corner_pos = vec4(pos, 1.0);
    }

#endif


#if defined(DO_FS)

    layout(binding = 0) uniform sampler3D volume_sampler;

    in VsOut {
        vec4 object_space_corner_pos;
    } fs_in;

    out vec4 fs_out;

    void main()
    {
        fs_out = vec4(0.4, 0.0, 1.0, 1.0);
    }

#endif

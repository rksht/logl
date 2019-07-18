#include "common_defs.inc.glsl"

#version 430 core

layout(std140, binding = 0) uniform ViewProjEtc {
    mat4 view_from_world;
    mat4 clip_from_view;
    mat4 view_from_clip; // Inverse of perspective projection matrix
    mat4 model_to_world;
    vec4 eye_position;

    vec2 screen_wh;
};

#if defined(DO_VS)

    layout(location = 0) in vec3 pos;

    out VsOut {
        vec4 pos_wrt_local_space; // These are points in [-1, -1, -1] x [1, 1, 1]
        vec3 uvw; // 3D texture coordinates. Can derive from object_space pos itself, but still.
    } vs_out;

    void main()
    {
        gl_Position = clip_from_view * view_from_world * vec4(pos, 1.0);
        vs_out.pos_wrt_local_space = vec4(pos, 1.0);
        vs_out.uvw = (pos + 1.0) * 0.5;
    }

#endif


#if defined(DO_FS)

    // TODO:CONCEPT: Convince or prove to self that this distance suffices for any direction.
    const float max_distance_to_march = sqrt(3.0);
    const int volume_texture_resolution = VOLUME_TEX_RESOLUTION;

    // Step size is such that we don't cross more than one texel worth of local space distance
    const float step_size = max_distance_to_march / float(volume_texture_resolution);

    const vec3 light_position = vec3(0.25, 1.0, 3.0);
    uniform vec3 light_intensity = vec3(15.0);
    uniform float absorption = 1.0;
    const int num_light_samples = 32;
    const float light_dir_scale = max_distance_to_march / float(num_light_samples);
    const float density_factor = 5.0;

    layout(binding = 0) uniform sampler3D volume_sampler;

    in VsOut {
        vec4 pos_wrt_local_space;
        vec3 uvw;
    } fs_in;

    out vec4 fs_out;


#if 0
    void main()
    {
        float value = texture(volume_sampler, fs_in.uvw).r;
        // fs_out = vec4(0.4, 0.0, 1.0, 1.0);
        fs_out = vec4(value, value * value, 1.0 - value, 1.0);
    }
#endif

    struct Ray {
        vec3 o;
        vec3 d;
    };

    bool test_ray_aabb(Ray r, vec3 box_min, vec3 box_max, out float t_0, out float t_1) {
        vec3 ood = 1.0 / r.d;
        vec3 t_top = ood * (box_max - r.o);
        vec3 t_bot = ood * (box_min - r.o);

        vec3 t_min = min(t_top, t_bot);
        vec3 t_max = max(t_top, t_bot);

        vec2 t = max(t_min.xx, t_min.yz);
        t_0 = max(t.x, t.y);
        t_1 = min(t.x, t.y);
        return t_0 < t_1;
    }

    void main()
    {
        #if 0
        // The following assumes viewport origin is at window (0, 0) and viewport width and height are equal
        // to window width and height.

        // Fragment position's xy in NDC
        vec3 pos_wrt_ndc = vec3((gl_FragCoord.xy / screen_wh) * 2 - 1.0,
                                gl_FragCoord.z * 2 - 1);

        float w_clip_space = 1 / gl_FragCoord.w;
        vec4 pos_wrt_clip_space = vec4(pos_wrt_ndc * w_clip_space, w_clip_space);
        vec4 pos_wrt_view_space = view_from_clip * pos_wrt_clip_space;

        #endif

        // Eye position wrt *local space*.
        // TODO: Better to do this in gl side and send via uniform
        vec4 eye_position = (eye_position * view_from_world) * model_to_world;

        // Ray from eye to fragment in local space
        Ray r =  Ray(eye_position.xyz, normalize(eye_position.xyz - fs_in.pos_wrt_local_space.xyz));

        // Ray and AABB intersection. Here, we have fixed the domain.

        float t_near, t_far;
        test_ray_aabb(r, vec3(-1, -1, -1), vec3(1, 1, 1), t_near, t_far);

        t_near = max(t_near, 0.0);
        // ^ Mathematically, we should always intersect and t_near should not be negative, since we are
        // drawing front faces and derive the ray from a point on the cube itself. But this I think is a clamp
        // to prevent floating point errors.

        vec3 ray_start = r.o + r.d * t_near;
        vec3 ray_stop = r.o + r.d * t_far;

        // ^Those are local space, transform to normalized 3d texture space i.e. (x, y, z) in ([0, 1], [0, 1],
        // [0, 1]). This assumes domain is a unit cube.
        ray_start = (ray_start + 1.0) * 0.5;
        ray_stop = (ray_stop + 1.0) * 0.5;
        vec3 pos = ray_start;
        vec3 step = normalize(ray_stop - ray_stop) * step_size;

        float travel = distance(ray_start, ray_stop);

        float T = 1.0; // TODO: Whatever this denotes. I'm guessing reflectance or something like that.

        vec3 light_out = vec3(0);

        for (int i = 0; i < volume_texture_resolution && travel > 0.0; ++i, pos += step, travel -= step_size) {
            float density = texture(volume_sampler, pos).r;

            if (density <= 0.0) {
                continue;
            }

            T *= 1.0 - density * step_size;
            if (T <= 0.01) {
                break;
            }

            vec3 light_dir = normalize(light_position - pos) * light_dir_scale;
            float Tl = 1.0;
            vec3 light_pos = pos + light_dir;

            for (int s = 0; s < num_light_samples; ++s) {
                float ld = density;
                Tl *= 1.0 - absorption * step_size * ld;
                if (Tl <= 0.01) {
                    light_pos += light_dir;
                }
            }
        }

        fs_out = vec4(light_out, 1 - T);
    }

#endif

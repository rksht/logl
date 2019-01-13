#version 430 core

#include <common_defs.inc.glsl>

struct Particle {
    vec2 position;
    vec2 velocity;
};

struct ParticleDensity {
    float density;
};

struct VsOutputStruct {
    vec2 position;
    float speed_sq;
    float density;
    float particle_index;
};

#include "simulation_constants.inc.glsl"

#if defined(DO_VS)

#define VS_MAIN main

BUFFER_BINDPOINT(PARTICLES_RO_BINDPOINT, std430) buffer ParticlesRO { Particle particles_ro[]; };

BUFFER_BINDPOINT(DENSITIES_BINDPOINT, std430) buffer ParticleDensities { ParticleDensity densities[]; };

out Particle_Render_VsOut { VsOutputStruct s; }
o_vs;

DEFINE_CAMERA_UBLOCK(CAMERA_UBLOCK_BINDPOINT, Camera_Ublock);

#define RED vec4(1.0, 0.0, 0.0, 1.0)
#define BLUE vec4(0.0, 0.0, 1.0, 1.0)

void VS_MAIN() {
    const uint vid = VID;

    uint particle_index = vid / 6;
    uint vertex_in_quad = vid % 6;

    vec2 position = particles_ro[particle_index].position;
    vec2 velocity = particles_ro[particle_index].velocity;

    vec2 v_pos;

    // Get the vertex's position in the unit quad
    v_pos.x = 2u <= vertex_in_quad && vertex_in_quad <= 4 ? 1.0 : -1.0;
    v_pos.y = 1u <= vertex_in_quad && vertex_in_quad <= 3 ? -1.0 : 1.0;

    // Stretch by particle "radius"

#if 0
    // Debugging this particle. For some reason it's bugging out.
    if (particle_index != 1329) {
        v_pos *= PARTICLE_SIZE;
    } else {
        v_pos *= 0.1;
    }

#endif

    v_pos *= u_particle_render_size;

    // Map-space position
    v_pos = position + v_pos;

    o_vs.s.position = v_pos;

    float color_factor = float(particle_index) / u_num_particles;

    // o_vs.color_rgba = vec4(color_factor, 0.0, 1.0 - color_factor, 1.0);

    o_vs.s.position = position;
    o_vs.s.speed_sq = dot(velocity, velocity);
    o_vs.s.density = densities[particle_index].density;
    o_vs.s.particle_index = float(particle_index);

    // Then position wrt view and projection.

    gl_Position = u_clipFromView * u_viewFromWorld * vec4(v_pos.xy, -1.0, 1.0);
}

#elif defined(DO_FS)

#define FS_MAIN main

in Particle_Render_VsOut { VsOutputStruct s; }
i_fs;

layout(location = 0) out float attribs_out;

void FS_MAIN() {
    // frag_color = vec4(0.2, 0.2, 0.4, 0.2);
    // frag_color = i_fs.color_rgba;

#if 0
    attribs_out.xy = i_fs.s.position;
    attribs_out.z = i_fs.s.density;
    attribs_out.w = float(i_fs.s.particle_index);
#endif

    attribs_out = i_fs.s.speed_sq;
    // attribs_out = i_fs.s.density;
    // attribs_out = 100.0;
}

#else

#error "DO_VS or DO_FS"

#endif

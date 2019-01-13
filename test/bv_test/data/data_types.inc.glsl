#include "common_defs.inc.glsl"

// std430 layout has vec2's aligned to 2x floats so it's same as the C++ side.

struct Particle {
    vec2 position;
    vec2 velocity;
};

struct ParticleForces {
    vec2 acceleration;
}

struct ParticleDensity {
    float density;
};

layout(binding = SIM_CONSTANTS_UBO_BINDPOINT, std140) uniform SimulationConstantsUB {
    uint u_num_particles;
    float u_timestep;
    float u_smooth_len;
    float u_pressure_stiffness;
    float u_rest_density;
    float u_density_coeff;
    float u_grad_pressure_coef;
    float u_lap_viscosity_coef;
    float u_wall_stiffness;

    vec4 u_gravity;
    vec4 u_grid_dim;
    vec4 u_planes[4]; // w = 0 for each since 2D planes i.e. lines
};

#define SIMULATION_BLOCK_SIZE 256

// Storage buffers for compute
layout(location = BINDPOINT_RW_PARTICLES_SSBO, std430) buffer ParticlesRW {
    Particle particles_rw[];
};

layout(location = BINDPOINT_RO_PARTICLES_SSBO, std430) readonly buffer ParticlesRO {
    Particle particles_ro[];
};

layout(location = BINDPOINT_RW_PARTICLES_DENSITY_SSBO, std430) buffer ParticlesDensityRW {
    ParticleDensity particles_density_rw[];
};

layout(location = BINDPOINT_RO_PARTICLES_DENSITY_SSBO, std430) readonly buffer ParticlesDensityRO {
    ParticleDensity particles_density_ro[];
};

layout(location = BINDPOINT_RW_PARTICLES_FORCES_SSBO, std430) buffer ParticlesForcesRW {
    ParticleForces particles_forces_rw[];
};

layout(location = BINDPOINT_RO_PARTICLES_FORCES_SSBO, std430) readonly buffer ParticlesForcesRO {
    ParticleForces particles_forces_ro[];
};

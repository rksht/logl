BUFFER_BINDPOINT(SIM_CONSTANTS_UBO_BINDPOINT, std140) uniform SimulationConstantsUB {
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
    vec2 u_screen_wh;

    float u_particle_render_size;
};

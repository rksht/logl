// Learning/playing with order independent transparency. This one is a demo showing why we do need to sort
// objects by depth if we are not using OIT.

#include "essentials.h"
#include <cxxopts.hpp>

using namespace fo;
using namespace math;

// -- Configurations

const auto usual_vs = R"(
#version 430 core

layout(location = 0) in vec3 position;

layout(binding = 0, std140) uniform WVP_Block {
    mat4 model_to_world;
    mat4 view;
    mat4 proj;
} wvp;

out UsualVsOut {
    vec4 color;
} vs_out;

uniform vec4 uniform_color;

void main() {
    gl_Position = wvp.proj * wvp.view * wvp.model_to_world * vec4(position, 1.0);
    vs_out.color = uniform_color;
}

)";

const auto usual_fs = R"(
#version 430 core

in UsualVsOut {
    vec4 color;
} fs_in;

out vec4 frag_color;

void main() {
    frag_color = fs_in.color;
}

)";

struct Cubes {
    std::vector<Matrix4x4> arr_model_to_world;
    std::vector<RGBA8> arr_colors;

    // Indices to the cube attributes in the arrays above are kept in order of increasing z (camera space)
    std::vector<size_t> depth_sorted;
};

void sort_by_depth(Cubes &cubes, const Matrix4x4 &view_matrix) {
    TempAllocator128 ta(memory_globals::default_allocator());

    Array<float> arr_camspace_z(ta, cubes.arr_model_to_world.size());

    for (uint32_t i = 0; i < size(arr_camspace_z); ++i) {
        auto view_space_pos = view_matrix * (cubes.arr_model_to_world[i] * Vector4{0.0f, 0.0f, 0.0f, 1.0f});
        arr_camspace_z[i] = view_space_pos.z;
    }

    std::sort(
        cubes.depth_sorted.begin(), cubes.depth_sorted.end(),
        [&arr_camspace_z](const auto &i, const auto &j) { return arr_camspace_z[i] < arr_camspace_z[j]; });
}

Cubes parse_cubes(const fs::path &cubes_file) {
    auto cd = simple_parse_file(cubes_file.u8string().c_str(), true);

    const auto root = nfcd_root(cd);

    const int cubes_per_side =
        nfcd_to_number(cd, SIMPLE_MUST(nfcd_object_lookup(cd, root, "cubes_per_side")));
    const float cube_scale =
        (float)nfcd_to_number(cd, SIMPLE_MUST(nfcd_object_lookup(cd, root, "cube_scale")));

    const auto cubes_loc = SIMPLE_MUST(nfcd_object_lookup(cd, root, "cubes"));

    const int num_cubes = nfcd_array_size(cd, cubes_loc);

    std::vector<Matrix4x4> arr_model_to_world;
    arr_model_to_world.reserve((size_t)num_cubes);

    std::vector<RGBA8> arr_colors;

    for (int i = 0; i < num_cubes; ++i) {
        auto cube_attribs = nfcd_array_item(cd, cubes_loc, i);

        RGBA8 rgba =
            html_color(nfcd_to_string(cd, SIMPLE_MUST(nfcd_object_lookup(cd, cube_attribs, "color"))));
        uint8_t alpha =
            (uint8_t)nfcd_to_number(cd, SIMPLE_MUST(nfcd_object_lookup(cd, cube_attribs, "alpha")));
        rgba.a = alpha;

        Vector3 position =
            SimpleParse<Vector3>::parse(cd, SIMPLE_MUST(nfcd_object_lookup(cd, cube_attribs, "position")));

        // Convert to world coordinates

        position.x = (position.x - cubes_per_side / 2.0f) * cube_scale;
        position.y = (position.y - cubes_per_side / 2.0f) * cube_scale;
        position.z = (position.z - cubes_per_side / 2.0f) * cube_scale;

        const auto scale = cube_scale * 0.5;
        auto model_to_world = translation_matrix(position) * xyz_scale_matrix(scale, scale, scale);

        arr_model_to_world.push_back(model_to_world);
        arr_colors.push_back(rgba);
    }

    std::vector<size_t> depth_sorted((size_t)num_cubes);
    for (size_t i = 0; i < (size_t)num_cubes; ++i) {
        depth_sorted[i] = i;
    }

    nfcd_free(cd);

    return Cubes{std::move(arr_model_to_world), std::move(arr_colors), std::move(depth_sorted)};
}

int main(int ac, char **av) {
    memory_globals::init();

    cxxopts::Options options_desc("Playing with order-independent transparency");

    options_desc.add_options()("cubes", "cubes info JSON file", cxxopts::value<std::string>());
    options_desc.add_options()("nosort", "do not sort objects by depth", cxxopts::value<bool>());

    auto options = options_desc.parse(ac, av);

    if (options.count("cubes") != 1) {
        std::cerr << "Expected a cubes file" << std::endl;
        exit(EXIT_FAILURE);
    }

    bool opt_sort_objects = options.count("nosort") == 0;

    GLFWwindow *window;
    eng::start_gl(&window, 800, 600, 4, 4);
    eng::enable_debug_output(nullptr, nullptr);

    // Set render states
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    // glDisable(GL_BLEND);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto cubes = parse_cubes(options["cubes"].as<std::string>());

    struct WVP_UniformBlock {
        Matrix4x4 model_to_world;
        Matrix4x4 view_matrix;
        Matrix4x4 proj_matrix;
    };

    WVP_UniformBlock wvp{};

    wvp.proj_matrix = persp_proj(0.1f, 1000.0f, 70.0f * one_deg_in_rad, 800.0f / 600.0f);
    auto eye = eye::toward_negz(2.0f);
    eye::update_view_transform(eye, wvp.view_matrix);

    GLuint wvp_block = create_uniform_buffer(0, sizeof(WVP_UniformBlock), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_UNIFORM_BUFFER, wvp_block);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(WVP_UniformBlock), &wvp);

    // Create 2 meshes. One for the cube, one for the sphere, and one for the cube after it.

    auto cube_mesh = par_shapes_create_cube();
    shift_par_cube(cube_mesh);

    // Create vertex buffer
    GLuint vertices_vbo;
    glGenBuffers(1, &vertices_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vertices_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vector3) * cube_mesh->npoints, nullptr, GL_STATIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Vector3) * cube_mesh->npoints, cube_mesh->points);

    // Element buffer
    GLuint cube_ebo;
    glGenBuffers(1, &cube_ebo);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, cube_mesh->ntriangles * 3 * sizeof(uint16_t), cube_mesh->triangles,
                 GL_STATIC_DRAW);

    // Vertex format vao
    GLuint vertices_vao;
    glGenVertexArrays(1, &vertices_vao);

    glBindVertexArray(vertices_vao);
    glVertexAttribFormat(0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexAttribBinding(0, 0);
    glEnableVertexAttribArray(0);

    // Load the usual program
    GLuint program = eng::create_program(usual_vs, usual_fs);

    stop_watch::State<std::chrono::high_resolution_clock> stopwatch{};
    stop_watch::start(stopwatch);

    if (opt_sort_objects) {
        sort_by_depth(cubes, wvp.view_matrix);
        LOG_F(INFO, "We sort transparent objects by depth");
    } else {
        LOG_F(INFO, "We are *not sorting* transparent objects by depth. Unintended results will show up.");
    }

    auto uniform_color_loc = glGetUniformLocation(program, "uniform_color");

    while (true) {
        glfwPollEvents();

        float frame_time = stop_watch::restart(stopwatch).count() * 1e-9;

        if (eng::handle_eye_input(window, eye, frame_time, wvp.view_matrix)) {
            glBindBuffer(GL_UNIFORM_BUFFER, wvp_block);
            glBufferSubData(GL_UNIFORM_BUFFER, offsetof(WVP_UniformBlock, view_matrix), sizeof(Matrix4x4),
                            &wvp.view_matrix);

            if (opt_sort_objects) {
                sort_by_depth(cubes, wvp.view_matrix);
            }
        }

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            break;
        }

        // Render

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Draw the cubes
        glUseProgram(program);

        // Bind WVP uniform block
        glBindBuffer(GL_UNIFORM_BUFFER, wvp_block);

        glBindVertexArray(vertices_vao);

        glBindVertexBuffer(0, vertices_vbo, 0, sizeof(Vector3));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_ebo);

        for (uint32_t cube_index : cubes.depth_sorted) {
            // draw_cube(cube_index);
            glBufferSubData(GL_UNIFORM_BUFFER, offsetof(WVP_UniformBlock, model_to_world), sizeof(Matrix4x4),
                            &cubes.arr_model_to_world[cube_index]);

            const RGBA8 &color = cubes.arr_colors[cube_index];

            glUniform4f(uniform_color_loc, color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                        color.a / 255.0f);

            glDrawElements(GL_TRIANGLES, cube_mesh->ntriangles * 3, GL_UNSIGNED_SHORT, 0);
        }

        glfwSwapBuffers(window);
    }

    glfwTerminate();

    par_shapes_free_mesh(cube_mesh);

    memory_globals::shutdown();
}

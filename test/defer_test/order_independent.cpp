#include "essentials.h"
#include <cxxopts.hpp>

using namespace fo;
using namespace math;

constexpr int screen_width = 800;
constexpr int screen_height = 600;

struct Cubes {
    std::vector<Matrix4x4> arr_model_to_world;
    std::vector<RGBA8> arr_colors;

    // Indices to the cube attributes in the arrays above are kept in order of increasing z (camera space)
    std::vector<size_t> depth_sorted;

    size_t count() const { return arr_model_to_world.size(); }
};

// Sorts the cube in increasing order of depth.
void sort_cubes_badly(Cubes &cubes, const Matrix4x4 &view_matrix) {
    TempAllocator128 ta(memory_globals::default_allocator());

    Array<float> arr_camspace_z(ta, cubes.arr_model_to_world.size());

    for (uint32_t i = 0; i < size(arr_camspace_z); ++i) {
        auto view_space_pos = view_matrix * (cubes.arr_model_to_world[i] * Vector4{0.0f, 0.0f, 0.0f, 1.0f});
        arr_camspace_z[i] = view_space_pos.z;
    }

    std::sort(
        cubes.depth_sorted.begin(), cubes.depth_sorted.end(),
        [&arr_camspace_z](const auto &i, const auto &j) { return arr_camspace_z[i] > arr_camspace_z[j]; });
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

    fps_title_init("oit demo");

    cxxopts::Options options_desc("Playing with order-independent transparency");

    options_desc.add_options()("cubes", "cubes info JSON file", cxxopts::value<std::string>());
    // options_desc.add_options()("nosort", "do not sort objects by depth", cxxopts::value<bool>());

    auto options = options_desc.parse(ac, av);

    if (options.count("cubes") != 1) {
        std::cerr << "Expected a cubes file" << std::endl;
        exit(EXIT_FAILURE);
    }

    // bool opt_sort_objects = options.count("nosort") == 0;

    GLFWwindow *window;
    eng::start_gl(&window, screen_width, screen_height, "order_independent", 4, 4);
    eng::enable_debug_output(nullptr, nullptr);

    // Set render states
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glDepthFunc(GL_LEQUAL);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

    // No thanks. We are doing our own blending in the shader.
    glDisable(GL_BLEND);

    // We will have to call the list building fragment shader for *all* fragments even if they get obscured.
    // The way to do this is to either disabled depth test or turn off early tests.
    glDisable(GL_DEPTH_TEST);

    auto cubes = parse_cubes(options["cubes"].as<std::string>());

    struct WVP_UniformBlock {
        Matrix4x4 model_to_world;
        Matrix4x4 view_matrix;
        Matrix4x4 proj_matrix;
    };

    WVP_UniformBlock wvp{};

    wvp.proj_matrix = persp_proj(0.1f, 1000.0f, 70.0f * one_deg_in_rad, screen_width / screen_height);
    auto eye = eye::toward_negz(2.0f);
    eye::update_view_transform(eye, wvp.view_matrix);

    sort_cubes_badly(cubes, wvp.view_matrix);

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

    // We will draw a full screen quad to drive the blending fragment-shader
    GLuint fullscreen_quad_vbo, fullscreen_quad_vao;
    Quad quad({-1.0f, -1.0f}, {1.0f, 1.0f});
    quad.make_vao(&fullscreen_quad_vbo, &fullscreen_quad_vao);

    // Load the list builder program
    auto oit_vs =
        create_shader(::CreateShaderOptionBits::k_prepend_version, "#version 430 core",
                               GL_VERTEX_SHADER, fs::path(SOURCE_DIR) / "oit_build_list.vert");
    auto oit_fs =
        ::create_shader(::CreateShaderOptionBits::k_prepend_version, "#version 430 core",
                               GL_FRAGMENT_SHADER, fs::path(SOURCE_DIR) / "oit_build_list.frag");
    auto list_builder_program = eng::create_program(oit_vs, oit_fs);

    // Blending program
    auto blend_vs =
        ::create_shader(::CreateShaderOptionBits::k_prepend_version, "#version 430 core",
                               GL_VERTEX_SHADER, fs::path(SOURCE_DIR) / "oit_blend_frags.vert");

    auto blend_fs =
        ::create_shader(::CreateShaderOptionBits::k_prepend_version, "#version 430 core",
                               GL_FRAGMENT_SHADER, fs::path(SOURCE_DIR) / "oit_blend_frags.frag");

    auto blend_program = eng::create_program(blend_vs, blend_fs);

    stop_watch::State<std::chrono::high_resolution_clock> stopwatch{};
    stop_watch::start(stopwatch);

    auto uniform_color_loc = glGetUniformLocation(list_builder_program, "uniform_color");

    // Before rendering each frame, we must re-initialize the head pointers to 0 (i.e. null). For that, we
    // create a pixel buffer filled with end of list markers.

    constexpr uint32_t MAX_FRAMEBUFFER_WIDTH = screen_width;
    constexpr uint32_t MAX_FRAMEBUFFER_HEIGHT = screen_height;
    constexpr uint32_t TOTAL_PIXELS = MAX_FRAMEBUFFER_WIDTH * MAX_FRAMEBUFFER_HEIGHT;
    constexpr GLuint END_OF_LIST_MARKER = ~uint32_t(0);

    GLuint end_markers_pbo;
    glGenBuffers(1, &end_markers_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, end_markers_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, TOTAL_PIXELS * sizeof(GLuint), nullptr, GL_STATIC_DRAW);
    GLuint *data = (GLuint *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, TOTAL_PIXELS * sizeof(GLuint),
                                              GL_MAP_WRITE_BIT);
    std::fill(data, data + TOTAL_PIXELS, END_OF_LIST_MARKER);
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

    // The 2D image that will be used to store the head pointers for the per-pixel linked lists.
    constexpr uint32_t AVERAGE_OVERDRAW_PER_PIXEL = 4;
    constexpr uint32_t FRAGMENT_DATA_SLOTS = AVERAGE_OVERDRAW_PER_PIXEL * TOTAL_PIXELS;

    // One dimensional buffer from which data for each fragment is allocated from. Each fragment data stores
    // [Color: uint32_t, Depth: uint32_t, NextSlot: uint32_t] encoded as RGBA32. Not using the A channel.
    // RGB32 buffer texture is not supported in core gl. Could use a SS buffer too.

    #if 0
    GLuint fragment_data_buffer;
    glGenBuffers(1, &fragment_data_buffer);
    glBindBuffer(GL_TEXTURE_BUFFER, fragment_data_buffer);
    constexpr size_t SLOT_SIZE = sizeof(uint32_t) * 4;
    glBufferData(GL_TEXTURE_BUFFER, FRAGMENT_DATA_SLOTS * SLOT_SIZE, nullptr, GL_DYNAMIC_COPY);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);

    // This buffer will be accessed from the fragment shader via an imageBuffer data type.
    GLuint fragment_data_imagebuffer;
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &fragment_data_imagebuffer);
    glBindTexture(GL_TEXTURE_BUFFER, fragment_data_imagebuffer);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32UI, fragment_data_buffer);
    glBindImageTexture(0, fragment_data_imagebuffer, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32UI);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    #endif

    GLuint fragment_data_ssbo;
    glGenBuffers(1, &fragment_data_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, fragment_data_ssbo);
    constexpr size_t SLOT_SIZE = sizeof(uint32_t) * 4;
    glBufferData(GL_SHADER_STORAGE_BUFFER, FRAGMENT_DATA_SLOTS * SLOT_SIZE, nullptr, GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, fragment_data_ssbo);

    // R32UI texture. Each texel points to the first fragment data for the corresponding pixel. Accessed
    // atomically.
    glActiveTexture(GL_TEXTURE0);
    GLuint head_pointer_texture;
    glGenTextures(1, &head_pointer_texture);
    glBindTexture(GL_TEXTURE_2D, head_pointer_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, MAX_FRAMEBUFFER_WIDTH, MAX_FRAMEBUFFER_HEIGHT, 0, GL_RED_INTEGER,
                 GL_UNSIGNED_INT, nullptr);

    GLuint next_free_pointer_ACB;
    glGenBuffers(1, &next_free_pointer_ACB);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, next_free_pointer_ACB);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_COPY);

    auto reset_linked_list = [&]() {
        // Reset the head pointers
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, end_markers_pbo);
        glBindTexture(GL_TEXTURE_2D, head_pointer_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, MAX_FRAMEBUFFER_WIDTH, MAX_FRAMEBUFFER_HEIGHT, 0,
                     GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr); // Data source is the pbo

        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, screen_width, screen_height, GL_RED_INTEGER, GL_UNSIGNED_INT,
                        nullptr);

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

        // glBindImageTexture(1, head_pointer_texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
        // Just changing the image unit, since we do not use an imagebuffer
        glBindImageTexture(0, head_pointer_texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

        // Reset the next_free_pointer atomic counter to 0
        glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, next_free_pointer_ACB);
        const GLuint zero = 0;
        glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(zero), &zero);
    };

    while (true) {
        glfwPollEvents();
        // glfwWaitEvents();

        float frame_time = stop_watch::restart(stopwatch).count() * 1e-9;

        fps_title_update(window, frame_time);

        if (eng::handle_eye_input(window, eye, frame_time, wvp.view_matrix)) {
            glBindBuffer(GL_UNIFORM_BUFFER, wvp_block);
            glBufferSubData(GL_UNIFORM_BUFFER, offsetof(WVP_UniformBlock, view_matrix), sizeof(Matrix4x4),
                            &wvp.view_matrix);

            // sort_cubes_badly(cubes, wvp.view_matrix);
        }

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            break;
        }

        // Render

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Draw the cubes
        glUseProgram(list_builder_program);

        // Bind WVP uniform block
        glBindBuffer(GL_UNIFORM_BUFFER, wvp_block);

        glBindVertexArray(vertices_vao);

        glBindVertexBuffer(0, vertices_vbo, 0, sizeof(Vector3));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_ebo);

        reset_linked_list();

        // glEnable(GL_DEPTH_TEST);
        // glDepthFunc(GL_LESS);

        for (size_t i = 0; i < cubes.count(); ++i) {
            glBufferSubData(GL_UNIFORM_BUFFER, offsetof(WVP_UniformBlock, model_to_world), sizeof(Matrix4x4),
                            &cubes.arr_model_to_world[i]);

            const RGBA8 &color = cubes.arr_colors[i];

            glUniform4f(uniform_color_loc, color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                        color.a / 255.0f);

            glDrawElements(GL_TRIANGLES, cube_mesh->ntriangles * 3, GL_UNSIGNED_SHORT, 0);

            // glClear(GL_DEPTH_BUFFER_BIT);
        }

        // glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

#if 0
        // Want to read the linked list
        glFinish();
        glfwSwapBuffers(window);

        glBindBuffer(GL_TEXTURE_BUFFER, fragment_data_buffer);
        glBindTexture(GL_TEXTURE_BUFFER, fragment_data_imagebuffer);

        std::vector<uint32_t> head_pointers(TOTAL_PIXELS * sizeof(uint32_t));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, head_pointers.data());

        struct RGBA32 {
            uint32_t r, g, b, a;
        };

        // std::vector<RGBA32>  fragment_slots(FRAGMENT_DATA_SLOTS);
        const RGBA32 *slots = (const RGBA32 *)glMapBufferRange(GL_TEXTURE_BUFFER, 0, FRAGMENT_DATA_SLOTS * sizeof(RGBA32), GL_MAP_READ_BIT);

        // Finding the linked lists
        struct ListInfo {
            int32_t x, y;
            uint32_t list_length;
        };

        std::vector<ListInfo> lists;
        lists.reserve(screen_width * screen_height / 2);

        for (int32_t y = 0; y < screen_height; ++y) {
            for (int32_t x = 0; x < screen_width; ++x) {
                const int32_t pixel = y * MAX_FRAMEBUFFER_WIDTH + x;
                if (head_pointers[pixel] == END_OF_LIST_MARKER) {
                    continue;
                }

                uint32_t frag_slot = head_pointers[pixel];
                lists.push_back({ x, y, 0 });
                while (frag_slot != END_OF_LIST_MARKER) {
                    lists.back().list_length++;
                    frag_slot = slots[frag_slot].b;
                }
            }
        }

        debug("Num lists = %zu", lists.size());

        for (auto &l : lists) {
            if (l.list_length > 1) {
                printf("List at [%i, %i] Length = %u\n", l.x, l.y, l.list_length);
            }
        }

        glUnmapBuffer(GL_TEXTURE_BUFFER);

        // exit(EXIT_SUCCESS);
#endif

            // glFinish();
            // glfwSwapBuffers(window);

            // The linked lists are built, and now we blend

#if 1
        glUseProgram(blend_program);

        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(fullscreen_quad_vao);
        glBindVertexBuffer(0, fullscreen_quad_vbo, 0, sizeof(Quad::VertexData));
        glDrawArrays(GL_TRIANGLES, 0, 6);
#endif
        glfwSwapBuffers(window);
    }

    glfwTerminate();

    par_shapes_free_mesh(cube_mesh);

    memory_globals::shutdown();
}

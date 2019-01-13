// Fiddling with lodbias

#include "essentials.h"

using namespace fo;
using namespace math;

constexpr auto vs = R"(
#version 430 core

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 st;

layout(binding = 0, std140) uniform WVP {
    mat4 world;
    mat4 view;
    mat4 proj;
} wvp;

out VsOut {
    vec3 pos_viewspace;
    vec3 normal_viewspace;
    vec2 st;
} vs_out;

void main() {
    vs_out.pos_viewspace = vec3(wvp.view * wvp.world * vec4(pos, 1.0));
    vs_out.normal_viewspace = vec3(wvp.view * vec4(normal, 0.0));
    vs_out.st = st;
    gl_Position = wvp.proj * vec4(vs_out.pos_viewspace, 1.0);
}

)";

constexpr auto frag_shader = R"(
#version 430 core

in VsOut {
    vec3 pos_viewspace;
    vec3 normal_viewspace;
    vec2 st;
} fs_in;

layout(binding = 0) uniform sampler2D the_sampler;

out vec4 fc;

void main() {
    vec4 color = texture(the_sampler, fs_in.st).rgba;
    // fc = vec4(fs_in.st.xy, 0.0, 1.0);
    fc = color;
}

)";

constexpr i32 WINDOW_WIDTH = 800;
constexpr i32 WINDOW_HEIGHT = 600;

double current_lod_bias = 0.0f;
double change_per_scroll = -0.2f;
bool lod_bias_changed = false;

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
    current_lod_bias += yoffset * change_per_scroll;
    LOG_F(INFO, "LOD Bias = %f", current_lod_bias);
    lod_bias_changed = true;
}

int main() {
    memory_globals::init();
    DEFER([]() { memory_globals::shutdown(); });
    {
        GLFWwindow *window;

        eng::start_gl(&window, WINDOW_WIDTH, WINDOW_HEIGHT, "lodbias");
        eng::enable_debug_output(nullptr, nullptr);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

        glfwSetScrollCallback(window, scroll_callback);

        eye::State eye = eye::toward_negz(0.2f);

        stop_watch::State<std::chrono::high_resolution_clock> sw;
        stop_watch::start(sw);

        struct WVP {
            Matrix4x4 world;
            Matrix4x4 view;
            Matrix4x4 proj;
        };

        WVP wvp;
        wvp.world = identity_matrix;
        eye::update_view_transform(eye, wvp.view);
        wvp.proj = persp_proj(0.1f, 1000.0f, 70.0f * one_deg_in_rad, float(WINDOW_WIDTH) / WINDOW_HEIGHT);
        GLuint wvp_ubo = create_uniform_buffer(0, sizeof(WVP));
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(WVP), &wvp);

        auto mesh = create_mesh();
        DEFER([mesh]() { par_shapes_free_mesh(mesh); });

        shift_par_cube(mesh);
        GLuint mesh_vbo, mesh_ebo;

        create_vbo_from_par_mesh(mesh, &mesh_vbo, &mesh_ebo);

        Vao mesh_vao;
        mesh_vao.gen()
            .bind()
            .add_with_format(0, 3, GL_FLOAT, false, 0, 0)
            .add_with_format(1, 3, GL_FLOAT, false, 0, 1)
            .add_with_format(2, 2, GL_FLOAT, false, 0, 2);

        GLuint program = eng::create_program(CSTR(vs), CSTR(frag_shader));

        GLuint texture;
        glGenTextures(1, &texture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glTexStorage2D(GL_TEXTURE_2D, log2_ceil(512) + 1, GL_RGBA8, 512, 512);
        // Initialize the texture data
        {
            std::vector<RGBA8> texture_data(512 * 512);

            std::array<RGBA8, 10> colors = {RGBA8::from_hex(0xff0000), RGBA8::from_hex(0xfa00fe),
                                            RGBA8::from_hex(0x003010), RGBA8::from_hex(0x000000),
                                            RGBA8::from_hex(0x3000fa), RGBA8::from_hex(0x5000fa),
                                            RGBA8::from_hex(0x00ff00), RGBA8::from_hex(0x30fa00),
                                            RGBA8::from_hex(0x50fa00), RGBA8::from_hex(0x303030)};

            u32 width = 512;
            for (u64 i = 0; i < log2_ceil(512) + 1; ++i) {
                LOG_F(INFO, "i = %lu", i);
                std::fill(texture_data.begin(), texture_data.end(), colors[i]);
                glTexSubImage2D(GL_TEXTURE_2D, i, 0, 0, width, width, GL_RGBA, GL_UNSIGNED_BYTE,
                                texture_data.data());
                width = width / 2;
            }
        }

        // Sampling params
        GLuint sampler;
        glGenSamplers(1, &sampler);
        glBindSampler(0, sampler);
        glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            float dt = std::chrono::duration<float, std::ratio<1, 1>>(stop_watch::restart(sw)).count();

            if (eng::handle_eye_input(window, eye, dt, wvp.view)) {
                glBindBuffer(GL_UNIFORM_BUFFER, wvp_ubo);
                glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(WVP), &wvp);
            }

            if (lod_bias_changed) {
                lod_bias_changed = false;
                glSamplerParameterf(sampler, GL_TEXTURE_LOD_BIAS, current_lod_bias);
            }

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glUseProgram(program);
            mesh_vao.bind();
            glBindBuffer(GL_ARRAY_BUFFER, mesh_vbo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh_ebo);
            glBindVertexBuffer(0, mesh_vbo, 0, sizeof(Vector3));
            glBindVertexBuffer(1, mesh_vbo, mesh->npoints * sizeof(Vector3), sizeof(Vector3));
            glBindVertexBuffer(2, mesh_vbo, 2 * mesh->npoints * sizeof(Vector3), sizeof(Vector2));
            glDrawElements(GL_TRIANGLES, mesh->ntriangles * 3, GL_UNSIGNED_SHORT, 0);

            glfwSwapBuffers(window);
        }
    }
}

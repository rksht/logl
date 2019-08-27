#include <learnogl/eng>
#include <shaderc/shaderc.hpp>

using namespace fo;
using namespace eng::math;

const char *pp_source = R"(
#version 430 core

#if defined(DO_VS)

void main() {}

#elif defined(DO_FS)

#else

out vec4 fc;

void main() {
    fc = vec4(1.0);
}

#error "wut"

#endif
}

)";

int main() {
    memory_globals::init();
    DEFERSTAT(memory_globals::shutdown());

    eng::StartGLParams glparams;

    eng::start_gl(glparams);
    DEFERSTAT(eng::close_gl(glparams));

    // Create shader program
    // GLuint program = eng::create_program(vs_src, fs_src);

    fo::Array<u8> vs_source(memory_globals::default_allocator());
    // auto path = make_path(SOURCE_DIR, "fs_quad_vs.vert");
    auto path = make_path(SOURCE_DIR, "empty.glsl");
    eng::ShaderDefines macro_defs;

    macro_defs.add("DO_VS");

    // GLuint vs = eng::create_shader_object(path, eng::VERTEX_SHADER, macro_defs, "fs_quad_vs.vert");
    // GLuint vs = eng::compile_preprocessed_shader(pp_source, eng::VERTEX_SHADER);

    GLuint vs = eng::create_shader_object(path, eng::VERTEX_SHADER, macro_defs, "empty.vert");

    CHECK_NE_F(vs, 0);

    macro_defs.remove("DO_VS").add("DO_FS");

    path = make_path(SOURCE_DIR, "fs_quad_color.frag");
    GLuint fs = eng::create_shader_object(path, eng::FRAGMENT_SHADER, macro_defs, "fs_quad_color.frag");

    GLuint program = eng::create_program(vs, fs);

    // exit(EXIT_SUCCESS);

    while (!glfwWindowShouldClose(eng::gl().window)) {
        glfwPollEvents();

        glUseProgram(program);
        // Just draw a full screen triangle
        glBindVertexBuffer(0, 0, 0, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        // glBindVertexArray(vao);
        glBindVertexArray(eng::gl().bs.no_attrib_vao());
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(eng::gl().window);
    }
}

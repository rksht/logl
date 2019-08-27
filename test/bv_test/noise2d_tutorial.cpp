#include <learnogl/start.h>

#include <learnogl/eng>

#include <thread> // sleep_for

#include "duk_helper.h"

#define WIDTH 128
#define HEIGHT 128

eng::StartGLParams glparams;

struct AppThings {
    fo::Array<f32> attribute_image;

    eng::FBO f32_fbo;
    GLuint float_texture;

    WrappedDukContext duk;
    DukTypedBufferInfo<f32> f32_buffer_duk;
};

GLOBAL_STORAGE(AppThings, the_app);
GLOBAL_ACCESSOR_FN(AppThings, the_app, get_app);
#define APPSTRUCT get_app()
#define APPDUK get_app().duk.C()



void init_opengl_resources() {
    fill_array(APPSTRUCT.attribute_image, 0.0f);
    glCreateTextures(GL_TEXTURE_2D, 1, &APPSTRUCT.float_texture);
    glTextureStorage2D(APPSTRUCT.float_texture, 1, GL_R32F, glparams.window_width, glparams.window_height);

    APPSTRUCT.f32_fbo.gen("@f32_fbo")
        .bind()
        .add_attachment(0, APPSTRUCT.float_texture)
        .set_done_creating()
        .bind_as_readable();
}

void init_grid() {
    const int top_on_entry = duk_get_top(APPDUK);

    bool ok = create_global_duk_object(APPDUK,
                                       "g_grid",
                                       "Grid",
                                       { DukFnArg((float)glparams.window_width),
                                         DukFnArg((float)glparams.window_height),
                                         DukFnArg("Test Grid") });

    get_qualified_duk_variable(APPDUK, "g_grid");

    CallDukMethodOnVariable duk_caller;
    duk_caller.pop_return_value = false;
    duk_caller(APPDUK, "g_grid", "fill", { DukFnArg(1.0f) });
    duk_pop(APPDUK);

    ok = get_qualified_duk_variable(APPDUK, "g_grid");
    assert(ok);

    ok = duk_get_prop_string(APPDUK, -1, "array");
    assert(ok);

    auto buffer_info = get_duk_buffer_info(APPDUK, -1);
    APPSTRUCT.f32_buffer_duk = cast_duk_buffer_info<f32>(buffer_info);

    duk_set_top(APPDUK, top_on_entry);
}

void display_image() {
    glClear(GL_COLOR_BUFFER_BIT);

    glBindTexture(GL_TEXTURE_2D, APPSTRUCT.float_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RED, GL_FLOAT, APPSTRUCT.f32_buffer_duk.ptr);

    // eng::SetInputOutputFBO inout_fbo;
    // inout_fbo.output_fbo = &eng::g_bs()._screen_fbo;
    // inout_fbo.input_fbo = &APPSTRUCT.f32_fbo;
    // inout_fbo.read_attachment_number = 0;

    // typedef void (APIENTRYP PFNGLBLITFRAMEBUFFERPROC)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
    // GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);

    APPSTRUCT.f32_fbo.bind_as_readable(0).set_read_buffer(0);
    glBlitFramebuffer(0, 0, WIDTH, HEIGHT, 0, 0, WIDTH, HEIGHT, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glfwSwapBuffers(eng::gl().window);
}

int main(int ac, char **av) {
    eng::init_memory();
    DEFERSTAT(eng::shutdown_memory());

    glparams.window_width = WIDTH;
    glparams.window_height = HEIGHT;

    eng::start_gl(glparams);
    DEFERSTAT(eng::close_gl(glparams));

    new (&APPSTRUCT) AppThings();

    init_opengl_resources();

    APPSTRUCT.duk.init();
    DEFERSTAT(APPSTRUCT.duk.destroy());

    LOG_F(INFO, "Initial duk top = %i", duk_get_top(APPDUK));

    [&]() {
        eval_duk_source_file(APPDUK, make_path(SOURCE_DIR, "grid_based_fluid.js"));
        duk_pop(APPDUK);
    }();

    init_grid();
    display_image();

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(2s);
}

#include <learnogl/eng>
#include <learnogl/start.h>

eng::StartGLParams glparams;

int main(int ac, char **av) {
    glparams.window_title = "sound test 1";

    eng::init_memory();
    DEFERSTAT(eng::shutdown_memory());

    eng::start_gl(glparams);
    DEFERSTAT(eng::close_gl(glparams));

    add_resource_root(eng::gl().res_man, RESOURCES_DIR);

    LET wave_file = "whistle.wav";

    // play the audio file

    eng::ResourceHandle res_handle;
    CHECK_F(load_resource(eng::gl().res_man, wave_file, &res_handle));
}

#include <imgui.h>
#include <learnogl/imgui_pipeline.h>

GLOBAL_STORAGE(ImguiRenderPipeline, imgui_render_pipeline);
GLOBAL_ACCESSOR_FN(ImguiRenderPipeline, imgui_render_pipeline, get_imgui_render_pipeline);

sca_ imgui_vs_source = R"(

layout(location = 0) in vec2 pos_xy;
layout(location = 1) in vec2 st;
layout(location = 2) in vec4 color;

// Only the proj matrix is used.
layout(binding = 0, std140) uniform CameraTransformUblock {
	mat4 view;
	mat4 proj;
	vec4 camera_pos;
	vec4 camera_quat;
};

out VsOut {
	vec2 st;
	vec4 color;
} vo;

void main() {
	gl_Position = proj * vec4(pos_xy, -1.0, 1.0);
	vo.st = st;
	vo.color = color;
}
)";

sca_ imgui_fs_source = R"(#version 430 core

in VsOut {
	vec2 st;
	vec4 color;
} fin;

layout(binding = 0) uniform sampler2D s;

out vec4 fc;

void main() {
	fc = fin.color * texture(s, fin.st);
}
)";

ImguiRenderPipeline *create_imgui_render_pipeline(bool recreate) {
    auto &pl = get_imgui_render_pipeline();

    if (!recreate) {
        CHECK_NE_F(pl.vbo_handle.rmid(),
                   0,
                   "Already initialized. Don't call more than once or pass recreate = true");
    }

    // Create the shaders

    eng::ShaderDefines defs;

    pl.vs_handle = eng::create_vertex_shader(eng::g_rm(), imgui_vs_source, defs, "vs@imgui");
    pl.fs_handle = eng::create_fragment_shader(eng::g_rm(), imgui_fs_source, defs, "fs@imgui");

    // Create vao

    auto vao_format = eng::VaoFormatDesc::from_attribute_formats(
        { eng::VaoAttributeFormat(2, GL_FLOAT, GL_FALSE, IM_OFFSETOF(ImDrawVert, pos)),
          eng::VaoAttributeFormat(2, GL_FLOAT, GL_FALSE, IM_OFFSETOF(ImDrawVert, uv)),
          eng::VaoAttributeFormat(4, GL_UNSIGNED_BYTE, GL_TRUE, IM_OFFSETOF(ImDrawVert, col)) });

    VertexArrayHandle create_vao(RenderManager &self, const VaoFormatDesc &ci, const char *debug_label = nullptr);

    pl.vao_handle = eng::create_vao(eng::g_rm(), vao_format, "vao@imgui");

    // Create global render states
}

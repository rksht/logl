#pragma once

#include <learnogl/essential_headers.h>
#include <learnogl/gl_misc.h>
#include <learnogl/shader.h>

#include <scaffold/math_types.h>

#include <imgui.h>

bool imgl3_init();
void imgl3_shutdown();
void imgl3_new_frame();

void imgl3_render_draw_data(ImDrawData *draw_data);

// Called by Init/NewFrame/Shutdown
bool imgl3_create_font_texture();
void imgl3_destroy_fonts_texture();
bool imgl3_create_opengl_objects();
void imgl3_destroy_opengl_objects();

// -- Impl

struct Imgui_OpenGL3_Data {
	GLuint font_texture_handle = 0;
	GLuint shader_program_handle = 0;
	GLuint vs_handle = 0;
	GLuint fs_handle = 0;

	GLuint vbo_handle = 0;
	GLuint ebo_handle = 0;
	GLuint vao_handle = 0;

	GLuint camera_transform_ubo_bindpoint = 0;
	// ^ Doesn't change. But we might want to later.
};

GLOBAL_STORAGE(Imgui_OpenGL3_Data, g_imgui_gl3_data);
GLOBAL_ACCESSOR_FN(Imgui_OpenGL3_Data, g_imgui_gl3_data, g_imgl3);

#define G_IMGL3 g_imgl3()

bool imgl3_init()
{
	new (&G_IMGL3) Imgui_OpenGL3_Data{};

	ImGuiIO &io = ImGui::GetIO();
	io.BackendRendererName = "OpenGL 4.3 Backend";

#if 0
	IM_ASSERT(strlen(glsl_version) + 2 < sizeof(Imgui_OpenGL3_Data::glsl_version_string));

	strcpy(G_IMGL3.glsl_version_string, glsl_version);
	strcat(G_IMGL3.glsl_version_string, "\n");

#endif

	imgl3_create_opengl_objects();
	imgl3_create_font_texture();

	return true;
}

void imgl3_shutdown()
{
	if (G_IMGL3.vbo_handle) {
		glDeleteBuffers(1, &G_IMGL3.vbo_handle);
		G_IMGL3.vbo_handle = 0;
	}

	if (G_IMGL3.ebo_handle) {
		glDeleteBuffers(1, &G_IMGL3.ebo_handle);
		G_IMGL3.ebo_handle = 0;
	}
}

const char *imgl3_vs_source = R"(#version 430 core

layout(location = 0) in vec2 pos_xy;
layout(location = 1) in vec2 st;
layout(location = 2) in vec4 color;

// Only the proj matrix is used.
layout(binding = 4, std140) uniform CameraTransformUblock {
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

const char *imgl3_fs_source = R"(#version 430 core

in VsOut {
	vec2 st;
	vec4 color;
} fin;

layout(binding = 4) uniform sampler2D sam_tex_imgl;

out vec4 fc;

void main() {
	fc = fin.color * texture(sam_tex_imgl, fin.st);
}

)";

bool imgl3_create_opengl_objects()
{
	// We don't backup bindings or state. That's not how we roll.

	glGenBuffers(1, &G_IMGL3.vbo_handle);
	glGenBuffers(1, &G_IMGL3.ebo_handle);

	G_IMGL3.vs_handle =
	  eng::create_shader_object(imgl3_vs_source, eng::ShaderKind::VERTEX_SHADER, {}, "@imgui_vs");
	G_IMGL3.fs_handle =
	  eng::create_shader_object(imgl3_fs_source, eng::ShaderKind::FRAGMENT_SHADER, {}, "@imgui_fs");
	G_IMGL3.shader_program_handle = eng::create_program(G_IMGL3.vs_handle, G_IMGL3.fs_handle);

	// Create the vao, just need one.
	auto vao_format = eng::VaoFormatDesc::from_attribute_formats(
	  { eng::VaoAttributeFormat(2, GL_FLOAT, GL_FALSE, IM_OFFSETOF(ImDrawVert, pos)),
		eng::VaoAttributeFormat(2, GL_FLOAT, GL_FALSE, IM_OFFSETOF(ImDrawVert, uv)),
		eng::VaoAttributeFormat(4, GL_UNSIGNED_BYTE, GL_TRUE, IM_OFFSETOF(ImDrawVert, col)) });

	eng::VertexArrayHandle vao = eng::create_vao(eng::g_rm(), vao_format);
	G_IMGL3.vao_handle = eng::get_gluint_from_rmid(eng::g_rm(), vao.rmid());

	eng::set_vao_label(G_IMGL3.vao_handle, "@imgui_vao");

	return true;
}

void imgl3_new_frame()
{

#if 0
	if (G_IMGL3.font_texture_handle == 0) {
		imgl3_create_opengl_objects();
		imgl3_create_font_texture();
	}
#endif
}

void imgl3_render_draw_data(ImDrawData *draw_data)
{
	ImGuiIO &io = ImGui::GetIO();
	int fb_width = int(draw_data->DisplaySize.x * io.DisplayFramebufferScale.x);
	int fb_height = int(draw_data->DisplaySize.y * io.DisplayFramebufferScale.y);

	if (fb_width <= 0 || fb_height <= 0) {
		ABORT_F("fb_width <= 0 or fb_height <= 0, fb_width = %f, fb_height = %f",
				(float)fb_width,
				(float)fb_height);
		return;
	}

	draw_data->ScaleClipRects(io.DisplayFramebufferScale);

	// Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled,
	// polygon fill. TODO: Use a collected state description for these.
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);

#ifdef GL_POLYGON_MODE
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

	glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);

	glUseProgram(G_IMGL3.shader_program_handle);

	float L = draw_data->DisplayPos.x;
	float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
	float T = draw_data->DisplayPos.y;
	float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;

	fo::Matrix4x4 proj_matrix = eng::math::ortho_proj(
	  { L, R }, { -1.0f, 1.0f }, { B, T }, { -1.0f, 1.0f }, { -1.0f, 1.0f }, { -1.0f, 1.0f });

	static std::once_flag once;

	std::call_once(once, [&]() { print_matrix_classic("imgui ortho proj", proj_matrix); });

	eng::CameraTransformUB camera_transform_ub = {};
	camera_transform_ub.proj = proj_matrix;

	GLuint ubo_handle = eng::get_gluint_from_rmid(eng::g_rm(), eng::g_rm().camera_ubo_handle().rmid());

	glBindBuffer(GL_UNIFORM_BUFFER, ubo_handle);
	glBindBufferRange(GL_UNIFORM_BUFFER, 4, ubo_handle, 0, sizeof(eng::CameraTransformUB));

	glBufferSubData(
	  GL_UNIFORM_BUFFER, sizeof(eng::CameraTransformUB), 0, REINPCAST(void *, &camera_transform_ub));

	glBindVertexArray(G_IMGL3.vao_handle);

	const bool clip_origin_lower_left = true; // OpenGL, so yes

	ImVec2 topleft = draw_data->DisplayPos;

	for (int i = 0; i < draw_data->CmdListsCount; ++i) {
		const ImDrawList *cmd_list = draw_data->CmdLists[i];
		ImDrawIdx idx_buffer_offset = 0;

		glBindBuffer(GL_ARRAY_BUFFER, G_IMGL3.vbo_handle);
		glBufferData(GL_ARRAY_BUFFER,
					 (GLsizeiptr)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert),
					 (const GLvoid *)cmd_list->VtxBuffer.Data,
					 GL_STREAM_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, G_IMGL3.ebo_handle);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,
					 (GLsizeiptr)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx),
					 (const GLvoid *)cmd_list->IdxBuffer.Data,
					 GL_STREAM_DRAW);

		glBindVertexBuffer(0, G_IMGL3.vbo_handle, 0, sizeof(ImDrawVert));

		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i) {
			// LOG_F(INFO, "cmd_i = %i, num commands = %i", cmd_i, cmd_list->CmdBuffer.Size);
			const ImDrawCmd &p_cmd = cmd_list->CmdBuffer[i];

			CHECK_EQ_F(p_cmd.UserCallback, nullptr, "p_cmd.UserCallback == %p", p_cmd.UserCallback);

			if (p_cmd.UserCallback != nullptr) {
				invoke(p_cmd.UserCallback, cmd_list, &p_cmd);

			} else {
				ImVec4 clip_rect = ImVec4(p_cmd.ClipRect.x - topleft.x,
										  p_cmd.ClipRect.y - topleft.y,
										  p_cmd.ClipRect.z - topleft.x,
										  p_cmd.ClipRect.w - topleft.y);
				if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f &&
					clip_rect.w >= 0.0f) {
					// Apply scissor/clipping rectangle
					if (clip_origin_lower_left) {
						glScissor((int)clip_rect.x,
								  (int)(fb_height - clip_rect.w),
								  (int)(clip_rect.z - clip_rect.x),
								  (int)(clip_rect.w - clip_rect.y));
					} else {
						glScissor((int)clip_rect.x,
								  (int)clip_rect.y,
								  (int)clip_rect.z,
								  (int)clip_rect.w); // Support for GL 4.5's glClipControl(GL_UPPER_LEFT)
					}

					// Bind texture, Draw
					glBindSampler(4, 0);
					glBindTextureUnit(4, p_cmd.TextureId);

					glDrawElements(GL_TRIANGLES,
								   (GLsizei)p_cmd.ElemCount,
								   GL_UNSIGNED_SHORT,
								   (void *)(idx_buffer_offset * sizeof(u16)));
				}
			}

			idx_buffer_offset += p_cmd.ElemCount;
		}
	}
}

bool imgl3_create_font_texture()
{
	// Build texture atlas
	ImGuiIO &io = ImGui::GetIO();
	unsigned char *pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	// Upload texture to graphics system
	glCreateTextures(GL_TEXTURE_2D, 1, &G_IMGL3.font_texture_handle);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glBindTexture(GL_TEXTURE_2D, G_IMGL3.font_texture_handle);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glTextureParameteri(G_IMGL3.font_texture_handle, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(G_IMGL3.font_texture_handle, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	eng::set_texture_label(G_IMGL3.font_texture_handle, "@imgui_font_atlas");

	LOG_F(INFO, "Font texture ID = %u", G_IMGL3.font_texture_handle);

	// Store our identifier
	io.Fonts->SetTexID(G_IMGL3.font_texture_handle);

	LOG_F(INFO, "ImGui Fonts created - GLuint = %u", io.Fonts->TexID);

	CHECK_F(io.Fonts->IsBuilt(), "");

	return true;
}

void imgl3_destroy_opengl_objects()
{
	if (G_IMGL3.vbo_handle) {
		glDeleteBuffers(1, &G_IMGL3.vbo_handle);
	}
	if (G_IMGL3.ebo_handle) {
		glDeleteBuffers(1, &G_IMGL3.ebo_handle);
	}

	G_IMGL3.vbo_handle = G_IMGL3.ebo_handle = 0;

	if (G_IMGL3.shader_program_handle) {
		glDeleteProgram(G_IMGL3.shader_program_handle);
	}

	if (G_IMGL3.vs_handle) {
		glDeleteShader(G_IMGL3.vs_handle);
	}

	if (G_IMGL3.fs_handle) {
		glDeleteShader(G_IMGL3.fs_handle);
	}

	G_IMGL3.shader_program_handle = 0;
	G_IMGL3.vs_handle = 0;
	G_IMGL3.fs_handle = 0;

	// Destroy fonts texture
	if (G_IMGL3.font_texture_handle) {
		ImGuiIO &io = ImGui::GetIO();
		glDeleteTextures(1, &G_IMGL3.font_texture_handle);
		io.Fonts->TexID = 0;
		G_IMGL3.font_texture_handle = 0;
	}
}

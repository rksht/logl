#pragma once

#include <learnogl/typed_gl_resources.h>

// A pipeline for rendering imgui's data
struct ImguiRenderPipeline {
    eng::VertexShaderHandle vs_handle;
    eng::FragmentShaderHandle fs_handle;
    eng::Texture2DHandle font_texture_handle;

    eng::VertexBufferHandle vbo_handle;
    eng::IndexBufferHandle ebo_handle;

    eng::DepthStencilStateDesc ds_state_desc;
    eng::DepthStencilStateId ds_state_id;

    eng::RasterizerStateDesc rs_state_desc;
    eng::RasterizerStateId rs_state_id;

    eng::BlendFunctionDesc blend_func_desc;
    eng::BlendFunctionDescId blend_func_id;
};


ImguiRenderPipeline *create_imgui_render_pipeline(bool recreate);

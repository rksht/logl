#include "d3d11_misc.h"
#include "d3d_eye.h"

#include <learnogl/nf_simple.h>

using namespace fo;
using namespace math;

static d3d11_misc::InitD3DConfig d3dconf;

constexpr u32 WIDTH = 1024;
constexpr u32 HEIGHT = 1024;

// vertex buffer for the quad
struct VertexData {
    xm4 position;
    xm3 normal;
    xm2 texcoord;
};

const xm3 common_normal(0.0f, 0.0f, 1.0f);

VertexData vertices[] = { { xm4(1.0f, 1.0f, 0.0f, 1.0f), common_normal, xm2(1.0f, 0.0f) },
                          { xm4(-1.0f, 1.0f, 0.0f, 1.0f), common_normal, xm2(1.0f, 1.0f) },
                          { xm4(-1.0f, -1.0f, 0.0f, 1.0f), common_normal, xm2(0.0f, 1.0f) },
                          { xm4(1.0f, -1.0f, 0.0f, 1.0f), common_normal, xm2(0.0f, 0.0f) } };
u16 indices[] = { 0, 1, 2, 0, 2, 3 };

// Returns a point in clip space that projects onto the given (wnd_x, wnd_y) window coordinate. The returned
// point when transformed to view space, will be on the near plane.
inline Vector4 window_to_clip_coord(const App &app, float wnd_x, float wnd_y) {
    const float ndc_x = (2 * wnd_x) / width - 1;
    const float ndc_y = -(2 * wnd_y) / height + 1;
    const Vector4 clip = { ndc_x * NEAR, ndc_y * NEAR, -NEAR, NEAR }; // Point in clip space
    return clip;
}

inline Vector4 window_to_view_coord(const App &app, float wnd_x, float wnd_y) {
    const Vector4 clip = window_to_clip_coord(app, wnd_x, wnd_y);
    return app.inv_proj_mat * clip;
}

xmv window_to_world_coord(const D3DCamera &cam, f32 wnd_x, f32 wnd_y, f32 window_width, f32 window_height) {
    // Get corresponding clip coordinates with z = 0.5
    const f32 ndc_x = (2 * wnd_x) / window_width - 1;
    const f32 ndc_y = -(2 * wnd_y) / window_height + 1;

    const xmv clip = XMVectorSet(ndc_x * cam._near_z, ndc_y * cam._near_z, -cam._near_z, cam._near_z);
    const xmv view = XMVector4Transform(clip, xmload(cam.proj_xform()));

    const xmm world_from_view = d3d_eye::get_world_from_view_transform(cam._eye);
    return XMVector4Transform(view, world_from_view);
}

struct CBPerCamera {
    xm44 view;
    xm44 proj;
    xm44 inv_view;
    xm44 inv_proj;
};

struct Sphere {
    xm4 center_and_radius;

    Sphere() = default;

    Sphere(fxmv center, f32 radius) {
        xmv v = center;
        v = XMVectorSetW(v, radius);
        xmstore(v, center_and_radius);
    }

    f32 radius() const { return xm_w(xmload(center_and_radius)); }

    xmv center() const {
        auto v = xmload(center_and_radius);
        v = XMVectorSetW(v, 1.0f);
        return v;
    }
};

struct App {
    input::BaseHandlerPtr<App> current_handler = input::make_handler<input::InputHandlerBase<App>>();
    auto &current_input_handler() { return current_handler; }
    void set_input_handler(input::BaseHandlerPtr<App> handler) { current_handler = std::move(handler); }

    d3d11_misc::WindowContext d3d;

    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3DBlob> vs_blob;
    ComPtr<ID3DBlob> ps_blob;

    ComPtr<ID3D11InputLayout> vertex_layout;
    ComPtr<ID3D11Buffer> vb;
    ComPtr<ID3D11Buffer> ib;
    ComPtr<ID3D11Buffer> cb_per_camera;
    ComPtr<ID3D11RasterizerState> rs_state;

    // A structured buffer containing the list of all spheres in the scene
    ComPtr<ID3D11Texture2D> random_numbers_texture;
    ComPtr<ID3D11ComputeShader> rng_cs;
    u32 groups_x;
    u32 groups_y;
    ComPtr<ID3D11UnorderedAccessView> random_numbers_tex_uav;
    ComPtr<ID3D11ShaderResourceView> random_numbers_tex_srv;
    ComPtr<ID3D11SamplerState> random_numbers_sampler;

    xmm world;

    CBPerCamera cbh_per_camera;

    D3DCamera camera;

    xm4 mesh_color = xm4(0.7f, 0.7f, 0.7f, 1.0f);

    bool close = false;

    int capture_count = 0;

    std::vector<Sphere> muh_spheres;
};

class TestHandler : public input::InputHandlerBase<App> {
  public:
    virtual void handle_on_key(App &, input::OnKeyArgs) {}
    virtual void handle_on_mouse_move(App &, input::OnMouseMoveArgs) {}
    virtual void handle_on_mouse_button(App &, input::OnMouseButtonArgs) {}
    virtual void handle_on_char_input(App &, input::OnCharInputArgs) {}
};

DEFINE_GLFW_CALLBACKS(App);

void create_rng_compute_shader(App &app) {
    HRESULT hr;

    inistorage::Storage ini(fs::path(SOURCE_DIR) / "rng_cs_tweak.sjson");

    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = (u32)ini.number("texture_width");
    tex_desc.Height = (u32)ini.number("texture_height");
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_R32_FLOAT;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    tex_desc.CPUAccessFlags = 0;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;

    hr = app.d3d.dev->CreateTexture2D(&tex_desc, nullptr, app.random_numbers_texture.GetAddressOf());
    DXASSERT("CreateTexture2D", hr);
    d3d11_misc::set_debug_name(app.random_numbers_texture, "@random_numbers_texture");

    // Also create a UAV for writing from the compute pipeline
    D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
    uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice = 0;
    hr = app.d3d.dev->CreateUnorderedAccessView(
        app.random_numbers_texture.Get(), &uav_desc, app.random_numbers_tex_uav.GetAddressOf());
    DXASSERT("CreateUnorderedAccessView", hr);

    // And an SRV for reading from the pixel shader
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;
    hr = app.d3d.dev->CreateShaderResourceView(
        app.random_numbers_texture.Get(), &srv_desc, app.random_numbers_tex_srv.GetAddressOf());
    DXASSERT("CreateShaderResourceView", hr);

    auto str_tpg_x = std::to_string((u32)ini.number("threads_per_group_x"));
    auto str_tpg_y = std::to_string((u32)ini.number("threads_per_group_y"));

    auto str_texture_width = std::to_string((u32)ini.number("texture_width"));

    // Create the compute shader
    D3D_SHADER_MACRO cs_defines[] = { { "tpg_x", str_tpg_x.c_str() },
                                      { "tpg_y", str_tpg_y.c_str() },
                                      { "TEXTURE_WIDTH", str_texture_width.c_str() },
                                      { nullptr, nullptr } };

    ini.number("groups_x", app.groups_x, 0);
    ini.number("groups_y", app.groups_y, 0);
    CHECK_LE_F(app.groups_x, D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION, "");
    CHECK_LE_F(app.groups_y, D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION, "");

    u32 shader_flags = 0;
#ifdef _DEBUG
    shader_flags |= D3DCOMPILE_DEBUG;
    shader_flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> cs_blob;

    hr = d3d11_misc::compile_hlsl_file(fs::path(SOURCE_DIR) / "spheres_intr_cs.hlsl",
                                       cs_defines,
                                       "CS_main",
                                       "cs_5_0",
                                       shader_flags,
                                       0,
                                       cs_blob.GetAddressOf());
    DXASSERT("compile_hlsl_file - compute", hr);

    hr = app.d3d.dev->CreateComputeShader(
        cs_blob->GetBufferPointer(), cs_blob->GetBufferSize(), nullptr, app.rng_cs.GetAddressOf());
    DXASSERT("CreateComputeShader", hr);

    // Set the texture resource view
    app.d3d.imm->CSSetShader(app.rng_cs.Get(), nullptr, 0);
    auto uav_list = make_scratch_vector({ app.random_numbers_tex_uav.Get() });
    auto uav_offsets = make_scratch_vector({ 0u });
    app.d3d.imm->CSSetUnorderedAccessViews(0, 1, uav_list.data(), uav_offsets.data());

    LOG_F(INFO, "Created compute shader");

    // Also create a sampler state for sampling the texture from the pixel shader
    D3D11_SAMPLER_DESC sampler_desc = {};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    hr = app.d3d.dev->CreateSamplerState(&sampler_desc, app.random_numbers_sampler.GetAddressOf());
    DXASSERT("CreateSamplerState", hr);
}

stop_watch::State<std::chrono::high_resolution_clock> sw_state = {};

namespace app_loop {

template <> void init<App>(App &app) {
    stop_watch::start(sw_state);

    create_rng_compute_shader(app);

    HRESULT hr;

    DWORD shader_flags = D3DCOMPILE_ENABLE_STRICTNESS;

#ifdef _DEBUG
    shader_flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    DXASSERT("compile shader",
             d3d11_misc::compile_hlsl_file(make_path(SOURCE_DIR, "triangle_vs.hlsl"),
                                           nullptr,
                                           "VS_main",
                                           "vs_4_0",
                                           shader_flags,
                                           0,
                                           app.vs_blob.GetAddressOf()));
    DXASSERT("compile shader",
             d3d11_misc::compile_hlsl_file(make_path(SOURCE_DIR, "triangle_ps.hlsl"),
                                           nullptr,
                                           "PS_main",
                                           "ps_4_0",
                                           shader_flags,
                                           0,
                                           app.ps_blob.GetAddressOf()));

    hr = app.d3d.dev->CreateVertexShader(
        app.vs_blob->GetBufferPointer(), app.vs_blob->GetBufferSize(), nullptr, app.vs.GetAddressOf());
    hr = app.d3d.dev->CreatePixelShader(
        app.ps_blob->GetBufferPointer(), app.ps_blob->GetBufferSize(), nullptr, app.ps.GetAddressOf());

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION",
          0,
          DXGI_FORMAT_R32G32B32A32_FLOAT,
          0,
          offsetof(VertexData, position),
          D3D11_INPUT_PER_VERTEX_DATA,
          0 },

        { "NORMAL",
          0,
          DXGI_FORMAT_R32G32B32_FLOAT,
          0,
          offsetof(VertexData, normal),
          D3D11_INPUT_PER_VERTEX_DATA,
          0 },

        { "TEXCOORD",
          0,
          DXGI_FORMAT_R32G32_FLOAT,
          0,
          offsetof(VertexData, texcoord),
          D3D11_INPUT_PER_VERTEX_DATA,
          0 },
    };

    hr = app.d3d.dev->CreateInputLayout(layout,
                                        ARRAY_SIZE(layout),
                                        app.vs_blob->GetBufferPointer(),
                                        app.vs_blob->GetBufferSize(),
                                        app.vertex_layout.GetAddressOf());

    DXASSERT("create input layout", hr);

    app.d3d.imm->IASetInputLayout(app.vertex_layout.Get());

    d3d11_misc::set_debug_name(app.vs, "Vertex shader");
    d3d11_misc::set_debug_name(app.ps, "Pixel shader");

    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.ByteWidth = sizeof(vertices);
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = vertices;

        hr = app.d3d.dev->CreateBuffer(&desc, &sub, app.vb.GetAddressOf());
        DXASSERT("create vertex buffer", hr);

        const u32 strides[] = { sizeof(VertexData) };
        const u32 offsets[] = { 0u };

        auto vbptrs = make_scratch_vector<ID3D11Buffer *>({ app.vb.Get() });
        app.d3d.imm->IASetVertexBuffers(0, 1, vbptrs.data(), strides, offsets);
    }

    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.ByteWidth = sizeof(indices);
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = indices;

        hr = app.d3d.dev->CreateBuffer(&desc, &sub, app.ib.GetAddressOf());
        DXASSERT("create index buffer", hr);

        app.d3d.imm->IASetIndexBuffer(app.ib.Get(), DXGI_FORMAT_R16_UINT, 0);
    }

    D3D11_RASTERIZER_DESC rs_desc = {};
    rs_desc.CullMode = D3D11_CULL_BACK;
    rs_desc.FrontCounterClockwise = true;
    rs_desc.FillMode = D3D11_FILL_SOLID;
    rs_desc.MultisampleEnable = true;
    app.d3d.dev->CreateRasterizerState(&rs_desc, app.rs_state.GetAddressOf());
    app.d3d.imm->RSSetState(app.rs_state.Get());

    app.d3d.imm->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Camera setup
    app.camera.set_look_at(xmload(xm3(0.0f, 0.0f, -2.0f)), xm_origin(), xm_unit_y());
    app.camera.set_proj(2.0f, 4000.0f, XM_PI / 4.0f, float(d3dconf.window_width) / d3dconf.window_height);

    xmstore(app.camera.view_xform(), app.cbh_per_camera.view);
    xmstore(app.camera.proj_xform(), app.cbh_per_camera.proj);

    {
        auto x = app.camera.right();
        auto y = app.camera.up();
        auto z = app.camera.forward();
        LOG_F(INFO,
              "X(right): [%.3f, %.3f, %.3f], Y(up): [%.3f, %.3f, %.3f], Z(fwd): [%.3f, %.3f, %.3f]",
              XM_XYZ(x),
              XM_XYZ(y),
              XM_XYZ(z));
    }

    // Constant buffer setup
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.ByteWidth = sizeof(CBPerCamera);

        DXASSERT("create constant buffer",
                 app.d3d.dev->CreateBuffer(&desc, nullptr, app.cb_per_camera.GetAddressOf()));

        d3d11_misc::set_debug_name(app.cb_per_camera, "cb_vs_per_object");

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = app.d3d.imm->Map(app.cb_per_camera.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &app.cbh_per_camera, sizeof(CBPerCamera));
        app.d3d.imm->Unmap(app.cb_per_camera.Get(), 0);
    }

    // Set the shader programs
    app.d3d.imm->VSSetShader(app.vs.Get(), nullptr, 0);
    app.d3d.imm->PSSetShader(app.ps.Get(), nullptr, 0);

    {
        auto cb_list = make_scratch_vector<ID3D11Buffer *>({ app.cb_per_camera.Get() });
        app.d3d.imm->VSSetConstantBuffers(0, 1, cb_list.data());
    }

// Run and wait for the compute to finish
#if 1
    {
        D3D11_QUERY_DESC query_desc = {};
        query_desc.Query = D3D11_QUERY_EVENT;
        ComPtr<ID3D11Query> query;
        hr = app.d3d.dev->CreateQuery(&query_desc, query.GetAddressOf());
        DXASSERT("CreateQuery", hr);

        app.d3d.imm->Dispatch(app.groups_x, app.groups_y, 1);
        app.d3d.imm->End(query.Get());

        LOG_F(INFO, "Dispatched in groups [%u, %u, %u]", app.groups_x, app.groups_y, 1);

        // Wait till done
        BOOL done = FALSE;
        while (app.d3d.imm->GetData(query.Get(), &done, sizeof(done), 0) != S_OK) {
        }

        // Unbind the uav from the CS stage
        ID3D11UnorderedAccessView *uav_list[1] = { nullptr };
        UINT uav_offsets[1] = { 0 };
        app.d3d.imm->CSSetUnorderedAccessViews(0, 1, uav_list, uav_offsets);

        // Notify done with compute by removing the shader program from the CS stage.
        app.d3d.imm->CSSetShader(nullptr, nullptr, 0);

        double sec = seconds(stop_watch::restart(sw_state));
        LOG_F(INFO, "Compute done - took <= %f seconds", sec);
    }
#endif
    // d3d11_misc::trigger_renderdoc_frame_capture(app.d3d, 3);

    // Set the PS's texture and the sampler
    auto srv_list = make_scratch_vector({ app.random_numbers_tex_srv.Get() });
    app.d3d.imm->PSSetShaderResources(0, 1, srv_list.data());
    auto sampler_list = make_scratch_vector({ app.random_numbers_sampler.Get() });
    app.d3d.imm->PSSetSamplers(0, 1, sampler_list.data());
}

template <> void update<App>(App &app, State &state) {
    glfwPollEvents();

    handle_camera_input(app.d3d.window, app.camera, state.frame_time_in_sec);
    xmstore(app.camera.view_xform(), app.cbh_per_camera.view);

    if (glfwGetKey(app.d3d.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        app.close = true;
    }
}

template <> void render<App>(App &app) {
    if (app.capture_count < 3) {
        // d3d11_misc::trigger_renderdoc_frame_capture(app.d3d);
        // app.capture_count = 1;
        // assert(d3d11_misc::is_renderdoc_frame_capturing(app.d3d));
        // d3d11_misc::start_renderdoc_frame_capture(app.d3d);
        d3d11_misc::trigger_renderdoc_frame_capture(app.d3d);
        ++app.capture_count;
    }

    app.d3d.imm->ClearRenderTargetView(app.d3d.rtv_screen.Get(), DirectX::Colors::AliceBlue);
    app.d3d.imm->ClearDepthStencilView(
        app.d3d.depth_stencil_view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    d3d11_misc::start_renderdoc_frame_capture(app.d3d);

    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        app.d3d.imm->Map(app.cb_per_camera.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &app.cbh_per_camera, sizeof(CBPerCamera));
        app.d3d.imm->Unmap(app.cb_per_camera.Get(), 0);
    }

    app.d3d.imm->DrawIndexed(ARRAY_SIZE(indices), 0, 0);

    app.d3d.schain->Present(0, 0);

    // LOG_F(INFO, "Frame capturing? %s", d3d11_misc::is_renderdoc_frame_capturing(app.d3d) ? "Yes" :
    // "False");
    //    if (app.capture_count == 0) {
    //        d3d11_misc::end_renderdoc_frame_capture(app.d3d);
    //        app.capture_count = 1;
    //    }
}

template <> void close<App>(App &app) {}

template <> bool should_close<App>(App &app) { return app.close || glfwWindowShouldClose(app.d3d.window); }

} // namespace app_loop

int main(int ac, char **av) {
    memory_globals::init();
    DEFERSTAT(memory_globals::shutdown());
    init_pmr_globals();
    DEFERSTAT(shutdown_pmr_globals());

    rng::init_rng();

    d3dconf.window_width = WIDTH;
    d3dconf.window_height = HEIGHT;
    d3dconf.window_title = "d3d_test";
    d3dconf.load_renderdoc = false;
    d3dconf.capture_path_template = "captures/d3d_ray";

    // Initialise application window.
    App app;
    app.d3d = d3d11_misc::init_d3d_window(d3dconf);
    app.set_input_handler(input::make_handler<TestHandler>());
    DEFERSTAT(d3d11_misc::close_d3d_window(d3dconf));

    REGISTER_GLFW_CALLBACKS(app, app.d3d.window);

    app.d3d.imm->ClearRenderTargetView(app.d3d.rtv_screen.Get(), DirectX::Colors::AliceBlue);
    app.d3d.schain->Present(0, 0);

    app_loop::State timer;
    app_loop::run(app, timer);
}

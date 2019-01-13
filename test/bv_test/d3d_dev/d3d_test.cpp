#include "d3d11_misc.h"
#include "d3d_eye.h"

using namespace fo;
using namespace math;

static d3d11_misc::InitD3DConfig d3dconf;

// vertex buffer for the quad
struct VertexData {
	xm4 position;
	xm3 normal;
	xm2 texcoord;
};

const xm3 common_normal(0.0f, 0.0f, 1.0f);

VertexData vertices[] = {{xm4(1.0f, 1.0f, 0.0f, 1.0f), common_normal, xm2(1.0f, 0.0f)},
						 {xm4(-1.0f, 1.0f, 0.0f, 1.0f), common_normal, xm2(1.0f, 1.0f)},
						 {xm4(-1.0f, -1.0f, 0.0f, 1.0f), common_normal, xm2(0.0f, 1.0f)},
						 {xm4(1.0f, -1.0f, 0.0f, 1.0f), common_normal, xm2(0.0f, 0.0f)}};
u16 indices[] = {0, 1, 2, 0, 2, 3};

struct CBCameraTransform {
	xm44 view;
	xm44 proj;
};

struct App {
	input::BaseHandlerPtr<App> current_handler = input::make_handler<input::InputHandlerBase<App>>();
	auto &current_input_handler() { return current_handler; }
	void set_input_handler(input::BaseHandlerPtr<App> handler) { current_handler = std::move(handler); }

	d3d11_misc::WindowContext d3d;

	ComPtr<ID3D11VertexShader> vs;
	ComPtr<ID3D11PixelShader> ps;
	ComPtr<ID3D10Blob> vs_blob;
	ComPtr<ID3D10Blob> ps_blob;

	ComPtr<ID3D11InputLayout> vertex_layout;
	ComPtr<ID3D11Buffer> vb;
	ComPtr<ID3D11Buffer> ib;
	ComPtr<ID3D11Buffer> cb_per_camera;
	ComPtr<ID3D11RasterizerState> rs_state;
	xmm world;

	CBCameraTransform cbh_per_camera;

	D3DCamera camera;

	xm4 mesh_color = xm4(0.7f, 0.7f, 0.7f, 1.0f);

	bool close = false;
};

class TestHandler : public input::InputHandlerBase<App>
{
  public:
	virtual void handle_on_key(App &, input::OnKeyArgs) {}
	virtual void handle_on_mouse_move(App &, input::OnMouseMoveArgs) {}
	virtual void handle_on_mouse_button(App &, input::OnMouseButtonArgs) {}
	virtual void handle_on_char_input(App &, input::OnCharInputArgs) {}
};

DEFINE_GLFW_CALLBACKS(App);

void create_shaders(App &app) {}

void create_buffers(App &app) {}

namespace app_loop
{

template <> void init<App>(App &app)
{
	{

		auto m = XMMatrixRotationAxis(xm_unit_y(), XM_PI / 4.0f);
		auto q = XMQuaternionRotationMatrix(m);
		auto m2 = XMMatrixRotationQuaternion(q);

		LOG_F(INFO, "Matrix before = %s", fo::string_stream::c_str(matrix_to_string(m)));
		LOG_F(INFO, "Matrix after = %s", fo::string_stream::c_str(matrix_to_string(m2)));
	}

	HRESULT hr;

	DWORD shader_flags = D3DCOMPILE_ENABLE_STRICTNESS;

#ifdef _DEBUG
	shader_flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	DXASSERT("compile shader",
			 d3d11_misc::compile_hlsl_file(make_path(SOURCE_DIR, "triangle_vs.hlsl"), nullptr, "VS_main",
										   "vs_4_0", shader_flags, 0, app.vs_blob.GetAddressOf()));
	DXASSERT("compile shader",
			 d3d11_misc::compile_hlsl_file(make_path(SOURCE_DIR, "triangle_ps.hlsl"), nullptr, "PS_main",
										   "ps_4_0", shader_flags, 0, app.ps_blob.GetAddressOf()));

	hr = app.d3d.dev->CreateVertexShader(app.vs_blob->GetBufferPointer(), app.vs_blob->GetBufferSize(),
										 nullptr, app.vs.GetAddressOf());
	hr = app.d3d.dev->CreatePixelShader(app.ps_blob->GetBufferPointer(), app.ps_blob->GetBufferSize(),
										nullptr, app.ps.GetAddressOf());

	D3D11_INPUT_ELEMENT_DESC layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(VertexData, position),
		 D3D11_INPUT_PER_VERTEX_DATA, 0},

		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VertexData, normal),
		 D3D11_INPUT_PER_VERTEX_DATA, 0},

		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(VertexData, texcoord),
		 D3D11_INPUT_PER_VERTEX_DATA, 0},
	};

	hr = app.d3d.dev->CreateInputLayout(layout, ARRAY_SIZE(layout), app.vs_blob->GetBufferPointer(),
										app.vs_blob->GetBufferSize(), app.vertex_layout.GetAddressOf());

	DXASSERT("create input layout", hr);

	app.d3d.imm->IASetInputLayout(app.vertex_layout.Get());

	d3d11_misc::set_debug_name(app.vs, "@usual_vs");
	d3d11_misc::set_debug_name(app.ps, "@usual_ps");

	{
		D3D11_BUFFER_DESC desc = {};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.ByteWidth = sizeof(vertices);
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

		D3D11_SUBRESOURCE_DATA sub = {};
		sub.pSysMem = vertices;

		hr = app.d3d.dev->CreateBuffer(&desc, &sub, app.vb.GetAddressOf());
		DXASSERT("create vertex buffer", hr);

		const u32 strides[] = {sizeof(VertexData)};
		const u32 offsets[] = {0u};

		auto vbptrs = scratch_vector<ID3D11Buffer *>({app.vb.Get()});
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
		LOG_F(INFO, "X(right): [%.3f, %.3f, %.3f], Y(up): [%.3f, %.3f, %.3f], Z(fwd): [%.3f, %.3f, %.3f]",
			  XM_XYZ(x), XM_XYZ(y), XM_XYZ(z));
	}

	// Constant buffer setup
	{
		D3D11_BUFFER_DESC desc = {};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.ByteWidth = sizeof(CBCameraTransform);

		DXASSERT("create constant buffer",
				 app.d3d.dev->CreateBuffer(&desc, nullptr, app.cb_per_camera.GetAddressOf()));

		d3d11_misc::set_debug_name(app.cb_per_camera, "cb_vs_per_object");

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		hr = app.d3d.imm->Map(app.cb_per_camera.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		memcpy(mapped.pData, &app.cbh_per_camera, sizeof(CBCameraTransform));
		app.d3d.imm->Unmap(app.cb_per_camera.Get(), 0);
	}

	// Set the shader programs
	app.d3d.imm->VSSetShader(app.vs.Get(), nullptr, 0);
	app.d3d.imm->PSSetShader(app.ps.Get(), nullptr, 0);

	{
		auto cb_list = scratch_vector<ID3D11Buffer *>({app.cb_per_camera.Get()});
		app.d3d.imm->VSSetConstantBuffers(0, 1, cb_list.data());
	}
}

template <> void update<App>(App &app, State &state)
{
	glfwPollEvents();

	handle_camera_input(app.d3d.window, app.camera, state.frame_time_in_sec);
	xmstore(app.camera.view_xform(), app.cbh_per_camera.view);

	if (glfwGetKey(app.d3d.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
		app.close = true;
	}
}

template <> void render<App>(App &app)
{
	app.d3d.imm->ClearRenderTargetView(app.d3d.rtv_screen.Get(), DirectX::Colors::AliceBlue);
	app.d3d.imm->ClearDepthStencilView(app.d3d.depth_stencil_view.Get(),
									   D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	static u32 capture_count = 0;

	if (capture_count < 3) {
		trigger_renderdoc_frame_capture(app.d3d);
		++capture_count;
	}

	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		app.d3d.imm->Map(app.cb_per_camera.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		memcpy(mapped.pData, &app.cbh_per_camera, sizeof(CBCameraTransform));
		app.d3d.imm->Unmap(app.cb_per_camera.Get(), 0);
	}

	app.d3d.imm->DrawIndexed(ARRAY_SIZE(indices), 0, 0);

	app.d3d.schain->Present(0, 0);
}

template <> void close<App>(App &app) {}

template <> bool should_close<App>(App &app) { return app.close || glfwWindowShouldClose(app.d3d.window); }

} // namespace app_loop

constexpr u32 WIDTH = 1024;
constexpr u32 HEIGHT = 640;

int main(int ac, char **av)
{
	memory_globals::init();
	DEFERSTAT(memory_globals::shutdown());
	init_pmr_globals();
	DEFERSTAT(shutdown_pmr_globals());

	rng::init_rng();

	d3dconf.window_width = WIDTH;
	d3dconf.window_height = HEIGHT;
	d3dconf.window_title = "d3d_test";
	d3dconf.load_renderdoc = true;

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

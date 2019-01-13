#include "d3d11_misc.h"

static UINT quality_levels_supported(ID3D11Device1 *dev, UINT msaa_sample_count, DXGI_FORMAT format)
{
	UINT sample_counts = msaa_sample_count;
	UINT num_msaa_levels = 0;
	DXASSERT("...", dev->CheckMultisampleQualityLevels(format, msaa_sample_count, &num_msaa_levels));
	return num_msaa_levels;
}

static ID3D11Device1 *d3d11_device;
static ID3D11DeviceContext1 *d3d11_device_context;

namespace d3d11_misc
{
	static void load_full_screen_quad();

	ID3D11Device *device() { return d3d11_device; }

	ID3D11DeviceContext1 *context() { return d3d11_device_context; }

	WindowContext init_d3d_window(const InitD3DConfig &config)
	{
		WindowContext d3d;

		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		d3d.window =
			glfwCreateWindow(config.window_width, config.window_height, config.window_title, nullptr, nullptr);

		d3d.win32_window = glfwGetWin32Window(d3d.window);

		HRESULT hr = S_OK;
		UINT create_device_flags = 0;

#if !defined(NDEBUG) || defined(DEBUG) || defined(_DEBUG)
		create_device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		D3D_DRIVER_TYPE driver_types[] = {
			D3D_DRIVER_TYPE_HARDWARE,
			D3D_DRIVER_TYPE_WARP,
			D3D_DRIVER_TYPE_REFERENCE,
		};

		UINT num_driver_types = ARRAY_SIZE(driver_types);

		D3D_FEATURE_LEVEL feature_levels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
		};

		const char *str_feature_levels[] = {
			"D3D 11.1",
			"D3D 11.0",
			"D3D 10.1",
			"D3D 10.0",
		};

		const u32 num_feature_levels = ARRAY_SIZE(feature_levels);

		for (u32 i = 0; i < num_driver_types; ++i) {
			d3d.driver_type = driver_types[i];
			hr = D3D11CreateDevice(nullptr,
														 d3d.driver_type,
														 nullptr,
														 create_device_flags,
														 feature_levels,
														 num_feature_levels,
														 D3D11_SDK_VERSION,
														 d3d.device.GetAddressOf(),
														 &d3d.feature_level,
														 d3d.immcontext.GetAddressOf());

			if (hr == E_INVALIDARG) {
				// DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we
				// need to retry without it
				hr = D3D11CreateDevice(nullptr,
															 d3d.driver_type,
															 nullptr,
															 create_device_flags,
															 &feature_levels[1],
															 num_feature_levels - 1,
															 D3D11_SDK_VERSION,
															 d3d.device.GetAddressOf(),
															 &d3d.feature_level,
															 d3d.immcontext.GetAddressOf());
			}

			if (SUCCEEDED(hr)) {
				break;
			}
		}

		DXASSERT("", hr);

		LOG_F(INFO, "Feature level = %s", str_feature_levels[d3d.feature_level - feature_levels[0]]);

		// Obtain DXGI factory from device (since we used nullptr for pAdapter above)
		IDXGIFactory1 *dxgiFactory = nullptr;
		{
			IDXGIDevice *dxgiDevice = nullptr;
			hr = d3d.device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgiDevice));
			if (SUCCEEDED(hr)) {
				IDXGIAdapter *adapter = nullptr;
				hr = dxgiDevice->GetAdapter(&adapter);
				if (SUCCEEDED(hr)) {
					hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&dxgiFactory));
					adapter->Release();
				}
				dxgiDevice->Release();
			}
		}

		DXASSERT("", hr);

		// Create swap chain
		IDXGIFactory2 *dxgiFactory2 = nullptr;
		hr = dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void **>(&dxgiFactory2));
		DXASSERT("", hr);

		if (dxgiFactory2) {
			// DirectX 11.1 or later
			hr = d3d.device->QueryInterface(__uuidof(ID3D11Device1),
																			reinterpret_cast<void **>(d3d.dev.GetAddressOf()));
			if (SUCCEEDED(hr)) {
				(void)d3d.immcontext->QueryInterface(__uuidof(ID3D11DeviceContext1),
																						 reinterpret_cast<void **>(d3d.imm.GetAddressOf()));
			}

			DXGI_SWAP_CHAIN_DESC1 sd;
			ZeroMemory(&sd, sizeof(sd));
			sd.Width = config.window_width;
			sd.Height = config.window_height;
			sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sd.SampleDesc.Count = config.msaa_sample_count;
			sd.SampleDesc.Quality = config.msaa_quality_level;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.BufferCount = 1;

			// Check msaa supported
			UINT msaa_levels = quality_levels_supported(d3d.dev.Get(), sd.SampleDesc.Count, sd.Format);
			CHECK_NE_F(msaa_levels,
								 0,
								 "MSAA not supported by hardware for format - "
								 "DXGI_FORMAT_R8G8B8A8_UNORM");

			hr = dxgiFactory2->CreateSwapChainForHwnd(
				d3d.device.Get(), d3d.win32_window, &sd, nullptr, nullptr, d3d.schain.GetAddressOf());

			if (SUCCEEDED(hr)) {
				hr = d3d.schain->QueryInterface(__uuidof(IDXGISwapChain),
																				reinterpret_cast<void **>(d3d.swapchain.GetAddressOf()));
			}

			dxgiFactory2->Release();
		} else {
			// DirectX 11.0 systems
			DXGI_SWAP_CHAIN_DESC sd;
			ZeroMemory(&sd, sizeof(sd));
			sd.BufferCount = 1;
			sd.BufferDesc.Width = config.window_width;
			sd.BufferDesc.Height = config.window_height;
			sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sd.BufferDesc.RefreshRate.Numerator = 60;
			sd.BufferDesc.RefreshRate.Denominator = 1;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.OutputWindow = d3d.win32_window;
			sd.SampleDesc.Count = config.msaa_sample_count;
			sd.SampleDesc.Quality = config.msaa_quality_level;
			sd.Windowed = TRUE;

			// Check msaa supported
			UINT msaa_levels = quality_levels_supported(d3d.dev.Get(), sd.SampleDesc.Count, sd.BufferDesc.Format);
			CHECK_NE_F(msaa_levels,
								 0,
								 "MSAA not supported by hardware for format - "
								 "DXGI_FORMAT_R8G8B8A8_UNORM");

			hr = dxgiFactory->CreateSwapChain(d3d.device.Get(), &sd, d3d.swapchain.GetAddressOf());
			DXASSERT("CreateSwapChain", hr);
		}

		d3d11_device = d3d.dev.Get();
		d3d11_device_context = d3d.imm.Get();

		// Create the rendertargetview for color output
		ComPtr<ID3D11Texture2D> back_buffer;
		DXASSERT("", d3d.swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)back_buffer.GetAddressOf()));
		DXASSERT("", d3d.device->CreateRenderTargetView(back_buffer.Get(), 0, d3d.rtv_screen.GetAddressOf()));

		// Create the depth+stencil buffer
		D3D11_TEXTURE2D_DESC depth_tex_desc;
		depth_tex_desc.Width = config.window_width;
		depth_tex_desc.Height = config.window_height;
		depth_tex_desc.MipLevels = 1;
		depth_tex_desc.ArraySize = 1;
		depth_tex_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depth_tex_desc.SampleDesc.Count = config.msaa_sample_count;
		depth_tex_desc.SampleDesc.Quality = config.msaa_quality_level;
		depth_tex_desc.Usage = D3D11_USAGE_DEFAULT;
		depth_tex_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		depth_tex_desc.MiscFlags = 0;
		depth_tex_desc.CPUAccessFlags = 0;

		// Check msaa supported
		UINT msaa_levels =
			quality_levels_supported(d3d.dev.Get(), depth_tex_desc.SampleDesc.Count, depth_tex_desc.Format);
		CHECK_NE_F(msaa_levels, 0, "MSAA not supported by hardware for format - DXGI_FORMAT_R8G8B8A8_UNORM");

		DXASSERT("Create depth-stencil texture",
						 d3d.dev->CreateTexture2D(&depth_tex_desc, nullptr, d3d.depth_stencil.GetAddressOf()));

		DXASSERT("Create depth-stencil view",
						 d3d.dev->CreateDepthStencilView(
							 d3d.depth_stencil.Get(), nullptr, d3d.depth_stencil_view.GetAddressOf()));

		// Bind the views to OM state
		std::array<ID3D11RenderTargetView *, 1> rtv_arr{ d3d.rtv_screen.Get() };
		d3d.imm->OMSetRenderTargets(1, rtv_arr.data(), d3d.depth_stencil_view.Get());

		// Set the viewport
		d3d.viewport.Width = (f32)config.window_width;
		d3d.viewport.Height = (f32)config.window_height;
		d3d.viewport.TopLeftX = 0.0f;
		d3d.viewport.TopLeftY = 0.0f;
		d3d.viewport.MinDepth = 0.0f;
		d3d.viewport.MaxDepth = 1.0f;
		d3d.imm->RSSetViewports(1, &d3d.viewport);
		LOG_F(INFO,
					"Initialized Direct3D 11.1, Width = %u, Height = %u, MSAA levels = %ux",
					config.window_width,
					config.window_height,
					config.msaa_sample_count);

		if (config.load_renderdoc) {
			::d3d11_misc::load_renderdoc(d3d, config.capture_path_template);
		}

		// load_full_screen_quad();

		return d3d;
	}

	void close_d3d_window(const InitD3DConfig &config)
	{
		(void)config;
		glfwTerminate();
	}

	void set_screen_as_render_target(WindowContext &d3d)
	{
		std::array<ID3D11RenderTargetView *, 1> rtv_arr{ d3d.rtv_screen.Get() };
		d3d.imm->OMSetRenderTargets(1, rtv_arr.data(), d3d.depth_stencil_view.Get());
	}

	HRESULT compile_hlsl_file(const fs::path path,
														D3D_SHADER_MACRO *macro_defines,
														const char *entry_point,
														const char *target,
														u32 flags1,
														u32 flags2,
														ID3DBlob **pp_code_blob)
	{
		HRESULT hr;

		if (!fs::exists(path)) {
			ABORT_F("File does not exist - '%s'", path.generic_u8string().c_str());
		}

#if defined(DEBUG) || defined(_DEBUG)
		// Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
		// Setting this flag improves the shader debugging experience, but still
		// allows the shaders to be optimized and to run exactly the way they will run
		// in the release configuration of this program.
		flags1 |= D3DCOMPILE_DEBUG;
#endif

		ComPtr<ID3DBlob> error_blob{ nullptr };

		hr = D3DCompileFromFile(path.c_str(),
														macro_defines,
														D3D_COMPILE_STANDARD_FILE_INCLUDE,
														entry_point,
														target,
														flags1,
														flags2,
														pp_code_blob,
														error_blob.GetAddressOf());

		if (error_blob) {
			LOG_F(ERROR, "%s", error_blob->GetBufferPointer());
		}

		return hr;
	}

	void
	set_viewport_dims(u32 width, u32 height, u32 topleft_x, u32 topleft_y, float min_depth, float max_depth)
	{
		D3D11_VIEWPORT viewport;

		viewport.TopLeftX = topleft_x;
		viewport.TopLeftY = topleft_y;
		viewport.Width = float(width);
		viewport.Height = float(height);
		viewport.MinDepth = min_depth;
		viewport.MaxDepth = max_depth;
		context()->RSSetViewports(1, &viewport);
	}

	std::aligned_storage_t<sizeof(FullScreenQuad)> _full_screen_quad_storage[1];

	FullScreenQuad &full_screen_quad()
	{
		return *reinterpret_cast<FullScreenQuad *>(_full_screen_quad_storage);
	}

	static void load_full_screen_quad()
	{
		HRESULT hr;

		new (&full_screen_quad()) FullScreenQuad;

		auto &fs = full_screen_quad();

		fo::Vector2 v0 = { -1.0f, 1.0f };
		fo::Vector2 v1 = { -1.0f, -1.0f };
		fo::Vector2 v2 = { 1.0f, -1.0f };
		fo::Vector2 v3 = { 1.0f, 1.0f };
		fo::Vector2 vertices[] = { v0, v1, v2, v0, v2, v3 };

		D3D11_BUFFER_DESC desc = {};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.ByteWidth = sizeof(vertices);
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

		D3D11_SUBRESOURCE_DATA sub = {};
		sub.pSysMem = vertices;

		hr = device()->CreateBuffer(&desc, &sub, fs.vb.GetAddressOf());
		DXASSERT("CreateBuffer", hr);

		D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};

		hr = device()->CreateInputLayout(layout, ARRAY_SIZE(layout), nullptr, 0, fs.pos2d_layout.GetAddressOf());
	}

} // namespace d3d11_misc

// SSAO demo. But uses the "rotate to random-basis with fragment normal being one of the axes" method. I would
// rather use the "reflect sample on a random plane" method.

#include "d3d11_misc.h"
#include "d3d_eye.h"

#include <learnogl/start.h>

#include <learnogl/gl_misc.h>

#include <learnogl/mesh.h>
#include <learnogl/random_sampling.h>

#include <AntTweakBar.h>

const int WIDTH = 800;
const int HEIGHT = 600;

#define DIRECTION_SAMPLES_ARRAY_LENGTH 16
#define RANDOM_ROTATIONS_TEX_SIZE 4

#define SSAO_BLUR_KERNEL_SIZE 11
#define SSAO_BLUR_KERNEL_XM4_COUNT (ceil_div(SSAO_BLUR_KERNEL_SIZE, 4))
// Don't tweak these values

d3d11_misc::InitD3DConfig d3dconf;

// Returns a gaussian blur kernel, with mu = 0, and given sigma.
fo::Vector<f32> generate_blur_weights(f32 sigma)
{
	constexpr int max_blur_radius = 5;

	// Estimate the kernel's radius from the sigma of the underlying normal distribution.
	int blur_radius = (int)ceil(2.0f * sigma);

	assert(blur_radius <= max_blur_radius);

	fo::Vector<f32> weights;
	fo::reserve(weights, 32);
	// ^ Conservatively allocate some space because we will pack the weights into float4

	// weights.resize(2 * blur_radius + 1);

	float sum = 0.0f;

	for (int i = -blur_radius; i <= blur_radius; ++i) {
		float x = (float)i;

		fo::push_back(weights, expf(-x * x / (2.0f * sigma * sigma)));

		sum += weights[i + blur_radius];
	}

	// Divide by the sum so all the weights add up to 1.0.
	for (u32 i = 0; i < fo::size(weights); ++i) {
		weights[i] /= sum;

		LOG_F(INFO, "weight %i = %f", i, weights[i]);
	}

	return weights;
}

struct CBCameraTransform {
	xm44 view;
	xm44 proj;
};

ComPtr<ID3D11Buffer> create_dynamic_cbuffer(size_t size, const char *debug_name = nullptr)
{
	D3D11_BUFFER_DESC desc = {};
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.ByteWidth = size;

	ComPtr<ID3D11Buffer> ptr;

	DXASSERT("create_dynamic_cbuffer", d3d11_misc::device()->CreateBuffer(&desc, nullptr, ptr.GetAddressOf()));

	if (debug_name) {
		d3d11_misc::set_debug_name(ptr, debug_name);
	}

	return ptr;
}

void source_into_constant_buffer(ID3D11Buffer *cb, void *data, size_t size)
{
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	DXASSERT("source_into_constant_buffer",
					 d3d11_misc::context()->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	memcpy(mapped.pData, data, size);
	d3d11_misc::context()->Unmap(cb, 0);
}

struct D3DShader {
	using VertexShaderPtr = ComPtr<ID3D11VertexShader>;
	using PixelShaderPtr = ComPtr<ID3D11PixelShader>;

	using ShaderVariant = ::VariantTable<VertexShaderPtr, PixelShaderPtr>;

	ComPtr<ID3D10Blob> _blob;
	ShaderVariant _shader;

	bool is_created() const
	{
		if (type_index(_shader) == vt_index<ShaderVariant, VertexShaderPtr>) {
			return get_value<VertexShaderPtr>(_shader).Get() != nullptr;
		}
		if (type_index(_shader) == vt_index<ShaderVariant, PixelShaderPtr>) {
			return get_value<PixelShaderPtr>(_shader).Get() != nullptr;
		}

		assert(false);

		return false;
	}

	template <typename ShaderInterface> ShaderInterface *get()
	{
		static_assert(one_of_type_v<ShaderInterface, ID3D11VertexShader, ID3D11PixelShader>,
									"Must be a ID3D11{...}Shader");

		return get_value<ComPtr<ShaderInterface>>(_shader).Get();
	}

	ID3D11VertexShader *vs() { return get<ID3D11VertexShader>(); }
	ID3D11PixelShader *ps() { return get<ID3D11PixelShader>(); }

	const char *default_shader_model(D3DShaderKind shader_kind)
	{
		if (shader_kind == D3DShaderKind::VERTEX_SHADER) {
			return "vs_4_0";
		}
		if (shader_kind == D3DShaderKind::PIXEL_SHADER) {
			return "ps_4_0";
		}
		return nullptr;
	}

	ID3D10Blob *blob() { return _blob.Get(); }

	HRESULT compile_hlsl_file(const fs::path &hlsl_file,
														D3DShaderKind shader_kind,
														const char *entry_point,
														D3D_SHADER_MACRO *macros = nullptr,
														const char *shader_model = nullptr)
	{
		assert(!is_created());

		HRESULT hr = S_OK;

		DWORD shader_flags = D3DCOMPILE_ENABLE_STRICTNESS;

#if defined(_DEBUG) || !defined(NDEBUG)
		shader_flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

		if (!shader_model) {
			shader_model = default_shader_model(shader_kind);
		}

		DXASSERT("compile shader",
						 d3d11_misc::compile_hlsl_file(
							 hlsl_file, macros, entry_point, shader_model, shader_flags, 0, _blob.GetAddressOf()));

		if (shader_kind == D3DShaderKind::VERTEX_SHADER) {
			_shader = ComPtr<ID3D11VertexShader>(nullptr);
			auto &s = get_value<ComPtr<ID3D11VertexShader>>(_shader);
			hr = d3d11_misc::device()->CreateVertexShader(
				_blob->GetBufferPointer(), _blob->GetBufferSize(), nullptr, s.GetAddressOf());

		} else if (shader_kind == D3DShaderKind::PIXEL_SHADER) {
			_shader = ComPtr<ID3D11PixelShader>(nullptr);
			auto &s = get_value<ComPtr<ID3D11PixelShader>>(_shader);
			hr = d3d11_misc::device()->CreatePixelShader(
				_blob->GetBufferPointer(), _blob->GetBufferSize(), nullptr, s.GetAddressOf());

		} else {
			assert(false);
		}

		return hr;
	}
};

struct SingleMipTexture2D {
	ComPtr<ID3D11Texture2D> _tex;
	ComPtr<ID3D11ShaderResourceView> _srv;

	virtual int _bind_flags() { return D3D11_BIND_SHADER_RESOURCE; }

	bool is_created() const { return _tex.Get() != nullptr; }

	ID3D11Texture2D *tex() { return _tex.Get(); }
	ID3D11ShaderResourceView *srv() { return _srv.Get(); }

	void create(u32 width,
							u32 height,
							DXGI_FORMAT texture_format,
							DXGI_FORMAT srv_format,
							const D3D11_SUBRESOURCE_DATA *subdata,
							const char *debug_name = nullptr)
	{
		assert(!is_created());

		D3D11_TEXTURE2D_DESC texdesc;

		// Create texture object
		texdesc.Width = width;
		texdesc.Height = height;
		texdesc.MipLevels = 1;
		texdesc.ArraySize = 1;
		texdesc.SampleDesc.Count = 1;
		texdesc.SampleDesc.Quality = 0;
		texdesc.Format = texture_format;
		texdesc.Usage = D3D11_USAGE_DEFAULT;
		texdesc.CPUAccessFlags = 0;
		texdesc.MiscFlags = 0;

		texdesc.BindFlags = _bind_flags();

		DXASSERT("", d3d11_misc::device()->CreateTexture2D(&texdesc, subdata, _tex.GetAddressOf()));

		if (debug_name) {
			d3d11_misc::set_debug_name(_tex, debug_name);
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
		srv_desc.Format = srv_format;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Texture2D.MipLevels = 1;

		DXASSERT("", d3d11_misc::device()->CreateShaderResourceView(_tex.Get(), &srv_desc, _srv.GetAddressOf()));
	}
};

struct RenderTexture2D : SingleMipTexture2D {
	ComPtr<ID3D11RenderTargetView> _rtv;

	virtual int _bind_flags() override { return D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; }

	ID3D11RenderTargetView *rtv() { return _rtv.Get(); }

	void create(u32 width,
							u32 height,
							DXGI_FORMAT texture_format,
							DXGI_FORMAT rtv_format,
							DXGI_FORMAT srv_format,
							const char *debug_name = nullptr)
	{
		SingleMipTexture2D::create(width, height, texture_format, srv_format, nullptr, debug_name);

		D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
		rtv_desc.Format = rtv_format;
		rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtv_desc.Texture2D.MipSlice = 0;

		DXASSERT("", d3d11_misc::device()->CreateRenderTargetView(_tex.Get(), &rtv_desc, _rtv.GetAddressOf()));
	}
};

struct DepthRenderTexture : SingleMipTexture2D {
	ComPtr<ID3D11DepthStencilView> _dsv;

	virtual int _bind_flags() { return D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE; }

	ID3D11DepthStencilView *dsv() { return _dsv.Get(); }

	void create(u32 width,
							u32 height,
							DXGI_FORMAT texture_format,
							DXGI_FORMAT dsv_format,
							DXGI_FORMAT srv_format,
							const char *debug_name = nullptr)
	{
		assert(!is_created());

		SingleMipTexture2D::create(width, height, texture_format, srv_format, nullptr, debug_name);

		D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc;
		dsv_desc.Format = dsv_format;
		dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		dsv_desc.Flags = 0;
		dsv_desc.Texture2D.MipSlice = 0;

		DXASSERT("", d3d11_misc::device()->CreateDepthStencilView(_tex.Get(), &dsv_desc, _dsv.GetAddressOf()));
	}
};

struct SsaoTextures {
	// First pass we record view space normals, and depth.
	RenderTexture2D normals;
	DepthRenderTexture normalized_depth;

	// A really small texture containing 2 component unit vectors denoting vector on the xy plane. Used to
	// rotate the hemisphere sampling kernel.
	SingleMipTexture2D rnd_xy_vector_tex;

	// A render target where the occlusion factor will be output to.
	RenderTexture2D occlusion_factor_output;

	// Two ping-pong render targets where the occlusion factor will be blurred using a separable filter.
	// @comeback - We don't need two here. Just use the occlusion_factor_output itself.
	RenderTexture2D blurred_output_0;
	RenderTexture2D blurred_output_1;

	ComPtr<ID3D11SamplerState> point_sampler;
	ComPtr<ID3D11SamplerState> random_directions_sampler;
	ComPtr<ID3D11SamplerState> linear_sampler;

	u32 scene_width = 0, scene_height = 0;
	u32 ao_width = 0, ao_height = 0;

	void create(u32 width,
							u32 height,
							u32 num_random_xy_vectors,
							const char *debug_name0 = "@normals_texture",
							const char *debug_name1 = "@normalized_depth")
	{
		assert(width % num_random_xy_vectors == 0);
		assert(height % num_random_xy_vectors == 0);

		scene_width = width;
		scene_height = height;

		// Create the render targets

		normals.create(width,
									 height,
									 DXGI_FORMAT_R16G16B16A16_FLOAT,
									 DXGI_FORMAT_R16G16B16A16_FLOAT,
									 DXGI_FORMAT_R16G16B16A16_FLOAT,
									 debug_name0);

		normalized_depth.create(width,
														height,
														DXGI_FORMAT_R24G8_TYPELESS,
														DXGI_FORMAT_D24_UNORM_S8_UINT,
														DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
														debug_name1);

		// ao_width = width / 4;
		// ao_height = height / 4;

		ao_width = width;
		ao_height = height;

		// Using fp16 format for storing occlusion factor
		const auto OCCLUSION_OUTPUT_FORMAT = DXGI_FORMAT_R16_FLOAT;

		occlusion_factor_output.create(ao_width,
																	 ao_height,
																	 OCCLUSION_OUTPUT_FORMAT,
																	 OCCLUSION_OUTPUT_FORMAT,
																	 OCCLUSION_OUTPUT_FORMAT,
																	 "@ao_output_texture");

		blurred_output_0.create(ao_width,
														ao_height,
														OCCLUSION_OUTPUT_FORMAT,
														OCCLUSION_OUTPUT_FORMAT,
														OCCLUSION_OUTPUT_FORMAT,
														"@ao_blur_0");

		blurred_output_1.create(ao_width,
														ao_height,
														OCCLUSION_OUTPUT_FORMAT,
														OCCLUSION_OUTPUT_FORMAT,
														OCCLUSION_OUTPUT_FORMAT,
														"@ao_blur_1");

		// Create the samplers.
		{
			// Usual normals and depth texture sampler. @comeback - Should be using border here with the max depth
			// value so that there is no occlusion based darkening at the edges of the screen.
			CD3D11_SAMPLER_DESC csamplerdesc(CD3D11_DEFAULT{});
			csamplerdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			csamplerdesc.AddressU = csamplerdesc.AddressV = csamplerdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

			D3D11_SAMPLER_DESC samplerdesc = (D3D11_SAMPLER_DESC)csamplerdesc;

			DXASSERT("CreateSamplerState",
							 d3d11_misc::device()->CreateSamplerState(&samplerdesc, point_sampler.GetAddressOf()));

			// Random texture sampler. Using wrap mode for 'tiling' the random texture over the full normals
			// texture.
			csamplerdesc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT{});
			// csamplerdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			csamplerdesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			csamplerdesc.AddressU = csamplerdesc.AddressV = csamplerdesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

			samplerdesc = (D3D11_SAMPLER_DESC)csamplerdesc;

			DXASSERT(
				"CreateSamplerState",
				d3d11_misc::device()->CreateSamplerState(&samplerdesc, random_directions_sampler.GetAddressOf()));

			// Linear sampler.
			csamplerdesc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT{});
			csamplerdesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			csamplerdesc.AddressU = csamplerdesc.AddressV = csamplerdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

			samplerdesc = (D3D11_SAMPLER_DESC)csamplerdesc;

			DXASSERT("CreateSamplerState",
							 d3d11_misc::device()->CreateSamplerState(&samplerdesc, linear_sampler.GetAddressOf()));
		}

		_create_random_xy_vectors(num_random_xy_vectors);
	}

	void _create_random_xy_vectors(u32 num_random_xy_vectors)
	{
		// Create the random xy-plane vectors texture
		{
			const u32 W = num_random_xy_vectors;
			const u32 H = num_random_xy_vectors;

			fo::TempAllocator1024 ta;
			fo::Vector<RGBA8> axis(W * H, ta);

			for (int i = 0; i < W; ++i) {
				for (int j = 0; j < H; ++j) {
					// This is not uniformly distributed, but whatever.
					axis[i * W + j] = { u8(rng::random(0, 256)), u8(rng::random(0, 256)), 0, 255u };
				}
			}

			D3D11_SUBRESOURCE_DATA subdata;
			subdata.pSysMem = fo::data(axis);
			subdata.SysMemPitch = W * sizeof(RGBA8);
			subdata.SysMemSlicePitch = H * subdata.SysMemPitch;

			rnd_xy_vector_tex.create(W,
															 H,
															 DXGI_FORMAT_R8G8B8A8_UNORM, // @comeback - Use 2 component format here?
															 DXGI_FORMAT_R8G8B8A8_UNORM,
															 constptr(subdata),
															 "@rnd_vectors_texture");
		}
	}

	// Set as render target and clear the buffers
	void set_normals_texture_as_render_target()
	{
		ID3D11RenderTargetView *rtv_array[] = { normals.rtv() };
		d3d11_misc::context()->OMSetRenderTargets(1, rtv_array, normalized_depth.dsv());

		f32 farthest_depth = 1000.0f; // wrt view space
		const f32 clear_value[] = { 0.f, 0.f, 0.f, farthest_depth };

		d3d11_misc::context()->ClearRenderTargetView(normals.rtv(), clear_value);
		d3d11_misc::context()->ClearDepthStencilView(
			normalized_depth.dsv(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	}

	// Bind the srv of the normals and normalized_depth textures to the pixel shader texture registers
	// t0 and t1.
	void bind_ssao_build_pass_srvs()
	{
		ID3D11ShaderResourceView *srv_array[] = { normals.srv(),
																							normalized_depth.srv(),
																							rnd_xy_vector_tex.srv() };

		d3d11_misc::context()->PSSetShaderResources(0, 3, srv_array);
		ID3D11SamplerState *samplers_array[] = { point_sampler.Get(), random_directions_sampler.Get() };
		d3d11_misc::context()->PSSetSamplers(0, 2, samplers_array);
	}

	void set_occlusion_output_as_render_target()
	{
		d3d11_misc::set_viewport_dims(ao_width, ao_height);

		ID3D11RenderTargetView *rtv_array[] = { occlusion_factor_output.rtv() };
		d3d11_misc::context()->OMSetRenderTargets(ARRAY_SIZE(rtv_array), rtv_array, nullptr); // No depth buffer
	}

	void set_rtv_and_srv_for_horizontal_blur()
	{
		d3d11_misc::set_viewport_dims(ao_width, ao_height);

		ID3D11RenderTargetView *rtv_array[] = { blurred_output_0.rtv() };
		d3d11_misc::context()->OMSetRenderTargets(ARRAY_SIZE(rtv_array), rtv_array, nullptr);
		// ^ No depth buffer

		ID3D11ShaderResourceView *srv_array[] = { normals.srv(),
																							normalized_depth.srv(),
																							occlusion_factor_output.srv() };

		d3d11_misc::context()->PSSetShaderResources(0, ARRAY_SIZE(srv_array), srv_array);

		ID3D11SamplerState *samplers_array[] = { point_sampler.Get() };
		d3d11_misc::context()->PSSetSamplers(0, 1, samplers_array);
	}

	void set_rtv_and_srv_for_vertical_blur()
	{
		d3d11_misc::set_viewport_dims(ao_width, ao_height);

		ID3D11RenderTargetView *rtv_array[] = { blurred_output_1.rtv() };
		d3d11_misc::context()->OMSetRenderTargets(ARRAY_SIZE(rtv_array), rtv_array, nullptr); // No depth buffer

		ID3D11ShaderResourceView *srv_array[] = { normals.srv(), normalized_depth.srv(), blurred_output_0.srv() };

		d3d11_misc::context()->PSSetShaderResources(0, ARRAY_SIZE(srv_array), srv_array);

		ID3D11SamplerState *samplers_array[] = { point_sampler.Get() };
		d3d11_misc::context()->PSSetSamplers(0, 1, samplers_array);
	}
};

constexpr bool ignore_direction_samples_length = false;

fo::Vector<xm4> generate_direction_samples(u32 count, f32 min_length, f32 max_length)
{
	fo::Vector<xm4> list;
	fo::reserve(list, count);

	for (u32 i = 0; i < count; ++i) {
		fo::Vector3 direction = random_point_on_hemisphere(1.0f);

		// My random_point_on_hemisphere returns a point where the hemisphere's axis is oriented along the y axis.
		// I swap the coordinates so that the z component contains the y component's value instead.
		std::swap(direction.z, direction.y);
		std::swap(direction.y, direction.x);

		xmv d = XMVectorSet(direction.x, direction.y, direction.z, 0.0f);

		if (!ignore_direction_samples_length) {
			f32 interpolant = (f32)rng::random();
			d = XMVectorScale(d, eng::math::lerp(min_length, max_length, interpolant * interpolant));
		}

		d = XMVector3Normalize(d);

		LOG_F(INFO, "Direction %i = [%.5f, %.5f, %.5f]", i, XM_XYZ(d));

		xmstore(d, push_back_get(list, {}));
	}

	return list;
}

struct SSAODemo {
	d3d11_misc::WindowContext d3d;

	struct {
		D3DShader render_usual_vs;
		D3DShader render_usual_ps;

		D3DShader record_normals_ps;

		D3DShader screen_quad_vs;
		D3DShader debug_ps;
		D3DShader ssao_sampling_ps;

		D3DShader ssao_blur_ps;

		D3DShader draw_with_ssao_ps;
	} shaders;

	SsaoTextures ssao_render_target;
	ComPtr<ID3D11Buffer> cb_ssao_params;
	ComPtr<ID3D11Buffer> cb_ssao_blur_params;

	// SsaoTextures dbg_render_target;
	// FullScreenQuad fs;

	ComPtr<ID3D11InputLayout> pos_normal_inputlayout;
	ComPtr<ID3D11Buffer> mesh_vb;
	ComPtr<ID3D11Buffer> mesh_ib;
	eng::mesh::StrippedMeshData mesh_data;

	struct {
		ComPtr<ID3D11RasterizerState> rs_opaque;
		ComPtr<ID3D11DepthStencilState> ds_yes_depth_test;
		ComPtr<ID3D11DepthStencilState> ds_no_depth_test;
	} render_states;

	D3DCamera camera;
	CBCameraTransform cbstruct_camera;
	ComPtr<ID3D11Buffer> cb_camera;

	xm4 default_mesh_color = xm4(0.7f, 0.7f, 0.7f, 1.0f);
	bool using_ao_pass = true;

	u32 num_random_xy_vectors = RANDOM_ROTATIONS_TEX_SIZE;

	bool close = false;
};

struct alignas(16) SSAOBuildPassCB {
	xm4 g_direction_samples[DIRECTION_SAMPLES_ARRAY_LENGTH];
	xm4 g_blur_weights[SSAO_BLUR_KERNEL_XM4_COUNT];

	float g_near_z;
	float g_far_z;
	float g_tan_half_yfov;
	float g_tan_half_xfov;

	fo::Vector2 g_scene_textures_wh;
	fo::Vector2 g_rnd_xy_texture_wh;
	fo::Vector2 g_occlusion_texture_wh;

	float max_z_offset = 0.2f; // Maximum offset along view space z to
	float g_surface_epsilon = 0.5f;
};

struct alignas(16) SSAOBlurParamsCB {
	u32 is_horizontal_or_vertical = 0;
};

// GeneralNote - Although we *could* just reconstruct the view space position from normalized depth, we are
// storing the view space depth in that normals render target, for a tiny bit of simpler math.

TwBar *twbar;
u32 direction_kernel_size;

namespace app_loop
{
	template <> void init<SSAODemo>(SSAODemo &app)
	{
		TwInit(TW_DIRECT3D11, d3d11_misc::device());
		TwWindowSize(d3dconf.window_width, d3dconf.window_height);

		twbar = TwNewBar("New Tweak Bar");
		TwAddVarRW(twbar,
							 "Num_Directions",
							 TW_TYPE_UINT32,
							 &direction_kernel_size,
							 "Number of direction samples to generate");

		HRESULT hr;

		// Load compute and graphics shaders
		{
			auto defines = eng::scratch_vector<D3D_SHADER_MACRO>();
			defines.reserve(8);

			std::string str_num_ssao_direction_samples = std::to_string(DIRECTION_SAMPLES_ARRAY_LENGTH);
			defines.push_back({ "DIRECTION_SAMPLES_ARRAY_LENGTH", str_num_ssao_direction_samples.c_str() });

			std::string str_ssao_blur_kernel_size = std::to_string(SSAO_BLUR_KERNEL_SIZE);
			defines.push_back({ "SSAO_BLUR_KERNEL_SIZE", str_ssao_blur_kernel_size.c_str() });

			std::string str_ssao_blur_kernel_xm4_count = std::to_string(SSAO_BLUR_KERNEL_XM4_COUNT);
			defines.push_back({ "SSAO_BLUR_KERNEL_XM4_COUNT", str_ssao_blur_kernel_xm4_count.c_str() });

			defines.push_back({ nullptr, nullptr });

			app.shaders.render_usual_vs.compile_hlsl_file(
				make_path(SOURCE_DIR, "usual_vs.hlsl"), D3DShaderKind::VERTEX_SHADER, "VS_main", defines.data());

			app.shaders.render_usual_ps.compile_hlsl_file(
				make_path(SOURCE_DIR, "usual_ps.hlsl"), D3DShaderKind::PIXEL_SHADER, "PS_main", defines.data());

			app.shaders.record_normals_ps.compile_hlsl_file(
				make_path(SOURCE_DIR, "record_normals_and_depth_ps.hlsl"),
				D3DShaderKind::PIXEL_SHADER,
				"PS_main",
				defines.data());

			app.shaders.screen_quad_vs.compile_hlsl_file(make_path(SOURCE_DIR, "full_screen_quad_vs.hlsl"),
																									 D3DShaderKind::VERTEX_SHADER,
																									 "VS_main",
																									 defines.data());

			app.shaders.debug_ps.compile_hlsl_file(make_path(SOURCE_DIR, "ssao_sampling_ps.hlsl"),
																						 D3DShaderKind::PIXEL_SHADER,
																						 "PS_main",
																						 defines.data());

			app.shaders.ssao_sampling_ps.compile_hlsl_file(make_path(SOURCE_DIR, "ssao_sampling_ps.hlsl"),
																										 D3DShaderKind::PIXEL_SHADER,
																										 "PS_ao_main",
																										 defines.data());

			app.shaders.ssao_blur_ps.compile_hlsl_file(make_path(SOURCE_DIR, "ssao_blur_pass_ps.hlsl"),
																								 D3DShaderKind::PIXEL_SHADER,
																								 "PS_main",
																								 defines.data());

			app.shaders.draw_with_ssao_ps.compile_hlsl_file(make_path(SOURCE_DIR, "draw_with_ssao_ps.hlsl"),
																											D3DShaderKind::PIXEL_SHADER,
																											"PS_main",
																											defines.data());
		}

		// Create render targets
		{
			app.ssao_render_target.create(d3dconf.window_width, d3dconf.window_height, app.num_random_xy_vectors);

#if 0
			app.dbg_render_target.create(
				d3dconf.window_width, d3dconf.window_height, "@rgba16f_debug", "@depth_stencil_debug");
#endif
		}

		// Load mesh
		eng::mesh::Model tri_mesh;

// Load the mesh
#if 0
		eng::mesh::load(tri_mesh, make_path(RESOURCES_DIR, "isosurface.obj").generic_u8string().c_str());
		// eng::load_sphere_mesh(tri_mesh);
#else
		// CHECK_F(eng::mesh::load(tri_mesh, make_path(RESOURCES_DIR, "arnold.obj").generic_u8string().c_str()));

		std::string objfile = make_path(RESOURCES_DIR, "arnold.obj").generic_u8string();

		auto rot_90_y = eng::math::rotation_about_y(180.0f * eng::math::one_deg_in_rad);

		eng::mesh::load_then_transform(tri_mesh,
																	 objfile.c_str(),
																	 {},
																	 mesh::ModelLoadFlagBits::TRIANGULATE |
																		 mesh::ModelLoadFlagBits::CALC_NORMALS,
																	 rot_90_y);

#endif

		LOG_F(INFO, "Loaded mesh");
		app.mesh_data = eng::mesh::StrippedMeshData(tri_mesh[0]);

		app.camera.set_look_at(xmload(xm3(0.0f, 0.0f, -2.0f)), xm_origin(), xm_unit_y());
		app.camera.set_proj(0.5f, 4000.0f, XM_PI / 4.0f, float(d3dconf.window_width) / d3dconf.window_height);

		CHECK_EQ_F(app.camera._proj_xform._34, 1.0f);

		xmstore(app.camera.view_xform(), app.cbstruct_camera.view);
		xmstore(app.camera.proj_xform(), app.cbstruct_camera.proj);

		// Create camera cbuffer
		{
			app.cb_camera = create_dynamic_cbuffer(sizeof(CBCameraTransform), "@cb_camera");
			source_into_constant_buffer(app.cb_camera.Get(), &app.cbstruct_camera, sizeof(CBCameraTransform));
		}

		// Create ssao related constant buffer
		{
			app.cb_ssao_params = create_dynamic_cbuffer(sizeof(SSAOBuildPassCB), "@cb_ssao_params");

			// This constant buffer data won't change (i.e. until we dynamically play with the scene's ssao
			// parameters). So setting default values here.

			SSAOBuildPassCB constants;
			constants.g_near_z = app.camera.near_z();
			constants.g_far_z = app.camera.far_z();
			constants.g_tan_half_xfov = app.camera.tan_half_xfov();
			constants.g_tan_half_yfov = app.camera.tan_half_yfov();
			constants.g_scene_textures_wh = { (f32)d3dconf.window_width, (f32)d3dconf.window_height };
			constants.g_rnd_xy_texture_wh = { (f32)app.num_random_xy_vectors, (f32)app.num_random_xy_vectors };
			constants.g_occlusion_texture_wh = { (f32)app.ssao_render_target.ao_width,
																					 (f32)app.ssao_render_target.ao_width };

			const f32 min_length = 0.02f;
			const f32 max_length = 0.05f;
			fo::Vector<xm4> directions =
				generate_direction_samples(DIRECTION_SAMPLES_ARRAY_LENGTH, min_length, max_length);
			std::copy(directions.begin(), directions.end(), constants.g_direction_samples);

			// Redundant, considering we never change these values.
			const float sigma = 2.5f;
			auto kernel_weights = generate_blur_weights(sigma);

			assert(fo::size(kernel_weights) == SSAO_BLUR_KERNEL_SIZE);
			static_assert(SSAO_BLUR_KERNEL_SIZE == 11, "");

			constants.g_blur_weights[0] = xm4(&kernel_weights[0]);
			constants.g_blur_weights[1] = xm4(&kernel_weights[4]);
			constants.g_blur_weights[2] = xm4(&kernel_weights[8]);

			source_into_constant_buffer(app.cb_ssao_params.Get(), &constants, sizeof(SSAOBuildPassCB));
		}

		// Create the blur pass constant buffer
		{
			app.cb_ssao_blur_params = create_dynamic_cbuffer(sizeof(SSAOBlurParamsCB), "@cb_ssao_blur_params");
		}

		D3D11_INPUT_ELEMENT_DESC layout[] = { { "POSITION",
																						0,
																						DXGI_FORMAT_R32G32B32_FLOAT,
																						0,
																						app.mesh_data.position_offset,
																						D3D11_INPUT_PER_VERTEX_DATA,
																						0 },
																					{ "NORMAL",
																						0,
																						DXGI_FORMAT_R32G32B32_FLOAT,
																						0,
																						app.mesh_data.normal_offset,
																						D3D11_INPUT_PER_VERTEX_DATA,
																						0 } };

		hr = app.d3d.dev->CreateInputLayout(layout,
																				ARRAY_SIZE(layout),
																				app.shaders.render_usual_vs.blob()->GetBufferPointer(),
																				app.shaders.render_usual_vs.blob()->GetBufferSize(),
																				app.pos_normal_inputlayout.GetAddressOf());

		DXASSERT("CreateInputLayout", hr);

		auto &mesh_data = tri_mesh[0];

		// Mesh gpu buffers
		{
			D3D11_BUFFER_DESC desc = {};
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.ByteWidth = eng::mesh::vertex_buffer_size(mesh_data);
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

			D3D11_SUBRESOURCE_DATA sub = {};
			sub.pSysMem = eng::mesh::vertices(mesh_data);

			hr = app.d3d.dev->CreateBuffer(&desc, &sub, app.mesh_vb.GetAddressOf());
			DXASSERT("create vertex buffer", hr);
		}

		{
			D3D11_BUFFER_DESC desc = {};
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.ByteWidth = eng::mesh::index_buffer_size(mesh_data);
			desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

			D3D11_SUBRESOURCE_DATA sub = {};
			sub.pSysMem = eng::mesh::indices(mesh_data);

			hr = app.d3d.dev->CreateBuffer(&desc, &sub, app.mesh_ib.GetAddressOf());
			DXASSERT("create index buffer", hr);
		}

		{
			// Rasterizer state
			D3D11_RASTERIZER_DESC rs_desc = {};
			rs_desc.FrontCounterClockwise = true;
			rs_desc.CullMode = D3D11_CULL_NONE;
			rs_desc.FillMode = D3D11_FILL_SOLID;
			rs_desc.MultisampleEnable = true;
			app.d3d.dev->CreateRasterizerState(&rs_desc, app.render_states.rs_opaque.GetAddressOf());

			// No depth test
			CD3D11_DEPTH_STENCIL_DESC cdesc(CD3D11_DEFAULT{});
			cdesc.DepthEnable = FALSE;
			D3D11_DEPTH_STENCIL_DESC desc = (D3D11_DEPTH_STENCIL_DESC)cdesc;
			DXASSERT("CreateDepthStencilState",
							 d3d11_misc::device()->CreateDepthStencilState(
								 &desc, app.render_states.ds_no_depth_test.GetAddressOf()));

			// Yes depth test
			cdesc = CD3D11_DEPTH_STENCIL_DESC(CD3D11_DEFAULT{});
			desc = (D3D11_DEPTH_STENCIL_DESC)cdesc;
			DXASSERT("CreateDepthStencilState",
							 d3d11_misc::device()->CreateDepthStencilState(
								 &desc, app.render_states.ds_yes_depth_test.GetAddressOf()));
		}

		LOG_F(INFO, "Init completed");
	}

	template <> void update<SSAODemo>(SSAODemo &app, State &state)
	{
		glfwPollEvents();

		handle_camera_input(app.d3d.window, app.camera, state.frame_time_in_sec);
		xmstore(app.camera.view_xform(), app.cbstruct_camera.view);

		if (glfwGetKey(app.d3d.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
			app.close = true;
		}

		if (glfwGetKey(app.d3d.window, GLFW_KEY_T) == GLFW_PRESS) {
			app.using_ao_pass = !app.using_ao_pass;
		}
	}

	enum RenderKind {
		WITH_SSAO,
		WITHOUT_SSAO,
	};

	static RenderKind render_kind = WITH_SSAO;

	static void render_without_ssao(SSAODemo &app)
	{
		d3d11_misc::context()->ClearRenderTargetView(app.d3d.rtv_screen.Get(), DirectX::Colors::AliceBlue);
		d3d11_misc::context()->ClearDepthStencilView(
			app.d3d.depth_stencil_view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		// Enable depth test
		d3d11_misc::context()->OMSetDepthStencilState(app.render_states.ds_yes_depth_test.Get(), 0);

		// Set shaders
		d3d11_misc::context()->VSSetShader(app.shaders.render_usual_vs.vs(), nullptr, 0);
		d3d11_misc::context()->PSSetShader(app.shaders.render_usual_ps.ps(), nullptr, 0);

		// Source camera matrix
		{
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			d3d11_misc::context()->Map(app.cb_camera.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
			memcpy(mapped.pData, &app.cbstruct_camera, sizeof(CBCameraTransform));
			d3d11_misc::context()->Unmap(app.cb_camera.Get(), 0);

			auto cb_array = scratch_vector<ID3D11Buffer *>({ app.cb_camera.Get() });
			d3d11_misc::context()->VSSetConstantBuffers(0, 1, cb_array.data());
		}

		d3d11_misc::context()->RSSetState(app.render_states.rs_opaque.Get());
		d3d11_misc::context()->IASetInputLayout(app.pos_normal_inputlayout.Get());

		// Set IA buffers
		{
			ID3D11Buffer *vb_at_slot[] = { app.mesh_vb.Get() };
			u32 strides[] = { app.mesh_data.packed_attr_size };
			u32 start_offsets[] = { 0u };

			d3d11_misc::context()->IASetVertexBuffers(0, 1, vb_at_slot, strides, start_offsets);
			d3d11_misc::context()->IASetIndexBuffer(app.mesh_ib.Get(), DXGI_FORMAT_R16_UINT, 0);
		}

		d3d11_misc::context()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Draw
		d3d11_misc::context()->DrawIndexed(eng::mesh::num_indices(app.mesh_data), 0, 0);

		app.d3d.schain->Present(0, 0);
	}

	static void render_with_ssao(SSAODemo &app)
	{
		LOCAL_FUNC set_full_screen_triangle_vb_ib = []() {
			d3d11_misc::context()->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
			d3d11_misc::context()->IASetIndexBuffer(nullptr, DXGI_FORMAT(0), 0);
			d3d11_misc::context()->IASetInputLayout(nullptr);

			d3d11_misc::context()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		};

		d3d11_misc::set_viewport_dims(d3dconf.window_width, d3dconf.window_height);

		// Pass 0, record scene's normals and depth in view space. Usually in a deferred scheme, I would do this
		// anyway.
		app.ssao_render_target.set_normals_texture_as_render_target();

		// Enable depth test
		d3d11_misc::context()->OMSetDepthStencilState(app.render_states.ds_yes_depth_test.Get(), 0);

		d3d11_misc::context()->VSSetShader(app.shaders.render_usual_vs.vs(), nullptr, 0);
		d3d11_misc::context()->PSSetShader(app.shaders.record_normals_ps.ps(), nullptr, 0);

		// Source camera matrix
		{
			auto cb_array = scratch_vector<ID3D11Buffer *>({ app.cb_camera.Get() });
			d3d11_misc::context()->VSSetConstantBuffers(0, 1, cb_array.data());

			D3D11_MAPPED_SUBRESOURCE mapped = {};
			d3d11_misc::context()->Map(app.cb_camera.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
			memcpy(mapped.pData, &app.cbstruct_camera, sizeof(CBCameraTransform));
			d3d11_misc::context()->Unmap(app.cb_camera.Get(), 0);
		}

		// Draw the mesh(es)
		{
			d3d11_misc::context()->RSSetState(app.render_states.rs_opaque.Get());
			d3d11_misc::context()->IASetInputLayout(app.pos_normal_inputlayout.Get());

			// Set IA buffers
			{
				ID3D11Buffer *vb_at_slot[] = { app.mesh_vb.Get() };
				u32 strides[] = { app.mesh_data.packed_attr_size };
				u32 start_offsets[] = { 0u };

				d3d11_misc::context()->IASetVertexBuffers(0, 1, vb_at_slot, strides, start_offsets);
				d3d11_misc::context()->IASetIndexBuffer(app.mesh_ib.Get(), DXGI_FORMAT_R16_UINT, 0);
			}

			d3d11_misc::context()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// Draw
			d3d11_misc::context()->DrawIndexed(eng::mesh::num_indices(app.mesh_data), 0, 0);
		}

		// Pass 1, ssao sampling pass - compute occlusion factor for each pixel

		// d3d11_misc::set_screen_as_render_target(app.d3d);

		app.ssao_render_target.set_occlusion_output_as_render_target();

		app.ssao_render_target.bind_ssao_build_pass_srvs();

		// Disable depth test
		d3d11_misc::context()->OMSetDepthStencilState(app.render_states.ds_no_depth_test.Get(), 0);

		// Full screen quad using null vertex/index buffer from now on.
		set_full_screen_triangle_vb_ib();

		d3d11_misc::context()->VSSetShader(app.shaders.screen_quad_vs.vs(), nullptr, 0);
		// d3d11_misc::context()->PSSetShader(app.shaders.debug_ps.ps(), nullptr, 0);

		d3d11_misc::context()->PSSetShader(app.shaders.ssao_sampling_ps.ps(), nullptr, 0);

		ID3D11Buffer *cb_array[] = { app.cb_ssao_params.Get(), app.cb_camera.Get() };
		d3d11_misc::context()->VSSetConstantBuffers(0, 1, cb_array);
		d3d11_misc::context()->PSSetConstantBuffers(0, 2, cb_array); // PS needs camera matrix

		d3d11_misc::context()->Draw(3, 0);

		// Blur passes - horizontal and vertical
		{
			// Horizontal pass

			SSAOBlurParamsCB blur_params;
			blur_params.is_horizontal_or_vertical = 0;

			source_into_constant_buffer(app.cb_ssao_blur_params.Get(), &blur_params, sizeof(SSAOBlurParamsCB));

			app.ssao_render_target.set_rtv_and_srv_for_horizontal_blur();

			d3d11_misc::context()->VSSetShader(app.shaders.screen_quad_vs.vs(), nullptr, 0);
			d3d11_misc::context()->PSSetShader(app.shaders.ssao_blur_ps.ps(), nullptr, 0);

			ID3D11Buffer *cb_array[] = { app.cb_ssao_params.Get(),
																	 app.cb_camera.Get(),
																	 app.cb_ssao_blur_params.Get() };

			d3d11_misc::context()->VSSetConstantBuffers(0, 1, cb_array);
			d3d11_misc::context()->PSSetConstantBuffers(0, 3, cb_array);

			set_full_screen_triangle_vb_ib();

			d3d11_misc::context()->Draw(3, 0);

			// Vertical pass
			blur_params.is_horizontal_or_vertical = 1;
			app.ssao_render_target.set_rtv_and_srv_for_vertical_blur();
			source_into_constant_buffer(app.cb_ssao_blur_params.Get(), &blur_params, sizeof(SSAOBlurParamsCB));
			d3d11_misc::context()->Draw(3, 0);
		}

		// Color pass
		{
			d3d11_misc::set_screen_as_render_target(app.d3d);

			d3d11_misc::set_viewport_dims(d3dconf.window_width, d3dconf.window_height);

			d3d11_misc::context()->PSSetShader(app.shaders.draw_with_ssao_ps.ps(), nullptr, 0);

			ID3D11ShaderResourceView *srv_array[] = { app.ssao_render_target.blurred_output_1.srv() };
			d3d11_misc::context()->PSSetShaderResources(0, 1, srv_array);

			ID3D11SamplerState *samplers_array[] = { app.ssao_render_target.linear_sampler.Get() };
			d3d11_misc::context()->PSSetSamplers(0, 1, samplers_array);

			d3d11_misc::context()->Draw(3, 0);
		}

		TwDraw();

		app.d3d.schain->Present(0, 0);
	}

	template <> void render<SSAODemo>(SSAODemo &app)
	{
		if (!app.using_ao_pass) {
			render_without_ssao(app);
		} else {
			render_with_ssao(app);
		}
	}

	template <> void close<SSAODemo>(SSAODemo &app) {}

	template <> bool should_close<SSAODemo>(SSAODemo &app)
	{
		return app.close || glfwWindowShouldClose(app.d3d.window);
	}

} // namespace app_loop

int main(int ac, char **av)
{
	eng::init_memory();
	DEFERSTAT(eng::shutdown_memory());

	u32 seed = 0xbadc0de;

	if (ac == 2) {
		seed = strtoul(av[1], nullptr, 10);
	}

	rng::init_rng(0xbadc0de);

	d3dconf.window_width = WIDTH;
	d3dconf.window_height = HEIGHT;
	d3dconf.window_title = "ssao_test";
	d3dconf.msaa_sample_count = 1;
	// d3dconf.load_renderdoc = true;

	SSAODemo app;
	app.d3d = d3d11_misc::init_d3d_window(d3dconf);
	app_loop::State timer;
	app_loop::run(app, timer);
}

#pragma once

#include <learnogl/essential_headers.h>

#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WGL
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFWAPI __declspec(dllimport)
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <DirectXColors.h>
#include <DirectXMath.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "dxerr.h"

#include <scaffold/string_stream.h>

#include <array>
#include <loguru.hpp>
#include <vector>

// Including these headers here because they will be used always

#include <learnogl/app_loop.h>
#include <learnogl/input_handler.h>
#include <learnogl/pmr_compatible_allocs.h>
#include <learnogl/renderdoc_app.h>
#include <learnogl/rng.h>

#include <loguru.hpp>

using Microsoft::WRL::ComPtr;

using namespace DirectX;

#if defined(min)
#	undef min
#endif

#if defined(max)
#	undef max
#endif

using namespace ::eng;
using namespace ::fo;

#define OKHR(expr) CHECK_F(!FAILED(expr), "")

#define DXASSERT(msg, hr)                                                                                    \
	if (FAILED(hr)) {                                                                                          \
		DXLOG_ERR(msg, hr);                                                                                      \
		CHECK_F(SUCCEEDED(hr), "See dxlog message");                                                             \
	}

namespace d3d11_misc
{

	struct WindowContext {
		ComPtr<ID3D11Device1> dev;
		ComPtr<ID3D11DeviceContext1> imm;
		ComPtr<IDXGISwapChain1> schain;

		GLFWwindow *window;
		HWND win32_window;

		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> immcontext;
		ComPtr<IDXGISwapChain> swapchain;

		ComPtr<ID3D11RenderTargetView> rtv_screen;
		ComPtr<ID3D11Texture2D> depth_stencil;
		ComPtr<ID3D11DepthStencilView> depth_stencil_view;

		D3D11_VIEWPORT viewport;

		D3D_DRIVER_TYPE driver_type;
		D3D_FEATURE_LEVEL feature_level;

		RENDERDOC_API_1_1_2 *rdoc = nullptr;
		bool capture_in_progress = false;
	};

	struct InitD3DConfig {
		u32 window_width = 1360;
		u32 window_height = 768;
		u32 msaa_sample_count = 4;
		u32 msaa_quality_level = D3D11_STANDARD_MULTISAMPLE_PATTERN;
		const char *window_title = "No title";

		bool load_renderdoc = false;
		const char *capture_path_template = nullptr;
	};

	WindowContext init_d3d_window(const InitD3DConfig &config);
	void close_d3d_window(const InitD3DConfig &config);

	// Global pointer to the d3d device. Kept for convenience.
	ID3D11Device *device();
	ID3D11DeviceContext1 *context();

	void set_screen_as_render_target(WindowContext &d3d);

	void set_viewport_dims(u32 width,
												 u32 height,
												 u32 topleft_x = 0,
												 u32 topleft_y = 0,
												 float min_depth = 0.0,
												 float max_depth = 1.0f);

	struct FullScreenQuad {
		ComPtr<ID3D11Buffer> vb;
		ComPtr<ID3D11InputLayout> pos2d_layout;
	};

	FullScreenQuad &full_screen_quad();

} // namespace d3d11_misc

enum class D3DShaderKind : u32 {
	VERTEX_SHADER,
	PIXEL_SHADER,
	TESS_CONTROL_SHADER,
	TESS_EVAL_SHADER,
	GEOMETRY_SHADER,
	COMPUTE_SHADER,
};

// --- helper stuff for the types and functions DirectXMath.h

using xm2 = XMFLOAT2;
using xm3 = XMFLOAT3;
using xm4 = XMFLOAT4;
using xm44 = XMFLOAT4X4;
using xm33 = XMFLOAT3X3;
using xmm = XMMATRIX;
using cxmm = CXMMATRIX;
using xmv = XMVECTOR;
using fxmv = FXMVECTOR;

REALLY_INLINE xm33 xm_identity33() { return XMFLOAT3X3(1.0f, 0, 0, 0, 1.0f, 0, 0, 0, 1.0f); }

REALLY_INLINE xm44 xm_identity44()
{
	return xm44(1.0f, 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 1.0f);
}

REALLY_INLINE xmv xm_unit_x() { return XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f); }
REALLY_INLINE xmv xm_unit_y() { return XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); }
REALLY_INLINE xmv xm_unit_z() { return XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); }
REALLY_INLINE xmv xm_unit_w() { return XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f); }
REALLY_INLINE xmv xm_origin() { return XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f); }
REALLY_INLINE xmv xm_identity_quat() { return XMQuaternionRotationAxis(xm_unit_x(), 0.0f); }

#define XM_XYZW(v) XMVectorGetX(v), XMVectorGetY(v), XMVectorGetZ(v), XMVectorGetW(v)
#define XM_XYZ(v) XMVectorGetX(v), XMVectorGetY(v), XMVectorGetZ(v)
#define XM_XY(v) XMVectorGetX(v), XMVectorGetY(v)

REALLY_INLINE f32 xm_x(fxmv v) { return XMVectorGetX(v); }
REALLY_INLINE f32 xm_y(fxmv v) { return XMVectorGetY(v); }
REALLY_INLINE f32 xm_z(fxmv v) { return XMVectorGetZ(v); }
REALLY_INLINE f32 xm_w(fxmv v) { return XMVectorGetW(v); }

// ## Overloaded functions for loading into xmv

REALLY_INLINE xmv xmload(const XMFLOAT4 &v) { return XMLoadFloat4(&v); }
REALLY_INLINE xmv xmload(const XMFLOAT3 &v) { return XMLoadFloat3(&v); }
REALLY_INLINE xmv xmload(const XMFLOAT2 &v) { return XMLoadFloat2(&v); }
REALLY_INLINE xmm xmload(const xm44 &m) { return XMLoadFloat4x4(&m); }
REALLY_INLINE xmv xvmake(f32 x, f32 y) { return XMVectorSet(x, y, 0, 0); }
REALLY_INLINE xmv xvmake(f32 x, f32 y, f32 z) { return XMVectorSet(x, y, z, 0.0); }
REALLY_INLINE xmv xvmake(f32 x, f32 y, f32 z, f32 w) { return XMVectorSet(x, y, z, w); }

// ## Overloaded functions for storing an xmv into FLOAT{4|3|2}

REALLY_INLINE auto &xmstore(fxmv v, xm4 &dest)
{
	XMStoreFloat4(&dest, v);
	return dest;
}

REALLY_INLINE auto &xmstore(fxmv v, xm3 &dest)
{
	XMStoreFloat3(&dest, v);
	return dest;
}

REALLY_INLINE auto &xmstore(fxmv v, xm2 &dest)
{
	XMStoreFloat2(&dest, v);
	return dest;
}

REALLY_INLINE auto &xmstore(cxmm m, xm44 &dest)
{
	XMStoreFloat4x4(&dest, m);
	return dest;
}

inline fo::string_stream::Buffer matrix_to_string(CXMMATRIX m)
{
	constexpr auto fmt = R"(
        +-                              -+
        |   %.3f    %.3f    %.3f    %.3f |
        |   %.3f    %.3f    %.3f    %.3f |
        |   %.3f    %.3f    %.3f    %.3f |
        |   %.3f    %.3f    %.3f    %.3f |
        +-                              -+
    )";

	fo::string_stream::Buffer ss(fo::memory_globals::default_allocator());
	fo::string_stream::printf(ss, fmt, XM_XYZW(m.r[0]), XM_XYZW(m.r[1]), XM_XYZW(m.r[2]), XM_XYZW(m.r[3]));
	return ss;
}

namespace d3d11_misc
{

	HRESULT compile_hlsl_file(const fs::path path,
														D3D_SHADER_MACRO *macro_defines,
														const char *entry_point,
														const char *target,
														u32 flags1,
														u32 flags2,
														ID3DBlob **pp_code_blob);

	inline void set_debug_name(ComPtr<IDXGIObject> &p_obj, const char *name)
	{
		#if defined(PROFILE) || defined(DEBUG) || !defined(NDEBUG)
		if (p_obj) {
			p_obj->SetPrivateData(WKPDID_D3DDebugObjectName, (u32)strlen(name), name);
		}
		#endif
	}

	inline void set_debug_name(ComPtr<ID3D11Device> &p_obj, const char *name)
	{
		#if defined(PROFILE) || defined(DEBUG) || !defined(NDEBUG)
		if (p_obj) {
			p_obj->SetPrivateData(WKPDID_D3DDebugObjectName, (u32)strlen(name), name);
		}
		#endif
	}

	template <typename T> inline void set_debug_name(ComPtr<T> &p_obj, const char *name)
	{
		#if defined(PROFILE) || defined(DEBUG) || !defined(NDEBUG)
		static_assert(std::is_base_of<ID3D11DeviceChild, T>::value, "");

		if (p_obj) {
			p_obj->SetPrivateData(WKPDID_D3DDebugObjectName, (u32)strlen(name), name);
		}
		#endif
	}

#define MAX_UNPACKED_COMPTRS 16

	template <typename PtrType> using UnpackedComptrs = std::array<PtrType, MAX_UNPACKED_COMPTRS>;

	template <typename PtrType> u32 _unpack_comptrs_next(UnpackedComptrs<PtrType> &out_array, u32 current)
	{
		UNUSED(out_array);
		UNUSED(current);
		return current;
	}

	template <typename PtrType, typename First, typename... Rest>
	u32 _unpack_comptrs_next(UnpackedComptrs<PtrType> &out_array, u32 current, First &first, Rest &... rest)
	{
		static_assert(IsInstanceOfTemplate<First, ComPtr>::value, "Not an instance of ComPtr");
		static_assert(std::is_same<strip_type_t<decltype(first.Get())>, PtrType>::value,
									"Not an instance of ComPtr");

		out_array[current] = first.Get();
		return _unpack_comptrs_next(out_array, current + 1, rest...);
	}

	template <typename PtrType, typename... T>
	u32 unpack_comptrs(UnpackedComptrs<PtrType> &out_array, T &... comptrs)
	{
		return _unpack_comptrs_next(out_array, 0, comptrs...);
	}

	// The learnogl api uses a GL context instead of D3D, so we are reimplementing
	// these functions here. Don't want to mess with learnogl API, as that is meant
	// to be a cross-platform.

	// Load renderdoc
	void load_renderdoc(WindowContext &d3d, const char *capture_file_prefix);

	// Begin a capture.
	void start_renderdoc_frame_capture(WindowContext &d3d);

	// End current capture.
	void end_renderdoc_frame_capture(WindowContext &d3d);

	// Returns if a frame is currently being captured
	bool is_renderdoc_frame_capturing(WindowContext &d3d);

	// Shutdown renderdoc
	void shutdown_renderdoc(WindowContext &d3d);

	// Sets the next frame to be captured
	void trigger_renderdoc_frame_capture(WindowContext &d3d, u32 num_frames = 1);

} // namespace d3d11_misc

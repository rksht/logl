#pragma once

#include "d3d11_misc.h"

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

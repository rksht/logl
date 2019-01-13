#version 430 core

#ifndef FONT_ATLAS_TEXUNIT
#error ""
#endif

const vec3 TEXT_COLOR = vec3(0.8, 0.8, 0.95);

in VsOut { vec2 uv; }
fs_in;

layout(binding = FONT_ATLAS_TEXUNIT) uniform sampler2D font_atlas_sampler;

#if defined(PROMPT_FS) && PROMPT_FS == 1

const vec3 color = vec3(0.4, 0.4, 0.9);

out vec4 fc;

void main() {
    float cov = texture(font_atlas_sampler, fs_in.uv).r;
    fc = vec4(color, cov);
}

#endif

#if defined(PAGER_FBO_FS) && PAGER_FBO_FS == 1

#if !defined(USE_TRANSPARENT_PAGER)

layout(location = 0) out float fc;

void main() {
    float cov = texture(font_atlas_sampler, fs_in.uv).r;
    fc = cov;
}

#else

layout(location = 0) out vec4 fc;

void main() {
	float cov = texture(font_atlas_sampler, fs_in.uv).r;
	fc = vec4(TEXT_COLOR, cov);
}

#endif

#endif

// -- Cursor

#if defined(CURSOR_FS) && CURSOR_FS == 1

const vec3 color = vec3(1.0, 0.0, 1.0);

out vec4 fc;

void main() { fc = vec4(color, 1.0); }

#endif

// Pager blit quad

#if defined(PAGER_QUAD_FS) && PAGER_QUAD_FS == 1

layout(binding = PAGER_TEXTURE_UNIT) uniform sampler2D pager_sampler;

const vec3 BG_COLOR = vec3(0.2, 0.2, 0.4);
const float console_alpha = 0.8;

out vec4 fc;

void main() {

#if !defined(USE_TRANSPARENT_PAGER)
    float text_cov = texture(pager_sampler, fs_in.uv).r;
    // fc = vec4(TEXT_COLOR, text_cov);
    vec3 color = mix(BG_COLOR, TEXT_COLOR, text_cov);
    fc = vec4(color, 1.0);

#else
	vec4 color = texture(pager_sampler, fs_in.uv).rgba;
	fc = color;
#endif
}

#endif

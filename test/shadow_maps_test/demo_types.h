#pragma once

#include <glad_compat/glad.h>

#include <learnogl/app_loop.h>
#include <learnogl/bounding_shapes.h>
#include <learnogl/colors.h>
#include <learnogl/gl_binding_state.h>
#include <learnogl/gl_misc.h>
#include <learnogl/intersection_test.h>
#include <learnogl/mesh.h>
#include <learnogl/kitchen_sink.h>
#include <learnogl/shader.h>
#include <learnogl/stb_image.h>
#include <learnogl/stopwatch.h>
#include <scaffold/const_log.h>

using namespace fo;
using namespace eng;
using namespace eng::math;

#define DATA_FOLDER (fs::path(SOURCE_DIR) / "data")

/*
    Only a static scene with simple shapes, with movable camera and probably the shadow light rotating.
*/

struct DirLightInfo {
    alignas(16) Vector3 position;
    alignas(16) Vector3 direction;
    alignas(16) Vector3 ambient;
    alignas(16) Vector3 diffuse;
    alignas(16) Vector3 specular;

    DirLightInfo() = default;

    DirLightInfo(Vector3 position, Vector3 direction, Vector3 ambient, Vector3 diffuse, Vector3 specular)
        : position(position)
        , direction(direction)
        , ambient(ambient)
        , diffuse(diffuse)
        , specular(specular) {
        normalize_update(this->direction);
    }
};

struct Material {
    Vector4 diffuse_albedo;
    Vector3 fresnel_R0;
    f32 shininess;
};

struct Common {
    ::optional<Material> material;
};

struct ShapeSphere {
    Vector3 center;
    f32 radius;

    Common common;
};

// A cube, always axis-aligned.
struct ShapeCube {
    Vector3 center;
    Vector3 half_extent;

    Common common;

    // Convert to an AABB
    explicit operator ::fo::AABB() const { return AABB { center + half_extent, center - half_extent }; }
};

struct LightGizmo : ShapeCube {};

struct Plane {
    Vector3 normal;
    Vector3 center;

    Common common;
};

struct ShapeModelPath {
    fs::path path;
    const char *diffuse_texture_path;

    Vector3 scale = { 1.0f, 1.0f, 1.0f };       // Scale wrt model space before rotation
    Vector3 euler_xyz_m = { 0.0f, 0.0f, 0.0f }; // Rotation in Euler angles wrt model space
    Vector3 position = { 0.0f, 0.f, 0.0f };     // Position in world space

    Common common;

    ShapeModelPath(const fs::path &path, Vector3 scale, Vector3 euler_xyz_m, Vector3 position) {
        this->path = path;
        this->scale = scale;
        this->euler_xyz_m = euler_xyz_m;
        this->position = position;
    }
};

using ShapeVariant = VariantTable<ShapeSphere, ShapeCube, ShapeModelPath, LightGizmo>;

struct RenderableShape {
    ShapeVariant shape;
    ::optional<Material> material;
};

static_assert(sizeof(Material) == sizeof(Vector4) + sizeof(Vector3) + sizeof(f32), "");

namespace demo_constants {

constexpr u32 light_count = 4;
constexpr i32 window_width = 1280;
constexpr i32 window_height = 720;

} // namespace demo_constants

namespace uniform_formats {

struct EyeBlock {
    Matrix4x4 view_from_world_xform;
    Matrix4x4 clip_from_view_xform;
    Vector3 eye_pos;
    float frame_time_in_sec;
};

template <size_t LIGHT_COUNT> struct DirLightsList { DirLightInfo list[LIGHT_COUNT]; };

struct PerObject {
    Matrix4x4 world_from_local_xform;
    Matrix4x4 inv_world_from_local_xform;
    Material material;
};

struct ShadowRelated {
    Matrix4x4 shadow_xform;
    BoundingSphere scene_bs;
};

static_assert(sizeof(uniform_formats::EyeBlock) == sizeof(Matrix4x4) * 2 + sizeof(Vector4), "");
static_assert(sizeof(uniform_formats::PerObject) == sizeof(Matrix4x4) * 2 + sizeof(Material), "");

} // namespace uniform_formats

inline constexpr Vector4 mul_rgb(Vector4 color, float k) { return Vector4(Vector3(color) * k, color.w); }

constexpr Material BALL_MATERIAL = { mul_rgb(colors::Tomato, 5.0f), Vector3 { 0.8f, 0.8f, 0.9f }, 0.7f };
constexpr Material FLOOR_MATERIAL = { mul_rgb(colors::BurlyWood, 3.0f), { 0.1f, 0.1f, 0.4f }, 0.4f };
constexpr Material TROPHY_MATERIAL = { mul_rgb(colors::BurlyWood, 5.0f), Vector3 { 1.0f, 1.0f, 1.0f }, 0.9f };
constexpr Material GIZMO_MATERIAL = { Vector4 { 0.0f, 0.0f, 0.95f, 0.05f }, zero_3, 0.0f };
constexpr Material PINK_MATERIAL = { Vector4 { XYZ(colors::HotPink), 0.5f }, zero_3, 0.0f };
constexpr Material LIGHT_GIZMO_MATERIAL = { colors::White, one_3, 1.0f };

struct RenderableData {
    // Everything in this struct remains constant once initialized, except the transforms for moving objects.
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLuint vao = 0;
    GLuint diffuse_texture = 0;

    u32 packed_attr_size = 0;
    u32 num_indices = 0;

    // per object uniform data
    uniform_formats::PerObject uniforms = { identity_matrix, identity_matrix, {} };
};

struct BoundUBO {
    gl_desc::UniformBuffer desc;
    GLuint binding;

    GLuint handle() { return desc.handle(); }
};

struct BoundTexture {
    gl_desc::SampledTexture desc;
    GLuint binding;

    GLuint handle() { return desc.handle(); }
    void set_handle(GLuint handle_) { desc._handle = handle_; }
};

// Loads a directional light mesh. I just generate the mesh programmatically instead of having it in an file.
// The forward direction points towards negative z. The mesh should be drawn with GL_TRIANGLES.
void load_dir_light_mesh(mesh::Model &m);

// For the HDR pipeline.

void load_irradiance_map(const fs::path path);

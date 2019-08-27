#pragma once

#include <learnogl/eng>

#include <learnogl/app_loop.h>
#include <learnogl/bounding_shapes.h>
#include <learnogl/colors.h>
#include <learnogl/font.h>
#include <learnogl/nf_simple.h>
#include <learnogl/shader.h>
#include <learnogl/stb_image.h>

#include <array>
#include <vector>

#include <learnogl/par_shapes.h>

using namespace fo;
using namespace math;

constexpr int k_window_width = 1360;
constexpr int k_window_height = 768;
constexpr float k_aspect_ratio = float(k_window_width) / k_window_height;
constexpr float Z_NEAR = 0.1f;
constexpr float Z_FAR = 1000.0f;
constexpr float HFOV = 70.0f;

// The radius of the scene
constexpr float scene_radius = 500.0f;

struct TextureFormat {
    GLint internal_format;
    GLenum external_format_components;
    GLenum external_format_component_type;
};

GLuint load_texture(const char *file_name, TextureFormat texture_format, GLenum texture_slot);

static inline GLuint create_vs_fs_prog(const char *vs_file, const char *fs_file) {
    using namespace fo;
    Array<uint8_t> vs_src(memory_globals::default_allocator());
    Array<uint8_t> fs_src(memory_globals::default_allocator());
    read_file(fs::path(vs_file), vs_src);
    read_file(fs::path(fs_file), fs_src);
    return eng::create_program((char *)data(vs_src), (char *)data(fs_src));
}

void flip_2d_array_vertically(uint8_t *data, uint32_t element_size, uint32_t w, uint32_t h);

AABB make_aabb(const Vector3 *vertices, const uint16_t *indices, size_t num_indices, Matrix4x4 transform);

inline par_shapes_mesh *create_mesh() { return par_shapes_create_parametric_sphere(20, 20); }

struct BoundTexture {
    gl_desc::SampledTexture texture;
    GLuint point;
};

struct BoundUBO {
    gl_desc::UniformBuffer ubo;
    GLuint point;
};

constexpr Matrix4x4 par_translate = translation_matrix(-0.5f, -0.5f, -0.5f);

inline Vector3 random_vector(float min, float max) {
    return Vector3{(float)rng::random(min, max), (float)rng::random(min, max), (float)rng::random(min, max)};
}

inline bool test_aabb_aabb(const AABB &a, const AABB &b) {
    if (a.max.x < b.min.x || a.min.x > b.max.x)
        return false;
    if (a.max.y < b.min.y || a.min.y > b.max.y)
        return false;
    if (a.max.z < b.min.z || a.min.z > b.max.z)
        return false;
    return true;
}

// Get the transformation matrix that needs to be applied on the unit cube to
// transform it into the given aabb.
Matrix4x4 AABB_transform(const AABB &bb);

// par_shapes generated cube puts the center of the cube at [0.5, 0.5, 0.5],
// this will shift it to origin.
static inline void shift_par_cube(par_shapes_mesh *cube) {
    for (int i = 0; i < cube->npoints; ++i) {
        cube->points[i * 3] -= 0.5f;
        cube->points[i * 3 + 1] -= 0.5f;
        cube->points[i * 3 + 2] -= 0.5f;
    }
}

constexpr size_t k_bars_in_shader = 500;

struct ViewProjBlock {
    Matrix4x4 view = identity_matrix;
    Matrix4x4 proj = identity_matrix;
    Vector4 eyepos_world;
};

struct DebugInfoOverlay {
    Vector<font::TwoTrianglesAlignedQuad> quads_buffer;
    font::FontData font_data;
    GLuint atlas_texture;
    PaddedRect screen_rectangle;

    u32 num_chars_in_line = 0;

    GLuint vbo_for_quads = 0;
    GLuint shader_program = 0;
    GLuint vao = 0;

    u32 screen_width;
    u32 screen_height;

    u32 rasterizer_state;

    PerCameraUBlockFormat camera_ublock;

    DebugInfoOverlay()
        : quads_buffer(0, memory_globals::default_allocator()) {}

    // Here's one way to initialize a info overlay
    void init(BindingState &bs, u32 screen_width, u32 screen_height, Vector3 text_color, Vector3 bg_color);

    void write_string(const char *string, u32 length = 0);

    void draw(BindingState &bs);
};

struct Quad {
    struct VertexData {
        Vector3 position;
        Vector2 st;
    };
    std::vector<VertexData> vertices = std::vector<VertexData>(6);

    Quad() = default;

    // I take a z here for no reason really and represent position with a vec3
    // for no reason really.
    Quad(Vector2 min, Vector2 max, float z = 0.4);
    void make_vao(GLuint *vbo, GLuint *vao);
};

enum SceneObjectKind : u32 {
    ASTEROID,
    WALL,
};

struct SceneObject {
    SceneObjectKind kind;
    LocalTransform xform;
};

REALLY_INLINE BoundingSphere sphere_from_local_transform(const LocalTransform &xform) {
    BoundingSphere sphere;
    sphere.radius = xform.scale.x;
    sphere.center = xform.position;
    return sphere;
}

struct MatrixTransform {
    Matrix4x4 m;
    Matrix4x4 inv_m;
};

inline MatrixTransform local_to_matrix_transform(const LocalTransform &xform) {
    return MatrixTransform{xform.get_mat4(), xform.get_inv_mat4()};
}

inline void assign_st_to_cube_face(
    const Vector3 &v0, const Vector3 &v1, const Vector3 &v2, Vector2 *st0, Vector2 *st1, Vector2 *st2) {
    // Find the common valued axis
    unsigned common = ~unsigned(0);
    unsigned swizzle[] = {0, 0};

    if (v0.x == v1.x && v1.x == v2.x) {
        common = 0;
        swizzle[0] = 1;
        swizzle[1] = 2;
    } else if (v0.y == v1.y && v1.y == v2.y) {
        common = 1;
        swizzle[0] = 0;
        swizzle[1] = 2;
    } else if (v0.z == v1.z && v1.z == v2.z) {
        common = 2;
        swizzle[0] = 0;
        swizzle[1] = 1;
    }

    assert(common != ~unsigned(0));

    st0->x = v0[swizzle[0]] + 0.5f;
    st0->y = v0[swizzle[1]] + 0.5f;
    st1->x = v1[swizzle[0]] + 0.5f;
    st1->y = v1[swizzle[1]] + 0.5f;
    st2->x = v2[swizzle[0]] + 0.5f;
    st2->y = v2[swizzle[1]] + 0.5f;
}

struct VertexData {
    Vector3 position;
    Vector3 normal;
    Vector2 st;
    Vector4 tangent;
    Vector3 diffuse;
};

// Calculates the tangents from a given mesh
void calculate_tangents(struct VertexData *vertices,
                        size_t num_vertices,
                        const uint16_t *indices,
                        size_t num_triangles);

struct Material {
    Vector4 diffuse_albedo = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector3 fresnel_R0 = {0.01f, 0.01f, 0.01f};
    float shininess = 1.02f; // 'm'

    Material() = default;

    Material(const Vector4 &diffuse_albedo, const Vector3 &fresnel_R0, float shininess)
        : diffuse_albedo(diffuse_albedo)
        , fresnel_R0(fresnel_R0)
        , shininess(shininess) {}
};

struct alignas(16) Light {
    Vector3 strength;
    float falloff_start; // For point and spot lights
    Vector3 direction;   // For spot and directional lights
    float falloff_end;   // For point and spot lights
    Vector3 position;    // Point and spot
    float spot_power;    // spot light only
};

inline float point_light_sphere(const Light &l) { return l.falloff_end; }

inline void parse_material(Material &mat, const fs::path &material_file) {
    auto cd = simple_parse_file(material_file.u8string().c_str(), true);

    auto root = nfcd_root(cd);

    mat.diffuse_albedo =
        SimpleParse<Vector4>::parse(cd, SIMPLE_MUST(nfcd_object_lookup(cd, root, "v4_diffuse")));
    mat.fresnel_R0 = SimpleParse<Vector3>::parse(cd, SIMPLE_MUST(nfcd_object_lookup(cd, root, "v3_R0")));
    mat.shininess = SimpleParse<float>::parse(cd, SIMPLE_MUST(nfcd_object_lookup(cd, root, "f_shine")));

    nfcd_free(cd);
}

struct Sphere {
    Vector3 center;
    float radius;
};

GLuint make_sphere_mesh_vao(u32 *num_mesh_indices);

GLuint make_sphere_xforms_ssbo(const LocalTransform *xforms, u32 count);

void make_light_material_ubo(BindingState &bs,
                             BoundUBO &material_ubo,
                             const Material &bar_material,
                             BoundUBO &light_properties_ubo,
                             const Array<Light> &light_properties);

constexpr int VIEWPROJ_UBO_BINDING = 0;
constexpr int SPHERE_XFORMS_SSBO_BINDING = 0;
constexpr int SPHERE_MATERIAL_UBO_BINDING = 2;
constexpr int LIGHT_PROPERTIES_UBO_BINDING = 3;
constexpr int LIGHTSPHERES_UBO_BINDING = 4;

static inline GLuint create_uniform_buffer(GLuint binding, size_t size, GLenum usage = GL_DYNAMIC_DRAW) {
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, buffer);
    glBufferData(GL_UNIFORM_BUFFER, size, nullptr, usage);
    glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer);
    return buffer;
}

inline Array<LocalTransform> read_sphere_transforms(const fs::path &spheres_file) {
    Array<LocalTransform> sphere_xforms(memory_globals::default_allocator());
    read_structs_into_array<SceneObject>(spheres_file,
                                         [](const SceneObject &sc) {
                                             CHECK_EQ_F(sc.kind, ASTEROID);
                                             return sc.xform;
                                         },
                                         sphere_xforms,
                                         "SceneObject -> LocalTransform");
    return sphere_xforms;
}

inline Array<Light> read_lights(const fs::path &lights_file) {
    Array<Light> lights(memory_globals::default_allocator());
    read_structs_into_array<Light>(lights_file, [](const Light &light) { return light; }, lights, "Light");
    return lights;
}

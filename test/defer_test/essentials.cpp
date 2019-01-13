#include "essentials.h"

#include <learnogl/gl_binding_state.h>
#include <scaffold/vector.h>

#include <numeric>

using namespace fo;
using namespace math;

GLuint load_texture(const char *file_name, TextureFormat texture_format, GLenum texture_slot) {
    int x, y, n;
    int force_channels = 4;

    stbi_set_flip_vertically_on_load(1);

    unsigned char *image_data = stbi_load(file_name, &x, &y, &n, force_channels);
    log_assert(image_data, "ERROR: could not load %s\n", file_name);
    // NPOT check
    if ((x & (x - 1)) != 0 || (y & (y - 1)) != 0) {
        log_warn("texture %s does not have power-of-2 dimensions\n", file_name);
    }

    GLuint tex;

    glGenTextures(1, &tex);
    glActiveTexture(texture_slot);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 texture_format.internal_format,
                 x,
                 y,
                 0,
                 texture_format.external_format_components,
                 texture_format.external_format_component_type,
                 image_data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    stbi_image_free(image_data);

    return tex;
}

AABB make_aabb(const Vector3 *vertices,
               const uint16_t *indices,
               size_t num_indices,
               const fo::Matrix4x4 transform) {
    fo::Vector3 min = {9999, 9999, 9999};
    fo::Vector3 max = {-9999, -9999, -9999};

    const float k_inc = 0.0f;

    for (size_t i = 0; i < num_indices; ++i) {
        const uint16_t index = indices[i];
        Vector3 v = math::mul(transform, fo::Vector4(vertices[index], 1.0f));
        if (v.x < min.x)
            min.x = v.x - k_inc;
        if (v.x > max.x)
            max.x = v.x + k_inc;
        if (v.y < min.y)
            min.y = v.y - k_inc;
        if (v.y > max.y)
            max.y = v.y + k_inc;
        if (v.z < min.z)
            min.z = v.z - k_inc;
        if (v.z > max.z)
            max.z = v.z + k_inc;
    }

    if (min.x == 9999 || min.y == 9999 || min.z == 9999 || max.x == -9999 || max.y == -9999 ||
        max.z == -9999) {
        assert(0);
    }

    return fo::AABB{min, max};
}

Matrix4x4 AABB_transform(const AABB &bb) {
    Vector3 extents = {bb.max.x - bb.min.x, bb.max.y - bb.min.y, bb.max.z - bb.min.z};

    Vector3 origin = 0.5f * Vector3{bb.max.x + bb.min.x, bb.max.y + bb.min.y, bb.max.z + bb.min.z};

    return Matrix4x4{{extents.x, 0.f, 0.f, 0.f},
                     {0.f, extents.y, 0.f, 0.f},
                     {0.f, 0.f, extents.z, 0.f},
                     {origin.x, origin.y, origin.z, 1.0f}};
}

void calculate_tangents(struct VertexData *vertices,
                        size_t num_vertices,
                        const uint16_t *indices,
                        size_t num_triangles) {
    Array<Vector3> intermediate(memory_globals::default_allocator(), num_vertices * 2);
    memset(data(intermediate), 0, vec_bytes(intermediate));

    Vector3 *tan1 = data(intermediate);
    Vector3 *tan2 = tan1 + num_vertices;

    for (size_t i = 0; i < num_triangles; ++i) {
        const auto i1 = indices[i * 3];
        const auto i2 = indices[i * 3 + 1];
        const auto i3 = indices[i * 3 + 2];

        auto &v1 = vertices[i1].position;
        auto &v2 = vertices[i2].position;
        auto &v3 = vertices[i3].position;

        auto &w1 = vertices[i1].st;
        auto &w2 = vertices[i2].st;
        auto &w3 = vertices[i3].st;

        // E1
        float x1 = v2.x - v1.x;
        float y1 = v2.y - v1.y;
        float z1 = v2.z - v1.z;
        // E2
        float x2 = v3.x - v1.x;
        float y2 = v3.y - v1.y;
        float z2 = v3.z - v1.z;
        // d_st1
        float s1 = w2.x - w1.x;
        float t1 = w2.y - w1.y;
        // d_st2
        float s2 = w3.x - w1.x;
        float t2 = w3.y - w1.y;

        float r = 1.0f / (s1 * t2 - s2 * t1);
        Vector4 sdir{(t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r, (t2 * z1 - t1 * z2) * r, 0.0f};
        Vector4 tdir{(s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r, (s1 * z2 - s2 * z1) * r, 0.0f};

        tan1[i1] = tan1[i1] + sdir;
        tan1[i2] = tan1[i2] + sdir;
        tan1[i3] = tan1[i3] + sdir;
        tan2[i1] = tan2[i1] + tdir;
        tan2[i2] = tan2[i2] + tdir;
        tan2[i3] = tan2[i3] + tdir;
    }

    // Orthogonalize
    for (size_t i = 0; i < num_vertices; ++i) {
        const auto &n = vertices[i].normal;
        const auto &t = tan1[i];

        Vector4 remain = {t - dot(n, t) * n, 0.0f};

        // Apparently par_shapes generates a all-zero normal which will produce an `inf` here
        // if we normalize.
        if (square_magnitude(remain) != 0.0f) {
            remain = Vector4{normalize(remain), 0.0f};
        } else {
            remain = Vector4{unit_x, 0.0f};
        }

        vertices[i].tangent = remain;
        vertices[i].tangent.w = dot(cross(n, t), tan2[i]) < 0.0f ? -1.0f : 1.0f;

        CHECK_F(!std::isnan(vertices[i].tangent.x), "");
        CHECK_F(!std::isnan(vertices[i].tangent.y), "");
        CHECK_F(!std::isnan(vertices[i].tangent.z), "");
    }
}

Quad::Quad(Vector2 min, Vector2 max, float z) {
    const Vector2 topleft = {min.x, max.y};
    const Vector2 bottomright = {max.x, min.y};

    auto set = [z](Vector3 &v, const Vector2 &corner) {
        v.x = corner.x;
        v.y = corner.y;
        v.z = z;
    };

    set(vertices[0].position, min);
    set(vertices[1].position, max);
    set(vertices[2].position, topleft);

    set(vertices[3].position, min);
    set(vertices[4].position, bottomright);
    set(vertices[5].position, max);

    vertices[0].st = Vector2{0, 0};
    vertices[1].st = Vector2{1, 1};
    vertices[2].st = Vector2{0, 1};

    vertices[3].st = Vector2{0, 0};
    vertices[4].st = Vector2{1, 0};
    vertices[5].st = Vector2{1, 1};
}

void Quad::make_vao(GLuint *vbo, GLuint *vao) {
    glGenVertexArrays(1, vao);
    glGenBuffers(1, vbo);

    glBindVertexArray(*vao);

    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, vec_bytes(vertices), vertices.data(), GL_STATIC_DRAW);

    glBindVertexBuffer(0, *vbo, 0, sizeof(VertexData));
    glVertexAttribBinding(0, 0); // pos
    glVertexAttribBinding(1, 0); // st
    glVertexAttribFormat(0, 3, GL_FLOAT, GL_FALSE, offsetof(VertexData, position));
    glVertexAttribFormat(1, 2, GL_FLOAT, GL_FALSE, offsetof(VertexData, st));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
}

GLuint make_sphere_mesh_vao(unsigned *num_indices) {

    par_shapes_mesh *cube = create_mesh();
    // par_shapes_unweld(cube, true);
    par_shapes_compute_normals(cube); // Want proper normals
    shift_par_cube(cube);

    std::vector<VertexData> vertices(cube->npoints);

    for (size_t i = 0; i < cube->npoints; ++i) {
        vertices.at(i).position.x = cube->points[i * 3];
        vertices.at(i).position.y = cube->points[i * 3 + 1];
        vertices.at(i).position.z = cube->points[i * 3 + 2];
        vertices.at(i).normal.x = cube->normals[i * 3];
        vertices.at(i).normal.y = cube->normals[i * 3 + 1];
        vertices.at(i).normal.z = cube->normals[i * 3 + 2];
        vertices.at(i).st.x = cube->tcoords[i * 2];
        vertices.at(i).st.y = cube->tcoords[i * 2 + 1];

        // Assign random color to vertex
        vertices.at(i).diffuse = random_vector(0.0, 1.0);
    }

    // Calculate tangents
    calculate_tangents(vertices.data(), vertices.size(), cube->triangles, cube->ntriangles);
    *num_indices = cube->ntriangles * 3;

    GLuint sphere_vao;

    glGenVertexArrays(1, &sphere_vao);
    glBindVertexArray(sphere_vao);

    GLuint bar_vbo, bar_ebo;

    glGenBuffers(1, &bar_vbo);
    glGenBuffers(1, &bar_ebo);

    glBindBuffer(GL_ARRAY_BUFFER, bar_vbo);
    glBufferData(GL_ARRAY_BUFFER, vec_bytes(vertices), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bar_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, *num_indices * sizeof(uint16_t), cube->triangles, GL_STATIC_DRAW);

    enum : unsigned int {
        k_pos_loc = 0,
        k_normal_loc = 1,
        k_tangent_loc = 2,
        k_diffuse_loc = 3,
        k_st_loc = 4
    };

    // Bindings
    glVertexAttribBinding(k_pos_loc, 0);
    glVertexAttribBinding(k_normal_loc, 0);
    glVertexAttribBinding(k_tangent_loc, 0);
    glVertexAttribBinding(k_diffuse_loc, 0);
    glVertexAttribBinding(k_st_loc, 0);

    // Attrib formats
    glVertexAttribFormat(k_pos_loc, 3, GL_FLOAT, GL_FALSE, offsetof(VertexData, position));
    glVertexAttribFormat(k_normal_loc, 3, GL_FLOAT, GL_FALSE, offsetof(VertexData, normal));
    glVertexAttribFormat(k_tangent_loc, 4, GL_FLOAT, GL_FALSE, offsetof(VertexData, tangent));
    glVertexAttribFormat(k_diffuse_loc, 3, GL_FLOAT, GL_FALSE, offsetof(VertexData, diffuse));
    glVertexAttribFormat(k_st_loc, 2, GL_FLOAT, GL_FALSE, offsetof(VertexData, st));

    // Enable
    glEnableVertexAttribArray(k_pos_loc);
    glEnableVertexAttribArray(k_normal_loc);
    glEnableVertexAttribArray(k_tangent_loc);
    glEnableVertexAttribArray(k_diffuse_loc);
    glEnableVertexAttribArray(k_st_loc);

    // Increase-per-N-instances (all increase per vertex)
    glVertexBindingDivisor(k_pos_loc, 0);
    glVertexBindingDivisor(k_normal_loc, 0);
    glVertexBindingDivisor(k_tangent_loc, 0);
    glVertexBindingDivisor(k_diffuse_loc, 0);
    glVertexBindingDivisor(k_st_loc, 0);
    // vb binding points
    glBindVertexBuffer(0, bar_vbo, 0, sizeof(VertexData));

    return sphere_vao;
}

GLuint make_sphere_xforms_ssbo(const LocalTransform *xforms, u32 count) {
    GLuint ssbo;
    glGenBuffers(1, &ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);

    glBufferData(GL_SHADER_STORAGE_BUFFER, count * sizeof(MatrixTransform), nullptr, GL_STATIC_DRAW);

    MatrixTransform *buffer = (MatrixTransform *)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY);
    for (u32 i = 0; i < count; ++i) {
        buffer[i] = local_to_matrix_transform(xforms[i]);
    }
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SPHERE_XFORMS_SSBO_BINDING, ssbo);
    return ssbo;
}

void make_light_material_ubo(BindingState &bs,
                             BoundUBO &material_ubo,
                             const Material &bar_material,
                             BoundUBO &light_properties_ubo,
                             const Array<Light> &light_properties) {
    {
        GLuint handle;
        glGenBuffers(1, &handle);
        glBindBuffer(GL_UNIFORM_BUFFER, handle);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(Material), &bar_material, GL_STATIC_DRAW);
        material_ubo.ubo = gl_desc::UniformBuffer(handle, 0, sizeof(Material));
        material_ubo.point = bs.bind_unique(material_ubo.ubo);
    }

    {

        GLuint handle;

        glGenBuffers(1, &handle);
        glBindBuffer(GL_UNIFORM_BUFFER, handle);
        glBufferData(GL_UNIFORM_BUFFER,
                     vec_bytes(light_properties),
                     data(light_properties),
                     GL_STATIC_DRAW);
        light_properties_ubo.ubo = gl_desc::UniformBuffer(handle, 0, sizeof(Light));
        light_properties_ubo.point = bs.bind_unique(light_properties_ubo.ubo);
    }
}

void DebugInfoOverlay::init(
    BindingState &bs, u32 screen_width, u32 screen_height, Vector3 text_color, Vector3 bg_color) {
    constexpr u32 font_height = 20;
    font::init(font_data, bs, LOGL_UI_FONT, font_height);

    this->screen_width = screen_width;
    this->screen_height = screen_height;

    LOG_F(INFO, "Loaded a font - info = %s", font::str(font_data).c_str());

    screen_rectangle.set_topleft_including_padding(
        zero_2, Vector2{(float)screen_width, (float)font_height}, 2.0f);

    fo::Vector2 min, max;
    screen_rectangle.get_inner_min_and_max(min, max);

    u32 max_chars = (u32)(max.x - min.x);

    // Initialize the quads buffer and the corresponding vertex buffer
    glGenBuffers(1, &vbo_for_quads);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_for_quads);
    glBufferData(
        GL_ARRAY_BUFFER, sizeof(font::TwoTrianglesAlignedQuad) * max_chars, nullptr, GL_DYNAMIC_DRAW);
    fo::resize(quads_buffer, 1 + max_chars); // 1 extra quad for background, which remains fixed

    push_back(quads_buffer, {});
    font::AlignedQuad bg_quad;
    bg_quad.set_from_padded_rect(screen_rectangle);

    // The bg quad gets its uv set to negative values. We check it in the fs to identity it as such and draw
    // bg color.
    bg_quad.vertices[font::TOPLEFT].uv = {-1.0f, -1.0f};
    bg_quad.vertices[font::TOPRIGHT].uv = {-1.0f, -1.0f};
    bg_quad.vertices[font::BOTRIGHT].uv = {-1.0f, -1.0f};
    bg_quad.vertices[font::BOTLEFT].uv = {-1.0f, -1.0f};
    quads_buffer[0].set_from_aligned_quad(bg_quad);

    ShaderDefines defs;
    defs.add("FONT_ATLAS_TEXTURE_UNIT", (int)font_data.texture_unit);
    defs.add("PER_CAMERA_UBLOCK", (int)bs.per_camera_ubo_binding());
    defs.add("COLOR", text_color);
    defs.add("BG_COLOR", bg_color);

    std::string defstring = defs.add("VERTEX_SHADER").get_string();

    LOG_F(INFO, "string = %s", defstring.c_str());

    GLuint vs = create_shader(CreateShaderOptionBits::k_prepend_version,
                              "#version 430 core\n",
                              GL_VERTEX_SHADER,
                              defstring.c_str(),
                              fs::path(SOURCE_DIR) / "debug_line_shader.glsl");

    defstring = defs.remove("VERTEX_SHADER").add("FRAGMENT_SHADER", 1).get_string();

    GLuint fs = create_shader(CreateShaderOptionBits::k_prepend_version,
                              "#version 430 core\n",
                              GL_FRAGMENT_SHADER,
                              defstring.c_str(),
                              fs::path(SOURCE_DIR) / "debug_line_shader.glsl");

    shader_program = eng::create_program(vs, fs);

    camera_ublock.view_from_world_xform = identity_matrix;
    camera_ublock.clip_from_view_xform = ortho_proj({0.0f, (f32)screen_width},
                                                    {-1.0f, 1.0f},
                                                    {0.0f, (f32)screen_height},
                                                    {1.0f, -1.0f},
                                                    {-1.0f, 1.0f},
                                                    {-1.0f, 1.0f});

    auto vao_format = VaoFormatDesc::from_attribute_formats(
        {VaoAttributeFormat(
             2, GL_FLOAT, GL_FALSE, offsetof(font::GlyphQuadVertexData, position_screen_space)),
         VaoAttributeFormat(2, GL_FLOAT, GL_FALSE, offsetof(font::GlyphQuadVertexData, uv))});

    vao = bs.get_vao(vao_format);
}

void DebugInfoOverlay::write_string(const char *string, u32 length) {
    if (length == 0) {
        length = strlen(string);
    }

    TempAllocator512 ta(memory_globals::default_allocator());
    Array<i32> codepoints(ta);
    resize(codepoints, length);
    std::copy(string, string + length, begin(codepoints));

    Vector2 min, max;
    screen_rectangle.get_inner_min_and_max(min, max);

    auto push_ret = font::push_line_quads(
        font_data, data(codepoints), size(codepoints), min.x, (max.x - min.x), min.y, data(quads_buffer) + 1);

    num_chars_in_line = push_ret.num_chars_pushed;

    if (push_ret.num_chars_pushed != length) {
        LOG_F(WARNING, "Could not push all characters");
    }

    if (false) {
        LOG_SCOPE_F(INFO, "Quads created for given line (in GL screen space) ");

        const Matrix4x4 N = camera_ublock.clip_from_view_xform * camera_ublock.view_from_world_xform;

        const auto to_ndc = [&](const Vector2 &pos) {
            Vector4 pos_4 = {pos.x, pos.y, 0.0f, 1.0f};
            return Vector2(N * pos_4);
        };

        const auto to_screen = [&](const Vector2 &ndc) {
            Vector2 in01 = (ndc + Vector2{1.0f, 1.0f}) / 2.0f;
            Vector2 in_screen = in01 * Vector2{(float)screen_width, (float)screen_height};
            return in_screen;
        };

        for (u32 i = 0; i < num_chars_in_line; ++i) {
            auto &q = quads_buffer[i + 1];
            Vector2 p0 = to_screen(to_ndc(q.vertices[font::TOPLEFT].position_screen_space));
            Vector2 p1 = to_screen(to_ndc(q.vertices[font::BOTRIGHT].position_screen_space));

            // Min and Max in GL screen space

            Vector2 min = {p0.x, p1.y};
            Vector2 max = {p1.x, p0.y};

            LOG_F(INFO, "min = [%f, %f], wh = [%f, %f]", XY(min), XY(max - min));
        }
    }

    glInvalidateBufferData(vbo_for_quads);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_for_quads);
    glBufferSubData(GL_ARRAY_BUFFER,
                    0,
                    (num_chars_in_line + 1) * sizeof(font::TwoTrianglesAlignedQuad),
                    data(quads_buffer));
}

void DebugInfoOverlay::draw(BindingState &bs) {
    glUseProgram(shader_program);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    glBindVertexArray(vao);

    glBindBuffer(GL_UNIFORM_BUFFER, bs.per_camera_ubo());
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(camera_ublock), &camera_ublock);
    glBindVertexBuffer(0, vbo_for_quads, 0, sizeof(font::GlyphQuadVertexData));
    // bg
    glDrawArrays(GL_TRIANGLES, 0, 6);
    // chars
    glDrawArrays(GL_TRIANGLES, 6, 6 * num_chars_in_line);
}

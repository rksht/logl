#pragma once

#include "diffuse_irradiance_map.h"

#include <learnogl/glsl_inspect.h>
#include <learnogl/mesh.h>
#include <learnogl/par_shapes.h>
#include <learnogl/scene_tree.h>

static const fs::path source_dir = SOURCE_DIR;

struct Geometry {
    eng::VertexBufferHandle vbo_handle;
    eng::IndexBufferHandle ebo_handle;

    eng::VertexArrayHandle vao_handle;

    u32 num_indices;
    u32 num_vertices;

    FixedString name;

    eng::mesh::MeshDataOffsetsAndSizes mesh_data;

    Geometry() = default;

    void init_from_par_mesh(par_shapes_mesh *pmesh, const char *name)
    {
        const_ vbo_name = fmt::format("vbo@{}", name);
        const_ ebo_name = fmt::format("ebo@{}", name);

        self_.num_indices = pmesh->ntriangles * 3;
        self_.num_vertices = pmesh->npoints;

        eng::BufferCreateInfo ci;
        ci.bytes = pmesh->npoints * sizeof(Vec3);
        ci.flags = eng::BufferCreateBitflags::SET_STATIC_STORAGE;
        ci.init_data = pmesh->points;
        ci.name = vbo_name.c_str();

        self_.vbo_handle = eng::create_vertex_buffer(eng::g_rm(), ci);

        ci = {};
        ci.bytes = pmesh->ntriangles * 3;
        ci.flags = eng::BufferCreateBitflags::SET_STATIC_STORAGE;
        ci.init_data = pmesh->triangles;
        ci.name = ebo_name.c_str();

        self_.ebo_handle = eng::create_element_array_buffer(eng::g_rm(), ci);

        self_.vao_handle = eng::g_rm().pnu_vao;

        self_.name.set(eng::g_strings(), name);

        var_ &md = self_.mesh_data;
        md.num_vertices = pmesh->npoints;
        md.num_faces = pmesh->ntriangles;
        md.packed_attr_size = sizeof(Vec3) * 2 + sizeof(Vec2);
        md.position_offset = 0;
        md.normal_offset = sizeof(Vec3);
        md.tangent_offset = eng::mesh::ATTRIBUTE_NOT_PRESENT;
        md.tex2d_offset = sizeof(Vec3) * 2;
    }

    void bind_geometry_buffers(bool bind_vao = true)
    {
        const_ vb_gluint = eng::gluint_from_globjecthandle(self_.vbo_handle);
        const_ eb_gluint = eng::gluint_from_globjecthandle(self_.ebo_handle);
        glBindVertexBuffer(0, vb_gluint, 0, self_.mesh_data.packed_attr_size);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, eb_gluint);

        if (bind_vao) {
            glBindVertexArray(eng::gluint_from_globjecthandle(self_.vao_handle));
        }
    }
};

struct UniformBufferBinding {
    eng::UniformBufferHandle ubo_handle;
    u32 byte_start_offset = 0;
    u32 size = 0;
};

struct Texture2DAndSamplerBinding {
    eng::Texture2DHandle texture_handle;
    eng::SamplerObjectHandle sampler_handle;
};

struct ResourceSlotInfo {
    u32 buffer_byte_offset = 0;
    u32 buffer_byte_size = 0;
    const char *name;
    u16 bindpoint; // Stands for texture unit

    ResourceSlotInfo() = default;

    static ResourceSlotInfo denoting_buffer(u32 byte_offset, u32 byte_size, const char *block_name);
    static ResourceSlotInfo denoting_texture(const char *sampler_name);
};

ResourceSlotInfo ResourceSlotInfo::denoting_buffer(u32 byte_offset, u32 byte_size, const char *block_name)
{
    return ResourceSlotInfo{ byte_offset, byte_size, block_name, 0 };
}

ResourceSlotInfo ResourceSlotInfo::denoting_texture(const char *sampler_name)
{
    return ResourceSlotInfo{ 0, 0, sampler_name, 0 };
}

struct BasicPipeline {
    eng::VertexShaderHandle vs_handle;
    eng::FragmentShaderHandle fs_handle;
    eng::ShaderProgramHandle program_handle;

    eng::VertexArrayHandle vao_handle;

    fo::Vector<UniformBufferBinding> unibuf_at_slot;
    fo::Vector<Texture2DAndSamplerBinding> tex_at_slot;

    fo::OrderedMap<std::string, ResourceSlotInfo> slot_info_of_data_block;
    fo::OrderedMap<std::string, ResourceSlotInfo> slot_info_of_sampler;

    eng::DepthStencilStateId ds_state_id = {};
    eng::RasterizerStateId rs_state_id = {};
    eng::BlendFunctionDescId blend_state_id = {};

    void init(fs::path vs_path,
              fs::path fs_path,
              eng::VertexArrayHandle vao_handle,
              fo::Vector<ResourceSlotInfo> expected_uniblock_names,
              fo::Vector<ResourceSlotInfo> expected_sampler_names)
    {
        eng::ShaderDefines defs;

        self_.vs_handle =
          eng::create_vertex_shader(eng::g_rm(), source_dir / "data/pos_only.vert", defs, "vs@first_try");

        self_.fs_handle =
          eng::create_fragment_shader(eng::g_rm(), source_dir / "data/usual_fs.frag", defs, "fs@first_try");

        self_.program_handle = eng::link_shader_program(
          eng::g_rm(),
          eng::ShadersToUse::from_vs_fs(self_.vs_handle.rmid(), self_.fs_handle.rmid()),
          "vsfs@first_try");

        self_.vao_handle = eng::g_rm().pos_vao;

        self_._note_down_slots(expected_uniblock_names, expected_sampler_names);
    }

    void _note_down_slots(const fo::Vector<ResourceSlotInfo> uniblock_slot_infos,
                          const fo::Vector<ResourceSlotInfo> sampler_slot_infos)
    {

        eng::InspectedGLSL glsl(eng::gluint_from_globjecthandle(self_.program_handle),
                                eng::pmr_default_resource());

        const_ &uniblocks = glsl.GetUniformBlocks();

        for (u32 i = 0; i < fo::size(uniblock_slot_infos); ++i) {
            var_ wip_slot_info = uniblock_slot_infos[i];

            const_ j = std_find_if_index(
              uniblocks, lam_(const_ & block_data) { return block_data.name == wip_slot_info.name; });

            CHECK_NE_F(j, -1, "No resource named - '%s' declared in shader program", wip_slot_info.name);

            wip_slot_info.bindpoint = uniblocks[j].bufferBinding;

            self_.slot_info_of_data_block[std::string(wip_slot_info.name)] = wip_slot_info;
        }

        const_ samplers = glsl.GetSamplers();

        for (u32 i = 0; i < fo::size(sampler_slot_infos); ++i) {
            var_ wip_slot_info = sampler_slot_infos[i];

            const_ j = std_find_if_index(
              samplers, lam_(const_ & sampler_info) { return sampler_info.fullName == wip_slot_info.name; });

            CHECK_NE_F(j, -1, "No resource named - '%s' declared in shader program", wip_slot_info.name);

            wip_slot_info.bindpoint = samplers[j].textureUnit;

            const_ name = std::string(wip_slot_info.name);
            self_.slot_info_of_sampler[name] = wip_slot_info;
        }
    }

    // Set the shader and the global render states. Blend state is only for
    void set_shader_and_render_state(u32 color_attachment_number = 0)->void
    {
        eng::ShadersToUse use;
        use.vs = self_.vs_handle.rmid();
        use.fs = self_.fs_handle.rmid();

        eng::set_shaders(eng::g_rm(), use);

        if (self_.rs_state_id) {
            eng::set_rs_state(eng::g_rm(), self_.rs_state_id);
        }

        if (self_.ds_state_id) {
            eng::set_ds_state(eng::g_rm(), self_.ds_state_id);
        }

        if (self_.blend_state_id) {
            eng::set_blendfunc_state(eng::g_rm(), color_attachment_number, self_.blend_state_id);
        }

        // Bind buffer ranges
    }

    void bind_uniform_buffer(eng::UniformBufferHandle ubo_handle,
                             const std::string &block_name,
                             bool discard = true)
      ->void
    {
        const_ gluint = eng::gluint_from_globjecthandle(ubo_handle);
        glBindBuffer(GL_UNIFORM_BUFFER, gluint);

        const_ &slot_info =
          ::find_with_end(self_.slot_info_of_data_block, block_name).keyvalue_must().second();

        if (discard) {
            glInvalidateBufferData(gluint);
        }

        glBindBufferRange(GL_UNIFORM_BUFFER,
                          slot_info.bindpoint,
                          gluint,
                          slot_info.buffer_byte_offset,
                          slot_info.buffer_byte_size);
    }
};

inline void upload_camera_transform(const eng::CameraTransformUB &camera_transforms,
                                    eng::UniformBufferHandle &ubo_handle)
{
    const_ gluint = eng::gluint_from_globjecthandle(ubo_handle);
    glBindBuffer(GL_UNIFORM_BUFFER, gluint);
    glInvalidateBufferData(gluint);
    glBufferSubData(GL_UNIFORM_BUFFER,
                    0,
                    sizeof(eng::CameraTransformUB),
                    reinterpret_cast<const void *>(&camera_transforms));
}

// Stores the data associated with each entity in arrays.
struct EntityStore {
    fo::Vector<Geometry> geometries;
    fo::OrderedMap<eng::StringSymbol, eng::SceneTree::Node *> name_to_node;
};

struct App {
    eng::input::BaseHandlerPtr<App> input_handler = eng::input::make_default_handler<App>();
    var_ &current_input_handler() { return self_.input_handler; }

    EntityStore entity_store;

    eng::FboId default_fbo;

    Geometry torus_geometry;

    BasicPipeline first_try_pip;

    struct UniformBuffers {
        eng::UniformBufferHandle camera_transforms;
    };

    UniformBuffers uniform_buffers;

    eng::Camera main_camera;

    void create_meshes()
    {
        {
            var_ pmesh = par_shapes_create_torus(20, 20, 1.0);
            DEFERSTAT(par_shapes_free_mesh(pmesh));
            par_shapes_compute_normals(pmesh);
            torus_geometry.init_from_par_mesh(pmesh, "torus");
        }
    }

    void create_camera_uniform_buffer()
    {
        eng::BufferCreateInfo ci;
        ci.bytes = sizeof(eng::CameraTransformUB);
        ci.flags = eng::BufferCreateBitflags::SET_DYNAMIC_STORAGE;
        ci.init_data = nullptr;
        ci.name = "ubo@camera_transform";

        self_.uniform_buffers.camera_transforms = eng::create_uniform_buffer(eng::g_rm(), ci);
    }

    void create_pipelines()
    {
        self_.first_try_pip.init(source_dir / "data/pos_only.vert",
                                 source_dir / "data/usual_fs.frag",
                                 eng::g_rm().pos_vao,
                                 { ResourceSlotInfo::denoting_buffer(0, 0, "CameraTransforms") },
                                 {});

        eng::RasterizerStateDesc rs_desc = eng::default_rasterizer_state_desc;
        rs_desc.cull_side = GL_NONE;
        self_.first_try_pip.rs_state_id = eng::create_rs_state(eng::g_rm(), rs_desc);

        eng::DepthStencilStateDesc ds_desc = eng::default_depth_stencil_desc;
        self_.first_try_pip.ds_state_id = eng::create_ds_state(eng::g_rm(), ds_desc);

        eng::BlendFunctionDesc blend_desc = eng::default_blendfunc_state;
        self_.first_try_pip.blend_state_id = eng::create_blendfunc_state(eng::g_rm(), blend_desc);
    }

    void render()
    {
        self_.first_try_pip.set_shader_and_render_state();
        self_.torus_geometry.bind_geometry_buffers(true);
    }
};

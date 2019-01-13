#include <learnogl/entity.h>
#include <learnogl/gl_binding_state.h>
#include <scaffold/array.h>
#include <scaffold/math_types.h>

namespace debug_shapes_component {

struct GLObjects {
    GLuint vs;
    GLuint fs;
    GLuint shader_program;
    GLuint uniform_buffer;
};

struct Manager {
    fo::Array<Entity> entities;
    mesh::StrippedMesh sphere_mesh_info;

    // GL handles
    gl_desc::UniformBuffer ubo_resource;

    GLObjects gl;

    DebugShapesManager(fo::Allocator &a);
};

// xyspoon: Color is not a separate component. It's managed by the debug shapes component.

void add_sphere(Entity e);

} // namespace debug_shapes_component

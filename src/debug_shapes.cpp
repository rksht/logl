#include <learnogl/debug_shapes.h>
#include <learnogl/gl_misc.h>

using namespace fo;
using namespace math;

namespace debug_shapes {

Manager(Allocator &a)
    : entities(a) {

    mesh::Model sphere_model;
    eng::load_sphere_mesh(sphere_model);
    this->sphere_mesh_info = mesh::StrippedMeshData(sphere_model[0]);
}

void add_sphere(Entity e, const SphereInfo &sphere) {
}

} // namespace debug_shapes

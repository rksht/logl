// --- In progress. Not a usable system yet.

#pragma once

#include <learnogl/entity.h>
#include <scaffold/pod_hash.h>

namespace eng {
namespace transform_component {

struct Transform {
    fo::Vector3 scale;
    fo::Quaternion orientation;
    fo::Vector3 position;

    static Transform identity() { return Transform{math::one_3, math::identity_versor, math::zero_3}; }
};

// An instance is simply an index into the list of transforms
struct Instance {
    u32 i;

    explicit Instance(u32 i)
        : i(i) {}

    explicit operator u32() const { return i; }
};

struct Manager {
    fo::Allocator *_allocator;

    // The storage for all transforms
    fo::Array<Transform> _transforms;

    // EntityID -> index in _transforms array
    fo::PodHash<Entity, u32, EntityHashFn, EntityEqualFn> _map;

    Manager(fo::Allocator &alloc, u32 starting_capacity);

    Manager(const Manager &) = delete;
    Manager(Manager &&) = delete;

    ~Manager();
};

/// Inserts a new transform
Instance insert(Manager &mgr, Entity e, const Transform &transform);

/// Remove given entity
void remove(Manager &mgr, Entity &e);

/// Look up the transform of this given entity
Instance lookup(const Manager &mgr, Entity &e);

/// Get the transform for this entity
Transform &get(Manager &mgr, Instance i);

/// Same as above, but const ref
const Transform &get(const Manager &mgr, Instance i);

} // namespace transform_component

} // namespace eng

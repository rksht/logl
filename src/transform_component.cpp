#include <learnogl/transform_component.h>
#include <loguru.hpp>

using namespace fo;

namespace transform_component {

Manager::Manager(Allocator &alloc, u32 starting_capacity)
    : _allocator(&alloc)
    , _transforms(alloc)
    , _map(alloc, alloc, entity_hash, static_cast<EntityEqualFn>(operator==)) {
    resize(_transforms, starting_capacity * sizeof(Transform));
}

Manager::~Manager() {}

Instance insert(Manager &k, Entity e, const Transform &transform) {
    fo::set(k._map, e, size(k._transforms));
    push_back(k._transforms, transform);

    return Instance(size(k._transforms) - 1);
}

void remove(Manager &k, Entity &e) { CHECK_F(false, "Not supporting removes yet"); }

Instance lookup(const Manager &k, Entity &e) {
    auto it = get(k._map, e);

    CHECK_F(it != end(k._map), "Did not find given entity. It was not inserted.");

    return Instance(it->value);
}

Transform &get(Manager &k, Instance i) { return k._transforms[u32(i)]; }

const Transform &get(const Manager &k, Instance i) { return k._transforms[u32(i)]; }

} // namespace transform_component

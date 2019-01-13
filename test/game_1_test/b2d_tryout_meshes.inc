#pragma once
#include <learnogl/gl_misc.h>

#include <learnogl/rng.h>

#define STOCK_ALLOCATOR fo::memory_globals::default_allocator()

struct EntityID {
    u32 i;

    EntityID() = default;

    constexpr EntityID(u32 i)
        : i(i) {}

    explicit operator u32() const { return i; }
};

REALLY_INLINE bool operator==(const EntityID a, const EntityID b) { return u32(a) == u32(b); }

struct EntityIDGenerator {
    u32 count = 0;

    EntityID generate() { return count++; }
};

static EntityIDGenerator &g_entity_id_generator() {
    static EntityIDGenerator entity_id_generator;

    return entity_id_generator;
}

// Denotes an invalid entity ID.
reallyconst ZERO_ENTITY_ID = EntityID(0);
reallyconst INVALID_TABLE_INDEX = std::numeric_limits<u32>::max();

struct StaticModel {
    eng::mesh::StrippedMeshData mesh_data;
    GLuint vbo;
    GLuint ebo;
    GLuint vao;
};

// Contains a map of entity id to indices. Inherited by storage classes.
struct EntityMap {
    fo::PodHash<EntityID, u32> _table = fo::make_pod_hash<EntityID, u32>();

    // Constructor. Reserves space for an estimated number of entities;
    EntityMap(u32 estimated_entity_count = 0, fo::Allocator &map_allocator = STOCK_ALLOCATOR)
        : _table(fo::make_pod_hash<EntityID, u32>(map_allocator)) {
        LOG_F(INFO, "Estimated entity count = %u", estimated_entity_count);
        fo::reserve(_table, )
    }

    u32 index_in_list(EntityID id) const {
        auto result = find_with_end(_table);
        if (result.not_found()) {
            return INVALID_TABLE_INDEX;
        }
        return result.res_it->value;
    }

    u32 index_in_list_must(EntityID id) const {
        u32 index = index_in_list(id);
        CHECK_F(index != INVALID_TABLE_INDEX, "id '%u' was not present in table", u32(id));
    }

    auto &table() { return _table; }

    void set(EntityID id, u32 list_index) { fo::set(_table, id, index); }

    auto find_with_end(EntityID id) { return ::find_with_end(_eidmap, id); }
};

struct StaticModelStorage {
    using DataType = StaticModel;

    EntityMap _eidmap;
    fo::Vector<DataType> _list;

    StaticModelStorage(u32 estimated_entity_count)
        : _eidmap(estimated_entity_count, STOCK_ALLOCATOR)
        , _list(STOCK_ALLOCATOR) {
        fo::reserve(_list, estimated_entity_count);
    }

    fo::Vector<DataType> &list() { return _list; }

    // Associate a new entity with given data
    void associate(EntityID id, DataType data) const {
        auto lookup_result = _eidmap.find_with_end();
        if (lookup_result.found()) {
            CHECK_F(false, "Entity %u already registered with this storage");
            return nullptr, "Unreachable";
        }

        // Allocate
        fo::push_back(_list, std::move(data));
        _eidmap.set(id, fo::size(_list) - 1u);
    }
};

#define MATRIX_CACHE_SIZE 128

struct LocalTransformStorage {
    using DataType = eng::math::LocalTransform;

    EntityMap _eidmap;
    fo::Vector<DataType> _list;

    std::array<fo::Matrix4x4, MATRIX_CACHE_SIZE> _matrix_cache{};
    std::array<EntityID, MATRIX_CACHE_SIZE> _eid_at_cache_slot{};

    LocalTransformStorage(u32 estimated_entity_count)
        : _e(estimated_entity_count)
        , _list(STOCK_ALLOCATOR) {
        fo::reserve(_list, estimated_entity_count);
        fo::reserve(_matrix_cache, matrix_cached_size);
    }

    // Associate a new entity with given data
    void associate(EntityID id, DataType data, bool add_to_matrix_cache = false) const {
        auto lookup_result = _eidmap.find_with_end();
        if (lookup_result.found()) {
            CHECK_F(false, "Entity %u already registered with this storage");
            return nullptr, "Unreachable";
        }

        if (add_to_matrix_cache) {
            data.set_mat4(_matrix_cache[_search_free_slot_in_cache()]);
        }

        // Allocate
        fo::push_back(_list, std::move(data));
        _eidmap.set(id, fo::size(_list) - 1u);
    }

    u32 _search_free_slot_in_cache() {
        for (u32 i = 0; i < MATRIX_CACHE_SIZE; ++i) {
            if (_eid_at_cache_slot[i] == ZERO_ENTITY_ID) {
                return i;
            }
        }
        return rng::random_i32(0, (i32)MATRIX_CACHE_SIZE);
    }
};

#include <learnogl/entity.h>
#include <loguru.hpp>

using checked_entity_masks = mask_t<entity_masks::IsActive, entity_masks::Uid>;

namespace entity_manager {

Entity allocate_new_entity(EntityManager &entity_manager) {
    Entity new_entity{0};
    new_entity.i = entity_masks::Uid::set(new_entity.i, entity_manager._next);
    new_entity.i = entity_masks::IsActive::set(new_entity.i, 1);

    entity_manager._storage.insert(new_entity);
    ++entity_manager._next;

    return new_entity;
}

void remove_entity(EntityManager &entity_manager) { CHECK_F(false, "This is a no-op."); }

} // namespace entity_manager

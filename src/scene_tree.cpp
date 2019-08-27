#include <learnogl/scene_tree.h>
#include <loguru.hpp>
#include <scaffold/vector.h>

namespace eng {

SceneTree::SceneTree(u32 node_pool_size)
    : _pool_allocator(sizeof(SceneTree::Node), node_pool_size) {}

SceneTree::Node *SceneTree::allocate_node() {
    SceneTree::Node *node =
        fo::push_back(_nodes_by_id, fo::make_new<SceneTree::Node>(_pool_allocator, ++_next_id_counter));

    CHECK_EQ_F(_next_id_counter, fo::size(_matrices_by_id));

    ++_next_id_counter;
    node->scene_tree_id = _next_id_counter;
    fo::push_back(_matrices_by_id, node->transform.get_mat4());

    fo::push_back(_nodes_by_id, node);
}

void SceneTree::remove_node(SceneTree::Node *node) {
    if (!node) {
        return;
    }

    _nodes_by_id[node->scene_tree_id] = nullptr;

    u32 id = node->scene_tree_id;
    std::swap(_matrices_by_id[id], fo::back(_matrices_by_id));

    fo::make_delete(_pool_allocator, node);
}

} // namespace eng

#pragma once

#include <learnogl/math_ops.h>
#include <scaffold/non_pods.h>
#include <scaffold/pool_allocator.h>

namespace eng {

class SceneTree {
  public:
    struct Node {
        math::LocalTransform transform = math::LocalTransform::identity();
        fo::Array<Node *> children;
        Node *parent = nullptr;
        u32 scene_tree_id;

        Node() = delete;

        Node(u32 id)
            : scene_tree_id(id) {}
    };

  private:
    fo::Vector<fo::Matrix4x4> _matrices_by_id;
    fo::Vector<Node *> _roots;
    fo::Vector<Node *> _nodes_by_id;

    fo::PoolAllocator _pool_allocator;

    u32 _next_id_counter = 0;

  public:
    SceneTree(u32 node_pool_size);

    Node *allocate_node();

    void remove_node(Node *node);
};

} // namespace eng

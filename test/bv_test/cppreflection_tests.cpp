#include <learnogl/cppreflection.h>
#include <learnogl/math_ops.h>
#include <learnogl/kitchen_sink.h>

using namespace fo;

struct Transform {
    Vector3 scale;
    Quaternion orientation;
    Vector3 position;

    DECL_FIELD_WRAPPER(scale);
    DECL_FIELD_WRAPPER(orientation);
    DECL_FIELD_WRAPPER(position);
};

int main(int ac, char **av) {
    memory_globals::init();
    DEFERSTAT(memory_globals::shutdown());

    auto fi = GET_FIELD_INFO(Transform, scale);
    printf("Field info - scale = [name = %s, size = %zu, offset = %zu]\n", fi.name, fi.size, fi.offset);

    auto fi2 = GET_FIELD_INFO(Transform, orientation);
    printf(
        "Field info - orientation = [name = %s, size = %zu, offset = %zu]\n", fi2.name, fi2.size, fi2.offset);
}

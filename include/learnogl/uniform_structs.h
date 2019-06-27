#include <scaffold/math_types.h>

namespace eng {
struct CameraTransformUB {
    fo::Matrix4x4 view;             // View <- World
    fo::Matrix4x4 proj;             // Homegenous clip space <- View
    fo::Vector4 camera_position;    // Camera position wrt world space
    fo::Vector4 camera_orientation; // Camera orientation wrt world space
};

} // namespace eng

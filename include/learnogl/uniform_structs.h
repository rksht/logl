#include <scaffold/math_types.h>

namespace eng {
struct CameraTransformUB {
    fo::Matrix4x4 view;             // View from World
    fo::Matrix4x4 proj;             // Homegenous clip space from View
    fo::Matrix4x4 inv_proj; 		// View space from clip space
    fo::Vector4 camera_position;    // Camera position wrt world space
    fo::Vector4 camera_orientation; // Camera orientation wrt world space
};

} // namespace eng

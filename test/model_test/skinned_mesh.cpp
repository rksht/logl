// Data structure representing a skinned mesh.

#include <scaffold/collection_types.h>
#include <scaffold/math_types.h>
#include <scaffold/types.h>
#include <vector>

namespace boneanim {

// Information at a single keyframe.
struct Keyframe {
    fo::Vector3 translation;
    fo::Vector3 scale;
    fo::Quaternion orientation;
};

// Keyframes for a single joint as it takes part in an animation.
struct JointKeyframes {
    fo::Array<Keyframe> keyframes;
};

// A single animation clip
struct Clip {
    std::vector<fo::Array<Keyframe>> list_of_recorded_keyframes;
};

struct JointHierarchy {
    // Within a hierarchy, the id of a bone is its index in the following arrays.

    // Index of parent bone
    fo::Array<u32> parents;

    // Offset transform
    fo::Array<fo::Matrix4x4> offset_transforms;

    // List of all animations created with this hierarchy.
    std::unordered_map<std::string, Clip> clips_by_name;
};

} // namespace boneanim

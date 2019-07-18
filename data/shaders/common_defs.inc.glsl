// Short hands for the builtin variables in a compute shader.

// ID of this thread relative to its group
#define GTID gl_LocalInvocationID

// ID of this thread's group
#define GID gl_WorkGroupID

// ID of this thread relative to the global grid
#define DTID gl_GlobalInvocationID

// Flat index of this thread relative to its group. GTID.x * GTID.y * GTID.z
#define GI gl_LocalInvocationIndex

// Number of groups in each axis
#define GSIZE gl_NumWorkGroups

// Number of threads in each axis of a group (also given as layout qualifer)
#define WSIZE gl_WorkGroupSize

#define NUMTHREADS_LAYOUT(x, y, z) layout(local_size_x = x, local_size_y = y, local_size_z = z) in;

// Vertex ID, semantic var defined in the vertex shader.
#define VID gl_VertexID

// Keep this updated with the one defined in the binding state file.
#define DEFINE_CAMERA_UBLOCK(binding_point, ublock_name)                                                     \
    layout(binding = binding_point, std140) uniform ublock_name {                                            \
        mat4 u_viewFromWorld;                                                                                \
        mat4 u_clipFromView;                                                                                 \
        mat4 u_invClipFromView;                                                                              \
        vec4 u_camPosition;                                                                                  \
        vec4 u_camOrientation;                                                                               \
    }

#define BUFFER_BINDPOINT(bindpoint, format) layout(binding = bindpoint, format)

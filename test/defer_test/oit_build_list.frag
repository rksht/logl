#version 430 core

#define END_OF_LIST_MARKER 0xffffffffu

layout(early_fragment_tests) in;

layout(binding = 0, offset = 0) uniform atomic_uint next_free_slot;

// Buffer containing slots. Each slot stores fragment data along with an
// offset to another fragment data in this very buffer.

// layout(binding = 0, rgba32ui) uniform writeonly uimageBuffer fragment_slots_buffer;

// 
layout(binding = 0, std430) buffer FragmentSlotsBuffer {
    uvec4 slots[];
} fragment_slots_buffer;

// Image storing the head pointer's index for each pixel
// layout(binding = 1, r32ui) uniform uimage2D head_pointers;
layout(binding = 0, r32ui) uniform uimage2D head_pointers;

in VS_Out {
    vec4 surface_color;
} fs_in;

out vec4 frag_color;

void main() {
    const uint pointer_index = atomicCounterIncrement(next_free_slot);
    const uint head_slot_index = imageAtomicExchange(head_pointers, ivec2(gl_FragCoord.xy), uint(pointer_index));

    uvec3 slot_data;

    // Fragment data. Just storing the simple color
    slot_data.r = packUnorm4x8(fs_in.surface_color);

    // The window-space depth
    slot_data.g = floatBitsToUint(gl_FragCoord.z);

    // 'next' slot is the old head slot
    slot_data.b = head_slot_index;

    // imageStore(fragment_slots_buffer, int(pointer_index), uvec4(slot_data, 0));

    fragment_slots_buffer.slots[pointer_index] = uvec4(slot_data, 0);


#if 0
    if (head_slot_index == END_OF_LIST_MARKER) {
        frag_color = vec4(1.0, 0.0, 1.0, 1.0);
    } else {
        frag_color = vec4(1.0, 1.0, 1.0, 1.0);
    }
#endif
    discard;
}

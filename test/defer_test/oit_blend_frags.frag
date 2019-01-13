#version 430 core

#define END_OF_LIST_MARKER 0xffffffffu
#define BACKGROUND_COLOR vec4(1.0)
// #define NO_LIST_COLOR vec4(0.8, 0.8, 0.95, 1.0)
#define NO_LIST_COLOR vec4(1.0, 0.0, 1.0, 1.0)
#define MAX_FRAGS_PER_PIXEL 10
#define AVERAGE_FRAGS_PER_PIXEL 4

#define FRAG_DATA_TYPE uvec3

// Buffer containing slots. Each slot stores fragment data along with an
// offset to another fragment data in this very buffer.

// layout(binding = 0, rgba32ui) uniform uimageBuffer fragment_slots_buffer;

layout(binding = 0, std430) buffer FragmentSlotsBuffer {
	uvec4 slots[];
} fragment_slots_buffer;


// Image storing the head pointer's index for each 
// layout(binding = 1, r32ui) uniform uimage2D head_pointers;
layout(binding = 0, r32ui) uniform uimage2D head_pointers;

out vec4 frag_color;

uint arr_fragment_color[MAX_FRAGS_PER_PIXEL];
float arr_fragment_depth[MAX_FRAGS_PER_PIXEL];

uint scan_list() {
	uint frag_count = 0;
	uint frag_slot = imageLoad(head_pointers, ivec2(gl_FragCoord.xy)).r;

	while (frag_slot != END_OF_LIST_MARKER && frag_count < MAX_FRAGS_PER_PIXEL) {
		// FRAG_DATA_TYPE frag_data = imageLoad(fragment_slots_buffer, int(frag_slot)).rgb;
		FRAG_DATA_TYPE frag_data = fragment_slots_buffer.slots[frag_slot].rgb;
		arr_fragment_color[frag_count] = frag_data.r;
		arr_fragment_depth[frag_count] = uintBitsToFloat(frag_data.g);
		++frag_count;
		frag_slot = frag_data.b;
	}

	return frag_count;
}

// Sort fragments in order of decreasing depth
void sort_fragments(int frag_count) {
	for (int i = 0; i < frag_count - 1; ++i) {
		for (int j = i + 1; j < frag_count; ++j) {
			if (arr_fragment_depth[i] < arr_fragment_depth[j]) {
				float temp_depth = arr_fragment_depth[i];
				arr_fragment_depth[i] = arr_fragment_depth[j];
				arr_fragment_depth[j] = temp_depth;

				uint temp_color = arr_fragment_color[i];
				arr_fragment_color[i] = arr_fragment_color[j];
				arr_fragment_color[j] = temp_color;
			}
		}
	}
}

// Debug purpose
vec4 frag_count_to_color(uint frag_count) {
	float c = float(frag_count) / float(MAX_FRAGS_PER_PIXEL);
	return vec4(c, c, c, 1.0);
}

float nth_fib(float n) {
	n = floor(n);

	const float sqfive = sqrt(5.0);

	return (pow(1 + sqfive, n) - pow(1 - sqfive, n)) / (pow(2.0, n) * sqfive);
}

vec4 fib_blend(vec4 src, vec4 dst, int max_partitions) {
	// Find which partition src and dst are
	float partition_size = 1.0 / max_partitions;
	vec3 src_partition = src.rgb / partition_size;
	vec3 dst_partition = dst.rgb / partition_size;

	vec3 minus_1 = src_partition - partition_size;
	vec3 minus_2 = minus_1 - partition_size;

	src_partition.r = nth_fib(src_partition.r);
	src_partition.g = nth_fib(src_partition.g);
	src_partition.b = nth_fib(src_partition.b);

	dst_partition.r = nth_fib(dst_partition.r);
	dst_partition.g = nth_fib(dst_partition.g);;
	dst_partition.b = nth_fib(dst_partition.b);;

	vec3 blended = (src_partition * src.a + (1 - src.a) * dst_partition) * partition_size;
	return vec4(blended, src.a);
}

#define GRAY_COUNT 1

void main() {
	uint frag_count = scan_list();

	if (frag_count == 0) {
		frag_color = NO_LIST_COLOR;
		return;
	}

#if GRAY_COUNT
	frag_color = frag_count_to_color(frag_count);
	return;
#endif

	sort_fragments(int(frag_count));

	// Start with background color and then keep blending
	vec4 color = BACKGROUND_COLOR;

	for (uint i = 0; i < frag_count; ++i) {
		vec4 c = unpackUnorm4x8(arr_fragment_color[i]);
		// color = vec4(c.rgb * c.a + color.rgb * (1 - c.a), c.a);
		color = fib_blend(c, color, 10);
	}

	frag_color = color;
}

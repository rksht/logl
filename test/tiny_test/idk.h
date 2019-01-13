#pragma once

#include <assert.h>
#include <learnogl/math_ops.h>
#include <learnogl/kitchen_sink.h>
#include <learnogl/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <learnogl/stb_image_write.h>

#include <scaffold/array.h>
#include <stdint.h>

using namespace fo;
using namespace math;

#define ALLOCATOR memory_globals::default_allocator()

using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8 = uint8_t;

using i64 = int64_t;
using i32 = int32_t;
using i16 = int16_t;
using i8 = int8_t;

using Index2D_i32 = Index2D<i32>;

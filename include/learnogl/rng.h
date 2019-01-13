/// Just contains a global random number generator
#pragma once

#include <scaffold/types.h>

namespace rng {

constexpr unsigned int DEFAULT_SEED = 0xdeadbeef;

void init_rng(unsigned int seed = DEFAULT_SEED);

/// Returns a random double in range [0 .. 1)
double random();

/// Returns a random double in range [start .. end)
double random(double start, double end);

/// Returns a random int in the given range
inline i32 random_i32(i32 start, i32 end) { return (i32)random((double)start, (double)end); }

} // namespace rng

#include <learnogl/rng.h>

#include <random>

using RandomEngine = std::default_random_engine;
using RealDist = std::uniform_real_distribution<double>;

// Global rng
static std::aligned_storage_t<sizeof(RandomEngine), alignof(RandomEngine)> re_store[1];
static std::aligned_storage_t<sizeof(RealDist), alignof(RealDist)> f64_dist_store[1];

namespace rng {

void init_rng(unsigned int seed) {
    new (re_store) RandomEngine(seed);
    new (f64_dist_store) RealDist(0.0, 1.0);
}

double random() {
    RandomEngine &re = *(RandomEngine *)(&re_store);
    RealDist &dist = *(RealDist *)(&f64_dist_store);

    return dist(re);
}

double random(double start, double end) { return start + random() * (end - start); }

} // namespace rng
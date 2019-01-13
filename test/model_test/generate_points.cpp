#include "essentials.h"
#include <scaffold/string_stream.h>

using namespace fo;

#define JSON_FILE SOURCE_DIR "/points.json"

constexpr float RADIUS = 5.0f;
constexpr size_t NUM_POINTS = 100;

void generate_points() {
    using namespace string_stream;
    Buffer b(memory_globals::default_allocator());

    FILE *f = fopen(JSON_FILE, "w");

    b << "{ \"points\": [";

    for (size_t i = 0; i < NUM_POINTS - 1; ++i) {
        float x = rng::random(-RADIUS, RADIUS);
        float y = rng::random(-RADIUS, RADIUS);
        float z = rng::random(-RADIUS, RADIUS);
        b << "[" << x << ", " << y << ", " << z << "],";
    }
    float x = rng::random(-RADIUS, RADIUS);
    float y = rng::random(-RADIUS, RADIUS);
    float z = rng::random(-RADIUS, RADIUS);
    b << "[" << x << ", " << y << ", " << z << "]";
    b << "]}";

    fwrite(data(b), sizeof(char), size(b), f);
    fclose(f);
}

int main() {
    memory_globals::init();
    rng::init_rng(0xdeadbeef);
    { generate_points(); }
    memory_globals::shutdown();
}

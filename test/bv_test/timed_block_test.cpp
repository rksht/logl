#include <learnogl/timed_block.h>

using namespace fo;

int factorial(int n) {
    TIMED_BLOCK;

    int f = 1;
    while (n > 0) {
        TIMED_BLOCK;

        f = f * n;
        --n;
    }

    return f;
}

void func_root() {
    TIMED_BLOCK;

    for (u32 i = 0; i < 10000; ++i) {
        factorial(i % 20);
    }
}

int main() {
    memory_globals::init();
    DEFER([]() { memory_globals::shutdown(); });

    func_root();

    auto &table = timedblock::get_table();

    for (u32 i = 0; i < TIMED_BLOCK_CAPACITY; ++i) {
        if (timedblock::is_nil_record(table._records[i])) {
            continue;
        }

        auto &rec = table._records[i];

        printf("Timeblock: %s: %i, Entered %lu times\n", rec.func_pointer, rec.line, rec.times_entered);
    }
}

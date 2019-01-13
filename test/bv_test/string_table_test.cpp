#include <learnogl/kitchen_sink.h>
#include <learnogl/string_table.h>
#include <scaffold/pod_hash.h>
#include <scaffold/pod_hash_usuals.h>

using namespace fo;

constexpr int max_count = 20000;

int main() {
    memory_globals::init();
    DEFER([]() { memory_globals::shutdown(); });
    {
        StringTable stab(15, 1000);

        fo::PodHash<int, char *> sym2string(memory_globals::default_allocator(),
                                            memory_globals::default_allocator(),
                                            usual_hash<int>,
                                            usual_equal<int>);

        auto file = fopen("/usr/share/dict/words", "r");
        DEFER([file]() { fclose(file); });

        int count = 0;

        while (!feof(file)) {
            char buf[512] = {};
            fgets(buf, sizeof(buf), file);

            auto sym = stab.to_symbol(buf);
            auto cstr = stab.to_string(sym);
            sym2string[sym._s] = strdup(cstr);

            if (++count == max_count) {
                break;
            }
        }

        for (auto i = cbegin(sym2string); i != cend(sym2string); ++i) {
            const char *str = stab.to_string(StringSymbol(i->key));
            auto        is_equal = strcmp(i->value, str) == 0;

            assert(is_equal);

            free(i->value);
            i->value = nullptr;
        }
    }
}

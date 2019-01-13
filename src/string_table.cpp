#include <learnogl/essential_headers.h>
#include <learnogl/string_table.h>

#include <cmath>
#include <scaffold/memory.h>
#include <string.h>
#include <type_traits>

using namespace fo;

namespace eng {

// Will use Allocator objects later perhaps. So introducing these three functions to abstract out the
// allocate, resize, and deallocate.
static nfst_StringTable *allocate_table(u32 bytes) { return (nfst_StringTable *)calloc(bytes, 1); }

// reallocates the table
static nfst_StringTable *reallocate_table(nfst_StringTable *st, u32 new_bytes) {
    fprintf(stderr, "New table bytes = %u\n", new_bytes);
    return (nfst_StringTable *)realloc(st, new_bytes);
}

static void deallocate_table(nfst_StringTable *st) { free(st); }

static nfst_StringTable *make_and_copy(const StringTable &other) {
    auto st = allocate_table(other._st->allocated_bytes);
    memcpy(st, other._st, other._st->allocated_bytes);
    return st;
}

StringTable::StringTable(u32 average_string_length, u32 num_unique_strings)
    : _st(nullptr) {
    reinit(average_string_length, num_unique_strings);
}

StringTable::StringTable(const StringTable &other) { _st = make_and_copy(other); }

StringTable::StringTable(StringTable &&other) { _st = std::exchange(_st, nullptr); }

StringTable::~StringTable() {
    if (_st) {
        deallocate_table(_st);
        _st = nullptr;
    }
}

StringTable &StringTable::operator=(const StringTable &other) {
    if (this == &other) {
        return *this;
    }

    if (_st) {
        deallocate_table(_st);
    }

    _st = make_and_copy(other);
    return *this;
}

StringTable &StringTable::operator=(StringTable &&other) {
    if (this == &other) {
        return *this;
    }

    if (_st) {
        deallocate_table(_st);
    }
    _st = std::exchange(other._st, nullptr);
    return *this;
}

void StringTable::reinit(u32 average_string_length, u32 num_unique_strings) {
    if (_st) {
        deallocate_table(_st);
    }

    u64 total_bytes = average_string_length * num_unique_strings * sizeof(i8);
    _st = allocate_table(total_bytes);
    nfst_init(_st, (int)total_bytes, (int)average_string_length);
}

StringSymbol StringTable::to_symbol(const char *str) {
    StringSymbol sym;
    sym._s = nfst_to_symbol(_st, str);

    while (sym._s == NFST_STRING_TABLE_FULL) {
        u32 new_bytes = u32(std::ceil(_st->allocated_bytes * 1.5));
        fprintf(stderr, "Current bytes = %u, count = %i\n", _st->allocated_bytes, _st->count);
        _st = reallocate_table(_st, new_bytes);
        CHECK_F(_st != nullptr, "reallocate_table failed");
        nfst_grow(_st, (int)new_bytes);
        sym._s = nfst_to_symbol(_st, str);
    }

    return sym;
}

void StringTable::pack() {
    int new_bytes = nfst_pack(_st);
    _st = reallocate_table(_st, (u32)new_bytes);
    CHECK_F(_st != nullptr, "reallocate_table failed");
}

const char *StringTable::to_string(const StringSymbol &sym) { return nfst_to_string(_st, sym._s); }

StringSymbol::StringSymbol(StringTable &string_table, const char *str) {
    _s = string_table.to_symbol(str)._s;
}

std::aligned_storage_t<sizeof(StringTable), alignof(StringTable)> g_default_strtab[1];

void init_default_string_table(u32 average_string_length, u32 num_unique_strings) {
    new (g_default_strtab) StringTable(average_string_length, num_unique_strings);
}

StringTable &default_string_table() { return *reinterpret_cast<StringTable *>(g_default_strtab); }

void free_default_string_table() { default_string_table().~StringTable(); }

} // namespace eng

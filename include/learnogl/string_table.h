// String interning. Not thread-safe. Should soon be. Just a mutex away. Although to_string() needs to return
// a copy.

#pragma once

#include "nflibs.h"
#include <scaffold/types.h>

#include <functional>
#include <string>

namespace eng {

struct StringTable;

// Light wrapper around the `int` symbol
struct StringSymbol {
    int _s;

    StringSymbol()
        : _s(NFST_STRING_TABLE_FULL) {}

    // Not usually needed. Let StringTable give you one.
    explicit StringSymbol(int s)
        : _s(s) {}

    // Usual copy ctor and assign.
    StringSymbol(const StringSymbol &s) = default;
    StringSymbol &operator=(const StringSymbol &s) = default;

    // Will use string_table.to_symbol(symbol_string) to initialize.
    StringSymbol(StringTable &string_table, const char *str);

    bool invalid() const { return _s == NFST_STRING_TABLE_FULL; }

    // @rksht - Remove this. Just use int() conversion.
    int to_int() const { return _s; }

    explicit operator int() const { return _s; }
};

// Operators == and < are defined for storing in a hash table.
inline bool operator==(const StringSymbol &me, const StringSymbol &other) { return me._s == other._s; }
inline bool operator<(const StringSymbol &me, const StringSymbol &other) { return me._s < other._s; }

} // namespace eng

namespace std {

template <> class hash<eng::StringSymbol> {
  public:
    using argument_type = eng::StringSymbol;

    size_t operator()(const eng::StringSymbol &s) const { return std::hash<int>{}(s._s); }
};

} // namespace std

namespace eng {

/// Convenience wrapper around nfst_StringTable type and its associated functions.
struct StringTable {
    nfst_StringTable *_st = nullptr;

    // Constructor. Allocates the memory required for storing. Provide some starting estimates as argument.
    StringTable(u32 average_string_length, u32 num_unique_strings);

    // Destructor. Frees storage.
    ~StringTable();

    // copy and move do the usual thing. After being moved from, you either stop using the StringTable object,
    // or call reinit() before starting over.
    StringTable(const StringTable &other);
    StringTable(StringTable &&other);
    StringTable &operator=(const StringTable &other);
    StringTable &operator=(StringTable &&other);

    void reinit(u32 average_string_length, u32 num_unique_strings);

    // Shrinks the memory allocated if possible.
    void pack();

    // These converts given string to a symbol. Might reallocate the underlying table if it's full.
    StringSymbol to_symbol(const char *str);
    StringSymbol to_symbol(const std::string &str) { return to_symbol(str.c_str()); }

    inline StringSymbol get_symbol(const char *str);

    // Converts the given symbol to a string. `s` must be some symbol that you received from a call to
    // `to_symbol`, or it's undefined behavior.
    const char *to_string(const StringSymbol &s);
};

// A default string table for most of our needs

void init_default_string_table(u32 average_string_length, u32 num_unique_strings);
StringTable &default_string_table();
void free_default_string_table();

// Impl of inlines

inline StringSymbol StringTable::get_symbol(const char *str) {
    int symbol = nfst_to_symbol_const(_st, str);
    if (symbol == NFST_STRING_TABLE_FULL) {
        return StringSymbol();
    }
    return StringSymbol(symbol);
}

} // namespace eng

#pragma once

#include <scaffold/array.h>
#include <scaffold/non_pods.h>
#include <scaffold/vector.h>

// A collection of strings in a single buffer. Can only add new strings, cannot remove added strings.
struct FixedStringBuffer {
    fo::Vector<char> _concat_of_strings;

    struct StartAndLength {
        u32 start_byte;
        u32 length;
    };

    // Stores the starting byte and the length of the nth string excluding the following 0 byte, allowing O(1)
    // length.
    fo::Array<StartAndLength> _start_and_length;

    // We return this wrapped index so that other code gets indication that this is a private index they
    // shouldn't peek.
    struct Index {
        u32 _i;
        bool operator<(const Index &i) const { return _i < i._i; }
    };

    FixedStringBuffer(u32 reserve_bytes = 0,
                      u32 estimated_num_strings = 0,
                      fo::Allocator &a = fo::memory_globals::default_allocator())
        : _concat_of_strings(a)
        , _start_and_length(a) {
        fo::reserve(_concat_of_strings, reserve_bytes);
        fo::reserve(_start_and_length, estimated_num_strings);
    }

    Index add(const char *s, u32 length = 0);

    // Returns a pointer to the string. index must be valid.
    const char *get(Index index) const;

    // Returns length of the given string corresponding to this index
    u32 length(Index index) const;
};

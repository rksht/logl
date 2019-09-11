#pragma once

#include <scaffold/string_stream.h>
#include <string.h>

// Error is my 'standard' error object. Go's standard library gets it. Error objects themselves don't need to
// be complicated.
struct Error {
    fo::string_stream::Buffer _ss;

    Error(const char *error_string = nullptr);
    const char *to_string() const;

    // Just to make it clear, passing `nullptr` denotes "no error".
    static Error ok() { return Error(nullptr); }

    // True if there is an error.
    operator bool() const { return fo::size(_ss) != 0; }
};

template <typename T> Error &operator<<(Error &error, T &value) {
    using namespace fo::string_stream;
    error._ss << value;
}

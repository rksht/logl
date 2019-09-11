#include <learnogl/error.h>
#include <learnogl/kitchen_sink.h>

#include <loguru.hpp>

using namespace fo;

Error::Error(const char *error_string)
    : _ss(memory_globals::default_allocator()) {
    if (error_string && strlen(error_string) == 0) {
        ABORT_F("Empty message given empty string. If you want to denote no error just default initialize");
    }

    if (error_string) {
        string_stream::printf(_ss, "%s", error_string);
    }
}

const char *Error::to_string() const {
    return string_stream::c_str(const_cast<string_stream::Buffer &>(_ss));
}

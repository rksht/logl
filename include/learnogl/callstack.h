#pragma once

#include <scaffold/string_stream.h>

namespace eng {

// Just prints the callstack to the given stream.
void print_callstack(fo::string_stream::Buffer &ss);

// Prints the callstack with loguru
void print_callstack();

} // namespace eng

#pragma once

#if defined(_MSC_VER)

#    if defined(_CRTDBG_MAP_ALLOC)
#        undef _CRTDBG_MAP_ALLOC
#    endif

#    include <cstdlib>

#endif

#define LOGURU_WITH_STREAMS 1
// #define LOGURU_USE_FMTLIB 1

#include <glad/glad.h>

#if defined(_MSC_VER)
// fmtlib generates too many signed/unsigned mismatch warnings. Disable them.
#    pragma warning(push)
#    pragma warning(disable : 4388)
#    pragma warning(disable : 4389)
#    pragma warning(disable : 4574)
#endif

#include <fmt/format.h>
#include <loguru.hpp>

#if defined(_MSC_VER)
#    pragma warning(pop)
#endif

#include <scaffold/memory.h>
#include <scaffold/types.h>

#include <learnogl/callstack.h>
#include <learnogl/kitchen_sink.h>
#include <learnogl/type_utils.h>

#include <learnogl/error.h>

#include <learnogl/debug_break.h>

reallyconst LOGL_MILD_LOG_CHANNEL = loguru::NamedVerbosity::Verbosity_1;
reallyconst LOGL_ERROR_LOG_CHANNEL = loguru::NamedVerbosity::Verbosity_ERROR;

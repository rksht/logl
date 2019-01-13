// Wraps GL_TIME_ELAPSED timer query for measuring time to complete sequences of GL commands.

#pragma once

#include <glad/glad.h>
#include <learnogl/string_table.h>
#include <loguru.hpp>
#include <scaffold/array.h>
#include <scaffold/pod_hash.h>
#include <scaffold/string_stream.h>

#include <array>
#include <string>

namespace eng {

namespace gl_timer_query {

constexpr u32 NUM_COPIES = 3;
constexpr u32 NUM_INTERVALS = 8;

struct _QueryData {
    GLuint query_object_handle;
    GLsync fence;
};

struct _TimestampData {
    GLuint ts_query_object;
    GLsync fence;
};

struct _TimerData {
    u64 time_taken_max[NUM_INTERVALS + 1];
    u64 time_taken_min[NUM_INTERVALS + 1];
    u64 time_taken_total;
    u64 times_hit;
    eng::StringSymbol name;

    SCAFFOLD_IGNORE_DEF_CTOR;
};

struct _FrameTimerData : _TimerData {
    u64 last_ts;
};

struct _FrameInfo {
    // Timer queries for each timer for this frame
    fo::Array<_QueryData> arr_query_data;

    // Timestamp query for the full frame
    _TimestampData ts_data;

    _FrameInfo(fo::Allocator &a)
        : arr_query_data(a)
        , ts_data{} {}
};

struct TimerQueryManager {
    // 3 Frames worth of timers
    std::array<_FrameInfo, NUM_COPIES> _per_frame_info;

    // The collected data for each timer
    fo::Array<_TimerData> _timer_data_list;

    // The collected data for frame
    _FrameTimerData _frame_timer_data;

    // Map from timer name to index in the _timer_data array
    fo::PodHash<int, u32, std::hash<int>> _name_to_index;

    i32 _cpu_frame;
    i32 _wait_frame;

    eng::StringTable *_name_table;

    bool _no_warning;

    inline TimerQueryManager(eng::StringTable &name_table,
                             fo::Allocator &allocator = fo::memory_globals::default_allocator());
};

inline TimerQueryManager::TimerQueryManager(eng::StringTable &name_table, fo::Allocator &allocator)
    : _per_frame_info{allocator, allocator, allocator}
    , _timer_data_list(allocator)
    , _name_to_index(allocator, allocator, std::hash<int>{}, fo::CallEqualOperator<int>{})
    , _cpu_frame(0)
    , _wait_frame(-1)
    , _name_table(&name_table)
    , _no_warning(false) {}

using TimerID = int;

// Adds a new timer
TimerID add_timer(TimerQueryManager &m, const char *timer_name);

// Call this after you are done adding all your timers
void done_adding(TimerQueryManager &m);

// Call just before issuing GL commands in a new frame
void new_frame(TimerQueryManager &m);

// Begin a timer query. Call the sequence of GL functions that you want to time using this timer after this,
// and then call `end_timer`. Also, you must begin and end a particular timer only once per frame, and not
// overlap the begin_timer and end_timer of two different timers (because GL supports only one GL_TIME_ELAPSED
// query at a time. I could use GL_TIMESTAMP instead, but nah).
void begin_timer(TimerQueryManager &m, TimerID timer_id);

// Same as above, but takes the name of the timer as a string.
inline void begin_timer(TimerQueryManager &m, const char *name) {
    eng::StringSymbol sym = m._name_table->to_symbol(name);
    begin_timer(m, sym.to_int());
}

// End the given timer query.
void end_timer(TimerQueryManager &m, TimerID timer_id);

// Same as above but takes the timer name as a string.
inline void end_timer(TimerQueryManager &m, const char *timer_name) {
    eng::StringSymbol sym = m._name_table->to_symbol(timer_name);
    end_timer(m, sym.to_int());
}

// Call just before finishing each frame.
void end_frame(TimerQueryManager &m);

// Before printing the max/min/avg times and closing, you might wanna wait on all pending queries. This does
// that.
void wait_for_last_frame(TimerQueryManager &m);

// Prints the results
void print_times(TimerQueryManager &m, fo::string_stream::Buffer &ss);

// Do not print warning messages timer waits do not return instanteneously
inline void set_no_warning(TimerQueryManager &m) { m._no_warning = true; }

// Disable the whole thing
inline void set_disabled(TimerQueryManager &m, bool disable) {
    if (disable) {
        wait_for_last_frame(m);
        m._cpu_frame = 9999;
    } else {
        m._cpu_frame = 0;
        m._wait_frame = -1;
    }
}

inline bool is_disabled(const TimerQueryManager &m) { return m._cpu_frame == 9999; }

} // namespace gl_timer_query

} // namespace eng

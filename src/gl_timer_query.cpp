#include <learnogl/gl_timer_query.h>
#include <learnogl/kitchen_sink.h>

static u64 g_cpu_frame_count = 0;

template <typename T, typename Compare> void insertion_sort(T *first, T *last, Compare comp) {
    for (T *p = last - 1; p >= first; --p) {
        if (comp(p[1], p[0])) {
            std::swap(p[1], p[0]);
        } else {
            break;
        }
    }
}

namespace eng {

namespace gl_timer_query {

static void init_timer_data(_TimerData &data, eng::StringSymbol name) {
    std::fill(data.time_taken_max, data.time_taken_max + NUM_INTERVALS, 0);
    std::fill(data.time_taken_min, data.time_taken_min + NUM_INTERVALS, std::numeric_limits<u64>::max());
    data.time_taken_total = 0;
    data.times_hit = 0;
    data.name = name;
}

// Add a new timer
int add_timer(TimerQueryManager &m, const char *timer_name) {
    GLuint queries[NUM_COPIES] = {};
    glGenQueries(NUM_COPIES, queries);

    LOG_F(INFO, "Adding timer query - %s", timer_name);

    eng::StringSymbol sym = m._name_table->to_symbol(timer_name);
    CHECK_F(!fo::has(m._name_to_index, sym.to_int()), "Timer '%s' already added", timer_name);

    for (u32 i = 0; i < NUM_COPIES; ++i) {
        _QueryData data = {};
        data.query_object_handle = queries[i];
        fo::push_back(m._per_frame_info[i].arr_query_data, data);
    }

    fo::set(m._name_to_index, sym.to_int(), fo::size(m._per_frame_info[0].arr_query_data) - 1);

    _TimerData timer_data;
    init_timer_data(timer_data, sym);
    fo::push_back(m._timer_data_list, timer_data);
    return sym.to_int();
}

void done_adding(TimerQueryManager &m) {
    // Initialize query objects and timer data for full frame

    GLuint queries[NUM_COPIES] = {};
    glGenQueries(NUM_COPIES, queries);

    for (u32 i = 0; i < NUM_COPIES; ++i) {
        m._per_frame_info[i].ts_data.ts_query_object = queries[i];
        m._per_frame_info[i].ts_data.fence = NULL;
    }

    eng::StringSymbol sym = m._name_table->to_symbol("full_frame");
    init_timer_data(m._frame_timer_data, sym);
    // Pretty sure the first timestamp won't be u64's max, so using it to denote we don't have >= 2 frames yet
    m._frame_timer_data.last_ts = std::numeric_limits<u64>::max();
}

static _QueryData &_get_query_data(TimerQueryManager &m, TimerID timer_id) {
    auto sym = StringSymbol(timer_id);

    auto it = fo::get(m._name_to_index, sym.to_int());

    DCHECK_NE_F(
        it, fo::end(m._name_to_index), "Timer with name '%s' not found", m._name_table->to_string(sym));

    DCHECK_LT_F(m._cpu_frame, (i32)NUM_COPIES);

    const u32 index = it->value;
    _QueryData &data = m._per_frame_info[m._cpu_frame].arr_query_data[index];

    return data;
}

static void wait_for_oldest_frame(TimerQueryManager &m, u64 timeout = 0) {
    if (is_disabled(m)) {
        return;
    }

    auto &arr_query_data = m._per_frame_info[m._wait_frame].arr_query_data;

    // Wait for GL_TIME_ELAPSED queries of each of the timers
    for (u32 i = 0; i < fo::size(arr_query_data); ++i) {
        auto &q = arr_query_data[i];
        auto &t = m._timer_data_list[i];

        if (q.fence == nullptr) {
            // This timer was not started/ended this frame
            continue;
        }

        // See if fence is ready
        GLenum status = glClientWaitSync(q.fence, 0, timeout);

        u64 time_elapsed = 0;

        if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
            glGetQueryObjectui64v(q.query_object_handle, GL_QUERY_RESULT, &time_elapsed);
        } else {
            if (!m._no_warning) {
                LOG_F(WARNING,
                      R"(%s - glClientWaitSync returned %s while waiting for timer - %s.
If it returned TIMEOUT_EXPIRED, the commands this timer encapsulates took too long to complete, and we will
keep waiting until they complete.
)",
                      __PRETTY_FUNCTION__,
                      status == GL_WAIT_FAILED
                          ? "GL_WAIT_FAILED"
                          : status == GL_TIMEOUT_EXPIRED ? "TIMEOUT_EXPIRED" : std::to_string(status).c_str(),
                      m._name_table->to_string(t.name));
            }

            // Flush commands and wait
            while (status == GL_TIMEOUT_EXPIRED) {
                status = glClientWaitSync(q.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 16 * MILLISECONDS_NS);
            }
            glGetQueryObjectui64v(q.query_object_handle, GL_QUERY_RESULT, &time_elapsed);
        }

        glDeleteSync(q.fence);
        q.fence = NULL;

        // Insertion sort into the max and min list.
        t.time_taken_min[NUM_INTERVALS] = time_elapsed;
        insertion_sort(t.time_taken_min, t.time_taken_min + NUM_INTERVALS, std::less<u64>{});

        t.time_taken_max[NUM_INTERVALS] = time_elapsed;
        insertion_sort(t.time_taken_max, t.time_taken_max + NUM_INTERVALS, std::greater<u64>{});

        t.times_hit++;
        t.time_taken_total += time_elapsed;
    }

    // Wait for the completion of the GL_TIMESTAMP query of the frame
    auto &q = m._per_frame_info[m._wait_frame].ts_data;
    auto &t = m._frame_timer_data;

    GLbitfield wait_flags = 0;
    if (timeout > 0) {
        wait_flags = GL_SYNC_FLUSH_COMMANDS_BIT;
    }

    // Same thing as before, see if fence is ready.
    GLenum status = glClientWaitSync(q.fence, wait_flags, timeout);

    u32 times_waited = 0;
    while (status == GL_TIMEOUT_EXPIRED) {
        status = glClientWaitSync(q.fence, wait_flags, 16 * MILLISECONDS_NS);
        ++times_waited;
    }

    u64 timestamp = 0;

    if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
        glGetQueryObjectui64v(q.ts_query_object, GL_QUERY_RESULT, &timestamp);
    } else {

        if (!m._no_warning) {
            LOG_F(ERROR,
                  "%s - glClientWaitSync returned %s while waiting for timer - %s. Should not happen.",
                  __PRETTY_FUNCTION__,
                  status == GL_WAIT_FAILED
                      ? "GL_WAIT_FAILED"
                      : status == GL_TIMEOUT_EXPIRED ? "TIMEOUT_EXPIRED" : std::to_string(status).c_str(),
                  m._name_table->to_string(t.name));
        }

        // Flush commands and wait
        while (status == GL_TIMEOUT_EXPIRED) {
            status = glClientWaitSync(q.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 16 * MILLISECONDS_NS);
        }
        glGetQueryObjectui64v(q.ts_query_object, GL_QUERY_RESULT, &timestamp);
    }

    glDeleteSync(q.fence);
    q.fence = NULL;

    if (t.last_ts != std::numeric_limits<u64>::max()) {
        u64 time_elapsed = timestamp - t.last_ts;
        t.time_taken_min[NUM_INTERVALS] = time_elapsed;
        insertion_sort(t.time_taken_min, t.time_taken_min + NUM_INTERVALS, std::less<u64>{});

        t.time_taken_max[NUM_INTERVALS] = time_elapsed;
        insertion_sort(t.time_taken_max, t.time_taken_max + NUM_INTERVALS, std::greater<u64>{});
        t.time_taken_total += time_elapsed;
    }

    t.last_ts = timestamp;
    t.times_hit++;
}

void new_frame(TimerQueryManager &m) {
    if (is_disabled(m)) {
        return;
    }

    if (m._wait_frame == -1) {
        return;
    }
    // Wait for oldest set of queries
    wait_for_oldest_frame(m);
}

void end_frame(TimerQueryManager &m) {
    if (is_disabled(m)) {
        return;
    }

    // Issue the timestamp query for the whole frame
    {
        auto &frame_query_data = m._per_frame_info[m._cpu_frame].ts_data;
        glQueryCounter(frame_query_data.ts_query_object, GL_TIMESTAMP);
        DCHECK_EQ_F(frame_query_data.fence, nullptr);
        frame_query_data.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }

    // Increment CPU frame to next slot
    m._cpu_frame = (m._cpu_frame + 1) % NUM_COPIES;

    // Increment GPU frame to next slot if pipeline is full (i.e. NUM_COPIES frames worth of commands have
    // been issued), otherwise don't increment yet. A bit of a corner case.
    if (m._wait_frame != -1) {
        m._wait_frame = (m._wait_frame + 1) % NUM_COPIES;
    } else {
        m._wait_frame = m._cpu_frame == 0 ? 0 : m._wait_frame;
    }

    ++g_cpu_frame_count;
}

void begin_timer(TimerQueryManager &m, TimerID timer_id) {
    if (is_disabled(m)) {
        return;
    }
    auto &data = _get_query_data(m, timer_id);
    glBeginQuery(GL_TIME_ELAPSED, data.query_object_handle);
}

void end_timer(TimerQueryManager &m, TimerID timer_id) {
    if (is_disabled(m)) {
        return;
    }
    auto &data = _get_query_data(m, timer_id);
    glEndQuery(GL_TIME_ELAPSED);
    data.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void wait_for_last_frame(TimerQueryManager &m) {
    if (is_disabled(m)) {
        return;
    }

    if (m._wait_frame == -1) {
        return;
    }

    LOG_F(INFO, "wait 0");

    wait_for_oldest_frame(m, 16 * MILLISECONDS_NS);
    m._wait_frame = (m._wait_frame + 1) % NUM_COPIES;

    LOG_F(INFO, "wait 1");
    wait_for_oldest_frame(m, 16 * MILLISECONDS_NS);
    m._wait_frame = (m._wait_frame + 1) % NUM_COPIES;

    LOG_F(INFO, "wait 2");
    wait_for_oldest_frame(m, 16 * MILLISECONDS_NS);
}

void print_times(TimerQueryManager &m, fo::string_stream::Buffer &ss) {
    if (is_disabled(m)) {
        return;
    }

    using namespace fo::string_stream;

    ss << "Timers - \n";

    const auto print = [&](_TimerData &timer_data, const char *timer_name) {
        ss << " " << timer_name << "\n";

        ss << " -- Max times - \n";

        bool unused = true;

        for (u32 i = 0; i < NUM_INTERVALS; ++i) {
            u64 time = timer_data.time_taken_max[i];
            if (time == 0) {
                continue;
            }
            ss << "      " << double(time) / MILLISECONDS_NS << " ms\n";
            unused = false;
        }

        if (unused) {
            ss << "      "
               << "Unused\n";
        }

        ss << " -- Min times - \n";

        if (!unused) {
            for (u32 i = 0; i < NUM_INTERVALS; ++i) {
                u64 time = timer_data.time_taken_min[i];
                if (time == std::numeric_limits<u64>::max()) {
                    continue;
                }
                unused = false;
                ss << "      " << double(time) / MILLISECONDS_NS << " ms\n";
            }

            ss << "-- Avg time - "
               << (double(timer_data.time_taken_total) / timer_data.times_hit) / MILLISECONDS_NS << " ms\n";
        } else {
        }

        if (unused) {
            ss << "      "
               << "Unused\n";
        }
    };

    for (auto &e : m._name_to_index) {
        auto &timer_data = m._timer_data_list[e.value];
        auto name = m._name_table->to_string(StringSymbol(e.key));
        print(timer_data, name);
    }

    print(m._frame_timer_data, "full_frame");
}

} // namespace gl_timer_query

} // namespace eng

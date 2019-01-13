#pragma once

#include "stopwatch.h"

#include <algorithm> // std::min

#ifdef _MSC_VER
#    undef min
#endif

namespace app_loop {

/// The number of frames per second we try to render.
constexpr unsigned TARGET_FPS = 60;
/// The time we should take to render each frame.
constexpr double TARGET_FRAME_TIME = 1.0 / TARGET_FPS;
/// Number of second in one nanosecond
constexpr double ONE_NANOSEC_IN_SEC = double(std::nano::num) / std::nano::den;

/// Clock and precision
using Clock = std::chrono::high_resolution_clock;

/// Want nanosecond precision available
static_assert(Clock::duration::period::num == 1 && Clock::duration::period::den == 1000000000,
              "Nanosecond precision not available");

/// Contains some state for the app.
struct State {
    double frame_time_in_sec;                 // Time elapsed between two completions of render
    double delta_time_in_sec;                 // The delta time. Use this interval for simulations
    double total_time_in_sec;                 // Time since first entry to the game loop
    stop_watch::State<Clock> frame_stopwatch; // Times successive renders
};

/// Called once before the game loop.
template <typename AppType> void init(AppType &app);

/// Called once each game loop.
template <typename AppType> void update(AppType &app, State &state);

/// Called once each game loop.
template <typename AppType> void render(AppType &app);

/// Called after game loop.
template <typename AppType> void close(AppType &app);

/// Called to check if app should close.
template <typename AppType> bool should_close(AppType &app);

/// The run function. Very bare-bones. Doesn't even swap the buffers.
template <typename AppType> void run(AppType &app, State &state);

template <typename AppType> void run(AppType &app, State &state) {
    init(app);

    stop_watch::start(state.frame_stopwatch);

    constexpr double dt = TARGET_FRAME_TIME;

    while (!should_close(app)) {
        double frame_time_in_sec = stop_watch::restart(state.frame_stopwatch).count() * ONE_NANOSEC_IN_SEC;
        state.frame_time_in_sec = frame_time_in_sec;

        // Did we take way too long, like in a debug session?
        if (frame_time_in_sec > 1.0) {
            frame_time_in_sec = dt;
            state.frame_time_in_sec = dt;
        }

#if 0
        while (frame_time_in_sec > 0.0) {
            state.delta_time_in_sec = std::min(state.frame_time_in_sec, dt);
            update(app, state);
            frame_time_in_sec -= state.delta_time_in_sec;
            state.total_time_in_sec += state.delta_time_in_sec;
        }
#else
        state.delta_time_in_sec = state.frame_time_in_sec;
        update(app, state);
        state.total_time_in_sec += state.delta_time_in_sec;
#endif

        render(app);
    }
    close(app);
}

} // namespace app_loop

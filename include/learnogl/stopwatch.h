#pragma once

#include <chrono>

namespace stop_watch {

/// Represents a stop watch. `ClockType` can be any clock type from
/// std::chrono.
template <typename ClockType = std::chrono::high_resolution_clock> struct State {
    typename ClockType::time_point _start_point; // Time point at which clock was last started
    typename ClockType::time_point _stop_point;  // Time point at which clock was last stopped
};

using HighRes = State<>;

/// 'Starts' the watch
template <typename ClockType> void start(State<ClockType> &w) { w._start_point = ClockType::now(); }

/// Stops the watch and returns the duration it ran. Calling this on a default
/// constructed watch will return 0, because std::chrono::time_point defaults
/// to the clocks epoch.
template <typename ClockType> typename ClockType::duration stop(State<ClockType> &w) {
    w._stop_point = ClockType::now();
    return w._stop_point - w._start_point;
}

/// Does the equivalent of stopping and starting the watch. Returns the
/// duration it ran.
template <typename ClockType> typename ClockType::duration restart(State<ClockType> &w) {
    w._stop_point = ClockType::now();
    auto dura = w._stop_point - w._start_point;
    w._start_point = ClockType::now();
    return dura;
}

} // namespace stop_watch

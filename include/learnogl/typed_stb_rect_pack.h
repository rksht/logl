#pragma once

#define STBRP_LARGE_RECTS 1

#include <learnogl/stb_rect_pack.h>
#include <scaffold/vector.h>

namespace eng {

struct ResultOfRectPack {
    int num_rects_given = 0;
    int num_rects_packed = 0;

    bool packed_all_of_em() const { return num_rects_not_packed == 0; }

    int num_rects_not_packed() const { return num_rects_given - num_rects_packed; }
};

enum class _TypedRectPackState {
    ADDING_NEW_RECTS,
    JUST_PACKED_RECTS,
};

template <typename T> struct TypedRectPack {
    fo::Vector<T> _elements;
    fo::Vector<stbrp_rect> _rects;
    u32 _starting_element_index = 0;

    bool calculated = false;

    stbrp_context _stbrp_context = {};

    _TypedRectPackState _state = ADDING_NEW_RECTS;

    TypedRectPack(u32 expected_rects = 0,
                  fo::Allocator &allocator = fo::memory_globals::default_allocator,
                  u32 starting_element_index = 0)
        : _elements(allocator)
        , _rects(allocator) {
        fo::reserve(_elements, expected_rects);
        fo::reserve(_rects, expected_rects);
        _starting_element_index = starting_element_index;
    }

    T &operator[](int i) { return elements[i]; }

    void add_rect(T element, int width, int height) {
        stbrp_rect rect{};
        rect.id = (int)(fo::size(_elements) + _starting_element_index);
        rect.w = width;
        rect.h = height;

        fo::push_back(_rects, rect);
        fo::push_back(_elements, std::move(element));
        _state = ADDING_NEW_RECTS;
    }

    // Try to pack the rectangles. Returns result indicating how many of the added rectangles got packed.
    bool pack_rects(fo::Allocator &node_storage_allocator, ResultOfRectPack &result_out) {
        if (_state == JUST_PACKED_RECTS) {
            LOG_F(INFO, "")
            return false;
        }
    }
};

} // namespace eng

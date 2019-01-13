// Lofi immediate mode UI
#include "essentials.h"

#include <argh.h>
#include <learnogl/font.h>

// widgets to do - slider, radio button list, check button list.

namespace ui {

struct WidgetID {
    uintptr_t id;
    int index;
    uintptr_t parent;
};

struct State {
    int mouse_x;
    int mouse_y;
    int delta_mouse_x;
    int delta_mouse_y;

    u8 left_up, left_down;
    u8 right_up, right_down;
    u8 middle_up, middle_down;

    WidgetID active;
    WidgetID hot;
    WidgetID hot_to_be;

    uintptr_t cur_parent;
    int cur_index;
    uintptr_t cur_id;

    u8 some_widget_is_hot;
    u8 some_widget_is_active;
    u8 some_widget_went_active;

    // Internal
    u8 prev_valid;
    int prev_mx;
    int prev_my;

    int drag_x;
    int drag_y;

    font::FontData *font_data; // Must point to valid FontData object
};

struct Layout {
    RGBA8 color;
    RGBA8 bg, bg_dark, bg_lite;

    int spacing_w;
    int spaceing_h;

    int force_w;
    int force_w_once;

    int button_padding_w;
    int button_padding_h;

    int slider_tab_w;
    int slider_tab_h;
    int slider_slot_h;
    int slider_slot_default_w; // only used if width can't be computed otherwise

    int slider_dot_spacing;

    int cx, cy;
};

void init(State *state, Layout *layout, font::FontData *font_data) {
    memset(state, 0, sizeof(State));
    memset(state, 0, sizeof(Layout));
    state->font_data = font_data;
}

void frame_begin(State &state) {
}

void frame_end(State &state);

void done_with_frame(State &state) {
    state.left_down = state.left_up = state.right_down = state.right_up = state.middle_down =
        state.middle_up = 0;
}

// Set the widget as "hot to be" in the next frame
inline void set_widget_as_hot(State &state, uintptr_t id) {
    state.hot_to_be.id = id;
    state.hot_to_be.index = state.cur_index;
    state.hot_to_be.parent = state.cur_parent;
}

inline void set_widget_as_active(State &state, uintptr_t id) {
    state.active.id = id;
    state.active.parent = state.cur_parent;
    state.active.index = state.cur_index;
}

inline void set_no_active_widget(State &state) {
    state.active.id = 0;
    state.active.index = 0;
    state.active.parent = 0;

    done_with_frame(state);
}

inline bool mouse_in_rect(const State &state, int x, int y, int w, int h) {
    const int xmax = x + h;
    const int ymax = y + h;
    return state.mouse_x >= x && state.mouse_x < xmax && state.mouse_y >= y && state.mouse_y < ymax;
}

inline bool any_widget_active(const State &state) { return state.active.id == 0; }

inline bool is_widget_hot(const State &state, uintptr_t id) {
    id = state.cur_id ? state.cur_id : id;
    return id == state.hot.id && state.cur_index == state.hot.index && state.cur_parent == state.hot.parent;
}

inline bool is_widget_active(const State &state, uintptr_t id) {
    id = state.cur_id ? state.cur_id : id;
    return id == state.active.id && state.cur_index == state.active.index &&
           state.cur_parent == state.active.parent;
}

// -- Widget logic

bool button(State &state, int x, int y, int w, int h, uintptr_t id) {
    bool result = false;

    const bool is_over = mouse_in_rect(state, x, y, w, h);
    if (!any_widget_active(state)) {
        if (is_over) {
            set_widget_as_hot(state, id);
        }

        if (is_widget_hot(state, id) && state.left_down) {
            set_widget_as_active(state, id);
        }
    }

    // If button active, react on left up
    if (is_widget_active(state, id)) {
        state.some_widget_is_active = 1;
        if (is_over) {
            set_widget_as_hot(state, id);
        }
        if (state.left_up) {
            if (is_widget_hot(state, id)) {
                result = true;
            }
            done_with_frame(state);
        }
    }

    if (is_widget_hot(state, id)) {
        state.some_widget_is_hot = 1;
    }

    return result;
}

// --- Impl

void frame_begin(State &ui) {
    ui.hot = ui.hot_to_be;
    ui.hot_to_be.parent = 0;
    ui.hot_to_be.id = 0;
    ui.hot_to_be.index = 0;
}

} // namespace ui

// Function definitions

int main(int ac, char **av) {
    eng::init_non_gl_globals();
    eng::init_gl_globals();
    DEFERSTAT(eng::close_gl_globals());
    DEFERSTAT(eng::close_non_gl_globals());

    ui::State state;
    ui::Layout layout;
}

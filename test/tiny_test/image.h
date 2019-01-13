#pragma once
#include "idk.h"

struct ColorRGBA8 {
    u8 r, g, b, a;

    static ColorRGBA8 from_hex_alpha(u32 hex) {
        return ColorRGBA8{u8((hex & 0xff000000) >> 24), u8((hex & 0x00ff0000) >> 16),
                          u8((hex & 0x0000ff00) >> 8), u8(hex & 0x000000ff)};
    }

    static ColorRGBA8 from_hex(u32 hex) {
        return ColorRGBA8{u8((hex & 0xff0000) >> 16), u8((hex & 0x00ff00) >> 8), u8(hex & 0x0000ff), 255};
    }
};

static_assert(sizeof(ColorRGBA8) == 4, "");

// Basic image object using stb_image functions
struct Image {
    ColorRGBA8 *_data;
    u32 _width;
    u32 _height;

    Image(u32 width, u32 height) {
        _width = width;
        _height = height;
        _set_self_allocated();
        _data = (ColorRGBA8 *)ALLOCATOR.allocate(num_pixels() * 4);
    }

    // Data must be returned by stbi_load
    Image(u8 *data, u32 width, u32 height)
        : _data(reinterpret_cast<ColorRGBA8 *>(data))
        , _width(width)
        , _height(height) {}

    ~Image() {
        if (!_data) {
            return;
        }

        if (_is_self_allocated()) {
            ALLOCATOR.deallocate(_data);
        } else {
            stbi_image_free(_data);
        }
        _data = nullptr;
    }

    Image(const Image &other) {
        _width = other.width();
        _height = other.height();
        _set_self_allocated();
        _data = reinterpret_cast<ColorRGBA8 *>(
            ALLOCATOR.allocate(other.width() * other.height() * sizeof(ColorRGBA8)));
        memcpy(_data, other._data, sizeof(ColorRGBA8) * num_pixels());
    }

    Image(Image &&other)
        : _data(other._data)
        , _width(other._width)
        , _height(other._height) {
        other._data = nullptr;
    }

    ColorRGBA8 *data() { return _data; }
    const ColorRGBA8 *data() const { return _data; }

    ColorRGBA8 &pixel(u32 x, u32 y) { return _data[y * width() + x]; }
    const ColorRGBA8 &pixel(u32 x, u32 y) const { return _data[y * width() + x]; }

    i32 height() const { return i32(_height & ~(u32(1) << 31)); }
    i32 width() const { return i32(_width); }

    u32 num_pixels() const { return width() * height(); }

    void _set_self_allocated() { _height |= (u32(1) << 31); }
    bool _is_self_allocated() { return (_height & (u32(1) << 31)) != 0; }

    void write_to_file(const char *filename) {
        log_assert(0 != stbi_write_png(filename, (int)width(), (int)height(), 4, (u8 *)data(), 0),
                   "Failed to write image to file");
    }

    void clear_with_color(ColorRGBA8 c) {
        for (u32 i = 0; i < num_pixels(); ++i) {
            _data[i] = c;
        }
    }

    void clear_with_zeroes() { memset(_data, 0, num_pixels() * 4); }

    static Image from_file(const char *filename) {
        int w, h, channels;
        u8 *data = stbi_load(filename, &w, &h, &channels, 4);
        assert(data);
        assert(channels == 4);
        return Image(data, w, h);
    }
};

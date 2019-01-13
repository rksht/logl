""" Generates a texture containing bitmap glyphs of the numerals
"""

import cairo
import subprocess as sp
from PIL import Image

image_size = (256, 32)
surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, *image_size)
cr = cairo.Context(surface)
padding = 3

cr.select_font_face('Iosevka Term', cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_BOLD)
cr.set_font_size(32)
cr.set_source_rgb(1, 1, 1)

glyphs = '''#pragma once

// Contains the position of a glyph in the bitmap. (0, 0) is in the upper left
// corner.
struct GlyphPosition {
    int x;
    int y;
};

struct GlyphMetrics {
    int x_bearing;
    int y_bearing;
    int width;
    int height;
    int x_advance;
    int y_advance;
};

struct Glyph {
    GlyphPosition position;
    GlyphMetrics metrics;
};

// clang-format off
static constexpr Glyph NumeralGlyphs[] = {
'''

# Render glyphs '0' through '9' and write out their contents
x, y = 0, 0
for character in '0123456789':
    extents = cr.text_extents(character)
    x_bearing, y_bearing, width, height, x_advance, y_advance = extents
    glyphs += '    {{%d, %d}, ' % (x, y)
    glyphs += '{%d, %d, %d, %d, %d, %d}},\n' % extents
    cr.save()
    cr.translate(x, -y_bearing)
    cr.text_path(character)
    cr.fill()
    cr.restore()
    x += width + padding
glyphs += '};\n// clang-format on\n'

texture_image_name = 'numerals_texture.png'
header_file_name = '../textures/numerals_texture.h'

# Extract the alpha channel and open it up for a quick preview (using feh)
surface.write_to_png(texture_image_name)
sp.run(['feh', texture_image_name])
image = Image.open(texture_image_name)
image.load()
image.split()[3].save(texture_image_name)
sp.run(['feh', texture_image_name])

import png_to_c

# File to write to
f = open(header_file_name, 'w+')
f.write(glyphs)
data, num_rows, num_cols = png_to_c.get_image_data(texture_image_name, 'L', '')
f.write(png_to_c.make_header(data, num_rows, num_cols, 'numerals_image_data'))
f.close()

sp.run(['clang-format', '-i', header_file_name])
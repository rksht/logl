#!/usr/bin/env python

""" Converts png file to C header.
"""

import argparse
from PIL import Image

DEBUG_SAVE_TRANSPOSED = True

channel_count = {'L': 1, 'RG': 2, 'RGB': 3, 'RGBA': 4}


def _transpose(data, num_cols, num_rows, mode):
    size = num_cols * num_rows
    transposed = [None] * size

    t_num_cols = num_rows
    for d_col in range(0, num_cols):
        t_row = d_col
        for i in range(0, num_rows):
            transposed[t_row * t_num_cols + i] = data[d_col * num_rows + i]

    if DEBUG_SAVE_TRANSPOSED:
        image = Image.new(mode, (num_rows, num_cols))
        image.putdata(transposed)
        image.save('Transposed.png')

    return transposed


def get_image_data(png_file, mode, save, noflip=False):
    """ Flips the image and converts it to row-major order. See comment below.
    Returns the image data and number of rows and columns
    """

    image = Image.open(png_file)
    image.load()
    num_cols, num_rows = image.size
    data = list(image.getdata())

    def swap_rows(c_0, c_1):
        start_0, start_1 = c_0 * num_rows, c_1 * num_rows
        for i in range(0, num_rows):
            pix_0, pix_1 = start_0 + i, start_1 + i
            data[pix_0], data[pix_1] = data[pix_1], data[pix_0]

    if not noflip:
        for i in range(0, num_cols // 2):
            swap_rows(i, num_cols - i - 1)

    if save != '':
        image.putdata(data)
        image.save(save, 'PNG')

    # PIL stores the data in column-major order, but C array should be in row
    # major, so we should transpose the data. We are doing this here after
    # saving with `putdata`, or it would fail if image is not a square.
    data = _transpose(data, num_cols, num_rows, mode)
    return data, num_rows, num_cols


def _flatten_sequence(data):
    for v in data:
        if type(v) is list or type(v) is tuple:
            for i in v:
                yield i
        else:
            yield v


def make_header(data, num_rows, num_cols, array_name):
    values = ', '.join(str(v) for v in _flatten_sequence(data))

    s1 = 'constexpr unsigned char {}[] = '.format(array_name)
    s3 = s1 + '{' + values + '};\n'
    s4 = s3 + 'int {}_width = {};\nint {}_height = {};\n'.format(array_name, num_cols, array_name, num_rows)
    return s4


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('png_file', type=str, help='File name')
    ap.add_argument('--mode', '-m', type=str, help='Mode to use (L, RGB, RGBA)')
    ap.add_argument('--noflip', '-f', default=True, action='store_true', help='Do not flip vertically')
    ap.add_argument('--save', '-s', default='', type=str, help='File to save resulting image (due to flip)')
    ap.add_argument('--name', '-n', default='image_array', type=str, help='Name of array')

    args = ap.parse_args()
    data, num_rows, num_cols = get_image_data(args.png_file, args.mode, args.save, args.noflip)
    print(make_header(data, num_rows, num_cols, args.name))

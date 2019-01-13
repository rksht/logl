""" Python interface to STB_IMAGE.and STB_IMAGE.write """

import ctypes as c
import numpy as np
import typing
import platform

USE_NUMPY = False

STB_IMAGE = None

def init(path_to_stb_dll):
    global STB_IMAGE
    STB_IMAGE = c.CDLL(path_to_stb_dll)

    STB_IMAGE.stbi_load.argtypes = [c.c_char_p, c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_int), c.c_int]
    STB_IMAGE.stbi_load.restype = c.POINTER(c.c_ubyte)
    STB_IMAGE.stbi_image_free.argtypes = [c.POINTER(c.c_ubyte)]
    STB_IMAGE.stbi_write_png.argtypes = [c.c_char_p, c.c_int, c.c_int, c.c_int, c.POINTER(c.c_ubyte), c.c_int]

class RawImage:
    def __init__(self, width, height, num_channels, image_data):
        self.width = width
        self.height = height
        self.num_channels = num_channels
        self.image_data = image_data # Image data, an array of bytes

    @staticmethod
    def create(width, height, num_channels):
        i = RawImage(width, height, num_channels, None)
        i.image_data = np.zeros(width * height * num_channels, dtype='uint8')
        return i


def load_png(filename, expected_num_channels):
    """ Returns a RawImage loaded from given filename """

    filename_cstr = c.create_string_buffer(bytes(filename.encode('utf-8')))
    width = c.c_int()
    height = c.c_int()
    num_channels = c.c_int()

    image_data_ptr = STB_IMAGE.stbi_load(filename_cstr, c.byref(width), c.byref(height), c.byref(num_channels), c.c_int(expected_num_channels))

    if num_channels.value != expected_num_channels:
        print("Warning - Expected num channels != actual num channels in png ({} != {})".format(expected_num_channels, num_channels.value))

    # Array type
    array_size = num_channels.value * width.value * height.value
    array_type = c.c_ubyte * array_size

    # We want the returned image data to be managed by Python, so copying into
    # a bytearray or np array
    if USE_NUMPY:
        address = c.addressof(image_data_ptr.contents)
        image_data = np.ctypeslib.as_array(array_type.from_address(address))
        image_data = np.array(image_data)
        assert image_data.dtype == 'uint8'
    else:
        image_data = array_type.from_address(c.addressof(image_data_ptr.contents))
        # Copy into bytearray
        image_data = bytearray(image_data)

    STB_IMAGE.stbi_image_free(image_data_ptr)
    return RawImage(width.value, height.value, num_channels.value, image_data)

def write_png(filename, image: RawImage):
    filename_cstr = c.create_string_buffer(bytes(filename.encode('utf-8')))
    w = c.c_int(image.width)
    h = c.c_int(image.height)
    nc = c.c_int(image.num_channels)
    zero = c.c_int(0)

    array_size = image.num_channels * image.width * image.height
    array_type = c.c_ubyte * array_size

    if USE_NUMPY:
        image_data_ptr = image.image_data.ctypes.data_as(c.POINTER(c.c_ubyte))
    else:
        # image_data_ptr = c.cast(image.image_data, c.POINTER(c.c_ubyte))
        image_data_carray = array_type.from_buffer(image.image_data)
        image_data_ptr = c.cast(image_data_carray, c.POINTER(c.c_ubyte))

    STB_IMAGE.stbi_write_png(filename_cstr, w, h, nc, image_data_ptr, zero)


def _test(filename):
    image = load_png(filename, 4)

    write_png('test.png', image)

def _test2():
    image = RawImage.create(640, 480, 4)

    from random import randint

    for i in range(640 * 480 * 4):
        image.image_data[i] = randint(0, 255)

    write_png('test.png', image)

## An image class providing some more convenience than the raw one
class Image:
    def __init__(self, raw: RawImage):
        self.raw = raw

    def __getitem__(self, xy):
        i = (xy[1] * self.raw.width + xy[0]) * self.raw.num_channels
        return [self.raw.image_data[j] for j in range(i, i + self.raw.num_channels)]

    def __setitem__(self, xy, color: typing.List):
        i = (xy[1] * self.raw.width + xy[0]) * self.raw.num_channels
        for j in range(0, self.raw.num_channels):
            self.raw.image_data[i + j] = color[j]

    @staticmethod
    def load_from_file(filename, expected_num_channels):
        raw = load_png(filename, expected_num_channels)
        return Image(raw)

    @staticmethod
    def make_new(width, height, num_channels, clear_color=None):    
        num_bytes = width * height * num_channels
        if USE_NUMPY:
            image_data = np.zeros((num_bytes, 0), dtype='uint8')
        else:
            image_data = bytearray(num_bytes)
        img = Image(RawImage(width, height, num_channels, image_data))
        if clear_color:
            img.clear(clear_color)
        return img

    def write_to_file(self, filename):
        write_png(filename, self.raw)

    def clear(self, color: typing.List):
        j = 0
        end = self.raw.width * self.raw.height * self.raw.num_channels - self.raw.num_channels
        while j != end:
            for i in range(0, self.raw.num_channels):
                self.raw.image_data[j + i] = color[i]
            j += self.raw.num_channels

def _test3(filename, expected_num_channels):
    img = Image.load_from_file(filename, expected_num_channels)

    height = img.raw.height // 2

    color = [0 for i in range(img.raw.num_channels)]

    for i in range(0, img.raw.width):
        img[(i, height)] = color

    width = img.raw.width // 2

    color = [100 for i in range(img.raw.num_channels)]

    for i in range(0, img.raw.height):
        img[(width, i)] = color

    img.write_to_file('test.png')

def _test4():
    img = Image.make_new(1000, 1000, 4, clear_color=[0, 0, 0, 255])

    width = 800

    color = [255, 255, 255, 255]

    for i in range(0, img.raw.height):
        img[(width, i)] = color

    img.write_to_file('test.png')



if __name__ == '__main__':
    import sys

    if platform.system() == 'Windows':
        init('./stb.dll')
    else:
        init(os.path.join(os.getenv('HOME'), 'build/learnogl-debug/scripts/stbi.so'))

    _test4()

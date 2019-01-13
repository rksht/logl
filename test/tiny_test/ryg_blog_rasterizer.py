# Implementing the rasterizer from Ryg blog

import stbi
import platform
import numpy as np

if platform.system() == 'Windows':
    stbi.init('./stb.dll')
else:
    stbi.init('./stb.so')

def clamp(low, high, value):
    return max(low, min(value, high))

MAX_X = 1024
MAX_Y = 1024

class Point2D:
    __slots__ = ['arr']
    def __init__(self, x, y):
        self.arr = np.array([int(x), int(y)], dtype='int32')

    @property
    def x(self): return self.arr[0]

    @x.setter
    def x(self, v):
        self.arr[0] = v

    @property
    def y(self): return self.arr[1]

    @y.setter
    def y(self, v):
        self.arr[1] = v

def signed_area2(a: Point2D, b: Point2D, c: Point2D):
    return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)

def rasterize_triangle(v0: Point2D, v1: Point2D, v2: Point2D):
    # Bounding box, clipped to screen width and height
    lo_x = min(v0.x, v1.x, v2.x)
    lo_y = min(v0.y, v1.y, v2.y)
    hi_x = max(v0.x, v1.x, v2.x)
    hi_y = max(v0.y, v1.y, v2.y)

    lo_x = max(0, lo_x)
    lo_y = max(0, lo_y)
    hi_x = min(MAX_X - 1, hi_x)
    hi_y = min(MAX_Y - 1, hi_y)

    # Rasterize
    p = Point2D(0, 0)
    for y in range(lo_y, hi_y + 1):
        p.y = y
        for x in range(lo_x, hi_x + 1):
            p.x = x
            w_0 = signed_area2(p, v0, v1)
            w_1 = signed_area2(p, v1, v2)
            w_2 = signed_area2(p, v2, v0)
            if w_0 >= 0 and w_1 >= 0 and w_2 >= 0:
                yield p, w_0, w_1, w_2

class Vertex:
    def __init__(self, x, y, color):
        self.v = Point2D(x, y)
        self.color = color

def draw_triangle(vert_0, vert_1, vert_2, img):
    area = signed_area2(vert_0.v, vert_1.v, vert_2.v)
    print(area)
    assert area >= 0.0
    for p, w_0, w_1, w_2 in rasterize_triangle(vert_0.v, vert_1.v, vert_2.v):
        color = vert_0.color * w_0 + vert_1.color * w_1 + vert_2.color * w_2
        color = color / area
        img[(p.x, p.y)] = [int(color[0] * 255), int(color[1] * 255), int(color[2] * 255)]


triangle_list = [
    [
        Vertex(200, 200, np.array([1.0, 0.0, 0.0])),
        Vertex(700, 700, np.array([0.0, 0.0, 1.0])),
        Vertex(100, 600, np.array([0.0, 1.0, 0.0]))
    ]
]

img = stbi.Image.make_new(MAX_X, MAX_Y, 3)
img.clear([255, 255, 255])

for t in triangle_list:
    draw_triangle(t[0], t[1], t[2], img)

img.write_to_file("tri.png")

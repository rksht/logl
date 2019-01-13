import numpy as np

class Vec2:
    def __init__(self, x = 0.0, y = 0.0):
        self.v = np.array([x, y])

    @property
    def x(self):
        return self.v[0]

    @property
    def y(self):
        return self.v[1]

    @x.setter
    def x_setter(self, value):
        self.v[0] = value

    @y.setter
    def y_setter(self, value):
        self.v[1] = value

    def __str__(self):
        return('[{}, {}]'.format(self.v[0], self.v[1]))


def corner_pos(i):
    v = Vec2()
    v.v[0] = 1.0 if (2 <= i and i <= 4) else -1.0
    v.v[1] = -1.0 if (1 <= i and i <= 3) else 1.0
    return v

for i in range(0, 6):
    print('{} - {}'.format(i, corner_pos(i)))

print("Bill's parameterization")

def vpos(id):
    v = Vec2()
    v.v[0] = (id // 2) * 4.0 - 1.0
    v.v[1] = (id % 2) * 4.0 - 1.0

    return v

def uv(id):
    v = Vec2()
    v.v[0] = (id // 2) * 2.0
    v.v[1] = 1.0 - (id % 2) * 2.0
    return v

for i in range(0, 3):
    print('id = {}, vpos = {}, uv = {}'.format(i, vpos(i), uv(i)))

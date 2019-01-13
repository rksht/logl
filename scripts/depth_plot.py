# The cob transformation of a position from view space to NDC space is non-linear if perspective projection is
# used. So the inverse from is also non-linear.

import matplotlib.pyplot as plt
import numpy as np

plt.style.use('seaborn-whitegrid')

class Vec4:
    def __init__(self, x = 0.0, y = 0.0, z = 0.0, w = 0.0, array = None):
        if array is not None:
            self.v = array
        else:
            self.v = np.array([x, y, z, w])

    @staticmethod
    def from_array(self, a):
        assert len(a) == 4
        return Vec4(None, None, None, None, a)

    @property
    def x(self):
        return self.v[0]

    @property
    def y(self):
        return self.v[1]

    def __str__(self):
        return('[{}, {}]'.format(self.v[0], self.v[1]))

    def transform_with(mat4):
        v = np.dot(mat4, self.v)
        return Vec4.from_array(v)


class Matrix4x4:
    def __init__(self, x, y, z, t):
        self.m = np.array([x, y, z, t])
        self.m = np.transpose(self.m)

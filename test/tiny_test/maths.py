import numpy as np
import math

ONE_DEG_IN_RADS = math.pi / 180.0

def create_clip_transform(n, f, hfov, a):
    e = 1.0 / math.tan(hfov / 2.0)
    sx = e
    sy = e * a
    sz = (n + f) / (n - f)
    qz = 2.0 * n * f / (n - f)

    return np.array([
        [sx, 0, 0, 0],
        [0, sy, 0, 0],
        [0, 0, sz, qz],
        [0, 0, -1, 0]
    ], dtype='float32')

def cross_prod(xyz1, xyz2):
    return np.array([
        xyz1[1] * xyz2[2] - xyz1[2] * xyz2[1],
        xyz1[2] * xyz2[0] - xyz1[0] * xyz2[2],
        xyz1[0] * xyz2[1] - xyz1[1] * xyz2[0]], dtype='float32'
    )

def signed_area2(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])

def normalize_vec3(xyz):
    return xyz / math.sqrt(np.dot(xyz, xyz))

def length(xyz):
    return math.sqrt(np.dot(xyz, xyz))

def length_squared(xyz):
    return np.dot(xyz, xyz)

def length4(xyzw):
    return np.sqrt(np.dot(xyzw, xyzw))

UNIT_X = np.array([1.0, 0.0, 0.0], dtype='float32')
UNIT_Y = np.array([0.0, 1.0, 0.0], dtype='float32')
UNIT_Z = np.array([0.0, 0.0, 1.0], dtype='float32')

IDENTITY_MAT4 = np.array([
    [1.0, 0.0, 0.0, 0.0],
    [0.0, 1.0, 0.0, 0.0],
    [0.0, 0.0, 1.0, 0.0],
    [0.0, 0.0, 0.0, 1.0]
], dtype='float32')

IDENTITY_MAT3 = np.array([UNIT_X, UNIT_Y, UNIT_Z], dtype='float32')

class Quaternion:
    def __init__(self, q):
        self.q = q

    def normalize(self):
        self.q = self.q / length4(self.q)

    @staticmethod
    def from_axis_angle(v3_axis, flt_angle):
        s = math.sin(flt_angle)
        c = math.cos(flt_angle)
        return Quaternion(np.array([s * v3_axis[0], s * v3_axis[1], s * v3_axis[2], c], dtype='float32'))

    def __mul__(self, other):
        return Quaternion(
            np.array(
                [
                    self.q[3]*other.q[0] + self.q[0]*other.q[3] + self.q[1]*other.q[2] - self.q[2]*other.q[1], # x
                    self.q[3]*other.q[1] - self.q[0]*other.q[2] + self.q[1]*other.q[3] + self.q[2]*other.q[0], # y
                    self.q[3]*other.q[2] + self.q[0]*other.q[1] - self.q[1]*other.q[0] + self.q[2]*other.q[3], # z
                    self.q[3]*other.q[3] - self.q[0]*other.q[0] - self.q[1]*other.q[1] - self.q[2]*other.q[2]  # w
                ]
            )
        )

    def apply(self, xyz):
        q_vec = np.array([self.q[0], self.q[1], self.q[2]], dtype='float32')
        v_0 = (self.q[3] * self.q[3] - np.dot(q_vec, q_vec)) * xyz
        v_1 = 2.0 * self.q[3] * cross_prod(q_vec, xyz)
        v_2 = 2.0 * np.dot(q_vec, xyz)
        return v_0 + v_1 + v_2

    def make_matrix(self):
        x = self.q[0];
        y = self.q[1];
        z = self.q[2];
        w = self.q[3];
        xs = x * x
        ys = y * y
        zs = z * z
        return np.array([
            [1 - 2*ys - 2*zs,   2*x*y - 2*w*z,      2*x*z + 2*w*y,      0],
            [2*x*y + 2*w*z,     1 - 2*xs - 2*zs,    2*y*z - 2*w*x,      0],
            [2*x*z - 2*w*y,     2*y*z + 2*w*x,      1 - 2*xs - 2*ys,    0],
            [0, 0, 0, 1]
        ], dtype='float32')

    def get_normalized(self):
        return Quaternion(self.q / math.sqrt(np.dot(self.q, self.q)))

IDENTITTY_VERSOR = Quaternion(np.array([0.0, 0.0, 0.0, 1.0], dtype='float32'))


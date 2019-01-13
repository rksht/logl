import numpy as np

def ortho(n, f, l, b, r, t):
    return np.array([
        [2.0/(r - l), 0.0, 0.0, -(r + l)/(r - l)],
        [0.0, 2.0/(t - b), 0.0, -(t + b)/(t - b)],
        [0.0, 0.0, -2.0/(f - n), -(f + n)/(f - n)],
        [0.0, 0.0, 0.0, 1.0]
    ])

def ortho_1(l, b, r, t):
    return np.array([
        [2.0/(r - l), 0.0, 0.0, -(r + l)/(r - l)],
        [0.0, 2.0/(t - b), 0.0, -(t + b)/(t - b)],
        [0.0, 0.0, 1.0, 1.0],
        [0.0, 0.0, 0.0, 1.0]
    ])


def vec2(v):
    return np.array([v[0], v[1], 0, 1])

vecs = [
    [25.000000, 25.000000],
    [49.000000, 55.000000],
    [25.000000, 55.000000],
    [25.000000, 25.000000],
    [49.000000, 25.000000],
    [49.000000, 55.000000]
]

vecs = list(vec2(v) for v in vecs)

w = 800
h = 600
n = 0.1
f = 1000.0

proj = ortho(n, f, 0, 0, 800, 600)
proj1 = ortho_1(0, 0, 800, 600)

for v in vecs:
    print(v, np.dot(proj, v), np.dot(proj1, v))
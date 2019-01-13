# Generate the weights required for filtering linear-interpolated texels so that the net-effect is that of
# applying Gaussian filtering on the un-interpolated texels.

from random import randint
from typing import List, Set, Dict, Tuple, Optional
from fractions import Fraction

class PascalsTriangle:
    def __init__(self, n):
        self.row_length = n + 1
        self.table = [0] * self.row_length * self.row_length

        for n in range(0, self.row_length):
            self.table[n * self.row_length] = 1

        for n in range(1, self.row_length):
            for k in range(1, self.row_length):
                self.table[n * self.row_length + k] = self.table[(n - 1) * self.row_length + k] + self.table[(n - 1) * self.row_length + (k - 1)]

    def row_begin(self, n):
        return n * self.row_length

    def row_end(self, n):
        return self.row_begin(n + 1)


def gen_binomial_coefficients(n):
    triangle = PascalsTriangle(n - 1)
    coefficients = [0] * n
    x = 0
    for i in range(triangle.row_begin(n - 1), triangle.row_end(n - 1)):
        coefficients[x] = triangle.table[i]
        x += 1

    return coefficients

def gaussian_weights(n):
    g = gen_binomial_coefficients(n)
    s = sum(g)
    return [x / s for x in g]


def weight_and_offset(g_n, g_n1):
    # Solve the two simple equations for w_n and a_n ---
    # w_n (1 - a_n) = g_n
    # w_n a_n = g_{n + 1}
    s = g_n + g_n1
    a_n = g_n1 / s
    w_n = s
    return w_n, a_n

print("Testing with kernel size of 5")

g = gaussian_weights(5)

w0, a0 = weight_and_offset(g[0], g[1])
w1, a1 = g[2], 0
w2, a2 = weight_and_offset(g[3], g[4])

ws = [w0, w1, w2]
offs = [a0, a1, a2]

print('ws = ', ws)
print('os = ', offs)

def run_usual_filter(values):
    assert len(values) == 5

    r = [v * w for v, w in zip(values, g)]
    print('r = ', r)
    return sum(r)

def lerp(v0, v1, o):
    return v0 * (1 - o) + v1 * o

def run_after_lerp(values):
    lerped = [0] * 3
    lerped[0] = lerp(values[0], values[1], offs[0])
    lerped[1] = values[2]
    lerped[2] = lerp(values[3], values[4], offs[2])

    r = [v * w for v, w in zip(lerped, ws)]
    print('Lerped r = ', r)
    return sum(r)


values = [9342, 8743, 8374382, 73423059, 7326873]
print(run_usual_filter(values))
print(run_after_lerp(values))

print("Testing with kernel size of 9")

g = gaussian_weights(9)

print('Gaussian weights =', g)

w0, a0 = weight_and_offset(g[0], g[1])
w1, a1 = weight_and_offset(g[2], g[3])
w2, a2 = g[4], 0
w3, a3 = weight_and_offset(g[5], g[6])
w4, a4 = weight_and_offset(g[7], g[8])

ws = [w0, w1, w2, w3, w4]
offs = [a0, a1, a2, a3, a4]

print('Weights to use with lerped samples', ws)
print('Offsets to use with lerped samples', offs)
print('Weight sum = ', sum(ws))

def run_usual_filter(values):
    assert len(values) == 9

    r = [v * w for v, w in zip(values, g)]
    print('r = ', r)
    return sum(r)

def run_after_lerp(values):
    lerped = [0] * 5
    lerped[0] = lerp(values[0], values[1], offs[0])
    lerped[1] = lerp(values[2], values[3], offs[1])
    lerped[2] = values[4]
    lerped[3] = lerp(values[5], values[6], offs[3])
    lerped[4] = lerp(values[7], values[8], offs[4])

    r = [v * w for v, w in zip(lerped, ws)]
    print('Lerped r = ', r)
    return sum(r)

values = [randint(0, 100) for i in range(0, 9)]
print(run_usual_filter(values))
print(run_after_lerp(values))


# Rastergrid's 

# Not like this
print('Rastergrids values')

def weight_and_offset_rgrid(g_0, g_1):
    w = g_0 + g_1
    o = g_0 / w
    return w, o

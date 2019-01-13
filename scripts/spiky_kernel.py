import matplotlib.pyplot as pyplot
from mpl_toolkits.mplot3d import Axes3D
import numpy as np
import random
import math

def v3(x, y, z): return np.array([x, y, z], dtype=float4)
def f32(n): return np.float4(n)

MAX_EXTENT = 1000.0
NUM_RANDOM_POINTS = 1000

# def random_in_sphere(radius):
#     y = random.uniform(-radius, radius)
#     theta = random.uniform(-math.pi, math.pi)
#     r = math.sqrt(radius * radius - y * y)
#     p = v3(r * math.cos(theta), y, r * math.sin(theta))
#     return random.uniform(0.001, radius) * p

SPIKY_NORM = 15.0 / (math.pi * (MAX_EXTENT ** 3))

def spiky(r):
    return  SPIKY_NORM * (1.0 - r / MAX_EXTENT) ** 3

points = np.linspace(0.0, MAX_EXTENT + 20, 10000)
results = [spiky(p) for p in points]

# Plot

# figure = pyplot.figure()
# ax = figure.gca(projection='3d')

pyplot.plot(points, results)
pyplot.show()

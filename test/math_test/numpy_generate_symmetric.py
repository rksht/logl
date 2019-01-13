# Generate some symmetric matrices with numpy

import random
import numpy as np
import argparse

DEFAULT_SEED = 0xdeadbeef
COUNT = 100

def generate_points(radius, num_points):
    divs = 10

    points = [None] * num_points
    for i in range(num_points):
        x = radius * float(random.randint(-divs, divs)) / divs
        y = radius * float(random.randint(-divs, divs)) / divs
        z = radius * float(random.randint(-divs, divs)) / divs
        points[i] = np.array([x, y, z])
    return points

def covariance_matrix(points):
    mean = sum(points) / len(points)
    cov = np.zeros((3, 3))

    cov[(0, 0)] = sum((p[0] - mean[0])**2 for p in points) / len(points)
    cov[(1, 1)] = sum((p[1] - mean[1])**2 for p in points) / len(points)
    cov[(2, 2)] = sum((p[2] - mean[2])**2 for p in points) / len(points)

    cov_01 = sum((p[0] - mean[0]) * (p[1] - mean[1]) for p in points) / len(points)
    cov_02 = sum((p[0] - mean[0]) * (p[2] - mean[2]) for p in points) / len(points)
    cov_12 = sum((p[1] - mean[1]) * (p[2] - mean[2]) for p in points) / len(points)

    cov[(0, 1)] = cov_01
    cov[(1, 0)] = cov_01
    cov[(0, 2)] = cov_02
    cov[(2, 0)] = cov_02
    cov[(1, 2)] = cov_12
    cov[(2, 1)] = cov_12
    return cov

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--count', type=int, help='Number of matrices')
    parser.add_argument('--seed', type=int, help='Seed')
    args = parser.parse_args()

    if args.count:
        COUNT = args.count

    if args.seed:
        random.seed(args.seed)
    else:
        random.seed(DEFAULT_SEED)

    with open('SymmetricMatrices.txt', 'w') as file:
        for i in range(COUNT):
            p = generate_points(1000, 100)
            cov = covariance_matrix(p)
            file.write(str(cov))
            file.write('\n')

# Generates some cube infos randomly

import random
import math
import json
from sys import argv

MAX_SCALE = 5.0
MAX_RADIUS = 50.0
SIG = 4

def make_tets(n):
    d = {'tets': []}
    l = d['tets']
    for i in range(0, n):
        cubeinfo = {}
        cubeinfo['scale'] = round(random.uniform(1.0, MAX_SCALE), SIG)
        r = random.uniform(1.0, MAX_RADIUS)
        psi = random.uniform(0.0, math.pi)  # azimuthal
        theta = random.uniform(0.0, math.pi) # planar
        cubeinfo['translate'] = [
            round(r * math.cos(psi) * math.sin(theta), SIG),
            round(r * math.sin(psi) * math.sin(theta), SIG),
            round(r * math.cos(theta), SIG)
        ]
        l.append(cubeinfo)
        cubeinfo['color'] = [
            round(random.uniform(0.0, 1.0), SIG),
            round(random.uniform(0.0, 1.0), SIG),
            round(random.uniform(0.0, 1.0), SIG)
        ]

    f = open('tetinfo.json', 'w+')
    json.dump(d, f)
    f.close()



if __name__ == '__main__':
    if len(argv) != 2:
        print('Usage: python {} <num_tets>'.format(argv[0]))
        exit()
    n = int(argv[1])
    make_tets(n)
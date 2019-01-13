import random
import json
import sys

SCREEN_WIDTH = 1024
SCREEN_HEIGHT = 768

GRID_SIDE = 10

NUM_ROWS = SCREEN_HEIGHT // GRID_SIDE
NUM_COLS = SCREEN_WIDTH // GRID_SIDE

def random_rects(count):
    d = []
    for i in range(count):
        color = random.randint(0, 0xffffff)

        position = {
            'x': GRID_SIDE * random.randint(0, NUM_COLS - 1),
            'y': GRID_SIDE * random.randint(0, NUM_ROWS - 1)
        }

        size = {
            'w': GRID_SIDE * random.randint(1, NUM_COLS - 1),
            'h': GRID_SIDE * random.randint(1, NUM_ROWS - 1)
        }

        d.append({'color': '#' + hex(color)[2:], 'position': position, 'size': size, 'type': 'rectangle'})

    return d


description = {
    'size': {'w': SCREEN_WIDTH, 'h': SCREEN_HEIGHT},
    'background': {'color': '#fefefe'},
    'grid_side': GRID_SIDE,
    'objects': random_rects(50)
}

print(json.dumps(description))

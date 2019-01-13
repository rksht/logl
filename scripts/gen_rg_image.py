import stbi
import argparse as ap
import platform
import os

ap = ap.ArgumentParser()
ap.add_argument('width', type=int, nargs='?')
ap.add_argument('height', type=int, nargs='?')

args = ap.parse_args()

if args.width is None:
    width = 1024
else:
    width = args.width

if args.height is None:
    height = 768
else:
    height = args.height


if platform.system() == 'Linux':
    stbi.init(os.path.join(os.getenv('HOME'), '.local/python_modules', 'stbi.so'))
else:
    stbi.init('./stbi.dll')


image = stbi.Image.make_new(width, height)

red = [255, 0, 0, 255]
blue = [0, 0, 255, 255]

for y in range(0, image.height):
    for x in range(0, image.width):
        if x % 2 == 0 and y % 2 != 0:
            image[(x, y)] = red
        else:
            image[(x, y)] = blue

image.write_to_file('rg_{}x{}.png'.format(image.width, image.height))

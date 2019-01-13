import math
import stbi
import struct


stbi.init('./stbi_module.so')

WIDTH = 400
HEIGHT = 400

CIRCLE_RADIUS = 30

distance_image = [0.0 for i in range(HEIGHT * WIDTH)]

for y in range(-HEIGHT // 2, HEIGHT // 2):
    for x in range(-WIDTH // 2, WIDTH // 2):
        d = math.sqrt(x * x + y * y) - CIRCLE_RADIUS
        pix_y = y + HEIGHT // 2
        pix_x = x + WIDTH // 2
        distance_image[pix_y * WIDTH + pix_x] = d

df_formatted_buffer = struct.pack('<ii{}f'.format(WIDTH * HEIGHT), WIDTH, HEIGHT, *distance_image)

# Find max distance
max_distance = max(distance_image)
min_distance = min(distance_image)
abs_min_distance = abs(min_distance)
shifted_max_distance = max_distance + abs_min_distance

print('AbsMinDistance = {}, shifted_max_distance = {}'.format(abs_min_distance, shifted_max_distance))

img = stbi.Image.make_new(WIDTH, HEIGHT, 1, clear_color = [0])

for pix_y in range(0, HEIGHT):
    for pix_x in range(0, WIDTH):
        shifted_distance = distance_image[pix_y * WIDTH + pix_x] + abs_min_distance
        shifted_distance = shifted_distance / shifted_max_distance
        color = [int(shifted_distance * 255.0)]
        img[(pix_x, pix_y)] = color

img.write_to_file('circle_df.png')

with open('circle_df.df', 'wb') as df_outfile:
    df_outfile.write(df_formatted_buffer)

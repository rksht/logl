from maths import *
import loadobj
import stbi
import numpy as np
import math
import os

# State
SCREEN_WIDTH = 800
SCREEN_HEIGHT = 600
ASPECT_RATIO = SCREEN_WIDTH / SCREEN_HEIGHT
FOV = 70 * ONE_DEG_IN_RADS
NEAR_Z = 0.1
FAR_Z = 1000.0

def create_back_buffer(width, height):
    return np.zeros((width, height), dtype='uint')

def create_z_buffer(width, height):
    return np.zeroes((width, height), dtype='float')

class Camera:
    def __init__(self, position, orientation):
        self.pos = position
        self.q = orientation

    @staticmethod
    def towards_negz(z_pos):
        return Camera(np.array([0.0, 0.0, z_pos]), IDENTITTY_VERSOR)

    def get_view_matrix(self):
        eye_mat = self.q.make_matrix()
        out = np.transpose(eye_mat)
        t = np.array([self.pos[0], self.pos[1], self.pos[2], 1.0], dtype='float32')
        t = np.dot(out, t)
        out[0][3] = -t[0]
        out[1][3] = -t[1]
        out[2][3] = -t[2]
        out[3][3] = 1.0
        return out

'''
def rasterize_triangle(v0, v1, v2):
    # Bounding box, clipped to screen width and height
    lo_x = min(v0[0], v1[0], v2[0])
    lo_y = min(v0[1], v1[1], v2[1])
    hi_x = max(v0[0], v1[0], v2[0])
    hi_y = max(v0[1], v1[1], v2[1])

    lo_x = max(0, lo_x)
    lo_y = max(0, lo_y)
    hi_x = min(SCREEN_WIDTH - 1, hi_x)
    hi_y = min(SCREEN_HEIGHT - 1, hi_y)

    print('Box {} {} {} {}'.format(lo_x, lo_y, hi_x, hi_y))

    # Rasterize
    p = np.array([0, 0], dtype='float32')
    for y in range(int(lo_y), int(hi_y + 1)):
        p[1] = y
        for x in range(int(lo_x), int(hi_x + 1)):
            p[0] = x
            w0 = signed_area2(p, v0, v1)
            w1 = signed_area2(p, v1, v2)
            w2 = signed_area2(p, v2, v0)
            if w0 >= 0 and w1 >= 0 and w2 >= 0:
                yield w0, w1, w2, p[0], p[1]
'''

def rasterize_triangle(v0, v1, v2):
    lo_x = min(v0[0], v1[0], v2[0])
    lo_y = min(v0[1], v1[1], v2[1])
    hi_x = max(v0[0], v1[0], v2[0])
    hi_y = max(v0[1], v1[1], v2[1])

    lo_x = max(0, lo_x)
    lo_y = max(0, lo_y)
    hi_x = min(SCREEN_WIDTH - 1, hi_x)
    hi_y = min(SCREEN_HEIGHT - 1, hi_y)

    print('llhh', lo_x, lo_y, hi_x, hi_y)

    # Calculate coefficents of the edge function for each edge
    coeff_A = np.array([
            v0[1] - v1[1],      #edge01
            v1[1] - v2[1],      #edge12
            v2[1] - v0[1]       #edge20
        ], dtype='int32')

    coeff_B = np.array([
            v1[0] - v0[0],      #edge01
            v2[0] - v1[0],      #edge12
            v0[0] - v2[0]       #edge20
        ], dtype='int32')

    add_C = np.array([
            v0[0] * v1[1] - v0[1] * v1[0],
            v1[0] * v2[1] - v1[1] * v2[0],
            v2[0] * v0[1] - v2[1] * v0[0]
        ], dtype='int32')

    # Value of edge functions at [lo_x, lo_y]
    y = lo_y
    x = lo_x
    row_uvw = coeff_A * lo_x + coeff_B * lo_y + add_C
    start_uvw = np.copy(row_uvw)
    while True:
        while True:
            if row_uvw[0] >= 0 and row_uvw[1] >= 0 and row_uvw[2] >= 0:
                yield row_uvw, x, y
            x += 1
            if x >= hi_x:
                break
            row_uvw += coeff_A
        y += 1
        if y >= hi_y:
            break
        x = lo_x
        row_uvw = start_uvw + coeff_B
        start_uvw = np.copy(row_uvw)


def draw_obj(model):
    m = model.meshes[0]
    i = 0

    img = stbi.Image.make_new(SCREEN_WIDTH, SCREEN_HEIGHT, 3)

    zbuffer = np.full((SCREEN_HEIGHT, SCREEN_WIDTH), -2 ** 16, dtype='float32')

    cam = Camera.towards_negz(0.2)

    # y_cam = cam.q.apply(UNIT_Y)

    # cam.q = Quaternion.from_axis_angle(y_cam, 30 * ONE_DEG_IN_RADS) * cam.q
    # cam.q.normalize()

    view_mat = cam.get_view_matrix()
    proj_mat = create_clip_transform(NEAR_Z, FAR_Z, FOV, ASPECT_RATIO)

    print(view_mat)

    shift_a_little = np.array(
        [[5.0, 0, 0, 0],
         [0, 3.0, 0, 0],
         [0, 0, 3.0, -10.0],
         [0, 0, 0, 1]], dtype='float32'
    )

    model_to_view = np.dot(view_mat, shift_a_little)

    print(proj_mat)

    print('Num triangles = ', len(m.indices) / 3)

    while i < len(m.indices):
        #print(i)

        v0 = np.dot(model_to_view, m.positions[m.indices[i]])
        v1 = np.dot(model_to_view, m.positions[m.indices[i + 1]])
        v2 = np.dot(model_to_view, m.positions[m.indices[i + 2]])

        # print('v0 = ', v0)
        # print('v1 = ', v1)
        # print('v2 = ', v2)

        # To clip space
        s0 = np.dot(proj_mat, v0)
        s1 = np.dot(proj_mat, v1)
        s2 = np.dot(proj_mat, v2)

        clipped = False

        # Clip space
        if not (-s0[3] <= s0[0] <= s0[3]):
            clipped = True
            print('Clipped 0 ', s0)

        if not (-s0[3] <= s0[1] <= s0[3]):
            clipped = True
            print('Clipped 1 ', s1)

        if not (-s0[3] <= s0[2] <= s0[3]):
            clipped = True
            print('Clipped 2', s2)

        if clipped:
            i += 3
            continue

        # Using normal as color data
        w_inv0 = 1/s0[3]
        w_inv1 = 1/s1[3]
        w_inv2 = 1/s2[3]
        c0_div_z = abs(normalize_vec3(m.normals[m.indices[i]]) * 255.0 * w_inv0)
        c1_div_z = abs(normalize_vec3(m.normals[m.indices[i + 1]]) * 255.0 * w_inv1)
        c2_div_z = abs(normalize_vec3(m.normals[m.indices[i + 2]]) * 255.0 * w_inv2)

        s0 /= s0[3]
        s1 /= s1[3]
        s2 /= s2[3]

        # print('NDC -', s0, s1, s2)

        # To viewport (same as screen in our examples)
        s0[0] = math.floor((s0[0] + 1.0) * 0.5 * SCREEN_WIDTH)
        s0[1] = math.floor((s0[1] + 1.0) * 0.5 * SCREEN_HEIGHT)

        s1[0] = math.floor((s1[0] + 1.0) * 0.5 * SCREEN_WIDTH)
        s1[1] = math.floor((s1[1] + 1.0) * 0.5 * SCREEN_HEIGHT)

        s2[0] = math.floor((s2[0] + 1.0) * 0.5 * SCREEN_WIDTH)
        s2[1] = math.floor((s2[1] + 1.0) * 0.5 * SCREEN_HEIGHT)

        # print('Screen -', s0, s1, s2)

        tri_area = signed_area2(s0, s1, s2)

        if tri_area == 0:
            print('Invalid triangle {} {} {}'.format(s0, s1, s2))
            i += 3
            continue

        # Present triangle in CCW order
        if tri_area < 0:
            # print('Swapping')
            tri_area = -tri_area
            s0, s1 = s1, s0
            c0_div_z, c1_div_z = c1_div_z, c0_div_z
            w_inv0, w_inv1 = w_inv1, w_inv0

        for uvw, x, y in rasterize_triangle(s0, s1, s2):
            # import pdb; pdb.set_trace()
            ix = int(x)
            iy = int(y)

            print(x, y)

            c_div_z = (uvw[0] * c0_div_z + uvw[1] * c1_div_z + uvw[2] * c2_div_z)
            w_inv = (uvw[0] * w_inv0 + uvw[1] * w_inv1 + uvw[2] * w_inv2)
            c = c_div_z / w_inv

            if w_inv >= zbuffer[iy, ix]:
                try:
                    img[ix, iy] = [int(c[0]), int(c[1]), int(c[2])]
                    zbuffer[iy, ix] = w_inv
                    #print('z[{}, {}] = {}'.format(int(x), int(y), -1/w_inv))
                except ValueError as e:
                    for c in [c0_div_z, c1_div_z, c2_div_z]:
                        print('ci = ', c)

                    for winv in [w_inv0, w_inv1, w_inv2]:
                        print('wi = ', winv)

                    print('cdiv =', c_div_z)
                    print('C = ', c)
                    print('uvw = ', uvw)
                    print('C = ', c)
                    print('W =', w_inv)
                    import pdb; pdb.set_trace()
                    raise e

        i += 3

    img.write_to_file("model.png")

class FakeModel:
    def __init__(self):
        m = loadobj.MeshData()
        m.positions = [np.array([30.0, 0.0, -50.0, 1.0], dtype='float32'),
                       np.array([300.0, 100.0, -60.0, 1.0], dtype='float32'),
                       np.array([-40.0, 50.0, -20.0, 1.0], dtype='float32')]

        # m.positions[0], m.positions[1] = m.positions[1], m.positions[0]

        m.indices = [0, 1, 2]
        m.normals = [np.array([0.5, 0.2, 0.0], dtype='float32'),
                     np.array([0.8, 0.0, 0.5], dtype='float32'),
                     np.array([0.0, 0.8, 0.5], dtype='float32')]

        self.meshes = [m]


if __name__ == '__main__':
    loadobj.init(os.path.join(os.getenv('HOME'), 'build/learnogl-debug/test/tiny_test/loadobj.so'))
    stbi.init(os.path.join(os.getenv('HOME'), 'build/learnogl-debug/scripts/stbi.so'))

    model = loadobj.Model.from_file('hazelnut.obj', loadobj.LoadOptions.CALCULATE_TANGENTS)
    draw_obj(model)
    # draw_obj(FakeModel())

    loadobj.close_learnogl()

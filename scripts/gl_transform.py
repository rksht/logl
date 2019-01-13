""" Examine how a single vector is transformed by the GL pipeline. Also plot a depth vs z graph
"""

import numpy as np
import math
import argparse as ap

ONE_DEG_IN_RAD = math.pi / 180.0

def persp_proj(n, f, hfov, w_on_h):
    e = 1.0 / math.tan(hfov / 2.0)
    sx = e
    sy = w_on_h * e
    sz = -(f + n) / (f - n)
    qz = -(2.0 * n * f) / (f - n)

    return np.array(
        [[sx, 0, 0, 0],
        [0, sy, 0, 0],
        [0, 0, sz, qz],
        [0, 0, -1.0, 0.0]]
    )

k_window_width = 1024
k_window_height = 768
P = persp_proj(0.1, 1000.0, 70 * ONE_DEG_IN_RAD, k_window_width / k_window_height)
Pinv = np.linalg.inv(P)
depth_quad_bl = np.array([-0.3, -0.3])
depth_quad_tr = np.array([0.3, 0.3])
depth_viewport_extent = np.array([depth_quad_tr[0] - depth_quad_bl[0], depth_quad_tr[1] - depth_quad_bl[1]])

depth_quad_bl_window = np.array([
    k_window_width * (depth_quad_bl[0] + 1.0) / 2.0,
    k_window_height * (depth_quad_bl[1] + 1.0) / 2.0
]);

depth_quad_tr_window = np.array([
    k_window_width * (depth_quad_tr[0] + 1.0) / 2.0,
    k_window_height * (depth_quad_tr[1] + 1.0) / 2.0
]);

depth_quad_extent = depth_quad_tr_window - depth_quad_bl_window;

def ndc_from_fc(fc, window_depth):
    return np.array([
        2.0 * (fc[0] - depth_quad_bl_window[0]) / depth_quad_extent[0] - 1.0,
        2.0 * (fc[1] - depth_quad_bl_window[1]) / depth_quad_extent[1] - 1.0,
        2.0 * window_depth - 1.0
    ])

E_1 = -1.0

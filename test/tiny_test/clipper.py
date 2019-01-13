from maths import *
import numpy as np
import math

class SegmentState:
    INSIDE      = 0
    OUTSIDE     = 1
    OVERLAPPING = 2

class Plane:
    __slots__ = ['n', 'd']
    def __init__(self, normal, dist_from_origin):
        self.n = normal
        self.d = dist_from_origin

def clip_segment(v0, v1, plane):
    d0 = np.dot(v0, plane.n)
    d1 = np.dot(v1, plane.n)

    dotprod0 = d0 + plane.d
    dotprod1 = d1 + plane.d

    inside_d = None
    inside_dotprod = None
    inside_point, outside_point = None, None

    if dotprod0 >= 0:
        if dotprod1 >= 0:
            return SegmentState.INSIDE
        inside_d = d0
        inside_dotprod = dotprod0
        inside_point, outside_point = v0, v1
    else:
        if dotprod1 < 0:
            return SegmentState.OUTSIDE
        inside_d = d1
        inside_dotprod = dotprod1
        inside_point, outside_point = v1, v0

    # Replace outside point with the intersected point
    v = normalize_vec3(v1 - v0)
    t = -inside_dotprod / np.dot(plane.normal, v)
    intersecting_point = inside_point + v * t
    outside_point[0] = intersecting_point[0]
    outside_point[1] = intersecting_point[1]
    outside_point[2] = intersecting_point[2]

def clip_triangle(v0, v1, v2):
    """ Clips the triangle. Will overwrite the vertex positions given if they get clipped """
    

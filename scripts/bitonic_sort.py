# Implementation of recursive bitonic sort.
import numpy as np
import random

def gt(a, b):
    return a > b

def lt(a, b):
    return a < b

should_swap = [gt, lt]

ASCENDING = True
DESCENDING = False

def bsort(seq, order):
    if len(seq) == 1:
        return

    h = len(seq) // 2
    n = len(seq)

    bsort(seq[0 : h], ASCENDING)
    bsort(seq[h : n], DESCENDING)

    partition(seq, order)


def partition(seq, order):
    if len(seq) < 2:
        return

    n = len(seq)
    h = n // 2

    for i in range(0, h):
        if (seq[i] > seq[i + h]) == order:
            seq[i], seq[i + h] = seq[i + h], seq[i]

    partition(seq[0 : h], order)
    partition(seq[h : n], order)


NUM_ELEMENTS = 2 ** 12
numbers = np.array([random.randint(0, NUM_ELEMENTS) for i in range(NUM_ELEMENTS)])
print(numbers)
bsort(numbers, ASCENDING)
print(numbers)

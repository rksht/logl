import numpy as np
import matplotlib.pyplot as plot

f32 = np.float32

K = f32(0.5)
M = f32(1.0)

UNTENSED_LENGTH = f32(1.0)

NUM_ITERS = 2000

TIMESTEP = f32(16e-3)

class ParticleState:
    def __init__(self, position, velocity):
        self.position = f32(position)
        self.velocity = f32(velocity)

INITIAL_STATE = ParticleState(UNTENSED_LENGTH + 10.0, 0)

def force(x): return -K * x

def forward_euler_solve(particle_state, delta_t):
    # print(particle_state.position, particle_state.velocity)

    new_velocity = particle_state.velocity + force(particle_state.position) / M * delta_t
    new_position =  particle_state.position + particle_state.velocity * delta_t

    return ParticleState(new_position, new_velocity)

def semi_implicit_euler(particle_state, delta_t):
    new_velocity = particle_state.velocity + force(particle_state.position) / M * delta_t
    new_position = new_velocity * delta_t + particle_state.position
    return ParticleState(new_position, new_velocity)

def main(av):
    plot.xkcd()

    f_particle_states = [None] * NUM_ITERS
    f_particle_states[0] = INITIAL_STATE

    for i in range(1, NUM_ITERS):
        f_particle_states[i] = forward_euler_solve(f_particle_states[i - 1], TIMESTEP)

    plot.plot(list(range(0, NUM_ITERS)), list(s.position for s in f_particle_states), 'C0', label='fwd')

    s_particle_states = [None] * NUM_ITERS
    s_particle_states[0] = INITIAL_STATE

    for i in range(1, NUM_ITERS):
        s_particle_states[i] = semi_implicit_euler(s_particle_states[i - 1], TIMESTEP)
    plot.plot(list(range(0, NUM_ITERS)), list(s.position for s in s_particle_states), 'C1', label='semi')

    plot.legend()

    plot.show()
    plot.title("Euler integration")

if __name__ == '__main__':
    import sys
    main(sys.argv)

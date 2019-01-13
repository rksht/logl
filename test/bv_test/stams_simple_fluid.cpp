#include <learnogl/gl_misc.h>
#include <scaffold/const_log.h>

#include <thread> // sleep_for

#define WIDTH 128
#define HEIGHT 128

eng::StartGLParams glparams;
using namespace eng::math;

struct AppThings {
    eng::FBO f32_fbo;
    GLuint float_texture;
};

GLOBAL_STORAGE(AppThings, the_app);
GLOBAL_ACCESSOR_FN(AppThings, the_app, get_app);
#define APPSTRUCT get_app()
#define APPDUK get_app().duk.C()

void init_opengl_resources() {
    glCreateTextures(GL_TEXTURE_2D, 1, &APPSTRUCT.float_texture);
    glTextureStorage2D(APPSTRUCT.float_texture, 1, GL_R32F, glparams.window_width, glparams.window_height);

    APPSTRUCT.f32_fbo.gen("@f32_fbo")
        .bind()
        .add_attachment(0, APPSTRUCT.float_texture)
        .set_done_creating()
        .bind_as_readable();
}

struct FluidProperties {
    f32 kinematic_viscosity;
};

struct FluidQuantity {
    fo::Vector<double> _src;
    fo::Vector<double> _dst;

    int _w;
    int _h;
    // ^ Number of quantities sampled along x and y. Don't read as 'width' and 'height', as that gives a wrong
    // impression.


    double _ox;
    double _oy;
    // ^ x and y (in units of cell size) of the the quantity. The cell (i, j) stores the quantity sampled at
    // the location (i + _ox, j + _oy).

    double _hx;
    // ^ grid cell size.

    // Velocity is stored in real-units per second.

    FluidQuantity(int w, int h, double ox, double oy, double hx)
        : _w(w)
        , _h(h)
        , _ox(ox)
        , _oy(oy)
        , _hx(hx) {
        fo::resize_with_given(_src, _w * _h, 0.0);
        fo::resize_with_given(_dst, _w * _h, 0.0);
    }

    // idk why @tunabrain keeps this function in this class, it doesn't depend on the class at all.
    double lerp(double a, double b, double x) const { return ::eng::math::lerp(a, b, x); }

    struct Vector2_64 {
        double x;
        double y;
    };

    // Used by the advection routine. "Reverse" forward integration in time. Bridson recommends using RK3
    // instead but gonna wait.
    Vector2_64 backtrace_using_forward_euler(Vector2_64 pos,
                                             double timestep,
                                             const FluidQuantity &u,
                                             const FluidQuantity &v) const {
        double u_vel = u.reconstruct_at(pos) / _hx;
        double v_vel = v.reconstruct_at(pos) / _hx;
        // ^ Reconstruct the velocity at given position. Divide by cell size to get velocity in units of
        // `cells per second`

        pos.x -= u_vel * timestep;
        pos.y -= v_vel * timestep;
        return pos;
    }

    void flip() { std::swap(_src, _dst); }

    const auto &src() const { return _src; }
    const auto &dst() const { return _dst; }

    double &at(int ix, int iy) { return _src[iy * _w + ix]; }
    double at(int ix, int iy) const { return _src[iy * _w + ix]; }

    // Reconstruct this quantity at the point (x, y). Uses simple bilinear interpolation to estimate the value
    // of this quantity at given point.
    double reconstruct_at(Vector2_64 pos) const {
        double x = clamp(pos.x - _ox, 0.0, _w - 1.001);
        double y = clamp(pos.y - _ox, 0.0, _h - 1.001);
        int ix = int(x);
        int iy = int(y);

        x -= ix;
        y -= iy;
        // ^ Subtract to get the interpolants along x and y direction

        // Sample four corners of the grid
        double s00 = at(ix, iy);
        double s10 = at(ix + 1, iy);
        double s01 = at(ix, iy + 1);
        double s11 = at(ix + 1, iy + 1);

        double l0 = ::eng::lerp(s00, s10, x);
        double l1 = ::eng::lerp(s01, s11, x);
        double l = ::eng::lerp(l0, l1, y);
        // ^bilinear interpolation
        return l;
    }

    // Advects this quantity by the velocity field
    void advect(double timestep, const FluidQuantity &u, const FluidQuantity &v) {
        for (int iy = 0, idx = 0; iy < _h; ++iy) {
            for (int ix = 0; ix < _w; ++ix, ++idx) {
                double x = ix + _ox;
                double y = iy + _oy;
                // ^ Position of the stored samples

                Vector2_64 source_pos = backtrace_using_forward_euler({x, y}, timestep, u, v);

                // Get the quantity at the source position.
                double quantity = reconstruct_at(source_pos);
                _dst[idx] = quantity;
            }
        }
    }
};

struct FluidSolver {
    FluidQuantity d;
    // ^ Density

    FluidQuantity u;
    // ^ Velocity x component

    FluidQuantity v;
    // ^ Velocity y component

    int _w;
    int _h;
    // Width and height of the grid itself in terms of number of cells.

    double _hx;
    double _density;

    fo::Vector<double> _r;
    // RHS of pressure equation
    fo::Vector<double> _p;
    // Pressure solution
};

#if 0
void visualize() {
    glClear(GL_COLOR_BUFFER_BIT);

    glBindTexture(GL_TEXTURE_2D, APPSTRUCT.float_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RED, GL_FLOAT, APPSTRUCT.f32_buffer_duk.ptr);

    APPSTRUCT.f32_fbo.bind_as_readable(0).set_read_buffer(0);
    glBlitFramebuffer(0, 0, WIDTH, HEIGHT, 0, 0, WIDTH, HEIGHT, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glfwSwapBuffers(eng::gl().window);
}
#endif

int main(int ac, char **av) {
    eng::init_memory();
    DEFERSTAT(eng::shutdown_memory());

    glparams.window_width = WIDTH;
    glparams.window_height = HEIGHT;

    eng::start_gl(glparams);
    DEFERSTAT(eng::close_gl(glparams));

    new (&APPSTRUCT) AppThings();
    init_opengl_resources();

    // visualize();

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(2s);
}

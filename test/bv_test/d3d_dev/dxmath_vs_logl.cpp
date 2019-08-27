#include <learnogl/eng>

#include "d3d11_misc.h"

int main()
{
    eng::init_memory();
    DEFERSTAT(eng::shutdown_memory());

    f32 NEAR_Z = 0.2f;
    f32 FAR_Z = 1000.0f;
    f32 VERTICAL_FOV = XM_PI / 4.0f;

    f32 WIDTH = 1920;
    f32 HEIGHT = 1080;

    auto mat_logl = eng::math::perspective_projection(NEAR_Z, FAR_Z, VERTICAL_FOV, WIDTH / HEIGHT);
    auto mat_xmath = XMMatrixPerspectiveFovLH(VERTICAL_FOV, WIDTH / HEIGHT, NEAR_Z, FAR_Z);

    printf("%p", &mat_logl);
    printf("%p", &mat_xmath);
}

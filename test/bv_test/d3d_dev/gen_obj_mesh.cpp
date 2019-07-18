#include <cmath>
#include <stdio.h>

#include <learnogl/kitchen_sink.h>
#include <learnogl/rng.h>

#define MC_IMPLEMENTATION
#include "rjm_mc.h"

reallyconst_ cube_bound_range = 5.0f;

// Torus definition
reallyconst_ torus_R = 3.0f;
reallyconst_ torus_r = 2.0f;

reallyconst_ square_R = torus_R * torus_R;
reallyconst_ square_r = torus_r * torus_r;

/*
float sdTorus( vec3 p, vec2 t )
{
  vec2 q = vec2(length(p.xz)-t.x,p.y);
  return length(q)-t.y;
}
*/

fn_ sgndist_torus(Vec3 point)
{
	var_ lhs = eng::math::square_magnitude(point) + square_R - square_r;
	lhs = lhs * lhs;
	const_ rhs = 4 * square_R * (point.x * point.x + point.y * point.y);
	const_ f = lhs - rhs;
	return f;
};

float testIsoFn(const float *pos, float *extra, void *userparam)
{
	float x = pos[0], y = pos[1], z = pos[2];
	return sgndist_torus(Vec3(x, y, z));
}

int main()
{
	rng::init_rng();

	float bmin[3] = { -cube_bound_range, -cube_bound_range, -cube_bound_range };
	float bmax[3] = { cube_bound_range, cube_bound_range, cube_bound_range };
	float res = 0.1f; // step size

	McMesh mesh = mcGenerate(bmin, bmax, res, testIsoFn, NULL);

	// Save out as a .OBJ 3D model.
	printf("o isosurface\n");
	for (int n = 0; n < mesh.nverts; n++)
		printf("v %f %f %f\n", mesh.verts[n].x, mesh.verts[n].y, mesh.verts[n].z);
	for (int n = 0; n < mesh.nverts; n++)
		printf("vn %f %f %f\n", mesh.verts[n].nx, mesh.verts[n].ny, mesh.verts[n].nz);
	for (int n = 0; n < mesh.ntris; n++) {
		int a = mesh.indices[n * 3 + 0] + 1; // +1 because .OBJ counts from 1
		int b = mesh.indices[n * 3 + 1] + 1;
		int c = mesh.indices[n * 3 + 2] + 1;
		printf("f %i//%i %i//%i %i//%i\n", a, a, b, b, c, c);
	}

	mcFree(&mesh); // Free it when done.
	return 0;
}


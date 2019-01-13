#include <cmath>
#include <stdio.h>

#include <learnogl/rng.h>

#define MC_IMPLEMENTATION
#include "rjm_mc.h"

float range = 1.0f;

float testIsoFn(const float *pos, float *extra, void *userparam)
{
	float x = pos[0], y = pos[1], z = pos[2];
	// return (x * x + y * y + z * z * z * z) * rng::random(-range, range);
	return (x * x + y * y + z * z) - 1.0f;
}

int main()
{
	rng::init_rng();

	float bmin[3] = {-range, -range, -range}; // bounding-box to scan over
	float bmax[3] = {range, range, range};
	float res = 0.1f; // step size

	McMesh mesh = mcGenerate(bmin, bmax, res, testIsoFn, NULL);

	// Save out as a .OBJ 3D model.
	printf("o isosurface\n");
	for (int n = 0; n < mesh.nverts; n++)
		printf("v %f %f %f\n", mesh.verts[n].x, mesh.verts[n].y, mesh.verts[n].z);
	for (int n = 0; n < mesh.nverts; n++)
		printf("vn %f %f %f\n", mesh.verts[n].nx, mesh.verts[n].ny,
					 mesh.verts[n].nz);
	for (int n = 0; n < mesh.ntris; n++) {
		int a = mesh.indices[n * 3 + 0] + 1; // +1 because .OBJ counts from 1
		int b = mesh.indices[n * 3 + 1] + 1;
		int c = mesh.indices[n * 3 + 2] + 1;
		printf("f %i//%i %i//%i %i//%i\n", a, a, b, b, c, c);
	}

	mcFree(&mesh); // Free it when done.
	return 0;
}

#ifndef TEXTURE_WIDTH
#error "TEXTURE_WIDTH needs to be defined as a macro"
#endif

RWTexture2D<float> random_colors : register(u0);

uint wang_hash(uint seed) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

[numthreads(tpg_x, tpg_y, 1)]
void CS_main(uint3 gid : SV_GroupID, uint3 tid : SV_DispatchThreadID) {
    const uint i = tid.y * TEXTURE_WIDTH + tid.x;
    random_colors[gid.xy] = float(wang_hash(i) % (256)) / 256.0f; // 2 ** 23 is the largest 
}

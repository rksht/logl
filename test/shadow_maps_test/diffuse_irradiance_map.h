#pragma once

#include <learnogl/eng.h>

struct Material {
    Vec3 albedo;
    f32 metallic_factor;
    f32 roughness_factor;
    f32 ao;
};

struct PointLight {
    Vec3 position;
    Vec3 rgb_radiance;
};

// The scene we use will consist of balls.
struct BallEntity {
    eng::math::LocalTransform world_transform;

    Material material;

    BallEntity() = default;

    BallEntity(f32 radius, Vec3 position, Material material)
    {
        self_.world_transform.position = position;
        self_.world_transform.scale = Vec3(radius, radius, radius);
        self_.material = material;
    }
};

// The scene I'm using consists of a ring of point lights.
struct MyScene {
    fo::Vector<BallEntity> ball_entities;
    fo::Vector<PointLight> lights;

    MyScene(int num_lights, f32 ring_tilt_angle)
    {
        for (int i = 0; i < num_lights; ++i) {
            fo::push_back(self_.lights, {});
        }
    }
};

#pragma once

#define RADS_PER_DEG 0.0174532925199432957f
#define DEGS_PER_RAD 57.295779513082320876f

struct MyTest : public Test {
    static Test *Create() { return new MyTest; }

    MyTest() {
        b2BodyDef def;
        def.type = b2_dynamicBody;
        def.position.Set(0, 20);
        def.angle = 0;

        _dynamicBody = m_world->CreateBody(&def);

        b2PolygonShape boxShape;
        boxShape.SetAsBox(1, 1);

        b2FixtureDef boxFixtureDef;
        boxFixtureDef.shape = &boxShape;
        boxFixtureDef.density = 1;
        _dynamicBody->CreateFixture(&boxFixtureDef);

        _dynamicBody->SetTransform(b2Vec2(10, 20), 45 * RADS_PER_DEG);
        _dynamicBody->SetLinearVelocity(b2Vec2(0, 4.0f));
        _dynamicBody->SetAngularVelocity(-900.0f * RADS_PER_DEG);

        def.type = b2_staticBody;
        def.position.Set(0, 0);
        def.angle = 10.0f * RADS_PER_DEG;
        boxShape.SetAsBox(30.0f, 5.0f);
        _groundBox = m_world->CreateBody(&def);
        _groundBox->CreateFixture(&boxFixtureDef);
    }

    b2Body *_dynamicBody = nullptr;

    b2Body *_groundBox = nullptr;
};

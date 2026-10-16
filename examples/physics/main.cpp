// Ion physics sandbox: rigid bodies, ray casts and 3D spatial audio.
//
//   WASD / Space / C   fly         Right mouse drag or arrow keys   look
//   Left click or F    fire a ball E  blast where you are looking   B  drop a crate tower
//   R                  reset       Esc  quit
//
// The orbiting glowing sphere carries a siren: listen for panning, distance
// falloff and Doppler as it sweeps past. Every impact is placed in 3D.

#include <ion/audio/Audio.hpp>
#include <ion/audio/Synth.hpp>
#include <ion/core/Timer.hpp>
#include <ion/physics/PhysicsWorld.hpp>
#include <ion/platform/Input.hpp>
#include <ion/platform/Window.hpp>
#include <ion/render/Camera.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/Vertex.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>
#include <vector>

namespace {

using ion::Vector3;
using ion::Vector4;

// Gouraud-lit, per-object tint. Lighting is evaluated in the vertex shader so
// the fragment stage only needs the interpolated color.
const char* kMetalVertexShader = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 uMVP;
    float4 uTint;
    float4 uLightDir;
};

struct VSIn {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float3 normal [[attribute(3)]];
};

struct VSOut {
    float4 position [[position]];
    float4 color;
};

vertex VSOut vertexShader(VSIn in [[stage_in]],
                          const device Uniforms& uniforms [[buffer(0)]]) {
    VSOut out;
    out.position = uniforms.uMVP * float4(in.position, 1.0);
    float lambert = max(dot(normalize(in.normal), normalize(uniforms.uLightDir.xyz)), 0.0);
    float sky = 0.5 + 0.5 * in.normal.y;
    float light = 0.22 + 0.25 * sky + 0.65 * lambert;
    out.color = float4(in.normal * 0.5 + 0.5, 1.0);
    return out;
}
)";

const char* kMetalFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;

struct VSOut {
    float4 position [[position]];
    float4 color;
};

fragment float4 fragmentShader(VSOut in [[stage_in]]) {
    return in.color;
}
)";

const char* kGLSLVertexShader = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aNormal;

uniform mat4 uMVP;
uniform vec4 uTint;
uniform vec4 uLightDir;

out vec4 vColor;

void main() {
    gl_Position = uMVP * vec4(aPosition, 1.0);
    float lambert = max(dot(normalize(aNormal), normalize(uLightDir.xyz)), 0.0);
    float sky = 0.5 + 0.5 * aNormal.y;
    float light = 0.22 + 0.25 * sky + 0.65 * lambert;
    vColor = vec4(aColor.rgb * uTint.rgb * light + uTint.a, 1.0);
}
)";

const char* kGLSLFragmentShader = R"(
#version 410 core

in vec4 vColor;
out vec4 fragColor;

void main() {
    fragColor = vColor;
}
)";

struct Mesh {
    ion::VertexBuffer vertexBuffer;
    ion::IndexBuffer indexBuffer;
    uint32_t indexCount = 0;
};

// Appends a triangle, flipping its winding if needed so it faces `normal`.
void addTri(std::vector<ion::Vertex>& verts, std::vector<uint16_t>& idx, Vector3 a, Vector3 b,
            Vector3 c, Vector3 normal, Vector4 color) {
    if (Vector3::dot(Vector3::cross(b - a, c - a), normal) < 0.0f) {
        std::swap(b, c);
    }
    uint16_t base = (uint16_t)verts.size();
    for (const Vector3& p : {a, b, c}) {
        verts.push_back({p, color, {0.0f, 0.0f}, normal});
    }
    idx.push_back(base);
    idx.push_back(base + 1);
    idx.push_back(base + 2);
}

Mesh upload(ion::Renderer& r, const std::vector<ion::Vertex>& verts,
            const std::vector<uint16_t>& idx) {
    Mesh m;
    m.vertexBuffer = r.createVertexBuffer((uint32_t)(verts.size() * sizeof(ion::Vertex)), verts.data());
    m.indexBuffer = r.createIndexBuffer((uint32_t)idx.size(), true, idx.data());
    m.indexCount = (uint32_t)idx.size();
    return m;
}

Mesh buildUnitCube(ion::Renderer& r) { // spans [-0.5, 0.5]; scale by full extents
    std::vector<ion::Vertex> v;
    std::vector<uint16_t> idx;
    const Vector3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const Vector3& n : normals) {
        // Two in-plane axes orthogonal to the normal.
        Vector3 u = std::fabs(n.y) > 0.5f ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
        Vector3 w = Vector3::cross(n, u);
        u = Vector3::cross(w, n);
        Vector3 c = n * 0.5f;
        Vector3 p0 = c - u * 0.5f - w * 0.5f, p1 = c + u * 0.5f - w * 0.5f;
        Vector3 p2 = c + u * 0.5f + w * 0.5f, p3 = c - u * 0.5f + w * 0.5f;
        addTri(v, idx, p0, p1, p2, n, {1, 1, 1, 1});
        addTri(v, idx, p0, p2, p3, n, {1, 1, 1, 1});
    }
    return upload(r, v, idx);
}

Mesh buildUnitSphere(ion::Renderer& r, int rings = 14, int segs = 24) { // radius 1
    std::vector<ion::Vertex> v;
    std::vector<uint16_t> idx;
    const float pi = 3.14159265f;
    auto point = [&](int ring, int seg) {
        float phi = pi * (float)ring / (float)rings;
        float theta = 2.0f * pi * (float)seg / (float)segs;
        return Vector3(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
    };
    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < segs; j++) {
            Vector3 a = point(i, j), b = point(i + 1, j), c = point(i + 1, j + 1), d = point(i, j + 1);
            // Smooth shading: the vertex position is its own normal.
            auto tri = [&](Vector3 p, Vector3 q, Vector3 s) {
                if (Vector3::dot(Vector3::cross(q - p, s - p), p + q + s) < 0.0f) std::swap(q, s);
                uint16_t base = (uint16_t)v.size();
                for (const Vector3& x : {p, q, s}) v.push_back({x, {1, 1, 1, 1}, {0, 0}, x});
                idx.push_back(base);
                idx.push_back(base + 1);
                idx.push_back(base + 2);
            };
            tri(a, b, c);
            tri(a, c, d);
        }
    }
    return upload(r, v, idx);
}

Mesh buildFloor(ion::Renderer& r, int cells, float cell) { // top surface at y = 0
    std::vector<ion::Vertex> v;
    std::vector<uint16_t> idx;
    float half = cells * cell * 0.5f;
    for (int i = 0; i < cells; i++) {
        for (int j = 0; j < cells; j++) {
            float x = -half + i * cell, z = -half + j * cell;
            Vector4 col = ((i + j) % 2 == 0) ? Vector4(0.30f, 0.32f, 0.38f, 1) : Vector4(0.42f, 0.44f, 0.50f, 1);
            Vector3 up(0, 1, 0);
            addTri(v, idx, {x, 0, z}, {x + cell, 0, z}, {x + cell, 0, z + cell}, up, col);
            addTri(v, idx, {x, 0, z}, {x + cell, 0, z + cell}, {x, 0, z + cell}, up, col);
        }
    }
    return upload(r, v, idx);
}

struct Object {
    ion::BodyId body;
    ion::ShapeType shape;
    Vector3 size; // sphere: x = radius; box: half extents
    Vector4 tint;
    bool dynamic;
};

// --- Sounds -------------------------------------------------------------------

struct Sounds {
    ion::Sound thud, knock, whoosh, boom, siren;
};

Sounds makeSounds() {
    Sounds s;
    s.thud = ion::synth::sweep(150.0f, 55.0f, 0.35f, 0.9f);
    s.knock = ion::synth::noiseBurst(0.18f, 1.6f, 0.45f, 7.0f, 5);
    s.whoosh = ion::synth::noiseBurst(0.35f, 0.7f, 0.12f, 3.0f, 6);
    s.boom = ion::synth::noiseBurst(1.0f, 1.8f, 0.07f, 4.0f, 7);
    s.siren = ion::synth::wobbleLoop(420.0f, 640.0f, 1.0f, 2.0f, 0.4f);
    return s;
}

class App {
public:
    int run(const char* backendName) {
        ion::WindowConfig config;
        config.title = "Ion Physics Sandbox";
        config.appName = "IonPhysics";
        config.width = 1280;
        config.height = 720;
        window_ = ion::Window(config);
        if (!window_.create()) {
            std::printf("failed to create window\n");
            return 1;
        }

        ion::RendererConfig rc;
        if (std::strcmp(backendName, "metal") == 0) rc.backend = ion::RendererBackend::Metal;
        else if (std::strcmp(backendName, "gl") == 0) rc.backend = ion::RendererBackend::OpenGL;
        else if (std::strcmp(backendName, "null") == 0) rc.backend = ion::RendererBackend::Null;
        if (!renderer_.initialize(&window_, rc)) {
            std::printf("failed to initialize renderer\n");
            window_.destroy();
            return 1;
        }

        bool metal = renderer_.gpuInfo().backend == ion::RendererBackend::Metal;
        ion::ShaderSource src;
        src.vertex = metal ? kMetalVertexShader : kGLSLVertexShader;
        src.fragment = metal ? kMetalFragmentShader : kGLSLFragmentShader;
        shader_ = renderer_.createShader(src);
        if (!shader_.isValid()) {
            std::printf("failed to create shader\n");
            renderer_.shutdown();
            window_.destroy();
            return 1;
        }

        cube_ = buildUnitCube(renderer_);
        sphere_ = buildUnitSphere(renderer_);
        floor_ = buildFloor(renderer_, 40, 1.5f);

        audio_.initialize();
        sounds_ = makeSounds();

        camera_.setFov(1.1f);
        camera_.setViewport(window_.width(), window_.height());
        camera_.setFar(300.0f);

        reset_();
        std::printf("WASD/Space/C fly | right-drag or arrows look | click/F fire | E blast | B crates | R reset\n");

        timer_.reset();
        while (window_.isOpen()) {
            window_.pollEvents();
            float dt = std::min(timer_.tick(), 0.05f);
            if (ion::input::isKeyPressed(ion::Key::Escape)) break;
            update_(dt);
            render_();
            ion::input::update();
        }

        audio_.shutdown();
        for (Mesh* m : {&cube_, &sphere_, &floor_}) {
            renderer_.destroyVertexBuffer(m->vertexBuffer);
            renderer_.destroyIndexBuffer(m->indexBuffer);
        }
        renderer_.destroyShader(shader_);
        renderer_.shutdown();
        window_.destroy();
        return 0;
    }

private:
    // --- World setup ----------------------------------------------------------

    ion::BodyId addStatic_(Vector3 pos, Vector3 half, Vector4 tint) {
        ion::BodyDesc d;
        d.type = ion::BodyType::Static;
        d.shape = ion::ShapeType::Box;
        d.halfExtents = half;
        d.position = pos;
        d.restitution = 0.35f;
        d.friction = 0.6f;
        ion::BodyId id = world_.createBody(d);
        objects_.push_back({id, ion::ShapeType::Box, half, tint, false});
        return id;
    }

    ion::BodyId addBall_(Vector3 pos, float radius, Vector3 vel, Vector4 tint) {
        ion::BodyDesc d;
        d.shape = ion::ShapeType::Sphere;
        d.radius = radius;
        d.position = pos;
        d.velocity = vel;
        d.mass = radius * radius * radius * 4.0f;
        d.restitution = 0.65f;
        d.friction = 0.3f;
        ion::BodyId id = world_.createBody(d);
        objects_.push_back({id, ion::ShapeType::Sphere, {radius, 0, 0}, tint, true});
        trimDynamic_();
        return id;
    }

    ion::BodyId addCrate_(Vector3 pos, float half, Vector4 tint) {
        ion::BodyDesc d;
        d.shape = ion::ShapeType::Box;
        d.halfExtents = {half, half, half};
        d.position = pos;
        d.mass = half * half * half * 8.0f;
        d.restitution = 0.1f;
        d.friction = 0.7f;
        ion::BodyId id = world_.createBody(d);
        objects_.push_back({id, ion::ShapeType::Box, {half, half, half}, tint, true});
        trimDynamic_();
        return id;
    }

    // Keep the simulation light: drop the oldest dynamic objects past a cap.
    void trimDynamic_() {
        constexpr size_t kMaxDynamic = 160;
        size_t count = std::count_if(objects_.begin(), objects_.end(), [](const Object& o) { return o.dynamic; });
        for (auto it = objects_.begin(); it != objects_.end() && count > kMaxDynamic;) {
            if (it->dynamic) {
                world_.destroyBody(it->body);
                it = objects_.erase(it);
                --count;
            } else {
                ++it;
            }
        }
    }

    Vector4 randomTint_() {
        static const Vector4 palette[] = {
            {0.95f, 0.35f, 0.30f, 0}, {0.98f, 0.70f, 0.20f, 0}, {0.35f, 0.80f, 0.45f, 0},
            {0.30f, 0.65f, 0.95f, 0}, {0.75f, 0.45f, 0.90f, 0}, {0.30f, 0.85f, 0.85f, 0},
        };
        return palette[std::uniform_int_distribution<int>(0, 5)(rng_)];
    }

    void reset_() {
        for (const Object& o : objects_) world_.destroyBody(o.body);
        objects_.clear();
        audio_.mixer().stopAll();
        sirenVoice_ = ion::INVALID_VOICE;

        // Arena: floor slab, walls, a platform, and some steps.
        floorBody_ = addStatic_({0, -0.5f, 0}, {30, 0.5f, 30}, {0, 0, 0, 0});
        objects_.pop_back(); // the checkered floor mesh draws this one
        const Vector4 wall(0.55f, 0.58f, 0.68f, 0.0f);
        addStatic_({0, 1.5f, -14}, {14, 1.5f, 0.5f}, wall);
        addStatic_({0, 1.5f, 14}, {14, 1.5f, 0.5f}, wall);
        addStatic_({-14, 1.5f, 0}, {0.5f, 1.5f, 14}, wall);
        addStatic_({14, 1.5f, 0}, {0.5f, 1.5f, 14}, wall);
        addStatic_({7, 0.5f, -7}, {2.5f, 0.5f, 2.5f}, {0.60f, 0.50f, 0.40f, 0});
        addStatic_({9.5f, 1.0f, -7}, {1.0f, 1.0f, 2.5f}, {0.70f, 0.58f, 0.44f, 0});
        addStatic_({-8, 0.4f, -5}, {1.5f, 0.4f, 1.5f}, {0.45f, 0.55f, 0.60f, 0});
        addStatic_({-5.5f, 0.2f, -5}, {1.0f, 0.2f, 1.5f}, {0.50f, 0.60f, 0.65f, 0});

        // A moving platform (kinematic: carried along its path, pushes things).
        ion::BodyDesc k;
        k.type = ion::BodyType::Kinematic;
        k.shape = ion::ShapeType::Box;
        k.halfExtents = {2.0f, 0.25f, 2.0f};
        k.position = {0, 1.0f, 5};
        k.friction = 0.9f;
        platform_ = world_.createBody(k);
        objects_.push_back({platform_, ion::ShapeType::Box, k.halfExtents, {0.85f, 0.55f, 0.25f, 0}, false});
        platformTime_ = 0.0f;

        // Siren emitter: a ghost (collides with nothing) circling overhead.
        ion::BodyDesc s;
        s.type = ion::BodyType::Kinematic;
        s.shape = ion::ShapeType::Sphere;
        s.radius = 0.35f;
        s.position = {9, 3.0f, 0};
        s.layer = 8;
        s.mask = 0;
        siren_ = world_.createBody(s);
        objects_.push_back({siren_, ion::ShapeType::Sphere, {0.35f, 0, 0}, {0.1f, 0.1f, 0.1f, 0.9f}, false});
        sirenAngle_ = 0.0f;
        sirenVoice_ = audio_.play3D(sounds_.siren,
                                    {.position = {9, 3.0f, 0}, .minDistance = 3.0f, .maxDistance = 45.0f, .rolloff = 1.0f},
                                    {.volume = 0.7f, .loop = true});

        // A small pyramid of crates, and a few balls to start things off.
        buildTower_({-2.5f, 0, -8}, 4);
        for (int i = 0; i < 6; i++) {
            addBall_({(float)i * 1.4f - 3.5f, 6.0f + i * 0.9f, 2.0f}, 0.35f + 0.1f * (i % 3), {}, randomTint_());
        }

        camera_.setPosition({0, 3.5f, 11});
        camera_.lookAt({0, 1.5f, 0});
        prevCameraPos_ = camera_.position();
    }

    void buildTower_(Vector3 base, int layers) {
        const float h = 0.5f;
        for (int layer = 0; layer < layers; layer++) {
            int n = layers - layer;
            for (int i = 0; i < n; i++) {
                float x = base.x + (i - (n - 1) * 0.5f) * 1.05f * 2 * h;
                addCrate_({x, base.y + h + layer * (2 * h + 0.02f), base.z}, h, randomTint_());
            }
        }
    }

    // --- Frame --------------------------------------------------------------

    void update_(float dt) {
        namespace in = ion::input;
        if (in::isKeyPressed(ion::Key::R)) reset_();

        updateCamera_(dt);
        updateMovers_(dt);

        if (in::isMousePressed(ion::MouseButton::Left) || in::isKeyPressed(ion::Key::F)) fire_();
        if (in::isKeyPressed(ion::Key::E)) blast_();
        if (in::isKeyPressed(ion::Key::B)) {
            Vector3 p = camera_.position() + camera_.forward() * 7.0f;
            buildTower_({p.x, std::max(p.y, 0.0f) + 2.0f, p.z}, 3);
        }

        world_.step(dt);
        playImpacts_();

        // Anything that fell out of the world is recycled.
        for (auto it = objects_.begin(); it != objects_.end();) {
            if (it->dynamic && world_.position(it->body).y < -20.0f) {
                world_.destroyBody(it->body);
                it = objects_.erase(it);
            } else {
                ++it;
            }
        }

        // Spatial audio: the listener is the camera; the siren follows its body.
        Vector3 camVel = dt > 0 ? (camera_.position() - prevCameraPos_) / dt : Vector3();
        prevCameraPos_ = camera_.position();
        ion::Listener l;
        l.position = camera_.position();
        l.velocity = camVel;
        l.forward = camera_.forward();
        l.up = camera_.up();
        audio_.setListener(l);
        audio_.mixer().setVoiceSpatial(sirenVoice_, world_.position(siren_), world_.velocity(siren_));
    }

    void updateCamera_(float dt) {
        namespace in = ion::input;
        const float sens = 0.0025f;
        if (in::isMouseDown(ion::MouseButton::Right)) {
            camera_.setYaw(camera_.yaw() - in::mouseDeltaX() * sens);
            camera_.setPitch(camera_.pitch() - in::mouseDeltaY() * sens);
        }
        const float turn = 1.8f * dt;
        if (in::isKeyDown(ion::Key::ArrowLeft)) camera_.setYaw(camera_.yaw() + turn);
        if (in::isKeyDown(ion::Key::ArrowRight)) camera_.setYaw(camera_.yaw() - turn);
        if (in::isKeyDown(ion::Key::ArrowUp)) camera_.setPitch(camera_.pitch() + turn);
        if (in::isKeyDown(ion::Key::ArrowDown)) camera_.setPitch(camera_.pitch() - turn);

        Vector3 move;
        move.z = (in::isKeyDown(ion::Key::W) ? 1.0f : 0.0f) - (in::isKeyDown(ion::Key::S) ? 1.0f : 0.0f);
        move.x = (in::isKeyDown(ion::Key::D) ? 1.0f : 0.0f) - (in::isKeyDown(ion::Key::A) ? 1.0f : 0.0f);
        move.y = (in::isKeyDown(ion::Key::Space) ? 1.0f : 0.0f) - (in::isKeyDown(ion::Key::C) ? 1.0f : 0.0f);
        camera_.move(move * (7.0f * dt));
        // Stay inside the arena and above the floor.
        Vector3 p = camera_.position();
        p.x = std::clamp(p.x, -13.0f, 13.0f);
        p.z = std::clamp(p.z, -13.0f, 13.0f);
        p.y = std::clamp(p.y, 0.4f, 12.0f);
        camera_.setPosition(p);
    }

    void updateMovers_(float dt) {
        // Kinematic bodies are driven by setting their velocity each frame.
        platformTime_ += dt;
        float vx = std::cos(platformTime_ * 0.7f) * 4.0f * 0.7f;
        world_.setVelocity(platform_, {vx, 0, 0});

        sirenAngle_ += dt * 0.55f; // radians per second
        const float radius = 9.0f;
        Vector3 target(std::cos(sirenAngle_) * radius, 3.0f, std::sin(sirenAngle_) * radius);
        Vector3 v = (target - world_.position(siren_)) / std::max(dt, 1e-4f);
        world_.setVelocity(siren_, v);
    }

    void fire_() {
        Vector3 dir = camera_.forward();
        addBall_(camera_.position() + dir * 1.2f, 0.3f, dir * 22.0f, randomTint_());
        audio_.play(sounds_.whoosh, {.volume = 0.5f, .pitch = 1.0f + 0.2f * std::uniform_real_distribution<float>(-1, 1)(rng_)});
    }

    // Ray cast from the crosshair, then shove everything near the hit point.
    void blast_() {
        ion::RaycastHit hit;
        ion::Ray ray{camera_.position(), camera_.forward()};
        Vector3 center = camera_.position() + camera_.forward() * 10.0f;
        if (world_.raycast(ray, 60.0f, hit)) center = hit.point + hit.normal * 0.5f;

        for (ion::BodyId id : world_.overlapSphere(center, 5.0f)) {
            Vector3 away = world_.position(id) - center;
            float d = std::max(away.length(), 0.5f);
            away = away.normalized() + Vector3(0, 0.6f, 0);
            world_.applyImpulse(id, away.normalized() * (60.0f / (d + 1.0f)));
        }
        audio_.play3D(sounds_.boom, {.position = center, .minDistance = 4.0f, .maxDistance = 60.0f},
                      {.volume = 1.0f});
    }

    // Turn collision events into positioned sounds.
    void playImpacts_() {
        auto events = world_.takeEvents();
        std::sort(events.begin(), events.end(),
                  [](const ion::CollisionEvent& a, const ion::CollisionEvent& b) {
                      return a.impactSpeed > b.impactSpeed;
                  });
        int played = 0;
        for (const ion::CollisionEvent& e : events) {
            if (e.impactSpeed < 1.2f || played >= 6) break;
            bool ball = isBall_(e.a) || isBall_(e.b);
            float size = std::max(sizeOf_(e.a), sizeOf_(e.b));
            float volume = std::clamp(e.impactSpeed / 12.0f, 0.08f, 1.0f);
            float pitch = std::clamp(1.0f / (0.6f + size), 0.6f, 1.8f) *
                          (0.92f + 0.16f * std::uniform_real_distribution<float>(0, 1)(rng_));
            audio_.play3D(ball ? sounds_.thud : sounds_.knock,
                          {.position = e.point, .minDistance = 2.0f, .maxDistance = 50.0f, .rolloff = 1.2f},
                          {.volume = volume, .pitch = pitch});
            ++played;
        }
    }

    const Object* objectFor_(ion::BodyId id) const {
        for (const Object& o : objects_) {
            if (o.body == id) return &o;
        }
        return nullptr;
    }
    bool isBall_(ion::BodyId id) const {
        const Object* o = objectFor_(id);
        return o && o->shape == ion::ShapeType::Sphere;
    }
    float sizeOf_(ion::BodyId id) const {
        const Object* o = objectFor_(id);
        if (!o || !o->dynamic) return 0.0f;
        return o->shape == ion::ShapeType::Sphere ? o->size.x : o->size.x;
    }

    // --- Rendering ------------------------------------------------------------

    void draw_(const Mesh& mesh, const ion::Matrix4& model, const Vector4& tint) {
        renderer_.setUniform("uMVP", viewProj_ * model);
        renderer_.setUniform("uTint", tint);
        renderer_.setUniform("uLightDir", Vector4(0.45f, 0.85f, 0.35f, 0.0f));
        renderer_.setVertexBuffer(mesh.vertexBuffer);
        renderer_.setIndexBuffer(mesh.indexBuffer);
        renderer_.drawIndexed(mesh.indexCount);
    }

    void render_() {
        camera_.setViewport(window_.width(), window_.height());
        viewProj_ = camera_.viewProjection();

        renderer_.beginFrame();
        renderer_.clear(ion::Color(0.55f, 0.70f, 0.88f));
        renderer_.useShader(shader_);

        draw_(floor_, ion::Matrix4::identity(), {1, 1, 1, 0});
        for (const Object& o : objects_) {
            Vector3 p = world_.position(o.body);
            if (o.shape == ion::ShapeType::Sphere) {
                draw_(sphere_, ion::Matrix4::translation(p) * ion::Matrix4::scale({o.size.x, o.size.x, o.size.x}),
                      o.tint);
            } else {
                draw_(cube_, ion::Matrix4::translation(p) * ion::Matrix4::scale(o.size * 2.0f), o.tint);
            }
        }
        renderer_.endFrame();
    }

    ion::Window window_;
    ion::Renderer renderer_;
    ion::Shader shader_;
    ion::Camera camera_;
    ion::Timer timer_;
    Mesh cube_, sphere_, floor_;
    ion::Matrix4 viewProj_;

    ion::PhysicsWorld world_;
    std::vector<Object> objects_;
    ion::BodyId floorBody_ = ion::INVALID_BODY;
    ion::BodyId platform_ = ion::INVALID_BODY;
    ion::BodyId siren_ = ion::INVALID_BODY;
    float platformTime_ = 0.0f;
    float sirenAngle_ = 0.0f;

    ion::Audio audio_;
    Sounds sounds_;
    ion::VoiceId sirenVoice_ = ion::INVALID_VOICE;
    Vector3 prevCameraPos_;

    std::mt19937 rng_{12345};
};

} // namespace

int main(int argc, char** argv) {
    App app;
    return app.run(argc > 1 ? argv[1] : "auto");
}

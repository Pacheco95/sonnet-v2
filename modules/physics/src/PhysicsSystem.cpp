#include <sonnet/physics/PhysicsSystem.h>
#include <sonnet/world/GameObject.h>
#include <sonnet/world/Scene.h>

// Jolt must be included before anything that redefines assert.
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <thread>
#include <unordered_map>

namespace sonnet::physics {

// ── Object / broad-phase layers ───────────────────────────────────────────────

namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING     = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS  = 2;
}

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        m_objToBP[Layers::NON_MOVING] = JPH::BroadPhaseLayer{0};
        m_objToBP[Layers::MOVING]     = JPH::BroadPhaseLayer{1};
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return m_objToBP[layer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == JPH::BroadPhaseLayer{0} ? "NON_MOVING" : "MOVING";
    }
#endif
private:
    JPH::BroadPhaseLayer m_objToBP[Layers::NUM_LAYERS];
};

class ObjVsBPFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer obj, JPH::BroadPhaseLayer bp) const override {
        switch (obj) {
            case Layers::NON_MOVING: return bp == JPH::BroadPhaseLayer{1};
            case Layers::MOVING:     return true;
            default:                 return false;
        }
    }
};

class ObjLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer o1, JPH::ObjectLayer o2) const override {
        switch (o1) {
            case Layers::NON_MOVING: return o2 == Layers::MOVING;
            case Layers::MOVING:     return true;
            default:                 return false;
        }
    }
};

// ── Impl ──────────────────────────────────────────────────────────────────────

struct PhysicsSystem::Impl {
    std::unique_ptr<JPH::TempAllocatorImpl>  tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<BPLayerInterfaceImpl>    bpLayerInterface;
    std::unique_ptr<ObjVsBPFilterImpl>       objVsBPFilter;
    std::unique_ptr<ObjLayerPairFilterImpl>  objLayerPairFilter;
    std::unique_ptr<JPH::PhysicsSystem>      physicsSystem;

    std::unordered_map<const world::GameObject *, JPH::BodyID>    bodyIds;
    std::unordered_map<const world::GameObject *, PhysicsBodyDef> bodyDefs;
};

// ── PhysicsSystem ─────────────────────────────────────────────────────────────

PhysicsSystem::PhysicsSystem()  = default;
PhysicsSystem::~PhysicsSystem() { if (m_impl) shutdown(); }

void PhysicsSystem::init() {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    m_impl = std::make_unique<Impl>();

    constexpr JPH::uint kTempAllocBytes = 10u * 1024u * 1024u;
    m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(kTempAllocBytes);

    const int numThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    m_impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads);

    m_impl->bpLayerInterface   = std::make_unique<BPLayerInterfaceImpl>();
    m_impl->objVsBPFilter      = std::make_unique<ObjVsBPFilterImpl>();
    m_impl->objLayerPairFilter = std::make_unique<ObjLayerPairFilterImpl>();

    m_impl->physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    m_impl->physicsSystem->Init(
        /*maxBodies=*/        2048,
        /*numBodyMutexes=*/   0,
        /*maxBodyPairs=*/     4096,
        /*maxContactConstraints=*/ 2048,
        *m_impl->bpLayerInterface,
        *m_impl->objVsBPFilter,
        *m_impl->objLayerPairFilter);
    m_impl->physicsSystem->SetGravity(JPH::Vec3{0.0f, -9.81f, 0.0f});
}

void PhysicsSystem::shutdown() {
    if (!m_impl) return;

    auto &bi = m_impl->physicsSystem->GetBodyInterface();
    for (auto &[obj, bodyId] : m_impl->bodyIds) {
        if (!bodyId.IsInvalid()) {
            bi.RemoveBody(bodyId);
            bi.DestroyBody(bodyId);
        }
    }
    m_impl.reset();

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

void PhysicsSystem::addBody(world::GameObject &obj, const PhysicsBodyDef &def) {
    if (!m_impl) return;

    const glm::vec3 scale = obj.transform.getLocalScale();
    const glm::vec3 pos   = obj.transform.getWorldPosition();
    const glm::quat rot   = obj.transform.getWorldRotation();

    // Build shape.
    JPH::ShapeRefC shape;
    if (def.shapeType == ShapeType::Sphere) {
        const float radius = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)}) * 0.5f;
        shape = new JPH::SphereShape(std::max(radius, 0.01f));
    } else {
        const JPH::Vec3 halfExt{
            std::max(std::abs(scale.x) * 0.5f, 0.01f),
            std::max(std::abs(scale.y) * 0.5f, 0.01f),
            std::max(std::abs(scale.z) * 0.5f, 0.01f),
        };
        shape = new JPH::BoxShape(halfExt);
    }

    JPH::EMotionType motionType;
    JPH::ObjectLayer layer;
    switch (def.bodyType) {
        case BodyType::Dynamic:
            motionType = JPH::EMotionType::Dynamic;
            layer      = Layers::MOVING;
            break;
        case BodyType::Kinematic:
            motionType = JPH::EMotionType::Kinematic;
            layer      = Layers::MOVING;
            break;
        default:
            motionType = JPH::EMotionType::Static;
            layer      = Layers::NON_MOVING;
            break;
    }

    // Jolt quaternion is (x,y,z,w).
    JPH::BodyCreationSettings settings{
        shape,
        JPH::RVec3{pos.x, pos.y, pos.z},
        JPH::Quat{rot.x, rot.y, rot.z, rot.w},
        motionType,
        layer,
    };
    settings.mFriction    = def.friction;
    settings.mRestitution = def.restitution;

    if (def.bodyType != BodyType::Static) {
        settings.mLinearDamping    = def.linearDamping;
        settings.mAngularDamping   = def.angularDamping;
        settings.mOverrideMassProperties =
            JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = def.mass;
    }

    const JPH::EActivation activation = (def.bodyType != BodyType::Static)
        ? JPH::EActivation::Activate
        : JPH::EActivation::DontActivate;

    auto &bi = m_impl->physicsSystem->GetBodyInterface();
    const JPH::BodyID id = bi.CreateAndAddBody(settings, activation);

    m_impl->bodyIds[&obj]  = id;
    m_impl->bodyDefs[&obj] = def;
}

void PhysicsSystem::removeBody(world::GameObject *obj) {
    if (!m_impl || !obj) return;
    auto it = m_impl->bodyIds.find(obj);
    if (it == m_impl->bodyIds.end()) return;

    auto &bi = m_impl->physicsSystem->GetBodyInterface();
    bi.RemoveBody(it->second);
    bi.DestroyBody(it->second);

    m_impl->bodyIds.erase(it);
    m_impl->bodyDefs.erase(obj);
}

const PhysicsBodyDef *PhysicsSystem::getBodyDef(const world::GameObject *obj) const {
    if (!m_impl) return nullptr;
    auto it = m_impl->bodyDefs.find(obj);
    return it != m_impl->bodyDefs.end() ? &it->second : nullptr;
}

void PhysicsSystem::step(world::Scene & /*scene*/, float dt) {
    if (!m_impl) return;

    const float stepDt = std::min(dt, 1.0f / 30.0f);
    m_impl->physicsSystem->Update(stepDt, 1,
        m_impl->tempAllocator.get(),
        m_impl->jobSystem.get());

    auto &bi = m_impl->physicsSystem->GetBodyInterface();
    for (auto &[obj, bodyId] : m_impl->bodyIds) {
        if (bodyId.IsInvalid()) continue;
        const auto &def = m_impl->bodyDefs.at(obj);
        if (def.bodyType == BodyType::Static) continue;

        const JPH::RVec3 p = bi.GetPosition(bodyId);
        const JPH::Quat  q = bi.GetRotation(bodyId);

        // Cast away const — we own the GameObject and update its transform.
        auto *mutableObj = const_cast<world::GameObject *>(obj);
        mutableObj->transform.setWorldPosition({p.GetX(), p.GetY(), p.GetZ()});
        // glm::quat(w,x,y,z), Jolt Quat: GetX/Y/Z/W
        mutableObj->transform.setWorldRotation({q.GetW(), q.GetX(), q.GetY(), q.GetZ()});
    }
}

} // namespace sonnet::physics

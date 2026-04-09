#pragma once

#include <sonnet/core/RendererTraits.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace sonnet::world {

class Transform final {
public:
    explicit Transform(
        const core::RendererTraits &traits = core::presets::OpenGL);

    // ── Hierarchy ─────────────────────────────────────────────────────────────
    void setParent(Transform *parent, bool keepWorldTransform = true);
    [[nodiscard]] Transform                  *getParent()   const { return m_parent; }
    [[nodiscard]] const std::vector<Transform *> &children() const { return m_children; }

    // ── Local position ────────────────────────────────────────────────────────
    void setLocalPosition(const glm::vec3 &pos);
    [[nodiscard]] glm::vec3 getLocalPosition() const { return m_localPosition; }

    // ── World position ────────────────────────────────────────────────────────
    void setWorldPosition(const glm::vec3 &worldPos);
    [[nodiscard]] glm::vec3 getWorldPosition() const;

    // ── Local rotation ────────────────────────────────────────────────────────
    void setLocalRotation(const glm::quat &rot);
    [[nodiscard]] glm::quat getLocalRotation() const { return m_localRotation; }

    // ── World rotation ────────────────────────────────────────────────────────
    void setWorldRotation(const glm::quat &worldRot);
    [[nodiscard]] glm::quat getWorldRotation() const;

    // ── Scale (local only) ────────────────────────────────────────────────────
    void setLocalScale(const glm::vec3 &scale);
    [[nodiscard]] glm::vec3 getLocalScale() const { return m_localScale; }

    // ── Convenience direction vectors (world space) ───────────────────────────
    [[nodiscard]] glm::vec3 forward() const;
    [[nodiscard]] glm::vec3 up()      const;
    [[nodiscard]] glm::vec3 right()   const;

    // ── Model matrix (local × parent chain) ───────────────────────────────────
    [[nodiscard]] const glm::mat4 &getModelMatrix() const;

    // ── Mutation helpers ──────────────────────────────────────────────────────
    void translate(const glm::vec3 &delta);
    void rotate(const glm::vec3 &axis, float angleDeg);
    void rotate(const glm::quat &rot);
    void lookAt(const glm::vec3 &target,
                const glm::vec3 &worldUp = glm::vec3(0.0f, 1.0f, 0.0f));

private:
    void markDirty();
    [[nodiscard]] glm::mat4 localMatrix() const;
    [[nodiscard]] glm::mat4 parentWorldMatrix() const;

    core::RendererTraits m_traits;

    glm::vec3 m_localPosition{0.0f};
    glm::quat m_localRotation{1.0f, 0.0f, 0.0f, 0.0f}; // identity
    glm::vec3 m_localScale{1.0f};

    Transform              *m_parent   = nullptr;
    std::vector<Transform *> m_children{};

    mutable glm::mat4 m_modelMatrix{1.0f};
    mutable bool      m_dirty = true;
};

} // namespace sonnet::world

#define GLM_ENABLE_EXPERIMENTAL
#include <sonnet/world/Transform.h>

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace sonnet::world {

Transform::Transform(const core::RendererTraits &traits)
    : m_traits(traits) {}

// ── Hierarchy ─────────────────────────────────────────────────────────────────

void Transform::setParent(Transform *parent, bool keepWorldTransform) {
    if (m_parent == parent) return;

    glm::vec3 worldPos{};
    glm::quat worldRot{};
    if (keepWorldTransform) {
        worldPos = getWorldPosition();
        worldRot = getWorldRotation();
    }

    // Remove from old parent's children list.
    if (m_parent) {
        auto &siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    }

    m_parent = parent;

    if (m_parent) {
        m_parent->m_children.push_back(this);
    }

    if (keepWorldTransform) {
        setWorldPosition(worldPos);
        setWorldRotation(worldRot);
    }

    markDirty();
}

// ── Local position ────────────────────────────────────────────────────────────

void Transform::setLocalPosition(const glm::vec3 &pos) {
    m_localPosition = pos;
    markDirty();
}

// ── World position ────────────────────────────────────────────────────────────

void Transform::setWorldPosition(const glm::vec3 &worldPos) {
    if (!m_parent) {
        m_localPosition = worldPos;
    } else {
        const glm::mat4 invParent = glm::inverse(m_parent->getModelMatrix());
        m_localPosition = glm::vec3(invParent * glm::vec4(worldPos, 1.0f));
    }
    markDirty();
}

glm::vec3 Transform::getWorldPosition() const {
    return glm::vec3(getModelMatrix()[3]);
}

// ── Local rotation ────────────────────────────────────────────────────────────

void Transform::setLocalRotation(const glm::quat &rot) {
    m_localRotation = rot;
    markDirty();
}

// ── World rotation ────────────────────────────────────────────────────────────

void Transform::setWorldRotation(const glm::quat &worldRot) {
    if (!m_parent) {
        m_localRotation = worldRot;
    } else {
        m_localRotation = glm::inverse(m_parent->getWorldRotation()) * worldRot;
    }
    markDirty();
}

glm::quat Transform::getWorldRotation() const {
    if (!m_parent) return m_localRotation;
    return m_parent->getWorldRotation() * m_localRotation;
}

// ── Scale ─────────────────────────────────────────────────────────────────────

void Transform::setLocalScale(const glm::vec3 &scale) {
    m_localScale = scale;
    markDirty();
}

// ── Direction vectors ─────────────────────────────────────────────────────────

glm::vec3 Transform::forward() const {
    return getWorldRotation() * m_traits.forwardVector();
}

glm::vec3 Transform::up() const {
    return getWorldRotation() * m_traits.upVector();
}

glm::vec3 Transform::right() const {
    return getWorldRotation() * m_traits.rightVector();
}

// ── Model matrix ──────────────────────────────────────────────────────────────

const glm::mat4 &Transform::getModelMatrix() const {
    if (m_dirty) {
        m_modelMatrix = parentWorldMatrix() * localMatrix();
        m_dirty = false;
    }
    return m_modelMatrix;
}

// ── Mutation helpers ──────────────────────────────────────────────────────────

void Transform::translate(const glm::vec3 &delta) {
    setLocalPosition(m_localPosition + delta);
}

void Transform::rotate(const glm::vec3 &axis, float angleDeg) {
    const glm::quat rot = glm::angleAxis(glm::radians(angleDeg), glm::normalize(axis));
    m_localRotation = m_localRotation * rot;
    markDirty();
}

void Transform::rotate(const glm::quat &rot) {
    m_localRotation = m_localRotation * rot;
    markDirty();
}

void Transform::lookAt(const glm::vec3 &target, const glm::vec3 &worldUp) {
    const glm::vec3 worldPos = getWorldPosition();
    const glm::vec3 dir = glm::normalize(target - worldPos);
    setWorldRotation(glm::quatLookAt(dir, worldUp));
}

// ── Private helpers ───────────────────────────────────────────────────────────

void Transform::markDirty() {
    m_dirty = true;
    for (Transform *child : m_children) {
        child->markDirty();
    }
}

glm::mat4 Transform::localMatrix() const {
    glm::mat4 m = glm::mat4_cast(m_localRotation);
    m[0] *= m_localScale.x;
    m[1] *= m_localScale.y;
    m[2] *= m_localScale.z;
    m[3] = glm::vec4(m_localPosition, 1.0f);
    return m;
}

glm::mat4 Transform::parentWorldMatrix() const {
    if (!m_parent) return glm::mat4(1.0f);
    return m_parent->getModelMatrix();
}

} // namespace sonnet::world

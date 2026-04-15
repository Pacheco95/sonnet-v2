#pragma once

#include <sonnet/api/input/Key.h>
#include <sonnet/input/InputSystem.h>
#include <sonnet/world/Transform.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

// Input controller that drives a Transform. Projection parameters live in the
// CameraComponent on the same GameObject — this class owns neither.
class FlyCamera {
public:
    explicit FlyCamera(sonnet::world::Transform &transform) : m_transform(transform) {
        m_transform.setLocalPosition({0.0f, 0.0f, 3.0f});
        applyOrientation();
    }

    void update(float dt, const sonnet::input::InputSystem &input) {
        using sonnet::api::input::Key;

        const glm::vec2 delta = input.mouseDelta();
        m_yaw   += delta.x * m_sensitivity;
        m_pitch  = std::clamp(m_pitch - delta.y * m_sensitivity, -89.0f, 89.0f);
        applyOrientation();

        const glm::vec3 front = m_transform.forward();
        const glm::vec3 right = m_transform.right();
        const float     speed = m_speed * dt;

        glm::vec3 pos = m_transform.getLocalPosition();
        if (input.isKeyDown(Key::W)) pos += front    * speed;
        if (input.isKeyDown(Key::S)) pos -= front    * speed;
        if (input.isKeyDown(Key::D)) pos += right    * speed;
        if (input.isKeyDown(Key::A)) pos -= right    * speed;
        if (input.isKeyDown(Key::E)) pos += WORLD_UP * speed;
        if (input.isKeyDown(Key::Q)) pos -= WORLD_UP * speed;
        m_transform.setLocalPosition(pos);
    }

private:
    static constexpr glm::vec3 WORLD_UP{0.0f, 1.0f, 0.0f};

    void applyOrientation() {
        const float yr = glm::radians(m_yaw);
        const float pr = glm::radians(m_pitch);
        const glm::vec3 front = glm::normalize(glm::vec3{
            std::cos(yr) * std::cos(pr),
            std::sin(pr),
            std::sin(yr) * std::cos(pr),
        });
        m_transform.setLocalRotation(glm::quatLookAt(front, WORLD_UP));
    }

    sonnet::world::Transform &m_transform;
    float m_yaw{-90.0f}, m_pitch{0.0f};
    float m_speed{5.0f}, m_sensitivity{0.1f};
};

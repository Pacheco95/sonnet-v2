#include <sonnet/world/AnimationPlayer.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace sonnet::world {

// ── Keyframe interpolation helpers ───────────────────────────────────────────

// Find the index of the last keyframe with time ≤ t.
template<typename T>
static std::size_t findFrame(const std::vector<std::pair<float, T>> &keys, float t) {
    std::size_t i = 0;
    while (i + 1 < keys.size() && keys[i + 1].first <= t)
        ++i;
    return i;
}

static glm::vec3 sampleVec3(const std::vector<std::pair<float, glm::vec3>> &keys, float t) {
    if (keys.empty()) return glm::vec3{0.0f};
    if (keys.size() == 1) return keys[0].second;
    const std::size_t i = findFrame(keys, t);
    if (i + 1 >= keys.size()) return keys[i].second;
    const float t0 = keys[i].first, t1 = keys[i + 1].first;
    const float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
    return glm::mix(keys[i].second, keys[i + 1].second, alpha);
}

static glm::quat sampleQuat(const std::vector<std::pair<float, glm::quat>> &keys, float t) {
    if (keys.empty()) return glm::quat{1, 0, 0, 0};
    if (keys.size() == 1) return keys[0].second;
    const std::size_t i = findFrame(keys, t);
    if (i + 1 >= keys.size()) return keys[i].second;
    const float t0 = keys[i].first, t1 = keys[i + 1].first;
    const float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
    return glm::slerp(keys[i].second, keys[i + 1].second, alpha);
}

// ── AnimationPlayer::update ───────────────────────────────────────────────────

void AnimationPlayer::update(float dt) {
    if (clips.empty() || !playing) return;
    if (currentClip < 0 || currentClip >= static_cast<int>(clips.size())) return;

    const auto &clip = clips[currentClip];
    if (clip.duration <= 0.0f) return;

    time += dt;
    if (loop) {
        time = std::fmod(time, clip.duration);
    } else {
        if (time >= clip.duration) {
            time    = clip.duration;
            playing = false;
        }
    }

    for (const auto &ch : clip.channels) {
        auto it = m_targets.find(ch.nodeName);
        if (it == m_targets.end()) continue;
        Transform *t = it->second;

        if (!ch.positions.empty())
            t->setLocalPosition(sampleVec3(ch.positions, time));
        if (!ch.rotations.empty())
            t->setLocalRotation(sampleQuat(ch.rotations, time));
        if (!ch.scales.empty())
            t->setLocalScale(sampleVec3(ch.scales, time));
    }
}

} // namespace sonnet::world

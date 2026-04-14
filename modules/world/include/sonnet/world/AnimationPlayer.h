#pragma once

#include <sonnet/loaders/ModelLoader.h>
#include <sonnet/world/Transform.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace sonnet::world {

// Drives a set of named Transforms from glTF animation keyframe data.
// Attach one to the root GameObject of a loaded model; register each
// child node's Transform via addTarget() keyed by its Assimp node name.
class AnimationPlayer {
public:
    std::vector<loaders::AnimationClip> clips;
    int   currentClip = 0;
    float time        = 0.0f;
    bool  playing     = true;
    bool  loop        = true;

    // Register a target transform for a given node name.
    // Called once after the scene hierarchy is created.
    void addTarget(const std::string &nodeName, Transform *t) {
        m_targets[nodeName] = t;
    }

    // Advance playback by dt seconds and write interpolated TRS to all targets.
    void update(float dt);

private:
    std::unordered_map<std::string, Transform *> m_targets;
};

} // namespace sonnet::world

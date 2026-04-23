#pragma once

#include "PostProcess.h"
#include "RenderTargets.h"
#include "ShadowMaps.h"

#include <sonnet/api/render/FrameContext.h>
#include <sonnet/api/render/IRendererBackend.h>
#include <sonnet/input/InputSystem.h>
#include <sonnet/renderer/frontend/Renderer.h>
#include <sonnet/physics/PhysicsSystem.h>
#include <sonnet/scene/SceneLoader.h>
#include <sonnet/scripting/LuaScriptRuntime.h>
#include <sonnet/world/GameObject.h>
#include <sonnet/world/Scene.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

// Parameters passed to EditorUI::draw() each frame.
struct EditorParams {
    // Camera / viewport
    glm::mat4  viewMat{};
    glm::mat4  projMat{};
    glm::vec3  camPos{};
    glm::ivec2 fbSize{};
    // Opaque texture id for ImGui::Image (cast to ImTextureID at call site).
    ImTextureID viewportTexId{};

    // Frame context (needed for picking GPU render pass)
    const sonnet::api::render::FrameContext *ctx = nullptr;

    // Picking render target (from RenderTargets)
    sonnet::core::RenderTargetHandle pickingRT{};

    // Input
    const sonnet::input::InputSystem *input = nullptr;

    // Camera object reference (excluded from gizmo manipulation)
    const sonnet::world::GameObject *cameraObj = nullptr;

    // Shader reload notification (display in menu bar)
    std::string shaderReloadMsg;
    float       shaderReloadMsgTimer = 0.0f;

    // Shadow debug info (read-only)
    int         shadowLightCount = 0;
    const std::array<float, ShadowMaps::NUM_CASCADES> *csmSplitDepths = nullptr;

    // ── Tweakable render parameters (write-through raw pointers) ─────────────
    float *rotationSpeed   = nullptr;
    float *exposure        = nullptr;
    float *shadowBias      = nullptr;
    float *pointShadowBias = nullptr;
    float *bloomThreshold  = nullptr;
    float *bloomIntensity  = nullptr;
    int   *bloomIterations = nullptr;
    bool  *ssaoEnabled     = nullptr;
    float *ssaoRadius      = nullptr;
    float *ssaoBias        = nullptr;
    bool  *ssaoShow        = nullptr;
    bool  *fxaaEnabled     = nullptr;
    bool  *outlineEnabled  = nullptr;
    glm::vec3 *outlineColor = nullptr;
    bool  *ssrEnabled      = nullptr;
    int   *ssrMaxSteps     = nullptr;
    float *ssrStrength     = nullptr;
    float *ssrStepSize     = nullptr;
    float *ssrThickness    = nullptr;
    float *ssrMaxDistance  = nullptr;
    float *ssrRoughnessMax = nullptr;

    // Callback for File → Exit menu item
    std::function<void()> requestClose;
};

class EditorUI {
public:
    EditorUI(sonnet::renderer::frontend::Renderer  &renderer,
             sonnet::api::render::IRendererBackend &backend,
             sonnet::world::Scene                  &scene,
             sonnet::scripting::LuaScriptRuntime   &scripts,
             const sonnet::scene::LoadedScene      &loaded,
             const PostProcess                     &pp,
             sonnet::physics::PhysicsSystem        &physics,
             const char                            *sceneFilePath);

    // Draw all editor panels for this frame (called between imgui.begin/end).
    void draw(EditorParams &p);

    // State accessors (read by main loop).
    bool                           viewportFocused() const { return m_viewportFocused; }
    sonnet::world::GameObject     *selectedObject()  const { return m_selectedObject; }

private:
    void drawViewportPanel(EditorParams &p);
    void drawHierarchyPanel(EditorParams &p);
    void drawInspectorPanel();
    void drawAssetsPanel();
    void drawRenderSettingsPanel(EditorParams &p);
    void doPickingPass(EditorParams &p, ImVec2 vpMin, ImVec2 vpSize,
                       bool lmbClicked);
    void doGizmo(ImDrawList *dl, EditorParams &p,
                 ImVec2 vpMin, ImVec2 vpSize, bool lmbClicked);
    void saveSceneImpl();

    // References to engine / scene systems
    sonnet::renderer::frontend::Renderer  &m_renderer;
    sonnet::api::render::IRendererBackend &m_backend;
    sonnet::world::Scene                        &m_scene;
    sonnet::scripting::LuaScriptRuntime         &m_scripts;
    const sonnet::scene::LoadedScene            &m_loaded;
    const PostProcess                           &m_pp;
    sonnet::physics::PhysicsSystem              &m_physics;
    const char                                  *m_sceneFilePath;

    // Asset name caches (populated at construction)
    std::vector<std::string> m_assetMeshNames;
    std::vector<std::string> m_assetMaterialNames;
    std::vector<std::string> m_assetTextureNames;
    std::vector<std::string> m_assetShaderNames;

    // Selection + gizmo state
    sonnet::world::GameObject *m_selectedObject  = nullptr;
    glm::vec3                  m_editEuler{0.0f};

    enum class GizmoMode { Translate, Rotate, Scale };
    GizmoMode m_gizmoMode       = GizmoMode::Translate;
    int       m_gizmoHoverAxis  = 0;
    int       m_gizmoActiveAxis = 0;
    glm::vec3 m_dragStartPos{};
    glm::quat m_dragStartRot{1, 0, 0, 0};
    glm::vec3 m_dragStartScale{1.0f};
    ImVec2    m_dragStartMouse{};
    float     m_dragAccum = 0.0f;

    bool m_viewportFocused = false;
};

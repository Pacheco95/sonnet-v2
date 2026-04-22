#include "EditorUI.h"

#include <sonnet/api/input/Key.h>
#include <sonnet/api/render/RenderItem.h>

#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

// ── Constructor ───────────────────────────────────────────────────────────────

EditorUI::EditorUI(sonnet::renderer::frontend::Renderer        &renderer,
                    sonnet::renderer::opengl::GlRendererBackend &backend,
                    sonnet::world::Scene                        &scene,
                    sonnet::scripting::LuaScriptRuntime         &scripts,
                    const sonnet::scene::LoadedScene            &loaded,
                    const PostProcess                           &pp,
                    const char                                  *sceneFilePath)
    : m_renderer(renderer), m_backend(backend),
      m_scene(scene), m_scripts(scripts),
      m_loaded(loaded), m_pp(pp),
      m_sceneFilePath(sceneFilePath)
{
    // Populate asset name caches from scene.json.
    std::ifstream f{sceneFilePath};
    if (f) {
        auto doc = nlohmann::json::parse(f, nullptr, false);
        if (!doc.is_discarded() && doc.contains("assets")) {
            const auto &assets = doc["assets"];
            auto collect = [](const nlohmann::json &section,
                               std::vector<std::string> &out) {
                if (section.is_object())
                    for (const auto &[k, v] : section.items())
                        out.push_back(k);
            };
            collect(assets.value("meshes",    nlohmann::json::object()), m_assetMeshNames);
            collect(assets.value("materials", nlohmann::json::object()), m_assetMaterialNames);
            collect(assets.value("textures",  nlohmann::json::object()), m_assetTextureNames);
            collect(assets.value("shaders",   nlohmann::json::object()), m_assetShaderNames);
        }
    }
}

// ── Save scene ────────────────────────────────────────────────────────────────

void EditorUI::saveSceneImpl() {
    std::ifstream inFile{m_sceneFilePath};
    if (!inFile) return;
    nlohmann::json doc = nlohmann::json::parse(inFile, nullptr, false);
    inFile.close();
    if (doc.is_discarded() || !doc.contains("objects")) return;

    for (auto &objSpec : doc["objects"]) {
        const std::string name = objSpec.value("name", "");
        if (name.empty() || name.find('/') != std::string::npos) continue;
        auto it = m_loaded.objects.find(name);
        if (it == m_loaded.objects.end()) continue;
        const auto *obj = it->second;
        const auto &tf  = obj->transform;

        // Persist enabled state (omit key when true to keep JSON clean).
        if (!obj->enabled)
            objSpec["enabled"] = false;
        else
            objSpec.erase("enabled");

        const auto p = tf.getLocalPosition();
        objSpec["position"] = {p.x, p.y, p.z};

        const auto r = tf.getLocalRotation();
        objSpec["rotation"] = {r.x, r.y, r.z, r.w};

        const auto s = tf.getLocalScale();
        if (s == glm::vec3{1.0f})
            objSpec.erase("scale");
        else
            objSpec["scale"] = {s.x, s.y, s.z};
    }

    std::ofstream outFile{m_sceneFilePath};
    outFile << doc.dump(4) << '\n';
}

// ── Main draw entry point ─────────────────────────────────────────────────────

void EditorUI::draw(EditorParams &p) {
    using K = sonnet::api::input::Key;

    // Delete selected object (checked first, before any ImGui layout).
    if (m_selectedObject && p.input &&
        !ImGui::GetIO().WantCaptureKeyboard &&
        p.input->isKeyJustPressed(K::Delete)) {
        std::unordered_map<const sonnet::world::Transform *,
                           sonnet::world::GameObject *> tfToObj;
        for (auto &obj : m_scene.objects())
            tfToObj[&obj->transform] = obj.get();

        std::vector<sonnet::world::GameObject *> subtree;
        std::function<void(sonnet::world::GameObject *)> collect =
            [&](sonnet::world::GameObject *o) {
                subtree.push_back(o);
                for (auto *childTf : o->transform.children()) {
                    auto it = tfToObj.find(childTf);
                    if (it != tfToObj.end()) collect(it->second);
                }
            };
        collect(m_selectedObject);
        for (auto *o : subtree) {
            m_scripts.detachObject(o);
            if (m_selectedObject == o) m_selectedObject = nullptr;
        }
        m_scene.destroyObject(subtree[0]);
    }

    // ── Main menu bar ─────────────────────────────────────────────────────────
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) saveSceneImpl();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit") && p.requestClose) p.requestClose();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            ImGui::TextDisabled("No actions yet");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            ImGui::TextDisabled("Drag panels to re-dock them");
            ImGui::EndMenu();
        }
        if (p.shaderReloadMsgTimer > 0.0f && !p.shaderReloadMsg.empty()) {
            const bool isErr = p.shaderReloadMsg.rfind("Error", 0) == 0;
            ImGui::SetCursorPosX(200.0f);
            if (isErr)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
            else
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.5f, 1.0f));
            ImGui::TextUnformatted(p.shaderReloadMsg.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 80.0f);
        ImGui::TextDisabled("%.0f FPS", ImGui::GetIO().Framerate);
        ImGui::EndMainMenuBar();
    }

    // ── Full-window DockSpace ─────────────────────────────────────────────────
    {
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        constexpr ImGuiWindowFlags dsFlags =
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoCollapse  |
            ImGuiWindowFlags_NoResize    | ImGuiWindowFlags_NoMove      |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoDocking   | ImGuiWindowFlags_NoBackground;
        ImGui::Begin("##DockspaceHost", nullptr, dsFlags);
        ImGui::PopStyleVar(3);

        const ImGuiID dockId = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

        static bool layoutBuilt = false;
        if (!layoutBuilt) {
            layoutBuilt = true;
            ImGui::DockBuilderRemoveNode(dockId);
            ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);

            ImGuiID left, mid;
            ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.18f, &left, &mid);
            ImGuiID right, center;
            ImGui::DockBuilderSplitNode(mid, ImGuiDir_Right, 0.25f, &right, &center);
            ImGuiID viewport, bottom;
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, &bottom, &viewport);

            ImGui::DockBuilderDockWindow("Scene Hierarchy", left);
            ImGui::DockBuilderDockWindow("Viewport",        viewport);
            ImGui::DockBuilderDockWindow("Inspector",       right);
            ImGui::DockBuilderDockWindow("Render Settings", right);
            ImGui::DockBuilderDockWindow("Assets",          bottom);
            ImGui::DockBuilderFinish(dockId);
        }
        ImGui::End();
    }

    drawViewportPanel(p);
    drawHierarchyPanel(p);
    drawInspectorPanel();
    drawAssetsPanel();
    drawRenderSettingsPanel(p);
}

// ── Viewport panel ────────────────────────────────────────────────────────────

void EditorUI::drawViewportPanel(EditorParams &p) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");

    m_viewportFocused = ImGui::IsWindowFocused() || ImGui::IsWindowHovered();
    const ImVec2 sz = ImGui::GetContentRegionAvail();
    ImGui::Image(
        static_cast<ImTextureID>(static_cast<uintptr_t>(p.viewportTexId)),
        sz, ImVec2(0, 1), ImVec2(1, 0));

    const ImVec2 vpMin  = ImGui::GetItemRectMin();
    const ImVec2 vpMax  = ImGui::GetItemRectMax();
    const ImVec2 vpSize = ImVec2(vpMax.x - vpMin.x, vpMax.y - vpMin.y);

    // Gizmo mode hotkeys.
    if (m_viewportFocused && p.input) {
        if (p.input->isKeyJustPressed(sonnet::api::input::Key::W))
            m_gizmoMode = GizmoMode::Translate;
        if (p.input->isKeyJustPressed(sonnet::api::input::Key::E))
            m_gizmoMode = GizmoMode::Rotate;
        if (p.input->isKeyJustPressed(sonnet::api::input::Key::R))
            m_gizmoMode = GizmoMode::Scale;
    }

    const bool lmbClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    doPickingPass(p, vpMin, vpSize, lmbClicked);
    doGizmo(ImGui::GetWindowDrawList(), p, vpMin, vpSize, lmbClicked);

    // Toolbar overlay.
    {
        ImGui::SetCursorPos(ImVec2(8, 28));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 0.75f));

        auto modeBtn = [&](const char *label, GizmoMode mode, const char *key) {
            const bool active = (m_gizmoMode == mode);
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.9f, 0.9f));
            if (ImGui::SmallButton((std::string(key) + " " + label).c_str()))
                m_gizmoMode = mode;
            if (active) ImGui::PopStyleColor();
            ImGui::SameLine();
        };
        modeBtn("Translate", GizmoMode::Translate, "[W]");
        modeBtn("Rotate",    GizmoMode::Rotate,    "[E]");
        modeBtn("Scale",     GizmoMode::Scale,     "[R]");

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        if (p.input &&
            !p.input->isMouseDown(sonnet::api::input::MouseButton::Right) &&
            !m_selectedObject) {
            ImGui::SetCursorPos(ImVec2(8, 50));
            ImGui::TextDisabled("Click to select  |  RMB + WASDQE to fly");
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

// ── GPU picking pass ──────────────────────────────────────────────────────────

void EditorUI::doPickingPass(EditorParams &p, ImVec2 vpMin, ImVec2 vpSize,
                              bool lmbClicked) {
    if (!lmbClicked || m_gizmoActiveAxis != 0 || m_gizmoHoverAxis != 0) return;
    if (!p.ctx || !p.input) return;

    const ImVec2 mp = ImGui::GetMousePos();

    auto selectableAncestor = [&](sonnet::world::GameObject *obj)
            -> sonnet::world::GameObject * {
        while (obj && obj->name.find('/') != std::string::npos) {
            auto *par = obj->transform.getParent();
            if (!par) break;
            obj = nullptr;
            for (auto &o : m_scene.objects())
                if (&o->transform == par) { obj = o.get(); break; }
        }
        return obj;
    };

    std::vector<sonnet::api::render::RenderItem> pickQueue;
    std::vector<sonnet::world::GameObject *>     pickObjects;
    for (const auto &o : m_scene.objects()) {
        if (!o->enabled || !o->render) continue;
        const int id = static_cast<int>(pickObjects.size()) + 1;
        const glm::vec3 col{
            float(id & 0xFF)         / 255.0f,
            float((id >> 8)  & 0xFF) / 255.0f,
            float((id >> 16) & 0xFF) / 255.0f,
        };
        if (o->skin) {
            sonnet::api::render::MaterialInstance mat{m_pp.pickingSkinnedMatTmpl};
            mat.set("uPickColor", col);
            for (const auto &[name, val] : o->render->material.values())
                if (name.rfind("uBoneMatrices", 0) == 0)
                    mat.set(name, val);
            pickQueue.push_back({.mesh = o->render->mesh, .material = mat,
                                  .modelMatrix = o->transform.getModelMatrix()});
        } else {
            sonnet::api::render::MaterialInstance mat{m_pp.pickingMatTmpl};
            mat.set("uPickColor", col);
            pickQueue.push_back({.mesh = o->render->mesh, .material = mat,
                                  .modelMatrix = o->transform.getModelMatrix()});
        }
        pickObjects.push_back(o.get());
    }

    m_renderer.bindRenderTarget(p.pickingRT);
    m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                          static_cast<std::uint32_t>(p.fbSize.y));
    m_backend.clear({ .colors = {{0, {0.0f, 0.0f, 0.0f, 1.0f}}}, .depth = 1.0f });
    m_renderer.beginFrame();
    m_renderer.render(*p.ctx, pickQueue);
    m_renderer.endFrame();

    const int px = static_cast<int>((mp.x - vpMin.x) / vpSize.x * p.fbSize.x);
    const int py = p.fbSize.y - 1
                 - static_cast<int>((mp.y - vpMin.y) / vpSize.y * p.fbSize.y);
    std::array<std::uint8_t, 4> pixel{};
    glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());

    m_backend.bindDefaultRenderTarget();
    m_backend.setViewport(static_cast<std::uint32_t>(p.fbSize.x),
                          static_cast<std::uint32_t>(p.fbSize.y));

    const int hitId = int(pixel[0]) | (int(pixel[1]) << 8) | (int(pixel[2]) << 16);
    if (hitId > 0 && hitId <= static_cast<int>(pickObjects.size())) {
        auto *sel = selectableAncestor(pickObjects[hitId - 1]);
        m_selectedObject = sel ? sel : pickObjects[hitId - 1];
        m_editEuler = glm::degrees(glm::eulerAngles(
            m_selectedObject->transform.getLocalRotation()));
    } else {
        m_selectedObject = nullptr;
    }
}

// ── Transform gizmo ───────────────────────────────────────────────────────────

void EditorUI::doGizmo(ImDrawList *dl, EditorParams &p,
                        ImVec2 vpMin, ImVec2 vpSize, bool lmbClicked) {
    if (!m_selectedObject || (p.cameraObj && m_selectedObject == p.cameraObj)) return;

    const glm::vec3 origin = m_selectedObject->transform.getWorldPosition();
    const float dist = glm::distance(p.camPos, origin);
    const float gLen = dist * 0.15f;

    auto w2s = [&](glm::vec3 wp) -> std::optional<ImVec2> {
        glm::vec4 clip = p.projMat * p.viewMat * glm::vec4(wp, 1.0f);
        if (clip.w <= 0.0001f) return std::nullopt;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return ImVec2{vpMin.x + (ndc.x * 0.5f + 0.5f) * vpSize.x,
                      vpMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * vpSize.y};
    };

    auto ptSegDist = [](ImVec2 pt, ImVec2 a, ImVec2 b) -> float {
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float lenSq = dx * dx + dy * dy;
        if (lenSq < 1e-6f) return std::hypot(pt.x - a.x, pt.y - a.y);
        const float t = std::clamp(((pt.x-a.x)*dx + (pt.y-a.y)*dy) / lenSq, 0.f, 1.f);
        return std::hypot(pt.x - (a.x + t*dx), pt.y - (a.y + t*dy));
    };

    const glm::vec3 axes[4] = {{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
    constexpr ImU32 axisColors[4] = {
        0, IM_COL32(220,50,50,255), IM_COL32(50,220,50,255), IM_COL32(50,100,255,255)
    };
    constexpr ImU32 axisHover[4] = {
        0, IM_COL32(255,120,120,255), IM_COL32(120,255,120,255), IM_COL32(120,160,255,255)
    };

    const ImVec2 mousePos = ImGui::GetMousePos();
    const bool   lmbDown  = ImGui::IsMouseDown(ImGuiMouseButton_Left);

    if (m_gizmoActiveAxis > 0 && !lmbDown)
        m_gizmoActiveAxis = 0;

    m_gizmoHoverAxis = 0;
    if (m_gizmoActiveAxis == 0 && p.input &&
        !p.input->isMouseDown(sonnet::api::input::MouseButton::Right)) {
        if (auto os = w2s(origin)) {
            for (int i = 1; i <= 3; ++i) {
                if (auto ts = w2s(origin + axes[i] * gLen))
                    if (ptSegDist(mousePos, *os, *ts) < 8.0f)
                        m_gizmoHoverAxis = i;
            }
            if (m_gizmoMode == GizmoMode::Rotate && m_gizmoHoverAxis == 0) {
                for (int i = 1; i <= 3; ++i) {
                    if (auto ts = w2s(origin + axes[i] * gLen)) {
                        const float d = std::hypot(mousePos.x - ts->x,
                                                   mousePos.y - ts->y);
                        if (d < 12.0f) m_gizmoHoverAxis = i;
                    }
                }
            }
        }
    }

    if (lmbClicked && m_gizmoHoverAxis > 0) {
        m_gizmoActiveAxis = m_gizmoHoverAxis;
        m_dragStartPos    = m_selectedObject->transform.getWorldPosition();
        m_dragStartRot    = m_selectedObject->transform.getLocalRotation();
        m_dragStartScale  = m_selectedObject->transform.getLocalScale();
        m_dragStartMouse  = mousePos;
        m_dragAccum       = 0.0f;
    }

    const int activeAxis = m_gizmoActiveAxis > 0 ? m_gizmoActiveAxis : m_gizmoHoverAxis;
    if (m_gizmoActiveAxis > 0 && lmbDown) {
        const glm::vec3 axisDir = axes[m_gizmoActiveAxis];
        const auto os = w2s(origin);
        const auto ts = w2s(origin + axisDir * gLen);
        if (os && ts) {
            const ImVec2 screenAxis{ts->x - os->x, ts->y - os->y};
            const float  screenLen = std::hypot(screenAxis.x, screenAxis.y);
            if (screenLen > 0.5f) {
                const ImVec2 screenDir{screenAxis.x / screenLen, screenAxis.y / screenLen};
                const ImVec2 totalDelta{mousePos.x - m_dragStartMouse.x,
                                        mousePos.y - m_dragStartMouse.y};
                const float signed_px =
                    totalDelta.x * screenDir.x + totalDelta.y * screenDir.y;

                if (m_gizmoMode == GizmoMode::Translate) {
                    const float sensitivity = dist * 0.0018f;
                    const glm::vec3 newPos = m_dragStartPos + axisDir * signed_px * sensitivity;
                    m_selectedObject->transform.setWorldPosition(newPos);
                    m_editEuler = glm::degrees(glm::eulerAngles(
                        m_selectedObject->transform.getLocalRotation()));
                } else if (m_gizmoMode == GizmoMode::Rotate) {
                    m_dragAccum = signed_px * 0.5f;
                    const glm::quat delta = glm::angleAxis(
                        glm::radians(m_dragAccum), axisDir);
                    m_selectedObject->transform.setLocalRotation(
                        glm::normalize(delta * m_dragStartRot));
                    m_editEuler = glm::degrees(glm::eulerAngles(
                        m_selectedObject->transform.getLocalRotation()));
                } else {
                    const float factor = 1.0f + signed_px * 0.005f;
                    glm::vec3 newScale = m_dragStartScale;
                    newScale[m_gizmoActiveAxis - 1] *= std::max(0.001f, factor);
                    m_selectedObject->transform.setLocalScale(newScale);
                }
            }
        }
    }

    if (auto os = w2s(origin)) {
        for (int i = 1; i <= 3; ++i) {
            const ImU32 col      = (i == activeAxis) ? axisHover[i] : axisColors[i];
            const auto  ts       = w2s(origin + axes[i] * gLen);
            if (!ts) continue;
            dl->AddLine(*os, *ts, col, 2.5f);

            const ImVec2 shaft{ts->x - os->x, ts->y - os->y};
            const float  shaftLen = std::hypot(shaft.x, shaft.y);
            if (shaftLen < 1.0f) continue;
            const ImVec2 dir{shaft.x / shaftLen, shaft.y / shaftLen};
            const ImVec2 perp{-dir.y, dir.x};
            constexpr float kHeadLen = 10.0f, kHeadW = 5.0f;

            if (m_gizmoMode == GizmoMode::Translate) {
                const ImVec2 base{ts->x - dir.x*kHeadLen, ts->y - dir.y*kHeadLen};
                dl->AddTriangleFilled(*ts,
                    ImVec2{base.x + perp.x*kHeadW, base.y + perp.y*kHeadW},
                    ImVec2{base.x - perp.x*kHeadW, base.y - perp.y*kHeadW}, col);
            } else if (m_gizmoMode == GizmoMode::Scale) {
                constexpr float kSq = 5.0f;
                dl->AddRectFilled(ImVec2{ts->x - kSq, ts->y - kSq},
                                  ImVec2{ts->x + kSq, ts->y + kSq}, col);
            } else {
                dl->AddCircleFilled(*ts, 5.0f, col);
            }
        }
        dl->AddCircleFilled(*os, 4.0f, IM_COL32(220, 220, 220, 200));
    }
}

// ── Scene Hierarchy panel ─────────────────────────────────────────────────────

void EditorUI::drawHierarchyPanel(EditorParams &p) {
    ImGui::Begin("Scene Hierarchy");

    std::unordered_map<const sonnet::world::Transform *,
                       sonnet::world::GameObject *> tfToObj;
    for (auto &obj : m_scene.objects())
        tfToObj[&obj->transform] = obj.get();

    auto destroySubtree = [&](sonnet::world::GameObject *root) {
        std::vector<sonnet::world::GameObject *> subtree;
        std::function<void(sonnet::world::GameObject *)> collect =
            [&](sonnet::world::GameObject *o) {
                subtree.push_back(o);
                for (auto *childTf : o->transform.children()) {
                    auto it = tfToObj.find(childTf);
                    if (it != tfToObj.end()) collect(it->second);
                }
            };
        collect(root);
        for (auto *o : subtree) {
            m_scripts.detachObject(o);
            if (m_selectedObject == o) m_selectedObject = nullptr;
        }
        m_scene.destroyObject(root);
    };

    sonnet::world::GameObject *pendingDuplicate = nullptr;
    sonnet::world::GameObject *pendingDestroy   = nullptr;

    std::function<void(sonnet::world::GameObject &)> drawNode =
        [&](sonnet::world::GameObject &obj) {
            std::vector<sonnet::world::GameObject *> childObjs;
            for (auto *childTf : obj.transform.children()) {
                auto it = tfToObj.find(childTf);
                if (it != tfToObj.end())
                    childObjs.push_back(it->second);
            }
            ImGuiTreeNodeFlags flags =
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (childObjs.empty())
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (&obj == m_selectedObject)
                flags |= ImGuiTreeNodeFlags_Selected;

            ImGui::PushID(&obj);
            const bool opened = ImGui::TreeNodeEx(obj.name.c_str(), flags);
            if (ImGui::IsItemClicked()) {
                if (m_selectedObject != &obj) {
                    m_selectedObject = &obj;
                    m_editEuler = glm::degrees(glm::eulerAngles(
                        obj.transform.getLocalRotation()));
                }
            }
            if (ImGui::BeginPopupContextItem("node_ctx")) {
                if (ImGui::MenuItem("Duplicate")) pendingDuplicate = &obj;
                if (ImGui::MenuItem("Delete"))    pendingDestroy   = &obj;
                ImGui::EndPopup();
            }
            ImGui::PopID();

            if (opened && !childObjs.empty()) {
                for (auto *child : childObjs) drawNode(*child);
                ImGui::TreePop();
            }
        };

    for (auto &obj : m_scene.objects())
        if (obj->transform.getParent() == nullptr)
            drawNode(*obj);

    if (pendingDuplicate) {
        auto &dup = m_scene.duplicateObject(*pendingDuplicate);
        m_selectedObject = &dup;
        m_editEuler = glm::degrees(glm::eulerAngles(dup.transform.getLocalRotation()));
    }
    if (pendingDestroy)
        destroySubtree(pendingDestroy);

    ImGui::Separator();
    if (ImGui::Button("+ Add Empty")) {
        auto &neo = m_scene.createObject("Object");
        neo.transform.setLocalPosition(p.camPos + glm::vec3{0.0f, 0.0f, -2.0f});
        m_selectedObject = &neo;
        m_editEuler = glm::vec3{0.0f};
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Scene")) saveSceneImpl();
    ImGui::SameLine();
    ImGui::TextDisabled("persists transforms");

    ImGui::End();
}

// ── Inspector panel ───────────────────────────────────────────────────────────

void EditorUI::drawInspectorPanel() {
    ImGui::Begin("Inspector");
    if (m_selectedObject) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        ImGui::Text("%s", m_selectedObject->name.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::Checkbox("##enabled", &m_selectedObject->enabled);
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            glm::vec3 pos = m_selectedObject->transform.getLocalPosition();
            if (ImGui::DragFloat3("Position", &pos.x, 0.01f))
                m_selectedObject->transform.setLocalPosition(pos);
            if (ImGui::DragFloat3("Rotation", &m_editEuler.x, 0.5f))
                m_selectedObject->transform.setLocalRotation(
                    glm::quat(glm::radians(m_editEuler)));
            glm::vec3 scl = m_selectedObject->transform.getLocalScale();
            if (ImGui::DragFloat3("Scale", &scl.x, 0.01f, 0.001f, 100.0f))
                m_selectedObject->transform.setLocalScale(scl);
        }

        if (m_selectedObject->render) {
            if (ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto &mat = m_selectedObject->render->material;
                ImGui::TextDisabled("Material: %llu",
                    static_cast<unsigned long long>(mat.templateHandle().value));

                const auto *tmpl = m_renderer.getMaterial(mat.templateHandle());
                if (tmpl) {
                    auto isColor = [](const std::string &n) {
                        for (const char *k : {"Color","color","Colour","colour",
                                               "Albedo","albedo","Emissive","emissive",
                                               "Tint","tint"})
                            if (n.find(k) != std::string::npos) return true;
                        return false;
                    };

                    auto showUniform = [&](const std::string &name,
                                           const sonnet::core::UniformValue &val) {
                        ImGui::PushID(name.c_str());
                        std::visit([&](auto &&v) {
                            using T = std::decay_t<decltype(v)>;
                            if constexpr (std::is_same_v<T, float>) {
                                float fv = v;
                                float hi = (name.find("Strength") != std::string::npos ||
                                            name.find("strength") != std::string::npos) ? 20.0f
                                         : (name.find("Bias") != std::string::npos)     ? 0.05f
                                         : 1.0f;
                                if (ImGui::SliderFloat(name.c_str(), &fv, 0.0f, hi))
                                    mat.set(name, fv);
                            } else if constexpr (std::is_same_v<T, glm::vec3>) {
                                glm::vec3 cv = v;
                                bool changed = isColor(name)
                                    ? ImGui::ColorEdit3(name.c_str(), &cv.x)
                                    : ImGui::DragFloat3(name.c_str(), &cv.x, 0.01f);
                                if (changed) mat.set(name, cv);
                            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                                glm::vec4 cv = v;
                                bool changed = isColor(name)
                                    ? ImGui::ColorEdit4(name.c_str(), &cv.x)
                                    : ImGui::DragFloat4(name.c_str(), &cv.x, 0.01f);
                                if (changed) mat.set(name, cv);
                            }
                        }, val);
                        ImGui::PopID();
                    };

                    for (const auto &[name, defVal] : tmpl->defaultValues) {
                        if (std::holds_alternative<glm::mat4>(defVal)) continue;
                        if (std::holds_alternative<sonnet::core::Sampler>(defVal)) continue;
                        if (name.rfind("uBone", 0) == 0) continue;
                        const auto *inst = mat.tryGet(name);
                        showUniform(name, inst ? *inst : defVal);
                    }
                    for (const auto &[name, instVal] : mat.values()) {
                        if (tmpl->defaultValues.count(name)) continue;
                        if (std::holds_alternative<glm::mat4>(instVal)) continue;
                        if (std::holds_alternative<sonnet::core::Sampler>(instVal)) continue;
                        if (name.rfind("uBone", 0) == 0) continue;
                        showUniform(name, instVal);
                    }
                }
            }
        }

        if (m_selectedObject->light) {
            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto &lc = *m_selectedObject->light;
                ImGui::Checkbox("Enabled", &lc.enabled);
                ImGui::ColorEdit3("Color", &lc.color.x);
                using LT = sonnet::world::LightComponent::Type;
                if (lc.type == LT::Directional) {
                    ImGui::DragFloat3("Direction", &lc.direction.x, 0.01f, -1.0f, 1.0f);
                    ImGui::SliderFloat("Intensity", &lc.intensity, 0.0f, 4.0f);
                } else {
                    ImGui::SliderFloat("Intensity", &lc.intensity, 0.0f, 20.0f);
                }
            }
        }

        if (m_selectedObject->camera) {
            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("FOV",  &m_selectedObject->camera->fov,  10.0f, 120.0f);
                ImGui::SliderFloat("Near", &m_selectedObject->camera->near,  0.01f,   5.0f);
                ImGui::SliderFloat("Far",  &m_selectedObject->camera->far,  10.0f, 500.0f);
                const glm::vec3 wp = m_selectedObject->transform.getWorldPosition();
                ImGui::TextDisabled("World pos %.2f  %.2f  %.2f", wp.x, wp.y, wp.z);
            }
        }

        if (m_selectedObject->animationPlayer) {
            auto &ap = *m_selectedObject->animationPlayer;
            if (!ap.clips.empty() &&
                ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PushID("anim");
                if (ap.clips.size() > 1) {
                    if (ImGui::BeginCombo("Clip", ap.clips[ap.currentClip].name.c_str())) {
                        for (int c = 0; c < static_cast<int>(ap.clips.size()); ++c) {
                            const bool sel = (c == ap.currentClip);
                            if (ImGui::Selectable(ap.clips[c].name.c_str(), sel)) {
                                ap.currentClip = c;
                                ap.time = 0.0f;
                                ap.playing = true;
                            }
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    ImGui::TextDisabled("Clip: %s", ap.clips[0].name.c_str());
                }
                if (ImGui::Button(ap.playing ? "Pause" : "Play")) ap.playing = !ap.playing;
                ImGui::SameLine();
                if (ImGui::Button("Restart")) { ap.time = 0.0f; ap.playing = true; }
                ImGui::SameLine();
                ImGui::Checkbox("Loop", &ap.loop);
                const float dur = ap.clips[ap.currentClip].duration;
                ImGui::SliderFloat("Time", &ap.time, 0.0f, dur > 0.0f ? dur : 1.0f, "%.2f s");
                ImGui::PopID();
            }
        }
    } else {
        ImGui::TextDisabled("Select an object in the Scene Hierarchy");
    }
    ImGui::End();
}

// ── Assets panel ──────────────────────────────────────────────────────────────

void EditorUI::drawAssetsPanel() {
    ImGui::Begin("Assets");
    if (ImGui::BeginTabBar("AssetTabs")) {
        auto listAssets = [](const char *label,
                              const std::vector<std::string> &names,
                              const char *icon) {
            if (ImGui::BeginTabItem(label)) {
                ImGui::BeginChild("scroll");
                for (const auto &n : names)
                    ImGui::TextUnformatted((std::string(icon) + "  " + n).c_str());
                if (names.empty()) ImGui::TextDisabled("(none)");
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        };
        listAssets("Meshes",    m_assetMeshNames,     "[M]");
        listAssets("Materials", m_assetMaterialNames, "[MAT]");
        listAssets("Textures",  m_assetTextureNames,  "[TEX]");
        listAssets("Shaders",   m_assetShaderNames,   "[SHD]");
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ── Render Settings panel ─────────────────────────────────────────────────────

void EditorUI::drawRenderSettingsPanel(EditorParams &p) {
    ImGui::Begin("Render Settings");

    if (ImGui::CollapsingHeader("Directional Light")) {
        using LT = sonnet::world::LightComponent::Type;
        for (const auto &obj : m_scene.objects()) {
            if (!obj->light || obj->light->type != LT::Directional) continue;
            auto &lc = *obj->light;
            ImGui::DragFloat3("Direction",       &lc.direction.x, 0.01f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Color##sun",      &lc.color.x);
            ImGui::SliderFloat("Intensity##sun", &lc.intensity, 0.0f, 4.0f);
            break;
        }
    }

    if (ImGui::CollapsingHeader("Point Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
        using LT = sonnet::world::LightComponent::Type;
        int plCount = 0;
        for (const auto &obj : m_scene.objects()) {
            if (!obj->light || obj->light->type != LT::Point) continue;
            auto &lc = *obj->light;
            ImGui::PushID(obj->name.c_str());
            if (ImGui::CollapsingHeader(obj->name.c_str())) {
                ImGui::Checkbox("Enabled",           &lc.enabled);
                ImGui::ColorEdit3("Color##pl",        &lc.color.x);
                ImGui::SliderFloat("Intensity##pl",   &lc.intensity, 0.0f, 20.0f);
                const glm::vec3 wp = obj->transform.getWorldPosition();
                ImGui::TextDisabled("Pos  %.2f  %.2f  %.2f", wp.x, wp.y, wp.z);
            }
            ImGui::PopID();
            ++plCount;
        }
        if (plCount == 0) ImGui::TextDisabled("No point lights in scene");
    }

    if (ImGui::CollapsingHeader("Shadows")) {
        if (p.shadowBias)       ImGui::SliderFloat("Dir bias",   p.shadowBias,      0.0001f, 0.05f, "%.4f");
        if (p.pointShadowBias)  ImGui::SliderFloat("Point bias", p.pointShadowBias, 0.001f,  0.05f, "%.4f");
        ImGui::TextDisabled("Shadow lights: %d / %d",
                            p.shadowLightCount, ShadowMaps::MAX_SHADOW_LIGHTS);
        if (p.csmSplitDepths) {
            for (int c = 0; c < ShadowMaps::NUM_CASCADES; ++c)
                ImGui::TextDisabled("  Cascade %d split: %.2f m", c, (*p.csmSplitDepths)[c]);
        }
    }

    if (ImGui::CollapsingHeader("Tone-mapping"))
        if (p.exposure) ImGui::SliderFloat("Exposure", p.exposure, 0.1f, 5.0f);

    if (ImGui::CollapsingHeader("Bloom")) {
        if (p.bloomThreshold)  ImGui::SliderFloat("Threshold##bloom",  p.bloomThreshold,  0.5f, 3.0f);
        if (p.bloomIntensity)  ImGui::SliderFloat("Intensity##bloom",  p.bloomIntensity,  0.0f, 3.0f);
        if (p.bloomIterations) ImGui::SliderInt  ("Iterations##bloom", p.bloomIterations, 1,    8);
    }

    if (ImGui::CollapsingHeader("SSAO")) {
        if (p.ssaoEnabled) ImGui::Checkbox   ("Enable##ssao", p.ssaoEnabled);
        if (p.ssaoShow)    ImGui::Checkbox   ("Visualize AO", p.ssaoShow);
        if (p.ssaoRadius)  ImGui::SliderFloat("Radius##ssao", p.ssaoRadius, 0.1f, 3.0f);
        if (p.ssaoBias)    ImGui::SliderFloat("Bias##ssao",   p.ssaoBias,   0.01f, 0.2f, "%.3f");
    }

    if (ImGui::CollapsingHeader("SSR")) {
        if (p.ssrEnabled)      ImGui::Checkbox   ("Enable##ssr",        p.ssrEnabled);
        if (p.ssrStrength)     ImGui::SliderFloat("Strength##ssr",      p.ssrStrength,     0.0f, 2.0f);
        if (p.ssrMaxSteps)     ImGui::SliderInt  ("Max Steps##ssr",     p.ssrMaxSteps,     8, 128);
        if (p.ssrStepSize)     ImGui::SliderFloat("Step Size##ssr",     p.ssrStepSize,     0.01f, 0.5f);
        if (p.ssrThickness)    ImGui::SliderFloat("Thickness##ssr",     p.ssrThickness,    0.01f, 1.0f);
        if (p.ssrMaxDistance)  ImGui::SliderFloat("Max Distance##ssr",  p.ssrMaxDistance,  1.0f, 30.0f);
        if (p.ssrRoughnessMax) ImGui::SliderFloat("Roughness Max##ssr", p.ssrRoughnessMax, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Anti-aliasing"))
        if (p.fxaaEnabled) ImGui::Checkbox("FXAA", p.fxaaEnabled);

    if (ImGui::CollapsingHeader("Selection Outline")) {
        if (p.outlineEnabled) ImGui::Checkbox   ("Enable##outline", p.outlineEnabled);
        if (p.outlineColor)   ImGui::ColorEdit3 ("Color##outline",  &p.outlineColor->x);
    }

    if (ImGui::CollapsingHeader("Scene"))
        if (p.rotationSpeed) ImGui::SliderFloat("Rotation speed", p.rotationSpeed, 0.0f, 360.0f);

    ImGui::End();
}

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <sonnet/scripting/LuaScriptRuntime.h>

#include <sonnet/api/input/Key.h>
#include <sonnet/api/input/MouseButton.h>
#include <sonnet/input/InputSystem.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/GameObject.h>
#include <sonnet/world/Transform.h>

#include <glm/gtc/quaternion.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace sonnet::scripting {

using namespace sonnet::world;
using namespace sonnet::input;
using namespace sonnet::api::input;

// ── Key name → enum mapping ───────────────────────────────────────────────────

static Key strToKey(const std::string &name) {
    static const std::unordered_map<std::string, Key> kMap = {
        // Letters
        {"A",Key::A},{"B",Key::B},{"C",Key::C},{"D",Key::D},{"E",Key::E},
        {"F",Key::F},{"G",Key::G},{"H",Key::H},{"I",Key::I},{"J",Key::J},
        {"K",Key::K},{"L",Key::L},{"M",Key::M},{"N",Key::N},{"O",Key::O},
        {"P",Key::P},{"Q",Key::Q},{"R",Key::R},{"S",Key::S},{"T",Key::T},
        {"U",Key::U},{"V",Key::V},{"W",Key::W},{"X",Key::X},{"Y",Key::Y},
        {"Z",Key::Z},
        // Digits
        {"0",Key::Zero},{"1",Key::One},{"2",Key::Two},{"3",Key::Three},
        {"4",Key::Four},{"5",Key::Five},{"6",Key::Six},{"7",Key::Seven},
        {"8",Key::Eight},{"9",Key::Nine},
        // Special
        {"Space",Key::Space},{"Enter",Key::Enter},{"Escape",Key::Escape},
        {"Tab",Key::Tab},{"Backspace",Key::Backspace},
        // Arrows
        {"Up",Key::Up},{"Down",Key::Down},{"Left",Key::Left},{"Right",Key::Right},
        // Modifiers
        {"LeftShift",Key::LeftShift},{"RightShift",Key::RightShift},
        {"LeftControl",Key::LeftControl},{"RightControl",Key::RightControl},
        {"LeftAlt",Key::LeftAlt},{"RightAlt",Key::RightAlt},
        // Function keys
        {"F1",Key::F1},{"F2",Key::F2},{"F3",Key::F3},{"F4",Key::F4},
        {"F5",Key::F5},{"F6",Key::F6},{"F7",Key::F7},{"F8",Key::F8},
        {"F9",Key::F9},{"F10",Key::F10},{"F11",Key::F11},{"F12",Key::F12},
    };
    auto it = kMap.find(name);
    return it != kMap.end() ? it->second : Key::Unknown;
}

// ── Pimpl ─────────────────────────────────────────────────────────────────────

struct LuaScriptRuntime::Impl {
    sol::state                        lua;
    Scene                            *scene     = nullptr;
    const InputSystem                *input     = nullptr;
    bool                              initialised = false;

    struct Instance {
        GameObject              *obj;
        sol::table               self;
        sol::protected_function  onUpdate;
        std::string              scriptPath;
        fs::file_time_type       mtime;
    };
    std::vector<Instance> instances;

    void bindEngineAPI() {
        lua.new_usertype<Transform>("Transform",
            "getLocalPosition", [](Transform &t) {
                auto p = t.getLocalPosition();
                return std::make_tuple(p.x, p.y, p.z);
            },
            "setLocalPosition", [](Transform &t, float x, float y, float z) {
                t.setLocalPosition({x, y, z});
            },
            "getWorldPosition", [](Transform &t) {
                auto p = t.getWorldPosition();
                return std::make_tuple(p.x, p.y, p.z);
            },
            "setWorldPosition", [](Transform &t, float x, float y, float z) {
                t.setWorldPosition({x, y, z});
            },
            "getLocalScale", [](Transform &t) {
                auto s = t.getLocalScale();
                return std::make_tuple(s.x, s.y, s.z);
            },
            "setLocalScale", [](Transform &t, float x, float y, float z) {
                t.setLocalScale({x, y, z});
            },
            "getLocalEulerDegrees", [](Transform &t) {
                auto e = glm::degrees(glm::eulerAngles(t.getLocalRotation()));
                return std::make_tuple(e.x, e.y, e.z);
            },
            "setLocalEulerDegrees", [](Transform &t, float x, float y, float z) {
                t.setLocalRotation(glm::quat(glm::radians(glm::vec3{x, y, z})));
            },
            "rotate", [](Transform &t, float ax, float ay, float az, float deg) {
                t.rotate(glm::vec3{ax, ay, az}, deg);
            },
            "translate", [](Transform &t, float x, float y, float z) {
                t.translate({x, y, z});
            },
            "forward", [](Transform &t) {
                auto f = t.forward();
                return std::make_tuple(f.x, f.y, f.z);
            },
            "right", [](Transform &t) {
                auto r = t.right();
                return std::make_tuple(r.x, r.y, r.z);
            },
            "up", [](Transform &t) {
                auto u = t.up();
                return std::make_tuple(u.x, u.y, u.z);
            }
        );

        auto inp = lua.create_named_table("Input");
        inp.set_function("isKeyDown", [this](const std::string &k) {
            return input->isKeyDown(strToKey(k));
        });
        inp.set_function("isKeyJustPressed", [this](const std::string &k) {
            return input->isKeyJustPressed(strToKey(k));
        });
        inp.set_function("isKeyJustReleased", [this](const std::string &k) {
            return input->isKeyJustReleased(strToKey(k));
        });
        inp.set_function("isMouseDown", [this](int btn) {
            return input->isMouseDown(static_cast<MouseButton>(btn));
        });
        inp.set_function("mouseDelta", [this]() {
            auto d = input->mouseDelta();
            return std::make_tuple(d.x, d.y);
        });

        auto scn = lua.create_named_table("Scene");
        scn.set_function("find", [this](const std::string &name) -> Transform * {
            for (auto &o : scene->objects())
                if (o->name == name) return &o->transform;
            return nullptr;
        });

        auto log = lua.create_named_table("Log");
        log.set_function("info",  [](const std::string &s) {
            std::cout << "[Lua] " << s << "\n";
        });
        log.set_function("warn",  [](const std::string &s) {
            std::cerr << "[Lua WARN] " << s << "\n";
        });
        log.set_function("error", [](const std::string &s) {
            std::cerr << "[Lua ERROR] " << s << "\n";
        });
    }
};

// ── LuaScriptRuntime ──────────────────────────────────────────────────────────

LuaScriptRuntime::LuaScriptRuntime() : m_impl(std::make_unique<Impl>()) {}
LuaScriptRuntime::~LuaScriptRuntime() = default;

void LuaScriptRuntime::init(Scene &scene, const InputSystem &input) {
    m_impl->scene = &scene;
    m_impl->input = &input;

    m_impl->lua.open_libraries(sol::lib::base, sol::lib::math,
                                sol::lib::string, sol::lib::table);
    m_impl->bindEngineAPI();
    m_impl->initialised = true;

    for (auto &inst : m_impl->instances) {
        sol::protected_function onStart = inst.self[sol::metatable_key]["onStart"];
        if (!onStart.valid()) continue;
        auto r = onStart(inst.self);
        if (!r.valid()) {
            sol::error e = r;
            std::cerr << "[Lua onStart error] " << e.what() << "\n";
        }
    }
}

void LuaScriptRuntime::attachScript(GameObject &obj, const std::string &scriptPath) {
    auto loadResult = m_impl->lua.safe_script_file(scriptPath, sol::script_pass_on_error);
    if (!loadResult.valid()) {
        sol::error e = loadResult;
        std::cerr << "[Lua load error] " << scriptPath << ": " << e.what() << "\n";
        return;
    }

    sol::table cls = loadResult;
    cls["__index"] = cls;

    sol::table inst = m_impl->lua.create_table();
    inst[sol::metatable_key] = cls;
    inst["transform"] = &obj.transform;
    inst["name"]      = obj.name;

    sol::protected_function onUpdate{cls["onUpdate"]};
    fs::file_time_type mtime{};
    try { mtime = fs::last_write_time(scriptPath); } catch (...) {}
    m_impl->instances.push_back({&obj, inst, onUpdate, scriptPath, mtime});

    if (m_impl->initialised) {
        sol::protected_function onStart = cls["onStart"];
        if (onStart.valid()) {
            auto r = onStart(inst);
            if (!r.valid()) {
                sol::error e = r;
                std::cerr << "[Lua onStart error] " << scriptPath << ": " << e.what() << "\n";
            }
        }
    }
}

void LuaScriptRuntime::update(float dt) {
    for (auto &inst : m_impl->instances) {
        if (!inst.onUpdate.valid()) continue;
        auto r = inst.onUpdate(inst.self, dt);
        if (!r.valid()) {
            sol::error e = r;
            std::cerr << "[Lua onUpdate error] " << inst.obj->name << ": " << e.what() << "\n";
        }
    }
}

std::string LuaScriptRuntime::reload() {
    std::string result;
    for (auto &inst : m_impl->instances) {
        fs::file_time_type mtime{};
        try { mtime = fs::last_write_time(inst.scriptPath); }
        catch (...) { continue; }
        if (mtime == inst.mtime) continue;
        inst.mtime = mtime;

        const std::string name = fs::path(inst.scriptPath).filename().string();
        auto loadResult = m_impl->lua.safe_script_file(inst.scriptPath, sol::script_pass_on_error);
        if (!loadResult.valid()) {
            sol::error e = loadResult;
            result = "Error (" + name + "): " + e.what();
            continue;
        }

        sol::table cls = loadResult;
        cls["__index"] = cls;

        inst.self = m_impl->lua.create_table();
        inst.self[sol::metatable_key] = cls;
        inst.self["transform"] = &inst.obj->transform;
        inst.self["name"]      = inst.obj->name;
        inst.onUpdate          = sol::protected_function{cls["onUpdate"]};

        sol::protected_function onStart = cls["onStart"];
        if (onStart.valid()) {
            auto r = onStart(inst.self);
            if (!r.valid()) {
                sol::error e = r;
                result = "Error (" + name + "): " + e.what();
                continue;
            }
        }
        result = "Reloaded: " + name;
    }
    return result;
}

void LuaScriptRuntime::shutdown() {
    m_impl->instances.clear();
}

} // namespace sonnet::scripting

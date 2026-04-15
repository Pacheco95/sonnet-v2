#pragma once

#include <sonnet/scripting/IScriptRuntime.h>
#include <memory>
#include <string>

namespace sonnet::scripting {

// LuaScriptRuntime implements IScriptRuntime via sol2 + Lua 5.4.
// All sol2 types are hidden behind a pimpl so users of this header
// do not need sol2 on their include path.
class LuaScriptRuntime final : public IScriptRuntime {
public:
    LuaScriptRuntime();
    ~LuaScriptRuntime() override;

    void init(sonnet::world::Scene          &scene,
              const sonnet::input::InputSystem &input) override;
    void attachScript(sonnet::world::GameObject &obj,
                       const std::string         &scriptPath) override;
    void        update(float dt)                          override;
    void        detachObject(world::GameObject *obj)      override;
    std::string reload()                                  override;
    void        shutdown()                                override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sonnet::scripting

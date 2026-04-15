-- rotate.lua
-- Rotates the attached object around its local Y axis.
-- Attach to any object via "script": "scripts/rotate.lua" in scene.json.

local Rotate = {}
Rotate.__index = Rotate

function Rotate:onStart()
    self.speed = 45.0   -- degrees per second; override per-instance if needed
    Log.info("Rotate script started on: " .. self.name)
end

function Rotate:onUpdate(dt)
    self.transform:rotate(0, 1, 0, self.speed * dt)
end

return Rotate

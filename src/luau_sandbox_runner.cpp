#include "luau_sandbox_runner.h"

#include "luau_package_runtime.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <algorithm>
#include <cstdint>

namespace rai::luau {

void LuauSandboxRunner::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("run", "source", "input", "limits"),
        &LuauSandboxRunner::run,
        DEFVAL(godot::Dictionary())
    );
}

godot::Dictionary LuauSandboxRunner::run(const godot::String &source, const godot::Variant &input, const godot::Dictionary &supplied_limits) {
    godot::Dictionary limits;
    limits["maxSourceBytes"] = 256 * 1024;
    limits["maxModuleCount"] = 2;
    limits["maxPayloadBytes"] = 1024 * 1024;
    limits["maxDataDepth"] = 24;
    limits["maxDataItems"] = 25000;
    limits["maxMemoryBytes"] = 8 * 1024 * 1024;
    limits["timeoutUsec"] = 20000;
    for (const godot::Variant &key : supplied_limits.keys()) {
        if (limits.has(key)) {
            const int64_t ceiling = static_cast<int64_t>(limits[key]);
            const int64_t requested = static_cast<int64_t>(supplied_limits[key]);
            limits[key] = std::clamp<int64_t>(requested, 0, ceiling);
        }
    }

    const godot::String wrapper = R"LUAU(--!strict
local User = require_module("sandbox.user")
return {
    activate = function(context)
        return {input = context.input}
    end,
    handle_event = function(_session, event)
        if type(User) == "function" then
            return User(event.input)
        end
        if type(User) == "table" and type(User.run) == "function" then
            return User.run(event.input)
        end
        error("sandbox module must return a function or a table with run(input)")
    end,
    snapshot = function(session)
        return session
    end,
    deactivate = function(_session)
        return true
    end,
}
)LUAU";

    godot::Ref<LuauPackageRuntime> runtime;
    runtime.instantiate();
    godot::Dictionary configured = runtime->configure(limits);
    if (!static_cast<bool>(configured.get("ok", false))) {
        return configured;
    }
    godot::Dictionary modules;
    modules["sandbox.user"] = source;
    modules["sandbox.entrypoint"] = wrapper;
    godot::Dictionary loaded = runtime->load_modules(modules, "sandbox.entrypoint");
    if (!static_cast<bool>(loaded.get("ok", false))) {
        runtime->close();
        return loaded;
    }
    godot::Dictionary context;
    context["input"] = input;
    godot::Dictionary activated = runtime->activate(context);
    if (!static_cast<bool>(activated.get("ok", false))) {
        runtime->close();
        return activated;
    }
    godot::Dictionary event;
    event["input"] = input;
    godot::Dictionary result = runtime->handle_event(event);
    runtime->deactivate();
    const godot::Dictionary runtime_metrics = runtime->metrics();
    runtime->close();
    result["sandbox"] = true;
    result["metrics"] = runtime_metrics;
    return result;
}

} // namespace rai::luau

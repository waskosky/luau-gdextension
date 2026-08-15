extends SceneTree


func _initialize() -> void:
	assert(ClassDB.class_exists("LuauPackageRuntime"))
	var runtime = ClassDB.instantiate("LuauPackageRuntime")
	assert(runtime != null)
	var configured: Dictionary = runtime.call("configure", {
		"maxSourceBytes": 65536,
		"maxModuleCount": 4,
		"maxPayloadBytes": 65536,
		"maxMemoryBytes": 4 * 1024 * 1024,
		"timeoutUsec": 20000,
	})
	assert(bool(configured.get("ok", false)))
	var modules := {
		"smoke.entry": """
return {
    activate = function(context) return {count = context.start or 0} end,
    handle_event = function(session, event)
        if event.invalid_utf8_key then
            return {[string.char(255)] = true}
        end
        session.count += event.amount or 0
        return {count = session.count, commands = {}}
    end,
    snapshot = function(session) return {count = session.count} end,
    deactivate = function(_session) return true end,
}
""",
	}
	var loaded: Dictionary = runtime.call("load_modules", modules, "smoke.entry")
	assert(bool(loaded.get("ok", false)), str(loaded))
	assert(bool((runtime.call("activate", {"start": 2}) as Dictionary).get("ok", false)))
	var rejected: Dictionary = runtime.call("handle_event", {"unsupported": self})
	assert(not bool(rejected.get("ok", true)))
	var invalid_utf8: Dictionary = runtime.call("handle_event", {"invalid_utf8_key": true})
	assert(not bool(invalid_utf8.get("ok", true)))
	var result: Dictionary = runtime.call("handle_event", {"amount": 3})
	assert(bool(result.get("ok", false)))
	assert(int(result.get("count", -1)) == 5)
	var snapshot: Dictionary = runtime.call("snapshot")
	assert(bool(snapshot.get("ok", false)))
	assert(int((snapshot.get("value", {}) as Dictionary).get("count", -1)) == 5)
	runtime.call("deactivate")
	runtime.call("close")

	var sandbox = ClassDB.instantiate("LuauSandboxRunner")
	assert(sandbox != null)
	var timed_out: Dictionary = sandbox.call(
		"run",
		"return function(_input) while true do end end",
		{},
		{"timeoutUsec": 10 * 1000 * 1000, "maxMemoryBytes": 512 * 1024 * 1024},
	)
	assert(not bool(timed_out.get("ok", true)))
	assert(bool(timed_out.get("fatal", false)))
	var metrics: Dictionary = timed_out.get("metrics", {})
	assert(int(metrics.get("maximumCallUsec", 0)) < 1000 * 1000)
	quit(0)

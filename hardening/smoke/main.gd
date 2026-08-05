extends Node

const BatchedRuntime := preload("res://addons/luau_gdextension/runtime/batched_luau_runtime.gd")
const SandboxedRunner := preload("res://addons/luau_gdextension/runtime/sandboxed_luau_runner.gd")

var failures := 0


func _ready() -> void:
    _test_trusted_batch()
    _test_sandbox_success()
    _test_restricted_library()
    _test_non_string_error()
    _test_output_limit()
    _test_timeout()
    _test_memory_limit()

    if failures == 0:
        print("HARDENING_SMOKE_OK")
    else:
        push_error("HARDENING_SMOKE_FAILED: %d failure(s)" % failures)
    get_tree().quit(failures)


func _test_trusted_batch() -> void:
    var runtime = BatchedRuntime.new()
    var loaded: Dictionary = runtime.load_source("""
function game_step(payload)
    return {
        health = payload.health - payload.damage,
        alive = payload.health > payload.damage,
    }
end
""")
    _expect(loaded.ok, "trusted source loads: %s" % loaded.get("error", ""))
    if loaded.ok:
        var result: Dictionary = runtime.call_batch("game_step", {"health": 100, "damage": 12})
        _expect(result.ok, "trusted batch call succeeds: %s" % result.get("error", ""))
        if result.ok:
            _expect(result.value.health == 88, "trusted batch result is correct")
    runtime.close()


func _test_sandbox_success() -> void:
    var runner = SandboxedRunner.new()
    var result: Dictionary = runner.run_source("return input.value * 2", {"value": 21})
    _expect(result.ok, "sandboxed calculation succeeds: %s" % result.get("error", ""))
    if result.ok:
        _expect(result.value == 42.0, "sandboxed result is correct")


func _test_restricted_library() -> void:
    var runner = SandboxedRunner.new()
    var result: Dictionary = runner.run_source("return os.clock()")
    _expect(not result.ok, "OS library is unavailable")


func _test_non_string_error() -> void:
    var runner = SandboxedRunner.new()
    var result: Dictionary = runner.run_source("error({ reason = 'boom' })")
    _expect(not result.ok and result.error.contains("non-string"), "hostile non-string errors stay bounded")


func _test_output_limit() -> void:
    var runner = SandboxedRunner.new()
    runner.max_return_bytes = 1024
    var result: Dictionary = runner.run_source("return string.rep('x', 2048)")
    _expect(not result.ok and result.error.contains("output limit"), "oversized return values are rejected")


func _test_timeout() -> void:
    var runner = SandboxedRunner.new()
    runner.timeout_usec = 10_000
    runner.interrupt_poll_stride = 1
    var result: Dictionary = runner.run_source("while true do end")
    _expect(not result.ok and result.timed_out, "runaway loop is interrupted")


func _test_memory_limit() -> void:
    var runner = SandboxedRunner.new()
    runner.max_memory_bytes = 2 * 1024 * 1024
    runner.timeout_usec = 1_000_000
    runner.interrupt_poll_stride = 1
    var result: Dictionary = runner.run_source("""
local values = {}
while true do
    values[#values + 1] = string.rep("x", 4096)
end
""")
    _expect(not result.ok and result.memory_limited, "allocation flood reaches the memory ceiling")


func _expect(condition: bool, message: String) -> void:
    if condition:
        print("PASS: ", message)
    else:
        failures += 1
        push_error("FAIL: " + message)

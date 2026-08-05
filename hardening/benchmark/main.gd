extends Node

const BatchedRuntime := preload("res://addons/luau_gdextension/runtime/batched_luau_runtime.gd")
const ITERATIONS := 10_000_000


func _ready() -> void:
    var runtime = BatchedRuntime.new()
    var loaded: Dictionary = runtime.load_source("""
function benchmark_loop(count)
    local value = 0
    for i = 1, count do
        value += 1
    end
    return value
end
""", "benchmark")
    if not loaded.ok:
        push_error(loaded.error)
        get_tree().quit(1)
        return

    _gdscript_loop(1000)
    runtime.call_global("benchmark_loop", [1000])

    var gd_start := Time.get_ticks_usec()
    var gd_value := _gdscript_loop(ITERATIONS)
    var gd_usec := Time.get_ticks_usec() - gd_start

    var luau_start := Time.get_ticks_usec()
    var luau_result: Dictionary = runtime.call_global("benchmark_loop", [ITERATIONS])
    var luau_usec := Time.get_ticks_usec() - luau_start

    if not luau_result.ok:
        push_error(luau_result.error)
        runtime.close()
        get_tree().quit(1)
        return

    print("GDScript result=%d elapsed_usec=%d" % [gd_value, gd_usec])
    print("Luau result=%d elapsed_usec=%d" % [int(luau_result.value), luau_usec])
    print("Luau/GDScript elapsed ratio=%.4f" % (float(luau_usec) / maxf(float(gd_usec), 1.0)))
    print("This measures one coarse call. Benchmark engine-bound bridge traffic separately.")

    runtime.close()
    get_tree().quit(0)


func _gdscript_loop(count: int) -> int:
    var value := 0
    for _index in count:
        value += 1
    return value

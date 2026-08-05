class_name SandboxedLuauRunner
extends RefCounted

## Fresh-VM runner for untrusted or user-authored source.
## It exposes only data through the global `input` value and permits only
## primitive, Vector3, or buffer return values.

const SAFE_LIBRARIES: int = (
    LuaState.LIB_BASE
    | LuaState.LIB_COROUTINE
    | LuaState.LIB_TABLE
    | LuaState.LIB_STRING
    | LuaState.LIB_BIT32
    | LuaState.LIB_BUFFER
    | LuaState.LIB_UTF8
    | LuaState.LIB_MATH
    | LuaState.LIB_VECTOR
)

var max_source_bytes := 256 * 1024
var max_bytecode_bytes := 1024 * 1024
var max_memory_bytes := 32 * 1024 * 1024
var timeout_usec := 50_000
var interrupt_poll_stride := 64
var max_return_bytes := 1024 * 1024
var max_input_depth := 8
var max_input_items := 20_000
var max_input_bytes := 4 * 1024 * 1024


func run_source(source: String, input: Variant = null, chunk_name: String = "sandbox") -> Dictionary:
    var configuration_error := _validate_configuration()
    if not configuration_error.is_empty():
        return _failure(configuration_error)

    var source_bytes := source.to_utf8_buffer().size()
    if source_bytes > max_source_bytes:
        return _failure("Source exceeds %d bytes." % max_source_bytes)

    var budget := {"items": 0, "bytes": 0}
    var input_error := _validate_input(input, 0, budget)
    if not input_error.is_empty():
        return _failure(input_error)

    var options := LuaCompileOptions.new()
    options.set_optimization_level(2)
    options.set_debug_level(0)
    options.set_type_info_level(0)
    options.set_coverage_level(0)

    var bytecode := Luau.compile(source, options)
    if bytecode.is_empty():
        return _failure("Luau compiler returned empty bytecode.")
    if bytecode.size() > max_bytecode_bytes:
        return _failure("Bytecode exceeds %d bytes." % max_bytecode_bytes)

    var state := LuaState.new()
    if not state.is_valid():
        return _failure("Could not create Luau VM.")

    # Host-side setup is performed before the artificial VM ceiling is enabled.
    # Source, bytecode, and input are already bounded, and this avoids a Luau OOM
    # long-jump occurring through a C++ host conversion frame.
    state.open_libs(SAFE_LIBRARIES)

    # Populate allowed data and remove output-spam helpers before freezing globals.
    state.push_variant(input)
    state.raw_set_field(Luau.LUA_GLOBALSINDEX, "input")
    state.push_nil()
    state.raw_set_field(Luau.LUA_GLOBALSINDEX, "print")
    state.push_nil()
    state.raw_set_field(Luau.LUA_GLOBALSINDEX, "warn")

    if not state.load_bytecode(bytecode, chunk_name):
        var load_error := _take_stack_error(state, "Failed to load Luau bytecode")
        var load_metrics := _metrics(state)
        state.close()
        return _failure_with_metrics(load_error, load_metrics, Luau.LUA_ERRSYNTAX)

    state.sandbox()

    var initialized_bytes := state.get_memory_bytes()
    if initialized_bytes > max_memory_bytes:
        var early_metrics := _metrics(state)
        state.close()
        return _failure_with_metrics(
            "Sandbox initialization requires %d bytes, above the %d-byte VM limit." % [initialized_bytes, max_memory_bytes],
            early_metrics,
            Luau.LUA_ERRMEM
        )

    state.set_memory_limit_bytes(max_memory_bytes)
    state.start_timeout_usec(timeout_usec, interrupt_poll_stride)
    var status := state.pcall(0, 1)
    state.clear_timeout()

    var metrics := _metrics(state)
    var timed_out: bool = state.did_timeout()
    var memory_limited: bool = state.did_hit_memory_limit()

    if status != Luau.LUA_OK:
        var runtime_error := _take_stack_error(state, "Sandboxed Luau execution failed")
        state.close()
        return {
            "ok": false,
            "value": null,
            "error": runtime_error,
            "status": status,
            "timed_out": timed_out,
            "memory_limited": memory_limited,
            "source_bytes": source_bytes,
            "bytecode_bytes": bytecode.size(),
            "input_items": budget.items,
            "input_bytes": budget.bytes,
            "memory_bytes": metrics.memory_bytes,
            "peak_memory_bytes": metrics.peak_memory_bytes,
        }

    var extracted := _extract_safe_return(state)
    state.set_top(0)
    state.close()

    if not extracted.ok:
        return {
            "ok": false,
            "value": null,
            "error": extracted.error,
            "status": Luau.LUA_ERRRUN,
            "timed_out": false,
            "memory_limited": memory_limited,
            "source_bytes": source_bytes,
            "bytecode_bytes": bytecode.size(),
            "input_items": budget.items,
            "input_bytes": budget.bytes,
            "memory_bytes": metrics.memory_bytes,
            "peak_memory_bytes": metrics.peak_memory_bytes,
        }

    return {
        "ok": true,
        "value": extracted.value,
        "error": "",
        "status": status,
        "timed_out": false,
        "memory_limited": memory_limited,
        "source_bytes": source_bytes,
        "bytecode_bytes": bytecode.size(),
        "input_items": budget.items,
        "input_bytes": budget.bytes,
        "memory_bytes": metrics.memory_bytes,
        "peak_memory_bytes": metrics.peak_memory_bytes,
    }


func _validate_configuration() -> String:
    if max_source_bytes <= 0 or max_bytecode_bytes <= 0:
        return "Source and bytecode limits must be positive."
    if max_memory_bytes <= 0:
        return "The sandbox memory limit must be positive."
    if timeout_usec <= 0 or interrupt_poll_stride <= 0:
        return "The sandbox deadline and interrupt poll stride must be positive."
    if max_return_bytes <= 0 or max_input_depth < 0 or max_input_items <= 0 or max_input_bytes <= 0:
        return "Sandbox input and output limits are invalid."
    return ""


func _extract_safe_return(state: LuaState) -> Dictionary:
    if state.get_top() <= 0:
        return {"ok": true, "value": null, "error": ""}

    var value_type := state.type(-1)
    match value_type:
        Luau.LUA_TNIL:
            return {"ok": true, "value": null, "error": ""}
        Luau.LUA_TBOOLEAN:
            return {"ok": true, "value": state.to_boolean(-1), "error": ""}
        Luau.LUA_TNUMBER:
            return {"ok": true, "value": state.to_number(-1), "error": ""}
        Luau.LUA_TSTRING:
            if state.obj_len(-1) > max_return_bytes:
                return {"ok": false, "value": null, "error": "Returned string exceeds the output limit."}
            var text := state.to_string_inplace(-1)
            return {"ok": true, "value": text, "error": ""}
        Luau.LUA_TVECTOR:
            return {"ok": true, "value": state.to_vector3(-1), "error": ""}
        Luau.LUA_TBUFFER:
            if state.obj_len(-1) > max_return_bytes:
                return {"ok": false, "value": null, "error": "Returned buffer exceeds the output limit."}
            var bytes := state.to_buffer(-1)
            return {"ok": true, "value": bytes, "error": ""}
        _:
            return {
                "ok": false,
                "value": null,
                "error": "Sandbox may return only nil, boolean, number, string, Vector3, or buffer values.",
            }


func _validate_input(value: Variant, depth: int, budget: Dictionary) -> String:
    if depth > max_input_depth:
        return "Input nesting exceeds depth %d." % max_input_depth

    budget.items += 1
    if budget.items > max_input_items:
        return "Input exceeds %d values." % max_input_items

    match typeof(value):
        TYPE_NIL, TYPE_BOOL:
            return ""
        TYPE_INT:
            var integer_value := int(value)
            if integer_value > 9_007_199_254_740_991 or integer_value < -9_007_199_254_740_991:
                return "Integer input exceeds Luau's exact-number range."
            return ""
        TYPE_FLOAT:
            if not is_finite(float(value)):
                return "Input contains a non-finite floating-point value."
            return ""
        TYPE_STRING, TYPE_STRING_NAME:
            budget.bytes += String(value).to_utf8_buffer().size()
        TYPE_VECTOR3:
            var vector_value: Vector3 = value
            if not is_finite(vector_value.x) or not is_finite(vector_value.y) or not is_finite(vector_value.z):
                return "Input contains a non-finite Vector3."
        TYPE_PACKED_BYTE_ARRAY:
            budget.bytes += value.size()
        TYPE_ARRAY:
            for item in value:
                var item_error := _validate_input(item, depth + 1, budget)
                if not item_error.is_empty():
                    return item_error
        TYPE_DICTIONARY:
            for key in value.keys():
                if not (typeof(key) in [TYPE_STRING, TYPE_STRING_NAME, TYPE_INT]):
                    return "Dictionary keys must be strings, StringNames, or integers."
                var key_error := _validate_input(key, depth + 1, budget)
                if not key_error.is_empty():
                    return key_error
                var value_error := _validate_input(value[key], depth + 1, budget)
                if not value_error.is_empty():
                    return value_error
        _:
            return "Unsupported sandbox input type: %s." % type_string(typeof(value))

    if budget.bytes > max_input_bytes:
        return "Input exceeds %d data bytes." % max_input_bytes
    return ""


func _metrics(state: LuaState) -> Dictionary:
    return {
        "memory_bytes": state.get_memory_bytes(),
        "peak_memory_bytes": state.get_peak_memory_bytes(),
    }


func _take_stack_error(state: LuaState, prefix: String) -> String:
    if not state.is_valid() or state.get_top() <= 0:
        return prefix
    var message := prefix
    if state.is_string(-1):
        message += ": " + state.to_string_inplace(-1)
    else:
        message += ": non-string Luau error"
    state.pop(1)
    return message


func _failure(message: String, status: int = Luau.LUA_ERRRUN) -> Dictionary:
    return {
        "ok": false,
        "value": null,
        "error": message,
        "status": status,
        "timed_out": false,
        "memory_limited": false,
    }


func _failure_with_metrics(message: String, metrics: Dictionary, status: int) -> Dictionary:
    return {
        "ok": false,
        "value": null,
        "error": message,
        "status": status,
        "timed_out": false,
        "memory_limited": true,
        "memory_bytes": metrics.memory_bytes,
        "peak_memory_bytes": metrics.peak_memory_bytes,
    }

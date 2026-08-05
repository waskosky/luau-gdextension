class_name BatchedLuauRuntime
extends RefCounted

## Persistent trusted-code runtime intended for coarse, batched calls.
## Keep engine-heavy work in GDScript/C++ and exchange compact payloads.

const DEFAULT_LIBRARIES: int = (
    LuaState.LIB_BASE
    | LuaState.LIB_COROUTINE
    | LuaState.LIB_TABLE
    | LuaState.LIB_STRING
    | LuaState.LIB_BIT32
    | LuaState.LIB_BUFFER
    | LuaState.LIB_UTF8
    | LuaState.LIB_MATH
    | LuaState.LIB_VECTOR
    | LuaState.LIB_GODOT
)

var _state: LuaState
var _loaded := false
var _chunk_name := "trusted_batch"


func load_source(
    source: String,
    chunk_name: String = "trusted_batch",
    libraries: int = DEFAULT_LIBRARIES,
    max_source_bytes: int = 4 * 1024 * 1024
) -> Dictionary:
    close()

    var source_bytes := source.to_utf8_buffer().size()
    if source_bytes > max_source_bytes:
        return _failure("Source exceeds %d bytes." % max_source_bytes)

    _state = LuaState.new()
    if not _state.is_valid():
        return _failure("Could not create Luau VM.")

    _state.open_libs(libraries)

    var options := LuaCompileOptions.new()
    options.set_optimization_level(2)
    options.set_debug_level(1)
    options.set_type_info_level(0)
    options.set_coverage_level(0)

    var bytecode := Luau.compile(source, options)
    if bytecode.is_empty():
        close()
        return _failure("Luau compiler returned empty bytecode.")

    _chunk_name = chunk_name
    if not _state.load_bytecode(bytecode, chunk_name):
        var message := _take_stack_error("Failed to load Luau bytecode")
        close()
        return _failure(message)

    var status := _state.pcall(0, 0)
    if status != Luau.LUA_OK:
        var message := _take_stack_error("Failed to initialize trusted Luau chunk")
        close()
        return _failure(message, status)

    _loaded = true
    return {
        "ok": true,
        "error": "",
        "status": Luau.LUA_OK,
        "source_bytes": source_bytes,
        "bytecode_bytes": bytecode.size(),
    }


func call_batch(function_name: StringName, payload: Variant) -> Dictionary:
    return call_global(function_name, [payload])


func call_global(function_name: StringName, arguments: Array = []) -> Dictionary:
    if not _loaded or _state == null or not _state.is_valid():
        return _failure("Trusted Luau runtime is not loaded.")

    if arguments.size() > 64:
        return _failure("Refusing more than 64 arguments in one bridge call.")

    var value_type := _state.raw_get_field(Luau.LUA_GLOBALSINDEX, function_name)
    if value_type != Luau.LUA_TFUNCTION:
        _state.pop(1)
        return _failure("Global '%s' is not a Luau function." % function_name)

    for argument in arguments:
        _state.push_variant(argument)

    var status := _state.pcall(arguments.size(), 1)
    if status != Luau.LUA_OK:
        return _failure(_take_stack_error("Luau batch call failed"), status)

    var value := _state.to_variant(-1)
    _state.pop(1)
    return {
        "ok": true,
        "value": value,
        "error": "",
        "status": status,
    }


func get_memory_bytes() -> int:
    if _state == null or not _state.is_valid():
        return 0
    return _state.get_memory_bytes()


func get_peak_memory_bytes() -> int:
    if _state == null or not _state.is_valid():
        return 0
    return _state.get_peak_memory_bytes()


func close() -> void:
    _loaded = false
    if _state != null:
        if _state.is_valid():
            _state.close()
        _state = null


func _take_stack_error(prefix: String) -> String:
    if _state == null or not _state.is_valid() or _state.get_top() <= 0:
        return prefix

    var message := prefix
    if _state.is_string(-1):
        message += ": " + _state.to_string_inplace(-1)
    else:
        message += ": non-string Luau error"
    _state.pop(1)
    return message


func _failure(message: String, status: int = Luau.LUA_ERRRUN) -> Dictionary:
    return {
        "ok": false,
        "value": null,
        "error": message,
        "status": status,
    }

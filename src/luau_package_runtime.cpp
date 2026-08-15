#include "luau_package_runtime.h"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace rai::luau {
namespace {

constexpr const char *MODULE_CACHE_KEY = "__rai_module_cache";
constexpr int NO_REFERENCE = -1;

std::string lua_error_text(lua_State *state) {
    std::size_t length = 0;
    const char *text = lua_tolstring(state, -1, &length);
    return text != nullptr ? std::string(text, length) : std::string("unknown Luau error");
}

void remove_global(lua_State *state, const char *name) {
    lua_pushnil(state);
    lua_setfield(state, LUA_GLOBALSINDEX, name);
}

} // namespace

LuauPackageRuntime::LuauPackageRuntime() = default;

LuauPackageRuntime::~LuauPackageRuntime() {
    close_vm();
}

void LuauPackageRuntime::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("configure", "limits"), &LuauPackageRuntime::configure);
    godot::ClassDB::bind_method(godot::D_METHOD("load_modules", "modules", "entrypoint"), &LuauPackageRuntime::load_modules);
    godot::ClassDB::bind_method(godot::D_METHOD("activate", "context"), &LuauPackageRuntime::activate);
    godot::ClassDB::bind_method(godot::D_METHOD("handle_event", "event"), &LuauPackageRuntime::handle_event);
    godot::ClassDB::bind_method(godot::D_METHOD("snapshot"), &LuauPackageRuntime::snapshot);
    godot::ClassDB::bind_method(godot::D_METHOD("deactivate"), &LuauPackageRuntime::deactivate);
    godot::ClassDB::bind_method(godot::D_METHOD("close"), &LuauPackageRuntime::close);
    godot::ClassDB::bind_method(godot::D_METHOD("metrics"), &LuauPackageRuntime::metrics);
}

godot::Dictionary LuauPackageRuntime::configure(const godot::Dictionary &limits) {
    close_vm();
    limits_.maximum_source_bytes = bounded_size(limits, "maxSourceBytes", limits_.maximum_source_bytes, 1024, 64 * 1024 * 1024);
    limits_.maximum_module_count = bounded_size(limits, "maxModuleCount", limits_.maximum_module_count, 1, 4096);
    limits_.maximum_payload_bytes = bounded_size(limits, "maxPayloadBytes", limits_.maximum_payload_bytes, 1024, 64 * 1024 * 1024);
    limits_.maximum_data_depth = static_cast<int>(bounded_size(limits, "maxDataDepth", static_cast<std::size_t>(limits_.maximum_data_depth), 4, 128));
    limits_.maximum_data_items = bounded_size(limits, "maxDataItems", limits_.maximum_data_items, 64, 1000000);
    limits_.maximum_memory_bytes = bounded_size(limits, "maxMemoryBytes", limits_.maximum_memory_bytes, 1024 * 1024, 512 * 1024 * 1024);
    limits_.timeout_usec = bounded_u64(limits, "timeoutUsec", limits_.timeout_usec, 1000, 10 * 1000 * 1000);
    configured_ = true;
    godot::Dictionary result = success();
    godot::Dictionary effective_limits;
    effective_limits["maxSourceBytes"] = static_cast<int64_t>(limits_.maximum_source_bytes);
    effective_limits["maxModuleCount"] = static_cast<int64_t>(limits_.maximum_module_count);
    effective_limits["maxPayloadBytes"] = static_cast<int64_t>(limits_.maximum_payload_bytes);
    effective_limits["maxDataDepth"] = limits_.maximum_data_depth;
    effective_limits["maxDataItems"] = static_cast<int64_t>(limits_.maximum_data_items);
    effective_limits["maxMemoryBytes"] = static_cast<int64_t>(limits_.maximum_memory_bytes);
    effective_limits["timeoutUsec"] = static_cast<int64_t>(limits_.timeout_usec);
    result["limits"] = effective_limits;
    return result;
}

godot::Dictionary LuauPackageRuntime::load_modules(const godot::Dictionary &modules, const godot::String &entrypoint) {
    if (!configured_) {
        return failure("runtime_not_configured");
    }
    close_vm();
    configured_ = true;
    if (modules.is_empty() || static_cast<std::size_t>(modules.size()) > limits_.maximum_module_count) {
        return failure("module_count_limit", "module map is empty or exceeds its configured limit");
    }
    modules_.clear();
    std::size_t total_source_bytes = 0;
    const godot::Array keys = modules.keys();
    for (int64_t index = 0; index < keys.size(); ++index) {
        const godot::Variant key = keys[index];
        const godot::Variant source_value = modules[key];
        if ((key.get_type() != godot::Variant::STRING && key.get_type() != godot::Variant::STRING_NAME) || source_value.get_type() != godot::Variant::STRING) {
            return failure("module_map_invalid", "module identifiers and sources must be strings");
        }
        const std::string identifier = to_utf8(static_cast<godot::String>(key));
        const std::string source = to_utf8(static_cast<godot::String>(source_value));
        if (!valid_identifier(identifier)) {
            return failure("module_identifier_invalid", identifier);
        }
        total_source_bytes += source.size();
        if (total_source_bytes > limits_.maximum_source_bytes) {
            return failure("source_size_limit", "combined module source exceeds its configured limit");
        }
        modules_.emplace(identifier, source);
    }
    entrypoint_ = to_utf8(entrypoint);
    if (!valid_identifier(entrypoint_) || modules_.find(entrypoint_) == modules_.end()) {
        return failure("entrypoint_invalid", entrypoint_);
    }
    std::string error;
    if (!reset_vm(error)) {
        return failure("vm_initialization_failed", error);
    }
    begin_call();
    const bool loaded = load_module(entrypoint_, error);
    finish_call(loaded);
    if (!loaded) {
        close_vm();
        configured_ = true;
        return failure("entrypoint_load_failed", error);
    }
    entrypoint_reference_ = lua_ref(state_, -1);
    lua_pop(state_, 1);
    godot::Dictionary result = success();
    result["entrypoint"] = entrypoint;
    result["moduleCount"] = modules.size();
    result["sourceBytes"] = static_cast<int64_t>(total_source_bytes);
    return result;
}

godot::Dictionary LuauPackageRuntime::activate(const godot::Dictionary &context) {
    if (state_ == nullptr || entrypoint_reference_ == NO_REFERENCE || fatal_) {
        return failure("runtime_not_loaded");
    }
    if (session_reference_ != NO_REFERENCE) {
        return failure("runtime_already_active");
    }
    std::string error;
    const int stack_base = lua_gettop(state_);
    begin_call();
    if (!push_entrypoint_function("activate", true, error) || !push_input(context, error)) {
        lua_settop(state_, stack_base);
        finish_call(false);
        return failure("activation_input_failed", error);
    }
    if (!protected_call(1, 1, error)) {
        finish_call(false);
        return failure("activation_failed", error);
    }
    if (lua_isnoneornil(state_, -1)) {
        lua_pop(state_, 1);
        finish_call(false);
        return failure("activation_state_missing");
    }
    session_reference_ = lua_ref(state_, -1);
    godot::Variant activation_value;
    const bool converted = pull_output(-1, activation_value, error);
    lua_pop(state_, 1);
    finish_call(converted);
    if (!converted) {
        release_reference(session_reference_);
        return failure("activation_result_invalid", error);
    }
    godot::Dictionary result = success();
    result["value"] = activation_value;
    return result;
}

godot::Dictionary LuauPackageRuntime::handle_event(const godot::Dictionary &event) {
    if (state_ == nullptr || session_reference_ == NO_REFERENCE || fatal_) {
        return failure("runtime_not_active");
    }
    std::string error;
    const int stack_base = lua_gettop(state_);
    begin_call();
    if (!push_entrypoint_function("handle_event", true, error)) {
        lua_settop(state_, stack_base);
        finish_call(false);
        return failure("event_handler_missing", error);
    }
    lua_getref(state_, session_reference_);
    if (!push_input(event, error)) {
        lua_settop(state_, stack_base);
        finish_call(false);
        return failure("event_input_invalid", error);
    }
    if (!protected_call(2, 1, error)) {
        finish_call(false);
        return failure("event_handler_failed", error);
    }
    godot::Variant output;
    const bool converted = pull_output(-1, output, error);
    lua_pop(state_, 1);
    finish_call(converted);
    if (!converted) {
        return failure("event_result_invalid", error);
    }
    if (output.get_type() == godot::Variant::DICTIONARY) {
        godot::Dictionary result = output;
        if (!result.has("ok")) {
            result["ok"] = true;
        }
        return result;
    }
    godot::Dictionary result = success();
    result["value"] = output;
    return result;
}

godot::Dictionary LuauPackageRuntime::snapshot() {
    if (state_ == nullptr || session_reference_ == NO_REFERENCE || fatal_) {
        return failure("runtime_not_active");
    }
    std::string error;
    const int stack_base = lua_gettop(state_);
    begin_call();
    if (!push_entrypoint_function("snapshot", true, error)) {
        lua_settop(state_, stack_base);
        finish_call(false);
        return failure("snapshot_handler_missing", error);
    }
    lua_getref(state_, session_reference_);
    if (!protected_call(1, 1, error)) {
        finish_call(false);
        return failure("snapshot_failed", error);
    }
    godot::Variant output;
    const bool converted = pull_output(-1, output, error);
    lua_pop(state_, 1);
    finish_call(converted);
    if (!converted) {
        return failure("snapshot_result_invalid", error);
    }
    godot::Dictionary result = success();
    result["value"] = output;
    return result;
}

godot::Dictionary LuauPackageRuntime::deactivate() {
    if (state_ == nullptr || session_reference_ == NO_REFERENCE) {
        return success();
    }
    if (fatal_) {
        release_reference(session_reference_);
        return failure("runtime_fatal", "fatal runtimes cannot execute deactivation code");
    }
    std::string error;
    const int stack_base = lua_gettop(state_);
    bool succeeded = true;
    begin_call();
    if (push_entrypoint_function("deactivate", false, error)) {
        if (!lua_isnoneornil(state_, -1)) {
            lua_getref(state_, session_reference_);
            succeeded = protected_call(1, 1, error);
            if (succeeded) {
                lua_pop(state_, 1);
            }
        } else {
            lua_pop(state_, 1);
        }
    } else {
        lua_settop(state_, stack_base);
        succeeded = false;
    }
    finish_call(succeeded);
    release_reference(session_reference_);
    return succeeded ? success() : failure("deactivation_failed", error);
}

void LuauPackageRuntime::close() {
    close_vm();
    configured_ = false;
}

godot::Dictionary LuauPackageRuntime::metrics() const {
    godot::Dictionary result;
    result["configured"] = configured_;
    result["loaded"] = state_ != nullptr;
    result["active"] = session_reference_ != NO_REFERENCE;
    result["fatal"] = fatal_;
    result["moduleCount"] = static_cast<int64_t>(modules_.size());
    result["allocatedBytes"] = static_cast<int64_t>(allocated_bytes_);
    result["peakAllocatedBytes"] = static_cast<int64_t>(peak_allocated_bytes_);
    result["callCount"] = static_cast<int64_t>(calls_);
    result["failedCallCount"] = static_cast<int64_t>(failed_calls_);
    result["totalCallUsec"] = static_cast<int64_t>(total_call_usec_);
    result["maximumCallUsec"] = static_cast<int64_t>(maximum_call_usec_);
    result["lastError"] = godot::String(last_error_.c_str());
    return result;
}

void *LuauPackageRuntime::allocator(void *userdata, void *pointer, std::size_t old_size, std::size_t new_size) {
    auto *runtime = static_cast<LuauPackageRuntime *>(userdata);
    if (new_size == 0) {
        std::free(pointer);
        runtime->allocated_bytes_ = old_size > runtime->allocated_bytes_ ? 0 : runtime->allocated_bytes_ - old_size;
        return nullptr;
    }
    const std::size_t base = old_size > runtime->allocated_bytes_ ? 0 : runtime->allocated_bytes_ - old_size;
    if (new_size > runtime->limits_.maximum_memory_bytes || base > runtime->limits_.maximum_memory_bytes - new_size) {
        runtime->memory_limit_hit_ = true;
        return nullptr;
    }
    void *resized = std::realloc(pointer, new_size);
    if (resized == nullptr) {
        return nullptr;
    }
    runtime->allocated_bytes_ = base + new_size;
    runtime->peak_allocated_bytes_ = std::max(runtime->peak_allocated_bytes_, runtime->allocated_bytes_);
    return resized;
}

void LuauPackageRuntime::interrupt(lua_State *state, int) {
    LuauPackageRuntime *runtime = from_state(state);
    if (runtime == nullptr || runtime->deadline_usec_ == 0) {
        return;
    }
    if (godot::Time::get_singleton()->get_ticks_usec() > runtime->deadline_usec_) {
        luaL_error(state, "RAI execution time limit exceeded");
    }
}

int LuauPackageRuntime::require_callback(lua_State *state) {
    LuauPackageRuntime *runtime = from_state(state);
    if (runtime == nullptr) {
        luaL_error(state, "RAI runtime context is unavailable");
    }
    std::size_t length = 0;
    const char *identifier = luaL_checklstring(state, 1, &length);
    std::string error;
    if (!runtime->load_module(std::string(identifier, length), error)) {
        luaL_error(state, "%s", error.c_str());
    }
    return 1;
}

bool LuauPackageRuntime::reset_vm(std::string &error) {
    // The source map is the immutable input for the VM being rebuilt. close_vm()
    // clears it for public close/error paths, so preserve it across this internal
    // reset and restore it before loading the entrypoint.
    auto prepared_modules = std::move(modules_);
    auto prepared_entrypoint = std::move(entrypoint_);
    close_vm();
    modules_ = std::move(prepared_modules);
    entrypoint_ = std::move(prepared_entrypoint);
    configured_ = true;
    memory_limit_hit_ = false;
    allocated_bytes_ = 0;
    peak_allocated_bytes_ = 0;
    fatal_ = false;
    state_ = lua_newstate(&LuauPackageRuntime::allocator, this);
    if (state_ == nullptr) {
        error = memory_limit_hit_ ? "memory limit prevented VM creation" : "could not create Luau VM";
        return false;
    }
    lua_callbacks(state_)->userdata = this;
    lua_callbacks(state_)->interrupt = &LuauPackageRuntime::interrupt;
    install_environment();
    return true;
}

void LuauPackageRuntime::close_vm() {
    if (state_ != nullptr) {
        release_reference(session_reference_);
        release_reference(entrypoint_reference_);
        lua_close(state_);
    }
    state_ = nullptr;
    entrypoint_reference_ = NO_REFERENCE;
    session_reference_ = NO_REFERENCE;
    loading_.clear();
    modules_.clear();
    allocated_bytes_ = 0;
    deadline_usec_ = 0;
    fatal_ = false;
    memory_limit_hit_ = false;
}

void LuauPackageRuntime::install_environment() {
    luaL_openlibs(state_);
    remove_global(state_, "os");
    remove_global(state_, "debug");
    remove_global(state_, "loadfile");
    remove_global(state_, "dofile");
    remove_global(state_, "collectgarbage");
    remove_global(state_, "require");
    lua_getfield(state_, LUA_GLOBALSINDEX, "math");
    if (lua_istable(state_, -1)) {
        lua_pushnil(state_);
        lua_setfield(state_, -2, "random");
        lua_pushnil(state_);
        lua_setfield(state_, -2, "randomseed");
    }
    lua_pop(state_, 1);
    lua_pushcfunction(state_, &LuauPackageRuntime::require_callback, "require_module");
    lua_setfield(state_, LUA_GLOBALSINDEX, "require_module");
    lua_createtable(state_, 0, static_cast<int>(limits_.maximum_module_count));
    lua_setfield(state_, LUA_REGISTRYINDEX, MODULE_CACHE_KEY);
    luaL_sandbox(state_);
}

bool LuauPackageRuntime::load_module(const std::string &identifier, std::string &error) {
    if (!valid_identifier(identifier)) {
        error = "invalid logical module identifier: " + identifier;
        return false;
    }
    lua_getfield(state_, LUA_REGISTRYINDEX, MODULE_CACHE_KEY);
    lua_getfield(state_, -1, identifier.c_str());
    if (!lua_isnil(state_, -1)) {
        lua_remove(state_, -2);
        return true;
    }
    lua_pop(state_, 2);
    const auto source_it = modules_.find(identifier);
    if (source_it == modules_.end()) {
        error = "logical module is not present in the locked module map: " + identifier;
        return false;
    }
    if (!loading_.insert(identifier).second) {
        error = "logical module import cycle detected at: " + identifier;
        return false;
    }
    Luau::CompileOptions options;
    options.optimizationLevel = 1;
    options.debugLevel = 1;
    const std::string bytecode = Luau::compile(source_it->second, options);
    const std::string chunk_name = "=" + identifier;
    const int load_status = luau_load(state_, chunk_name.c_str(), bytecode.data(), bytecode.size(), 0);
    if (load_status != LUA_OK) {
        error = lua_error_text(state_);
        lua_pop(state_, 1);
        loading_.erase(identifier);
        return false;
    }
    const int call_status = lua_pcall(state_, 0, 1, 0);
    if (call_status != LUA_OK) {
        error = lua_error_text(state_);
        lua_pop(state_, 1);
        loading_.erase(identifier);
        make_fatal_if_required(call_status, error);
        return false;
    }
    if (lua_isnoneornil(state_, -1)) {
        lua_pop(state_, 1);
        loading_.erase(identifier);
        error = "module returned nil: " + identifier;
        return false;
    }
    lua_getfield(state_, LUA_REGISTRYINDEX, MODULE_CACHE_KEY);
    lua_pushvalue(state_, -2);
    lua_setfield(state_, -2, identifier.c_str());
    lua_pop(state_, 1);
    loading_.erase(identifier);
    return true;
}

bool LuauPackageRuntime::push_entrypoint_function(const char *name, bool required, std::string &error) {
    if (entrypoint_reference_ == NO_REFERENCE) {
        error = "entrypoint is not loaded";
        return false;
    }
    lua_getref(state_, entrypoint_reference_);
    if (!lua_istable(state_, -1)) {
        lua_pop(state_, 1);
        error = "entrypoint module must return a table";
        return false;
    }
    lua_getfield(state_, -1, name);
    lua_remove(state_, -2);
    if (lua_isnil(state_, -1) && !required) {
        return true;
    }
    if (!lua_isfunction(state_, -1)) {
        lua_pop(state_, 1);
        error = std::string("entrypoint function is missing: ") + name;
        return false;
    }
    return true;
}

bool LuauPackageRuntime::push_input(const godot::Variant &value, std::string &error) {
    BridgeState bridge;
    bridge.limits.maximum_depth = limits_.maximum_data_depth;
    bridge.limits.maximum_items = limits_.maximum_data_items;
    bridge.limits.maximum_payload_bytes = limits_.maximum_payload_bytes;
    return push_variant(state_, value, bridge, error);
}

bool LuauPackageRuntime::pull_output(int index, godot::Variant &value, std::string &error) {
    BridgeState bridge;
    bridge.limits.maximum_depth = limits_.maximum_data_depth;
    bridge.limits.maximum_items = limits_.maximum_data_items;
    bridge.limits.maximum_payload_bytes = limits_.maximum_payload_bytes;
    return pull_variant(state_, index, value, bridge, error);
}

bool LuauPackageRuntime::protected_call(int arguments, int results, std::string &error) {
    const int status = lua_pcall(state_, arguments, results, 0);
    if (status == LUA_OK) {
        return true;
    }
    error = lua_error_text(state_);
    lua_pop(state_, 1);
    make_fatal_if_required(status, error);
    return false;
}

void LuauPackageRuntime::begin_call() {
    deadline_usec_ = godot::Time::get_singleton()->get_ticks_usec() + limits_.timeout_usec;
}

void LuauPackageRuntime::finish_call(bool succeeded) {
    const std::uint64_t now = godot::Time::get_singleton()->get_ticks_usec();
    const std::uint64_t start = deadline_usec_ >= limits_.timeout_usec ? deadline_usec_ - limits_.timeout_usec : now;
    const std::uint64_t duration = now >= start ? now - start : 0;
    ++calls_;
    if (!succeeded) {
        ++failed_calls_;
    }
    total_call_usec_ += duration;
    maximum_call_usec_ = std::max(maximum_call_usec_, duration);
    deadline_usec_ = 0;
}

void LuauPackageRuntime::make_fatal_if_required(int status, const std::string &error) {
    last_error_ = error;
    if (status == LUA_ERRMEM || memory_limit_hit_ || error.find("execution time limit") != std::string::npos) {
        fatal_ = true;
    }
}

void LuauPackageRuntime::release_reference(int &reference) {
    if (state_ != nullptr && reference != NO_REFERENCE) {
        lua_unref(state_, reference);
    }
    reference = NO_REFERENCE;
}

LuauPackageRuntime *LuauPackageRuntime::from_state(lua_State *state) {
    lua_Callbacks *callbacks = lua_callbacks(state);
    return callbacks != nullptr ? static_cast<LuauPackageRuntime *>(callbacks->userdata) : nullptr;
}

std::string LuauPackageRuntime::to_utf8(const godot::String &value) {
    const godot::CharString utf8 = value.utf8();
    return std::string(utf8.get_data(), static_cast<std::size_t>(utf8.length()));
}

bool LuauPackageRuntime::valid_identifier(const std::string &identifier) {
    if (identifier.empty() || identifier.size() > 200 || identifier.front() == '.' || identifier.back() == '.') {
        return false;
    }
    bool previous_dot = false;
    for (const unsigned char character : identifier) {
        const bool dot = character == '.';
        if (!(std::isalnum(character) || character == '_' || character == '-' || dot) || (dot && previous_dot)) {
            return false;
        }
        previous_dot = dot;
    }
    return true;
}

std::size_t LuauPackageRuntime::bounded_size(const godot::Dictionary &source, const char *key, std::size_t fallback, std::size_t minimum, std::size_t maximum) {
    if (!source.has(key)) {
        return fallback;
    }
    const int64_t value = static_cast<int64_t>(source[key]);
    if (value < 0) {
        return minimum;
    }
    return std::clamp(static_cast<std::size_t>(value), minimum, maximum);
}

std::uint64_t LuauPackageRuntime::bounded_u64(const godot::Dictionary &source, const char *key, std::uint64_t fallback, std::uint64_t minimum, std::uint64_t maximum) {
    if (!source.has(key)) {
        return fallback;
    }
    const int64_t value = static_cast<int64_t>(source[key]);
    if (value < 0) {
        return minimum;
    }
    return std::clamp(static_cast<std::uint64_t>(value), minimum, maximum);
}

godot::Dictionary LuauPackageRuntime::success() {
    godot::Dictionary result;
    result["ok"] = true;
    return result;
}

godot::Dictionary LuauPackageRuntime::failure(const std::string &code, const std::string &message) const {
    godot::Dictionary result;
    result["ok"] = false;
    result["error"] = godot::String(code.c_str());
    if (!message.empty()) {
        result["message"] = godot::String(message.c_str());
    }
    result["fatal"] = fatal_;
    return result;
}

} // namespace rai::luau

#pragma once

#include "variant_bridge.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct lua_State;

namespace rai::luau {

class LuauPackageRuntime : public godot::RefCounted {
    GDCLASS(LuauPackageRuntime, godot::RefCounted)

public:
    LuauPackageRuntime();
    ~LuauPackageRuntime() override;

    godot::Dictionary configure(const godot::Dictionary &limits);
    godot::Dictionary load_modules(const godot::Dictionary &modules, const godot::String &entrypoint);
    godot::Dictionary activate(const godot::Dictionary &context);
    godot::Dictionary handle_event(const godot::Dictionary &event);
    godot::Dictionary snapshot();
    godot::Dictionary deactivate();
    void close();
    godot::Dictionary metrics() const;

protected:
    static void _bind_methods();

private:
    struct Limits {
        std::size_t maximum_source_bytes = 4 * 1024 * 1024;
        std::size_t maximum_module_count = 256;
        std::size_t maximum_payload_bytes = 4 * 1024 * 1024;
        int maximum_data_depth = 32;
        std::size_t maximum_data_items = 100000;
        std::size_t maximum_memory_bytes = 32 * 1024 * 1024;
        std::uint64_t timeout_usec = 50000;
    };

    lua_State *state_ = nullptr;
    Limits limits_;
    std::unordered_map<std::string, std::string> modules_;
    std::unordered_set<std::string> loading_;
    int entrypoint_reference_ = -1;
    int session_reference_ = -1;
    std::size_t allocated_bytes_ = 0;
    std::size_t peak_allocated_bytes_ = 0;
    bool memory_limit_hit_ = false;
    bool configured_ = false;
    bool fatal_ = false;
    std::uint64_t deadline_usec_ = 0;
    std::uint64_t calls_ = 0;
    std::uint64_t failed_calls_ = 0;
    std::uint64_t total_call_usec_ = 0;
    std::uint64_t maximum_call_usec_ = 0;
    std::string entrypoint_;
    std::string last_error_;

    static void *allocator(void *userdata, void *pointer, std::size_t old_size, std::size_t new_size);
    static void interrupt(lua_State *state, int gc);
    static int require_callback(lua_State *state);

    bool reset_vm(std::string &error);
    void close_vm();
    void install_environment();
    bool load_module(const std::string &identifier, std::string &error);
    bool push_entrypoint_function(const char *name, bool required, std::string &error);
    bool push_input(const godot::Variant &value, std::string &error);
    bool pull_output(int index, godot::Variant &value, std::string &error);
    bool protected_call(int arguments, int results, std::string &error);
    void begin_call();
    void finish_call(bool succeeded);
    void make_fatal_if_required(int status, const std::string &error);
    void release_reference(int &reference);

    static LuauPackageRuntime *from_state(lua_State *state);
    static std::string to_utf8(const godot::String &value);
    static bool valid_identifier(const std::string &identifier);
    static std::size_t bounded_size(const godot::Dictionary &source, const char *key, std::size_t fallback, std::size_t minimum, std::size_t maximum);
    static std::uint64_t bounded_u64(const godot::Dictionary &source, const char *key, std::uint64_t fallback, std::uint64_t minimum, std::uint64_t maximum);
    static godot::Dictionary success();
    godot::Dictionary failure(const std::string &code, const std::string &message = "") const;
};

} // namespace rai::luau

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace rai::luau {

class LuauSandboxRunner : public godot::RefCounted {
    GDCLASS(LuauSandboxRunner, godot::RefCounted)

public:
    godot::Dictionary run(const godot::String &source, const godot::Variant &input, const godot::Dictionary &limits = godot::Dictionary());

protected:
    static void _bind_methods();
};

} // namespace rai::luau

#pragma once

#include <godot_cpp/variant/variant.hpp>

#include <cstddef>
#include <string>
#include <unordered_set>

struct lua_State;

namespace rai::luau {

struct BridgeLimits {
    int maximum_depth = 32;
    std::size_t maximum_items = 100000;
    std::size_t maximum_payload_bytes = 4 * 1024 * 1024;
};

struct BridgeState {
    BridgeLimits limits;
    std::size_t items = 0;
    std::size_t approximate_bytes = 0;
    std::unordered_set<const void *> visiting_tables;
};

bool push_variant(lua_State *state, const godot::Variant &value, BridgeState &bridge, std::string &error, int depth = 0);
bool pull_variant(lua_State *state, int index, godot::Variant &value, BridgeState &bridge, std::string &error, int depth = 0);

} // namespace rai::luau

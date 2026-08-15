#include "variant_bridge.h"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <lua.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace rai::luau {
namespace {

bool consume(BridgeState &bridge, std::size_t items, std::size_t bytes, std::string &error) {
    bridge.items += items;
    bridge.approximate_bytes += bytes;
    if (bridge.items > bridge.limits.maximum_items) {
        error = "data item limit exceeded";
        return false;
    }
    if (bridge.approximate_bytes > bridge.limits.maximum_payload_bytes) {
        error = "data payload limit exceeded";
        return false;
    }
    return true;
}

bool check_depth(const BridgeState &bridge, int depth, std::string &error) {
    if (depth > bridge.limits.maximum_depth) {
        error = "data nesting limit exceeded";
        return false;
    }
    return true;
}

} // namespace

bool push_variant(lua_State *state, const godot::Variant &value, BridgeState &bridge, std::string &error, int depth) {
    if (!check_depth(bridge, depth, error) || !consume(bridge, 1, 0, error)) {
        return false;
    }
    switch (value.get_type()) {
        case godot::Variant::NIL:
            lua_pushnil(state);
            return true;
        case godot::Variant::BOOL:
            lua_pushboolean(state, static_cast<bool>(value));
            return true;
        case godot::Variant::INT: {
            const int64_t integer = static_cast<int64_t>(value);
            constexpr int64_t exact_limit = INT64_C(9007199254740991);
            if (integer < -exact_limit || integer > exact_limit) {
                error = "integer lies outside the portable exact range";
                return false;
            }
            lua_pushnumber(state, static_cast<double>(integer));
            return true;
        }
        case godot::Variant::FLOAT: {
            const double number = static_cast<double>(value);
            if (!std::isfinite(number)) {
                error = "non-finite numbers are forbidden";
                return false;
            }
            lua_pushnumber(state, number);
            return true;
        }
        case godot::Variant::STRING:
        case godot::Variant::STRING_NAME: {
            const godot::String text = static_cast<godot::String>(value);
            const godot::CharString utf8 = text.utf8();
            const std::size_t length = static_cast<std::size_t>(utf8.length());
            if (!consume(bridge, 0, length, error)) {
                return false;
            }
            lua_pushlstring(state, utf8.get_data(), length);
            return true;
        }
        case godot::Variant::ARRAY: {
            const godot::Array array = value;
            lua_createtable(state, array.size(), 0);
            for (int64_t index = 0; index < array.size(); ++index) {
                if (!push_variant(state, array[index], bridge, error, depth + 1)) {
                    return false;
                }
                lua_rawseti(state, -2, static_cast<int>(index + 1));
            }
            return true;
        }
        case godot::Variant::DICTIONARY: {
            const godot::Dictionary dictionary = value;
            lua_createtable(state, 0, dictionary.size());
            const godot::Array keys = dictionary.keys();
            for (int64_t index = 0; index < keys.size(); ++index) {
                const godot::Variant key = keys[index];
                if (key.get_type() != godot::Variant::STRING && key.get_type() != godot::Variant::STRING_NAME) {
                    error = "dictionary keys must be strings";
                    return false;
                }
                const godot::String text = static_cast<godot::String>(key);
                const godot::CharString utf8 = text.utf8();
                if (!consume(bridge, 0, static_cast<std::size_t>(utf8.length()), error)) {
                    return false;
                }
                if (!push_variant(state, dictionary[key], bridge, error, depth + 1)) {
                    return false;
                }
                lua_setfield(state, -2, utf8.get_data());
            }
            return true;
        }
        default:
            error = "unsupported Godot Variant type: " + std::to_string(static_cast<int>(value.get_type()));
            return false;
    }
}

bool pull_variant(lua_State *state, int index, godot::Variant &value, BridgeState &bridge, std::string &error, int depth) {
    if (!check_depth(bridge, depth, error) || !consume(bridge, 1, 0, error)) {
        return false;
    }
    index = lua_absindex(state, index);
    switch (lua_type(state, index)) {
        case LUA_TNIL:
            value = godot::Variant();
            return true;
        case LUA_TBOOLEAN:
            value = static_cast<bool>(lua_toboolean(state, index));
            return true;
        case LUA_TNUMBER: {
            const double number = lua_tonumber(state, index);
            if (!std::isfinite(number)) {
                error = "Luau returned a non-finite number";
                return false;
            }
            constexpr double exact_limit = 9007199254740991.0;
            if (std::floor(number) == number && number >= -exact_limit && number <= exact_limit) {
                value = static_cast<int64_t>(number);
            } else {
                value = number;
            }
            return true;
        }
#ifdef LUA_TINTEGER
        case LUA_TINTEGER: {
            int is_integer = 0;
            const int64_t integer = lua_tointeger64(state, index, &is_integer);
            constexpr int64_t exact_limit = INT64_C(9007199254740991);
            if (!is_integer || integer < -exact_limit || integer > exact_limit) {
                error = "Luau returned an integer outside the portable exact range";
                return false;
            }
            value = integer;
            return true;
        }
#endif
        case LUA_TSTRING: {
            std::size_t length = 0;
            const char *text = lua_tolstring(state, index, &length);
            if (!consume(bridge, 0, length, error)) {
                return false;
            }
            godot::String decoded;
            if (decoded.parse_utf8(text, static_cast<int64_t>(length)) != godot::OK) {
                error = "Luau returned a string that is not valid UTF-8";
                return false;
            }
            value = decoded;
            return true;
        }
        case LUA_TTABLE: {
            const void *pointer = lua_topointer(state, index);
            if (bridge.visiting_tables.count(pointer) != 0) {
                error = "cyclic Luau tables are forbidden";
                return false;
            }
            bridge.visiting_tables.insert(pointer);
            struct Entry {
                bool integer_key = false;
                int64_t integer = 0;
                godot::String string;
                godot::Variant value;
            };
            std::vector<Entry> entries;
            bool only_integer_keys = true;
            int64_t maximum_index = 0;
            lua_pushnil(state);
            while (lua_next(state, index) != 0) {
                Entry entry;
                const int key_type = lua_type(state, -2);
                if (key_type == LUA_TNUMBER) {
                    const double number = lua_tonumber(state, -2);
                    if (!std::isfinite(number) || number < 1 || std::floor(number) != number || number > 2147483647.0) {
                        lua_pop(state, 2);
                        bridge.visiting_tables.erase(pointer);
                        error = "table numeric keys must be positive consecutive integers";
                        return false;
                    }
                    entry.integer_key = true;
                    entry.integer = static_cast<int64_t>(number);
                    maximum_index = std::max(maximum_index, entry.integer);
                } else if (key_type == LUA_TSTRING) {
                    only_integer_keys = false;
                    std::size_t key_length = 0;
                    const char *key = lua_tolstring(state, -2, &key_length);
                    if (!consume(bridge, 0, key_length, error)) {
                        lua_pop(state, 2);
                        bridge.visiting_tables.erase(pointer);
                        return false;
                    }
                    godot::String decoded_key;
                    if (decoded_key.parse_utf8(key, static_cast<int64_t>(key_length)) != godot::OK) {
                        lua_pop(state, 2);
                        bridge.visiting_tables.erase(pointer);
                        error = "Luau returned a table key that is not valid UTF-8";
                        return false;
                    }
                    entry.string = decoded_key;
                } else {
                    lua_pop(state, 2);
                    bridge.visiting_tables.erase(pointer);
                    error = "table keys must be strings or positive integers";
                    return false;
                }
                if (!pull_variant(state, -1, entry.value, bridge, error, depth + 1)) {
                    lua_pop(state, 2);
                    bridge.visiting_tables.erase(pointer);
                    return false;
                }
                entries.push_back(entry);
                lua_pop(state, 1);
            }
            bridge.visiting_tables.erase(pointer);
            if (entries.empty()) {
                // The portable profile reserves an unmarked empty table for an empty array.
                // Empty objects must carry at least one schema/version field.
                value = godot::Array();
                return true;
            }
            if (only_integer_keys && maximum_index == static_cast<int64_t>(entries.size())) {
                godot::Array array;
                array.resize(maximum_index);
                std::vector<bool> occupied(static_cast<std::size_t>(maximum_index), false);
                for (const Entry &entry : entries) {
                    if (!entry.integer_key || occupied[static_cast<std::size_t>(entry.integer - 1)]) {
                        error = "table array keys are duplicate or mixed";
                        return false;
                    }
                    occupied[static_cast<std::size_t>(entry.integer - 1)] = true;
                    array[entry.integer - 1] = entry.value;
                }
                value = array;
                return true;
            }
            godot::Dictionary dictionary;
            for (const Entry &entry : entries) {
                if (entry.integer_key) {
                    error = "mixed table key shapes are forbidden";
                    return false;
                }
                dictionary[entry.string] = entry.value;
            }
            value = dictionary;
            return true;
        }
        default:
            error = "unsupported Luau return type: " + std::to_string(lua_type(state, index));
            return false;
    }
}

} // namespace rai::luau

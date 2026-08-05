#include "doctest.h"
#include "test_fixtures.h"

using namespace gdluau;
using namespace godot;

TEST_SUITE("Hardening")
{
    TEST_CASE_FIXTURE(LuaStateFixture, "tracked allocator reports usage and enforces a limit")
    {
        const int64_t baseline = state->get_memory_bytes();
        CHECK(baseline > 0);
        CHECK(state->get_peak_memory_bytes() >= baseline);

        state->set_memory_limit_bytes(baseline + 256 * 1024);
        lua_Status status = state->do_string(R"(
            local values = {}
            for i = 1, 100000 do
                values[i] = string.rep("x", 1024)
            end
            return #values
        )", "memory_limit");

        CHECK(status != LUA_OK);
        CHECK(state->did_hit_memory_limit());
        CHECK(state->get_memory_bytes() <= state->get_memory_limit_bytes());

        if (state->is_valid())
        {
            state->set_top(0);
            state->set_memory_limit_bytes(0);
            state->clear_memory_limit_hit();
        }
    }

    TEST_CASE_FIXTURE(LuaStateFixture, "monotonic deadline interrupts a runaway loop")
    {
        state->start_timeout_usec(2'000, 1);
        lua_Status status = state->do_string("while true do end", "timeout");
        state->clear_timeout();

        CHECK(status != LUA_OK);
        CHECK(state->did_timeout());

        if (state->is_valid())
        {
            state->set_top(0);
        }
    }

    TEST_CASE_FIXTURE(LuaStateFixture, "memory limit can be disabled")
    {
        state->set_memory_limit_bytes(1024 * 1024);
        CHECK(state->get_memory_limit_bytes() == 1024 * 1024);
        state->set_memory_limit_bytes(0);
        CHECK(state->get_memory_limit_bytes() == 0);
    }

    TEST_CASE_FIXTURE(LuaStateFixture, "empty library name populates the table on top of the stack")
    {
        Dictionary entries;
        entries["answer"] = 42;

        state->create_table();
        state->register_library("", entries);

        CHECK(state->get_top() == 1);
        CHECK(state->raw_get_field(-1, "answer") == LUA_TNUMBER);
        CHECK(state->to_integer(-1) == 42);
        state->pop(2);
    }
}

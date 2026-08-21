#include "register_types.h"

#include "lua_compileoptions.h"
#include "lua_debug.h"
#include "lua_state.h"
#include "luau_package_runtime.h"
#include "luau_sandbox_runner.h"
#include "luau.h"
#include "luau_script.h"
#include "static_strings.h"
#include "string_cache.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/engine_debugger.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <Luau/Common.h>

using namespace gdluau;
using namespace godot;

#ifndef GDLUAU_REGISTER_SCRIPT_RESOURCE_FORMATS
#define GDLUAU_REGISTER_SCRIPT_RESOURCE_FORMATS 1
#endif

static int assertionHandler(const char *expr, const char *file, int line, const char *function)
{
    ERR_PRINT(vformat("Luau assertion failed: %s, function %s, file %s, line %d", expr, function, file, line));

    EngineDebugger *debugger = EngineDebugger::get_singleton();
    if (debugger && debugger->is_active())
    {
        // Break in the debugger
        debugger->debug();
    }

    return 1;
}

static Ref<ResourceFormatLoaderLuauScript> resource_loader_luau;
static Ref<ResourceFormatSaverLuauScript> resource_saver_luau;
static bool script_resource_formats_registered = false;

namespace
{
GDExtensionInterfaceObjectFreeInstanceBinding object_free_instance_binding_unchecked = nullptr;

void object_free_instance_binding_if_valid(GDExtensionObjectPtr p_object, void *p_token)
{
    if (p_object != nullptr && object_free_instance_binding_unchecked != nullptr)
    {
        object_free_instance_binding_unchecked(p_object, p_token);
    }
}

void install_null_instance_binding_shutdown_guard()
{
    GDExtensionInterfaceObjectFreeInstanceBinding &free_instance_binding =
        godot::gdextension_interface::object_free_instance_binding;
    if (free_instance_binding == object_free_instance_binding_if_valid)
    {
        return;
    }

    object_free_instance_binding_unchecked = free_instance_binding;
    free_instance_binding = object_free_instance_binding_if_valid;
}
} // namespace

void initialize_gdluau(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
    {
        return;
    }

    // Initialize statics (must be done after Godot is initialized, not during DLL static init)
    initialize_static_strings();
    initialize_string_cache();

    // We generally try to avoid using the Luau C++ API (in favor of the C API),
    // for maximum compatibility with base Lua, but this appears to be the only
    // way to install a custom assertion handler. Without this, assertions will
    // be checked but never logged.
    ::Luau::assertHandler() = assertionHandler;

    GDREGISTER_RUNTIME_CLASS(gdluau::Luau);
    GDREGISTER_RUNTIME_CLASS(LuaCompileOptions);
    GDREGISTER_RUNTIME_CLASS(LuaDebug);
    GDREGISTER_RUNTIME_CLASS(LuaState);
    GDREGISTER_RUNTIME_CLASS(LuauScript);
    GDREGISTER_RUNTIME_CLASS(ResourceFormatLoaderLuauScript);
    GDREGISTER_RUNTIME_CLASS(ResourceFormatSaverLuauScript);
    GDREGISTER_CLASS(rai::luau::LuauPackageRuntime);
    GDREGISTER_CLASS(rai::luau::LuauSandboxRunner);

    // Register resource loader and saver for .lua and .luau files. Data-only
    // hosts can disable this while retaining the native package runtime, which
    // keeps portable modules out of Godot's editor import/UID pipeline.
    script_resource_formats_registered = GDLUAU_REGISTER_SCRIPT_RESOURCE_FORMATS != 0;
    if (script_resource_formats_registered)
    {
        resource_loader_luau.instantiate();
        ResourceLoader::get_singleton()->add_resource_format_loader(resource_loader_luau);

        resource_saver_luau.instantiate();
        ResourceSaver::get_singleton()->add_resource_format_saver(resource_saver_luau);
    }
}

void uninitialize_gdluau(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
    {
        return;
    }

    // Unregister resource loader and saver
    if (script_resource_formats_registered)
    {
        ResourceLoader::get_singleton()->remove_resource_format_loader(resource_loader_luau);
        resource_loader_luau.unref();

        ResourceSaver::get_singleton()->remove_resource_format_saver(resource_saver_luau);
        resource_saver_luau.unref();
        script_resource_formats_registered = false;
    }

    // Godot 4.7 exposes ClassDB as a static singleton. When godot-cpp resolves
    // the parent of an engine class that was dead-stripped from its static
    // archive (PackedScene is a common editor example), it caches a wrapper
    // whose underlying Godot object is intentionally null. Its generic core
    // shutdown path otherwise passes that null owner to
    // object_free_instance_binding and crashes. Install the guard only for the
    // remaining shutdown phases; ordinary non-null binding cleanup is still
    // delegated to Godot unchanged.
    install_null_instance_binding_shutdown_guard();

    // Cleanup statics
    uninitialize_string_cache();
    uninitialize_static_strings();
}

extern "C"
{
    // Initialization.
    GDExtensionBool GDE_EXPORT gdluau_entrypoint(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization)
    {
        godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

        init_obj.register_initializer(initialize_gdluau);
        init_obj.register_terminator(uninitialize_gdluau);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        return init_obj.init();
    }
}

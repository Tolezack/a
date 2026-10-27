#include "post.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
using namespace godot;
static void initialize_post(ModuleInitializationLevel level) {
    if (level == MODULE_INITIALIZATION_LEVEL_SCENE) ClassDB::register_class<SkyTearsNativeTerrainPost>();
}
static void uninitialize_post(ModuleInitializationLevel) {}
extern "C" GDExtensionBool GDE_EXPORT skytears_cpp_post_init(GDExtensionInterfaceGetProcAddress get_proc,
        GDExtensionClassLibraryPtr lib, GDExtensionInitialization *init) {
    GDExtensionBinding::InitObject obj(get_proc, lib, init);
    obj.register_initializer(initialize_post);
    obj.register_terminator(uninitialize_post);
    obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return obj.init();
}

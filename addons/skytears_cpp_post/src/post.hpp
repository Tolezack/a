#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/vector2i.hpp>
namespace godot {
class SkyTearsNativeTerrainPost : public RefCounted {
    GDCLASS(SkyTearsNativeTerrainPost, RefCounted)
protected:
    static void _bind_methods();
public:
    Dictionary apply_geology(PackedByteArray source, Vector2i chunk, int64_t seed, int highest_scan_y = 240);
    Dictionary apply_mountain(PackedByteArray source, Vector2i chunk, int64_t seed, int highest_scan_y, Dictionary settings);
};
}

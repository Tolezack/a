extends SceneTree

# Run only AFTER installing and activating the compiled GDExtension:
# godot --headless --path . --script res://addons/skytears_cpp_post/tests/parity.gd
const GEO = preload("res://Assets/Scripts/SkyTearsGeology.gd")
const MOUNTAIN = preload("res://Assets/Scripts/SkyTearsMountainNoise3D.gd")

func _initialize() -> void:
	if !ClassDB.class_exists("SkyTearsNativeTerrainPost"):
		printerr("C++ POSTPROCESS INDISPONIVEL: ative a GDExtension compilada.")
		quit(2)
		return
	var cpp: Object = ClassDB.instantiate("SkyTearsNativeTerrainPost")
	var blocks: PackedByteArray = PackedByteArray()
	blocks.resize(32 * 32 * 512)
	# Deterministic artificial terrain, including air, rock and soil.
	for z in range(32):
		for x in range(32):
			var col: int = x + 32 * z
			var top: int = 115 + (x + z) % 12
			for y in range(1, top):
				blocks[col + y * 1024] = 3
			blocks[col + top * 1024] = 1
	var mismatches: int = 0
	for pos: Vector2i in [Vector2i(0, 0), Vector2i(-10, 25), Vector2i(400, 400)]:
		var gd_mountain: Dictionary = MOUNTAIN.apply_to_chunk(blocks, pos, 234518, 160,
			{"noise3d_relief_strength": 1.0, "noise3d_flatland_protection": 0.49,
			 "noise3d_arches_enabled": true})
		var cpp_mountain: Dictionary = cpp.call("apply_mountain", blocks, pos, 234518, 160,
			{"noise3d_relief_strength": 1.0, "noise3d_flatland_protection": 0.49,
			 "noise3d_arches_enabled": true})
		mismatches += _compare("mountain " + str(pos), gd_mountain, cpp_mountain, blocks)
		var gd_geo: Dictionary = GEO.apply_to_chunk(blocks, pos, 234518, 160)
		var cpp_geo: Dictionary = cpp.call("apply_geology", blocks, pos, 234518, 160)
		mismatches += _compare("geology " + str(pos), gd_geo, cpp_geo, blocks)
	print("[PostProcess parity] different bytes:", mismatches)
	quit(0 if mismatches == 0 else 1)

func _compare(label: String, reference: Dictionary, actual: Dictionary, original: PackedByteArray) -> int:
	var a: PackedByteArray = reference.get("blocks", original)
	var b: PackedByteArray = actual.get("blocks", original)
	if a.size() != b.size():
		printerr(label, " invalid length: ", a.size(), " vs ", b.size())
		return 1000000
	var diff: int = 0
	for i in range(a.size()):
		if a[i] != b[i]:
			diff += 1
	print(label, " -> ", diff, " different voxels; changed ", reference.get("changed", false),
		" / ", actual.get("changed", false))
	return diff

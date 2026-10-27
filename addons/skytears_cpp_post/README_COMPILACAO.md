# SkyTears C++ geology + Noise3D postprocess (optional)

This package intentionally ships **source, not an unverified binary**. The main
SkyTears project runs with the original GDScript implementation until both the
matching DLL/SO and `skytears_cpp_post.gdextension` have been installed.

## Build via GitHub Actions (Windows x64 + Linux x64)

1. Upload the entire `SkyTears/` folder to a GitHub repository, at repo root.
2. Actions -> `Build SkyTears C++ geology and Noise3D` -> Run workflow.
3. Download the `SkyTears-NativePost-windows` or `...-linux` build artifact.
4. Extract the `bin/` binaries into `SkyTears/addons/skytears_cpp_post/bin/`.
5. Rename `skytears_cpp_post.gdextension.disabled` to `skytears_cpp_post.gdextension`
   (or copy the one packaged in the artifact). **Only activate it after the matching
   binaries are installed**. On Proton, use the Windows `.dll`, not Linux `.so`.
6. Reopen Godot / re-export SkyTears; check console for `C++ geology + mountain postprocess ligado`.

To build locally instead, install Python, SCons, and a C++ compiler. Within
`addons/skytears_cpp_post`:

    git clone --depth 1 --branch 4.5 https://github.com/godotengine/godot-cpp.git
    scons platform=linux target=template_release -j2
    scons platform=linux target=template_debug -j2

Replace `platform=linux` with `platform=windows` on a Windows build host.

## World/save protection
- Remains 32x32 horizontally and 512 tall; **no LOD**.
- Geology executes after Noise3D and before player edits, as before.
- The original .gd code is retained as automatic fallback without DLL.
- The implementation uses Godot's `FastNoiseLite` through GDExtension to track
  original noise semantics. Platform float precision may still yield slight
  terrain differences on threshold boundaries. Create a test world and compare
  with a fixed seed before enabling it on valuable saves.
- Disable/rename the .gdextension to return to GDScript; do not mix versions
  in the same unbacked-up save.

## Verification limits
Compiled libraries and Godot runtime were unavailable in the creation
container, so this is **source-level experimental** until CI builds and
Godot runtime tests are green. It is not a demonstrated FPS improvement yet.

## Parity check (recommended before testing existing worlds)

Once you have installed the matching binaries and activated the extension, run:

    godot --headless --path . --script res://addons/skytears_cpp_post/tests/parity.gd

The test compares complete native / GDScript chunk buffers for fixed seeds.
Zero differences means these synthetic samples match; it is not proof that
all saved worlds are bit-identical, so keep backups.

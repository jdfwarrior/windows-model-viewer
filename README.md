# Model Viewer

A native desktop viewer for inspecting 3D models on macOS and Windows. Open a file with the native picker, drag it into the window, or pass it to the executable.

## Download

Get a ZIP from [GitHub Releases](../../releases). macOS has one universal build for Apple silicon and Intel. Windows has separate x64 and ARM64 builds. Extract the archive before opening the app. Each release includes SHA-256 checksums.

The macOS bundle is ad hoc signed and has no Apple notarization. Windows executables have no Authenticode signature.

| Format | Support |
| --- | --- |
| `.gltf`, `.glb` | glTF 2.0. Keep external buffers and images in their original relative locations. |
| `.fbx` | Binary and ASCII FBX. Embedded and local base-color images are supported. |
| `.3ds` | Legacy 3D Studio meshes and local diffuse textures. |
| `.blend` | Opens through a separately installed Blender with a glTF exporter. The viewer loads a temporary GLB and removes it after import. |
| `.max` | Export the scene as FBX in Autodesk 3ds Max, then open the FBX. |

## Build on macOS

Install the Xcode Command Line Tools (`xcode-select --install`) and CMake 3.24 or later. The first build fetches checksum-pinned dependencies.

```sh
./script/build_and_run.sh
./script/build_and_run.sh --universal --build-only
```

The first command builds and opens `dist/ModelViewer.app`. Use `--build-only` to skip launching, `--verify` to check that the app is running, and `--universal` to build for both Apple silicon and Intel. Append a model path to open it at launch. The deployment target is macOS 11. The bundle is ad hoc signed for local use.

## Build on Windows

Install Visual Studio 2022 or its Build Tools with **Desktop development with C++**, a Windows SDK, and CMake 3.24 or later on PATH. Install the ARM64 C++ build tools if targeting ARM64.

```powershell
.\script\build-windows.ps1
.\script\build-windows.ps1 -Run -Model "C:\Models\sample.glb"
.\script\build-windows.ps1 -Architecture ARM64
```

The release executable is written to `dist/windows-x64/ModelViewer.exe` or `dist/windows-ARM64/ModelViewer.exe`. Use a graphics driver that provides OpenGL 3.3 or later.

## Inspect a model

- Drag to orbit. Shift-drag or right-drag to pan. Scroll to zoom.
- **F** frames the model. **P** switches perspective and orthographic views. **G** toggles the grid, **W** toggles wireframe, and **I** toggles the inspector.
- The inspector shows file information, bounds, geometry counts, the scene hierarchy, and material details.
- Open **Components** to show or hide nodes and mesh parts. Hiding a parent hides its descendants and retains their individual settings. **Show all** restores every part, and selecting a row highlights its geometry.
- **Command+O** on macOS or **Ctrl+O** on Windows opens the native file picker.

The viewer previews static geometry and reports available animation and skin counts. Shading includes vertex colors, base-color and emissive maps, and glTF alpha modes. Animation playback, skin and morph deformation, and advanced material maps such as normal maps are outside the current preview. glTF files that require unsupported extensions, including Draco and Meshopt compression, show a format-specific error.

For `.blend`, the viewer checks the standard Blender installation location and then PATH. Set `MODEL_VIEWER_BLENDER` to the Blender executable for a custom installation. Blender runs in the background with automatic script execution disabled. Blender imports can take longer because they export the scene first. Blender previews export static geometry, so their animation counts are omitted.

The release build links its viewer libraries statically. File parsing and image decoding run on a worker thread, mesh buffers and textures are uploaded once, and the viewport sleeps between interactions.

## Publish a release

Update the version in `CMakeLists.txt`, commit it, and push a matching tag such as `v1.0.0`. GitHub Actions builds the macOS universal and both Windows packages. It publishes a GitHub release after all three builds succeed.

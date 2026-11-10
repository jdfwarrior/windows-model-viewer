#include "model.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <random>
#include <stdexcept>

namespace viewer {
namespace {

std::string blenderExecutable()
{
    if (const char* configured = SDL_getenv("MODEL_VIEWER_BLENDER"); configured && *configured) return configured;
    std::error_code error;
#ifdef __APPLE__
    const auto app = std::filesystem::path("/Applications/Blender.app/Contents/MacOS/Blender");
    if (std::filesystem::is_regular_file(app, error)) return app.u8string();
#elif defined(_WIN32)
    if (const auto* programFiles = SDL_getenv("ProgramFiles")) {
        const auto root = std::filesystem::u8path(programFiles) / "Blender Foundation";
        std::filesystem::path latest;
        for (auto dir = std::filesystem::directory_iterator(root, error);
             !error && dir != std::filesystem::directory_iterator(); dir.increment(error)) {
            const auto candidate = dir->path() / "blender.exe";
            std::error_code fileError;
            if (candidate > latest && std::filesystem::is_regular_file(candidate, fileError)) latest = candidate;
        }
        if (!latest.empty()) return latest.u8string();
    }
#endif
    return "blender";
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto base = std::filesystem::temp_directory_path();
        std::random_device random;
        for (int attempt = 0; attempt < 8; ++attempt) {
            const auto candidate = base / ("model-viewer-" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) {
                path = candidate;
                return;
            }
        }
        throw std::runtime_error("A temporary folder could not be created for Blender.");
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    std::filesystem::path path;
};

Model loadBlend(const std::filesystem::path& path, const std::atomic_bool& canceled)
{
    const auto start = std::chrono::steady_clock::now();
    const auto source = std::filesystem::absolute(path);
    std::error_code error;
    const auto size = std::filesystem::file_size(source, error);
    if (error) throw std::runtime_error("The Blender file could not be opened.");

    const TemporaryDirectory temp;
    const auto output = temp.path / "scene.glb";
    const auto blender = blenderExecutable();
    const auto inputName = source.u8string();
    const auto outputName = output.u8string();
    constexpr auto script =
        "import bpy, sys; "
        "result = bpy.ops.export_scene.gltf(filepath=sys.argv[-1], export_format='GLB', export_animations=False); "
        "assert 'FINISHED' in result";
    const char* arguments[] = {blender.c_str(), "--background", "--factory-startup", "--disable-autoexec",
                               inputName.c_str(), "--python-exit-code", "1", "--python-expr", script,
                               "--", outputName.c_str(), nullptr};
    const std::unique_ptr<SDL_Process, decltype(&SDL_DestroyProcess)> process(
        SDL_CreateProcess(arguments, false), &SDL_DestroyProcess);
    if (!process) {
        throw std::runtime_error("Install Blender to open .blend files, or set MODEL_VIEWER_BLENDER to its executable.");
    }
    int exitCode = 0;
    while (!SDL_WaitProcess(process.get(), false, &exitCode)) {
        if (canceled.load()) {
            SDL_KillProcess(process.get(), true);
            SDL_WaitProcess(process.get(), true, nullptr);
            throw std::runtime_error("Blender import canceled.");
        }
        SDL_Delay(30);
    }
    if (exitCode != 0 || !std::filesystem::is_regular_file(output)) {
        throw std::runtime_error("Blender could not export this scene to GLB.");
    }
    auto model = loadModel(output);
    model.path = source;
    model.format = "Blender via GLB";
    model.fileSize = size;
    model.loadMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    return model;
}

} // namespace

Model loadDesktopModel(const std::filesystem::path& path, const std::atomic_bool& canceled)
{
    auto extension = path.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension == ".blend") return loadBlend(path, canceled);
    return loadModel(path);
}

} // namespace viewer

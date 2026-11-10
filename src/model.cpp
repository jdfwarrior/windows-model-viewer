#include "model.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 16384
#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace viewer {

void Bounds::include(glm::vec3 point)
{
    if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z)) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }
}

void Bounds::include(const Bounds& local, const glm::mat4& transform)
{
    if (!local.valid()) return;
    for (int corner = 0; corner < 8; ++corner) {
        glm::vec3 point{
            corner & 1 ? local.max.x : local.min.x,
            corner & 2 ? local.max.y : local.min.y,
            corner & 4 ? local.max.z : local.min.z
        };
        include(glm::vec3(transform * glm::vec4(point, 1.0f)));
    }
}

bool Bounds::valid() const
{
    return glm::all(glm::lessThanEqual(min, max));
}

bool Sampler::operator==(const Sampler& other) const
{
    return minFilter == other.minFilter && magFilter == other.magFilter &&
           wrapS == other.wrapS && wrapT == other.wrapT;
}

void FreePixels::operator()(unsigned char* data) const
{
    stbi_image_free(data);
}

std::size_t triangleCount(Topology topology, std::size_t indexCount)
{
    switch (topology) {
        case Topology::Triangles: return indexCount / 3;
        case Topology::TriangleStrip:
        case Topology::TriangleFan: return indexCount >= 3 ? indexCount - 2 : 0;
        default: return 0;
    }
}

std::string named(const char* name, const char* fallback, std::size_t index)
{
    return name && *name ? name : std::string(fallback) + " " + std::to_string(index + 1);
}

std::optional<std::filesystem::path> localAsset(const std::filesystem::path& model, std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos ||
        name.find('\0') != std::string::npos) return std::nullopt;
    return model.parent_path() / std::filesystem::u8path(name);
}

std::size_t addImage(Model& model, std::string name, const void* bytes, std::size_t size)
{
    Image image;
    image.name = std::move(name);
    int channels = 0;
    if (bytes && size && size <= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        const auto* data = static_cast<const unsigned char*>(bytes);
        image.pixels.reset(stbi_load_from_memory(data, static_cast<int>(size), &image.width,
                                                 &image.height, &channels, 4));
    }
    if (!image.pixels) {
        ++model.textureWarnings;
        return none;
    }
    model.images.push_back(std::move(image));
    return model.images.size() - 1;
}

std::size_t addImage(Model& model, const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size <= 0 || size > std::numeric_limits<int>::max()) {
        ++model.textureWarnings;
        return none;
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        ++model.textureWarnings;
        return none;
    }
    return addImage(model, path.filename().u8string(), bytes.data(), bytes.size());
}

Model loadModel(const std::filesystem::path& path)
{
    const auto start = std::chrono::steady_clock::now();
    Model model;
    model.path = std::filesystem::absolute(path);
    std::error_code error;
    model.fileSize = std::filesystem::file_size(model.path, error);
    if (error) throw std::runtime_error("The model file could not be opened.");

    auto extension = path.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension == ".glb" || extension == ".gltf") {
        loadGltf(model);
    } else if (extension == ".fbx") {
        loadFbx(model);
    } else if (extension == ".3ds") {
        load3ds(model);
    } else if (extension == ".blend") {
        throw std::runtime_error("Export this Blender scene to GLB or FBX, then open the exported file.");
    } else if (extension == ".max") {
        throw std::runtime_error("Open this scene in Autodesk 3ds Max and export it as FBX.");
    } else {
        throw std::runtime_error("Choose a .gltf, .glb, .fbx, or .3ds model.");
    }

    for (const auto& geometry : model.geometry) model.vertices += geometry.vertices.size();
    for (const auto& draw : model.draws) {
        const auto& geometry = model.geometry[draw.geometry];
        model.bounds.include(geometry.bounds, draw.transform);
        model.triangles += triangleCount(geometry.topology, draw.indexCount);
    }
    if (!model.bounds.valid() || model.draws.empty()) {
        throw std::runtime_error("This scene contains no renderable geometry.");
    }
    model.loadMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    return model;
}

} // namespace viewer

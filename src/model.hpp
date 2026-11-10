#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace viewer {

inline constexpr std::size_t none = std::numeric_limits<std::size_t>::max();

struct Bounds {
    glm::vec3 min{std::numeric_limits<float>::infinity()};
    glm::vec3 max{-std::numeric_limits<float>::infinity()};

    void include(glm::vec3 point);
    void include(const Bounds& local, const glm::mat4& transform);
    glm::vec3 center() const { return min + (max - min) * 0.5f; }
    glm::vec3 size() const { return max - min; }
    bool valid() const;
};

struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 uv{};
    glm::vec2 emissiveUv{};
    glm::vec4 color{1.0f};
};

enum class Topology { Points, Lines, LineLoop, LineStrip, Triangles, TriangleStrip, TriangleFan };
enum class AlphaMode { Opaque, Mask, Blend };

struct Geometry {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    Bounds bounds;
    Topology topology = Topology::Triangles;
    bool hasNormals = false;
    std::size_t vertexCount = 0;
};

struct FreePixels {
    void operator()(unsigned char* data) const;
};

struct Image {
    std::string name;
    int width = 0;
    int height = 0;
    std::unique_ptr<unsigned char, FreePixels> pixels;
};

struct Sampler {
    int minFilter = 9987; // Linear with trilinear mipmaps.
    int magFilter = 9729;
    int wrapS = 10497;
    int wrapT = 10497;

    bool operator==(const Sampler& other) const;
};

struct Material {
    std::string name;
    glm::vec4 color{1.0f};
    glm::vec3 emission{};
    float metallic = 0.0f;
    float roughness = 0.7f;
    float cutoff = 0.5f;
    std::size_t image = none;
    Sampler sampler;
    std::size_t emissiveImage = none;
    Sampler emissiveSampler;
    AlphaMode alpha = AlphaMode::Opaque;
    bool doubleSided = false;
    bool unlit = false;
};

struct Draw {
    std::size_t geometry;
    std::size_t material;
    std::size_t firstIndex;
    std::size_t indexCount;
    glm::mat4 transform;
    std::size_t node;
};

struct Node {
    std::string name;
    std::size_t parent = none;
    std::size_t depth = 0;
    std::size_t firstDraw = 0;
    std::size_t drawCount = 0;
    glm::mat4 transform{1.0f};
};

struct Model {
    std::filesystem::path path;
    std::string format;
    std::string sceneName;
    std::string creator;
    std::string units = "m";
    std::uintmax_t fileSize = 0;
    std::size_t meshes = 0;
    std::size_t animations = 0;
    std::size_t skins = 0;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    std::size_t textureWarnings = 0;
    double loadMilliseconds = 0;
    Bounds bounds;
    std::vector<Geometry> geometry;
    std::vector<Image> images;
    std::vector<Material> materials;
    std::vector<Draw> draws;
    std::vector<Node> nodes;
};

std::size_t triangleCount(Topology topology, std::size_t indexCount);
std::string named(const char* name, const char* fallback, std::size_t index);
std::optional<std::filesystem::path> localAsset(const std::filesystem::path& model, std::string name);
std::size_t addImage(Model& model, std::string name, const void* bytes, std::size_t size);
std::size_t addImage(Model& model, const std::filesystem::path& path);
Model loadModel(const std::filesystem::path& path);
void loadGltf(Model& model);
void loadFbx(Model& model);
void load3ds(Model& model);

} // namespace viewer

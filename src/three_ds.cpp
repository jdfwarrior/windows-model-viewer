#include "model.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace viewer {
namespace {

glm::vec3 vector(aiVector3D value)
{
    return {value.x, value.y, value.z};
}

glm::mat4 matrix(const aiMatrix4x4& value)
{
    return {{value.a1, value.b1, value.c1, value.d1},
            {value.a2, value.b2, value.c2, value.d2},
            {value.a3, value.b3, value.c3, value.d3},
            {value.a4, value.b4, value.c4, value.d4}};
}

Geometry importMesh(const aiMesh& mesh, std::size_t index)
{
    Geometry geometry;
    geometry.name = named(mesh.mName.C_Str(), "Mesh", index);
    geometry.hasNormals = mesh.HasNormals();
    geometry.vertices.resize(mesh.mNumVertices);
    geometry.indices.reserve(static_cast<std::size_t>(mesh.mNumFaces) * 3);
    for (std::size_t i = 0; i < mesh.mNumVertices; ++i) {
        auto& vertex = geometry.vertices[i];
        vertex.position = vector(mesh.mVertices[i]);
        geometry.bounds.include(vertex.position);
        if (geometry.hasNormals) vertex.normal = vector(mesh.mNormals[i]);
        if (mesh.HasTextureCoords(0)) {
            const auto uv = mesh.mTextureCoords[0][i];
            vertex.uv = {uv.x, 1.0f - uv.y};
        }
        if (mesh.HasVertexColors(0)) {
            const auto color = mesh.mColors[0][i];
            vertex.color = {color.r, color.g, color.b, color.a};
        }
    }
    for (std::size_t i = 0; i < mesh.mNumFaces; ++i) {
        const auto& face = mesh.mFaces[i];
        if (face.mNumIndices == 3) {
            geometry.indices.insert(geometry.indices.end(), face.mIndices, face.mIndices + 3);
        }
    }
    return geometry;
}

Material importMaterial(Model& model, const aiMaterial& source, std::size_t index,
                        std::unordered_map<std::string, std::size_t>& images, bool used)
{
    Material material;
    aiString name;
    source.Get(AI_MATKEY_NAME, name);
    material.name = named(name.C_Str(), "Material", index);
    aiColor3D diffuse{0.65f, 0.65f, 0.65f};
    source.Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
    material.color = {diffuse.r, diffuse.g, diffuse.b, 1.0f};
    source.Get(AI_MATKEY_OPACITY, material.color.a);
    if (material.color.a < 0.999f) material.alpha = AlphaMode::Blend;
    int twoSided = 0;
    source.Get(AI_MATKEY_TWOSIDED, twoSided);
    material.doubleSided = twoSided != 0;

    aiString texture;
    aiTextureMapMode wrap[] = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
    if (used && source.GetTexture(aiTextureType_DIFFUSE, 0, &texture, nullptr, nullptr, nullptr,
                                  nullptr, wrap) == AI_SUCCESS) {
        const auto [image, inserted] = images.try_emplace(texture.C_Str(), none);
        if (inserted) {
            if (const auto path = localAsset(model.path, image->first)) image->second = addImage(model, *path);
            else ++model.textureWarnings;
        }
        material.image = image->second;
        const auto wrapMode = [](aiTextureMapMode mode) {
            if (mode == aiTextureMapMode_Mirror) return 33648;
            if (mode == aiTextureMapMode_Clamp || mode == aiTextureMapMode_Decal) return 33071;
            return 10497;
        };
        material.sampler.wrapS = wrapMode(wrap[0]);
        material.sampler.wrapT = wrapMode(wrap[1]);
    }
    return material;
}

} // namespace

void load3ds(Model& model)
{
    // Read via filesystem::path so Windows paths work independently of the current code page.
    std::ifstream file(model.path, std::ios::binary | std::ios::ate);
    const auto length = file.tellg();
    if (length <= 0 || static_cast<std::uintmax_t>(length) > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("The 3DS file could not be read.");
    }
    std::vector<char> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(bytes.data(), length)) throw std::runtime_error("The 3DS file could not be read.");

    Assimp::Importer importer;
    const auto* scene = importer.ReadFileFromMemory(bytes.data(), bytes.size(), aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices | aiProcess_GenNormals | aiProcess_ValidateDataStructure, "3ds");
    if (!scene || !scene->mRootNode) throw std::runtime_error(std::string("3DS import failed: ") + importer.GetErrorString());
    std::vector<char>().swap(bytes);

    model.format = "3D Studio (3DS)";
    model.sceneName = "3DS scene";
    model.units = "scene units";
    model.animations = scene->mNumAnimations;
    model.meshes = scene->mNumMeshes;
    model.geometry.reserve(scene->mNumMeshes);
    for (std::size_t i = 0; i < scene->mNumMeshes; ++i) {
        model.geometry.push_back(importMesh(*scene->mMeshes[i], i));
    }

    struct PendingNode {
        const aiNode* source;
        std::size_t parent;
    };
    std::vector<PendingNode> pending{{scene->mRootNode, none}};
    std::vector<bool> used(scene->mNumMaterials);
    while (!pending.empty()) {
        const auto [source, parent] = pending.back();
        pending.pop_back();
        const auto nodeIndex = model.nodes.size();
        Node node;
        node.name = named(source->mName.C_Str(), "Node", nodeIndex);
        node.parent = parent;
        node.depth = parent == none ? 0 : model.nodes[parent].depth + 1;
        node.firstDraw = model.draws.size();
        const auto local = matrix(source->mTransformation);
        node.transform = parent == none ? local : model.nodes[parent].transform * local;
        for (std::size_t i = 0; i < source->mNumMeshes; ++i) {
            const auto mesh = source->mMeshes[i];
            const auto indexCount = model.geometry[mesh].indices.size();
            if (!indexCount) continue;
            const auto material = scene->mMeshes[mesh]->mMaterialIndex;
            used[material] = true;
            model.draws.push_back({mesh, material, 0, indexCount, node.transform, nodeIndex});
        }
        node.drawCount = model.draws.size() - node.firstDraw;
        model.nodes.push_back(std::move(node));
        for (std::size_t i = source->mNumChildren; i > 0; --i) pending.push_back({source->mChildren[i - 1], nodeIndex});
    }

    std::unordered_map<std::string, std::size_t> images;
    for (std::size_t i = 0; i < scene->mNumMaterials; ++i) {
        model.materials.push_back(importMaterial(model, *scene->mMaterials[i], i, images, used[i]));
    }
}

} // namespace viewer

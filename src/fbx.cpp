#include "model.hpp"

#include <ufbx.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace viewer {
namespace {

std::string string(ufbx_string value)
{
    return std::string(value.data, value.length);
}

glm::vec3 vector(ufbx_vec3 value)
{
    return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
}

glm::mat4 matrix(const ufbx_matrix& value)
{
    return {glm::vec4(vector(value.cols[0]), 0.0f), glm::vec4(vector(value.cols[1]), 0.0f),
            glm::vec4(vector(value.cols[2]), 0.0f), glm::vec4(vector(value.cols[3]), 1.0f)};
}

std::size_t loadTexture(Model& model, const ufbx_texture& texture)
{
    if (texture.content.size) return addImage(model, string(texture.name), texture.content.data, texture.content.size);

    const auto relative = localAsset(model.path, string(texture.relative_filename));
    std::error_code error;
    if (relative && std::filesystem::is_regular_file(*relative, error)) return addImage(model, *relative);

    auto original = string(texture.filename);
    std::replace(original.begin(), original.end(), '\\', '/');
    const auto filename = std::filesystem::u8path(original).filename().u8string();
    if (auto sibling = localAsset(model.path, filename)) return addImage(model, *sibling);
    ++model.textureWarnings;
    return none;
}

Material importMaterial(const ufbx_material& source, std::vector<std::optional<std::size_t>>& images,
                        Model& model, bool used)
{
    Material material;
    const auto& pbr = source.pbr;
    material.name = named(source.name.data, "Material", source.typed_id);
    material.color = glm::vec4(vector(pbr.base_color.value_vec3) * static_cast<float>(pbr.base_factor.value_real), 1.0f);
    material.metallic = static_cast<float>(pbr.metalness.value_real);
    if (pbr.roughness.has_value) material.roughness = static_cast<float>(pbr.roughness.value_real);
    material.emission = vector(pbr.emission_color.value_vec3) * static_cast<float>(pbr.emission_factor.value_real);
    material.doubleSided = !source.features.double_sided.is_explicit || source.features.double_sided.enabled;
    material.unlit = source.features.unlit.enabled;
    if (pbr.opacity.has_value) material.color.a = static_cast<float>(pbr.opacity.value_real);
    if (material.color.a < 0.999f || pbr.opacity.texture) material.alpha = AlphaMode::Blend;

    const auto importTexture = [&](const ufbx_material_map& map, Sampler& sampler) {
        const auto* texture = map.texture;
        if (!used || !texture || !map.texture_enabled) return none;
        if (texture->file_textures.count) texture = texture->file_textures.data[0];
        if (texture->type == UFBX_TEXTURE_FILE) {
            auto& image = images[texture->typed_id];
            if (!image) image = loadTexture(model, *texture);
            sampler.wrapS = texture->wrap_u == UFBX_WRAP_CLAMP ? 33071 : 10497;
            sampler.wrapT = texture->wrap_v == UFBX_WRAP_CLAMP ? 33071 : 10497;
            return *image;
        }
        ++model.textureWarnings;
        return none;
    };
    material.image = importTexture(pbr.base_color, material.sampler);
    material.emissiveImage = importTexture(pbr.emission_color, material.emissiveSampler);
    if (pbr.emission_color.texture && pbr.emission_color.texture_enabled && material.emissiveImage == none) {
        material.emission = glm::vec3(0.0f);
    }
    return material;
}

struct Section {
    std::size_t materialSlot;
    std::size_t first;
    std::size_t count;
};

struct ImportedMesh {
    std::size_t geometry;
    std::vector<Section> sections;
};

ImportedMesh importMesh(Model& model, const ufbx_mesh& source)
{
    ImportedMesh result{none, {}};
    if (!source.num_triangles || !source.vertex_position.exists) return result;

    Geometry geometry;
    geometry.name = named(source.name.data, "Mesh", source.typed_id);
    geometry.hasNormals = source.vertex_normal.exists;
    geometry.vertices.resize(source.num_indices);
    for (std::size_t i = 0; i < geometry.vertices.size(); ++i) {
        auto& vertex = geometry.vertices[i];
        vertex.position = vector(ufbx_get_vertex_vec3(&source.vertex_position, i));
        geometry.bounds.include(vertex.position);
        if (geometry.hasNormals) vertex.normal = vector(ufbx_get_vertex_vec3(&source.vertex_normal, i));
        if (source.vertex_uv.exists) {
            const auto uv = ufbx_get_vertex_vec2(&source.vertex_uv, i);
            vertex.uv = {static_cast<float>(uv.x), 1.0f - static_cast<float>(uv.y)};
            vertex.emissiveUv = vertex.uv;
        }
        if (source.vertex_color.exists) {
            const auto color = ufbx_get_vertex_vec4(&source.vertex_color, i);
            vertex.color = {static_cast<float>(color.x), static_cast<float>(color.y),
                            static_cast<float>(color.z), static_cast<float>(color.w)};
        }
    }

    geometry.indices.reserve(source.num_triangles * 3);
    std::vector<std::uint32_t> triangle(source.max_face_triangles * 3);
    for (const auto& part : source.material_parts) {
        const auto first = geometry.indices.size();
        for (const auto faceIndex : part.face_indices) {
            const auto face = source.faces.data[faceIndex];
            if (face.num_indices < 3 || (source.face_hole.count && source.face_hole.data[faceIndex])) continue;
            const auto count = ufbx_triangulate_face(triangle.data(), triangle.size(), &source, face);
            geometry.indices.insert(geometry.indices.end(), triangle.begin(), triangle.begin() + count * 3);
        }
        if (geometry.indices.size() > first) {
            result.sections.push_back({part.index, first, geometry.indices.size() - first});
        }
    }

    if (geometry.indices.empty()) return result;
    // FBX stores polygon corners separately. Merge identical corners and retain hard edges and UV seams.
    const ufbx_vertex_stream stream{geometry.vertices.data(), geometry.vertices.size(), sizeof(Vertex)};
    std::vector<std::uint32_t> remap(geometry.vertices.size());
    const auto vertices = ufbx_generate_indices(&stream, 1, remap.data(), remap.size(), nullptr, nullptr);
    if (vertices) {
        geometry.vertices.resize(vertices);
        for (auto& index : geometry.indices) index = remap[index];
    }
    result.geometry = model.geometry.size();
    model.geometry.push_back(std::move(geometry));
    return result;
}

} // namespace

void loadFbx(Model& model)
{
    ufbx_load_opts options{};
    options.file_format = UFBX_FILE_FORMAT_FBX;
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0;
    options.generate_missing_normals = true;
    options.use_blender_pbr_material = true;
    const auto path = model.path.u8string();
    options.filename = {path.c_str(), path.size()};

#ifdef _WIN32
    const std::unique_ptr<std::FILE, decltype(&std::fclose)> file(_wfopen(model.path.c_str(), L"rb"), &std::fclose);
#else
    const std::unique_ptr<std::FILE, decltype(&std::fclose)> file(std::fopen(model.path.c_str(), "rb"), &std::fclose);
#endif
    if (!file) throw std::runtime_error("The FBX file could not be opened.");
    ufbx_error error{};
    const std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> scene(
        ufbx_load_stdio(file.get(), &options, &error), &ufbx_free_scene);
    if (!scene) throw std::runtime_error("FBX import failed: " + string(error.description));

    model.format = std::string(scene->metadata.ascii ? "FBX ASCII " : "FBX binary ") +
                   std::to_string(scene->metadata.version);
    model.sceneName = "FBX scene";
    model.creator = string(scene->metadata.creator);
    model.animations = scene->anim_stacks.count;
    model.skins = scene->skin_deformers.count;

    std::vector<std::optional<ImportedMesh>> meshes(scene->meshes.count);
    std::vector<bool> used(scene->materials.count);
    struct PendingNode {
        const ufbx_node* source;
        std::size_t parent;
    };
    std::vector<PendingNode> pending{{scene->root_node, none}};
    while (!pending.empty()) {
        const auto [source, parent] = pending.back();
        pending.pop_back();
        const auto nodeIndex = model.nodes.size();
        Node node;
        node.name = source->is_root ? "Scene root" : named(source->name.data, "Node", nodeIndex);
        node.parent = parent;
        node.firstDraw = model.draws.size();
        node.depth = parent == none ? 0 : model.nodes[parent].depth + 1;
        node.transform = matrix(source->node_to_world);
        if (source->mesh && source->visible) {
            auto& mesh = meshes[source->mesh->typed_id];
            if (!mesh) {
                ++model.meshes;
                mesh = importMesh(model, *source->mesh);
            }
            for (const auto& section : mesh->sections) {
                const auto* material = section.materialSlot < source->materials.count ?
                                       source->materials.data[section.materialSlot] : nullptr;
                if (material) used[material->typed_id] = true;
                model.draws.push_back({mesh->geometry, material ? material->typed_id : none, section.first,
                                       section.count, matrix(source->geometry_to_world), nodeIndex});
            }
        }
        node.drawCount = model.draws.size() - node.firstDraw;
        model.nodes.push_back(std::move(node));
        for (std::size_t i = source->children.count; i > 0; --i) {
            pending.push_back({source->children.data[i - 1], nodeIndex});
        }
    }

    std::vector<std::optional<std::size_t>> images(scene->textures.count);
    for (const auto* material : scene->materials) {
        model.materials.push_back(importMaterial(*material, images, model, used[material->typed_id]));
    }
}

} // namespace viewer

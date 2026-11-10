#include "model.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <string_view>

namespace viewer {
namespace {

cgltf_result readFile(const cgltf_memory_options* memory, const cgltf_file_options*,
                      const char* path, cgltf_size* size, void** data)
{
    try {
        std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
        const auto length = file.tellg();
        if (length < 0) return cgltf_result_file_not_found;
        const auto bytes = *size ? *size : static_cast<cgltf_size>(length);
        if (bytes > static_cast<cgltf_size>(length)) return cgltf_result_data_too_short;
        void* buffer = memory->alloc_func(memory->user_data, bytes ? bytes : 1);
        if (!buffer) return cgltf_result_out_of_memory;
        file.seekg(0);
        if (!file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(bytes))) {
            memory->free_func(memory->user_data, buffer);
            return cgltf_result_io_error;
        }
        *size = bytes;
        *data = buffer;
        return cgltf_result_success;
    } catch (...) {
        return cgltf_result_io_error;
    }
}

std::string decodeUri(std::string uri)
{
    uri.resize(cgltf_decode_uri(uri.data()));
    return uri;
}

std::size_t loadImage(Model& model, const cgltf_options& options, const cgltf_image& image)
{
    const std::string name = image.name ? image.name : "Embedded image";
    if (image.buffer_view) {
        return addImage(model, name, cgltf_buffer_view_data(image.buffer_view), image.buffer_view->size);
    }
    const std::string_view uri = image.uri ? image.uri : "";
    if (uri.substr(0, 5) == "data:") {
        const auto comma = uri.find(',');
        if (comma != uri.npos && comma >= 7 && uri.substr(comma - 7, 7) == ";base64") {
            const auto encoded = uri.substr(comma + 1);
            if (encoded.size() >= 4 && encoded.size() % 4 == 0) {
                auto size = encoded.size() / 4 * 3;
                if (encoded.back() == '=') --size;
                if (encoded[encoded.size() - 2] == '=') --size;
                void* bytes = nullptr;
                if (cgltf_load_buffer_base64(&options, size, encoded.data(), &bytes) == cgltf_result_success) {
                    const std::unique_ptr<void, decltype(&std::free)> owned(bytes, &std::free);
                    return addImage(model, name, bytes, size);
                }
            }
        }
    } else if (auto path = localAsset(model.path, decodeUri(std::string(uri)))) {
        return addImage(model, *path);
    }
    ++model.textureWarnings;
    return none;
}

const cgltf_texture_view& baseTexture(const cgltf_material& material)
{
    if (material.has_pbr_specular_glossiness) return material.pbr_specular_glossiness.diffuse_texture;
    return material.pbr_metallic_roughness.base_color_texture;
}

Sampler importSampler(const cgltf_texture_view& view)
{
    Sampler result;
    if (view.texture && view.texture->sampler) {
        const auto& sampler = *view.texture->sampler;
        if (sampler.min_filter) result.minFilter = sampler.min_filter;
        if (sampler.mag_filter) result.magFilter = sampler.mag_filter;
        result.wrapS = sampler.wrap_s;
        result.wrapT = sampler.wrap_t;
    }
    return result;
}

Material importMaterial(const cgltf_material& source, std::size_t index)
{
    Material material;
    material.name = named(source.name, "Material", index);
    material.color = glm::make_vec4(source.pbr_metallic_roughness.base_color_factor);
    material.metallic = source.pbr_metallic_roughness.metallic_factor;
    material.roughness = source.pbr_metallic_roughness.roughness_factor;
    if (source.has_pbr_specular_glossiness) {
        material.color = glm::make_vec4(source.pbr_specular_glossiness.diffuse_factor);
        material.metallic = 0.0f;
        material.roughness = 1.0f - source.pbr_specular_glossiness.glossiness_factor;
    }
    material.emission = glm::make_vec3(source.emissive_factor);
    if (source.has_emissive_strength) material.emission *= source.emissive_strength.emissive_strength;
    material.doubleSided = source.double_sided;
    material.unlit = source.unlit;
    material.cutoff = source.alpha_cutoff;
    if (source.alpha_mode == cgltf_alpha_mode_mask) material.alpha = AlphaMode::Mask;
    if (source.alpha_mode == cgltf_alpha_mode_blend) material.alpha = AlphaMode::Blend;

    material.sampler = importSampler(baseTexture(source));
    material.emissiveSampler = importSampler(source.emissive_texture);
    return material;
}

cgltf_accessor sparseAccessor(const cgltf_accessor& original, bool indices)
{
    cgltf_accessor accessor{};
    const auto& sparse = original.sparse;
    accessor.buffer_view = indices ? sparse.indices_buffer_view : sparse.values_buffer_view;
    accessor.component_type = indices ? sparse.indices_component_type : original.component_type;
    accessor.type = indices ? cgltf_type_scalar : original.type;
    accessor.offset = indices ? sparse.indices_byte_offset : sparse.values_byte_offset;
    accessor.count = sparse.count;
    accessor.normalized = !indices && original.normalized;
    accessor.stride = cgltf_calc_size(accessor.type, accessor.component_type);
    return accessor;
}

void unpack(const cgltf_accessor& accessor, std::vector<float>& values)
{
    const auto components = cgltf_num_components(accessor.type);
    values.resize(accessor.count * components);
    auto base = accessor;
    base.is_sparse = false;
    if (cgltf_accessor_unpack_floats(&base, values.data(), values.size()) != values.size()) {
        throw std::runtime_error("A glTF vertex attribute could not be decoded.");
    }
    if (!accessor.is_sparse) return;

    const auto locations = sparseAccessor(accessor, true);
    const auto replacements = sparseAccessor(accessor, false);
    for (std::size_t i = 0; i < locations.count; ++i) {
        const auto index = cgltf_accessor_read_index(&locations, i);
        if (index >= accessor.count ||
            !cgltf_accessor_read_float(&replacements, i, values.data() + index * components, components)) {
            throw std::runtime_error("A sparse glTF vertex attribute is invalid.");
        }
    }
}

void loadIndices(Geometry& geometry, const cgltf_accessor* source)
{
    auto& indices = geometry.indices;
    if (!source) {
        indices.resize(geometry.vertices.size());
        std::iota(indices.begin(), indices.end(), 0u);
        return;
    }
    indices.resize(source->count);
    auto base = *source;
    base.is_sparse = false;
    if (base.buffer_view &&
        cgltf_accessor_unpack_indices(&base, indices.data(), sizeof(indices[0]), indices.size()) != indices.size()) {
        throw std::runtime_error("The glTF index buffer could not be decoded.");
    }
    if (source->is_sparse) {
        const auto locations = sparseAccessor(*source, true);
        const auto replacements = sparseAccessor(*source, false);
        for (std::size_t i = 0; i < locations.count; ++i) {
            const auto index = cgltf_accessor_read_index(&locations, i);
            if (index >= indices.size()) throw std::runtime_error("A sparse glTF index is out of range.");
            indices[index] = static_cast<std::uint32_t>(cgltf_accessor_read_index(&replacements, i));
        }
    }
    for (const auto index : indices) {
        if (index >= geometry.vertices.size()) throw std::runtime_error("A glTF mesh index is out of range.");
    }
}

void loadUv(Geometry& geometry, const cgltf_primitive& source, const cgltf_texture_view& view,
            glm::vec2 Vertex::*destination, std::vector<float>& values)
{
    if (!view.texture) return;
    const auto texcoord = view.has_transform && view.transform.has_texcoord ?
                          view.transform.texcoord : view.texcoord;
    const auto* uv = cgltf_find_accessor(&source, cgltf_attribute_type_texcoord, texcoord);
    if (!uv || uv->type != cgltf_type_vec2) return;

    unpack(*uv, values);
    const float c = std::cos(view.transform.rotation);
    const float s = std::sin(view.transform.rotation);
    const glm::mat2 rotation(c, s, -s, c);
    for (std::size_t i = 0; i < geometry.vertices.size(); ++i) {
        auto coordinate = glm::make_vec2(values.data() + i * 2);
        if (view.has_transform) {
            coordinate = glm::make_vec2(view.transform.offset) +
                         rotation * (glm::make_vec2(view.transform.scale) * coordinate);
        }
        geometry.vertices[i].*destination = coordinate;
    }
}

Geometry importPrimitive(const cgltf_primitive& source, std::string name)
{
    Geometry geometry;
    geometry.name = std::move(name);
    const auto* position = cgltf_find_accessor(&source, cgltf_attribute_type_position, 0);
    if (!position || !position->count) return geometry;
    if (position->type != cgltf_type_vec3 ||
        position->count > std::numeric_limits<std::uint32_t>::max() ||
        source.type < cgltf_primitive_type_points || source.type > cgltf_primitive_type_triangle_fan) {
        throw std::runtime_error("A glTF primitive uses an invalid vertex layout.");
    }
    geometry.topology = static_cast<Topology>(source.type - cgltf_primitive_type_points);
    geometry.vertices.resize(position->count);
    std::vector<float> values;
    unpack(*position, values);
    for (std::size_t i = 0; i < geometry.vertices.size(); ++i) {
        auto& vertex = geometry.vertices[i];
        vertex.position = glm::make_vec3(values.data() + i * 3);
        geometry.bounds.include(vertex.position);
    }

    const auto* normals = cgltf_find_accessor(&source, cgltf_attribute_type_normal, 0);
    if (normals && normals->type == cgltf_type_vec3) {
        unpack(*normals, values);
        for (std::size_t i = 0; i < geometry.vertices.size(); ++i) {
            geometry.vertices[i].normal = glm::make_vec3(values.data() + i * 3);
        }
        geometry.hasNormals = true;
    }
    const auto* colors = cgltf_find_accessor(&source, cgltf_attribute_type_color, 0);
    if (colors && (colors->type == cgltf_type_vec3 || colors->type == cgltf_type_vec4)) {
        const auto components = cgltf_num_components(colors->type);
        unpack(*colors, values);
        for (std::size_t i = 0; i < geometry.vertices.size(); ++i) {
            const auto* value = values.data() + i * components;
            geometry.vertices[i].color = {value[0], value[1], value[2], components == 4 ? value[3] : 1.0f};
        }
    }
    if (source.material) {
        loadUv(geometry, source, baseTexture(*source.material), &Vertex::uv, values);
        loadUv(geometry, source, source.material->emissive_texture, &Vertex::emissiveUv, values);
    }
    loadIndices(geometry, source.indices);
    return geometry;
}

void importScene(Model& model, const cgltf_data& data, const cgltf_scene* scene)
{
    struct PendingNode {
        const cgltf_node* source;
        std::size_t parent;
    };
    std::vector<PendingNode> pending;
    if (scene) {
        for (std::size_t i = scene->nodes_count; i > 0; --i) pending.push_back({scene->nodes[i - 1], none});
    } else {
        for (std::size_t i = data.nodes_count; i > 0; --i) {
            if (!data.nodes[i - 1].parent) pending.push_back({data.nodes + i - 1, none});
        }
    }

    std::vector<std::vector<std::size_t>> meshGeometry(data.meshes_count);
    while (!pending.empty()) {
        const auto [source, parent] = pending.back();
        pending.pop_back();
        glm::mat4 transform;
        cgltf_node_transform_local(source, glm::value_ptr(transform));
        const auto nodeIndex = model.nodes.size();
        const auto firstDraw = model.draws.size();
        Node node;
        node.name = named(source->name, "Node", nodeIndex);
        node.parent = parent;
        node.firstDraw = firstDraw;
        node.depth = parent == none ? 0 : model.nodes[parent].depth + 1;
        node.transform = parent == none ? transform : model.nodes[parent].transform * transform;

        if (source->mesh) {
            const auto meshIndex = cgltf_mesh_index(&data, source->mesh);
            auto& geometry = meshGeometry[meshIndex];
            if (geometry.empty()) {
                ++model.meshes;
                const auto name = named(source->mesh->name, "Mesh", meshIndex);
                for (std::size_t i = 0; i < source->mesh->primitives_count; ++i) {
                    const auto& primitive = source->mesh->primitives[i];
                    auto imported = importPrimitive(primitive, name);
                    geometry.push_back(imported.indices.empty() ? none : model.geometry.size());
                    if (!imported.indices.empty()) model.geometry.push_back(std::move(imported));
                }
            }
            for (std::size_t i = 0; i < geometry.size(); ++i) {
                if (geometry[i] == none) continue;
                const auto* material = source->mesh->primitives[i].material;
                model.draws.push_back({geometry[i], material ? cgltf_material_index(&data, material) : none,
                                       0, model.geometry[geometry[i]].indices.size(), node.transform, nodeIndex});
            }
        }
        node.drawCount = model.draws.size() - firstDraw;
        model.nodes.push_back(std::move(node));
        for (std::size_t i = source->children_count; i > 0; --i) {
            pending.push_back({source->children[i - 1], nodeIndex});
        }
    }
}

} // namespace

void loadGltf(Model& model)
{
    cgltf_options options{};
    options.file.read = readFile;
    options.memory.alloc_func = [](void*, cgltf_size size) { return std::malloc(size); };
    options.memory.free_func = [](void*, void* data) { std::free(data); };
    cgltf_data* raw = nullptr;
    const auto path = model.path.u8string();
    const auto result = cgltf_parse_file(&options, path.c_str(), &raw);
    if (result == cgltf_result_legacy_gltf) throw std::runtime_error("This viewer requires glTF 2.0.");
    if (result != cgltf_result_success) throw std::runtime_error("The glTF or GLB file could not be parsed.");
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, &cgltf_free);
    for (std::size_t i = 0; i < data->extensions_required_count; ++i) {
        const std::string_view extension = data->extensions_required[i];
        if (extension != "KHR_materials_unlit" && extension != "KHR_texture_transform" &&
            extension != "KHR_mesh_quantization" && extension != "KHR_materials_emissive_strength" &&
            extension != "KHR_materials_pbrSpecularGlossiness") {
            throw std::runtime_error("This glTF requires the unsupported " + std::string(extension) + " extension.");
        }
    }
    for (std::size_t i = 0; i < data->buffers_count; ++i) {
        const std::string_view uri = data->buffers[i].uri ? data->buffers[i].uri : "";
        if (!uri.empty() && uri.substr(0, 5) != "data:" && !localAsset(model.path, decodeUri(std::string(uri)))) {
            throw std::runtime_error("A glTF buffer points to an unsupported external location.");
        }
    }
    if (cgltf_load_buffers(&options, raw, path.c_str()) != cgltf_result_success) {
        throw std::runtime_error("The glTF geometry buffers could not be read. Check for companion .bin files.");
    }
    if (cgltf_validate(raw) != cgltf_result_success) {
        throw std::runtime_error("The glTF scene failed validation.");
    }
    const auto* scene = data->scene ? data->scene : (data->scenes_count ? data->scenes : nullptr);
    model.format = data->file_type == cgltf_file_type_glb ? "GLB 2.0" : "glTF 2.0";
    model.sceneName = scene && scene->name ? scene->name : "Default scene";
    if (data->asset.generator) model.creator = data->asset.generator;
    model.animations = data->animations_count;
    model.skins = data->skins_count;

    importScene(model, *data, scene);
    std::vector<bool> used(data->materials_count);
    for (const auto& draw : model.draws) {
        if (draw.material != none) used[draw.material] = true;
    }
    std::vector<std::optional<std::size_t>> images(data->images_count);
    const auto textureImage = [&](const cgltf_texture_view& view) {
        const auto* texture = view.texture;
        if (!texture) return none;
        if (!texture->image) {
            ++model.textureWarnings;
            return none;
        }
        auto& image = images[cgltf_image_index(raw, texture->image)];
        if (!image) image = loadImage(model, options, *texture->image);
        return *image;
    };
    for (std::size_t i = 0; i < data->materials_count; ++i) {
        const auto& source = data->materials[i];
        auto material = importMaterial(source, i);
        if (used[i]) {
            material.image = textureImage(baseTexture(source));
            material.emissiveImage = textureImage(source.emissive_texture);
            if (source.emissive_texture.texture && material.emissiveImage == none) {
                material.emission = glm::vec3(0.0f);
            }
        }
        model.materials.push_back(std::move(material));
    }
}

} // namespace viewer

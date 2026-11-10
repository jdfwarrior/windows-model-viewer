#include "renderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace viewer {
namespace {

constexpr float fov = glm::radians(42.0f);

template<typename T>
T glFunction(const char* name)
{
    auto fn = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    if (!fn) throw std::runtime_error(std::string("The graphics driver lacks ") + name + ".");
    return fn;
}

struct GL {
    PFNGLGENVERTEXARRAYSPROC genVertexArrays = glFunction<PFNGLGENVERTEXARRAYSPROC>("glGenVertexArrays");
    PFNGLDELETEVERTEXARRAYSPROC deleteVertexArrays = glFunction<PFNGLDELETEVERTEXARRAYSPROC>("glDeleteVertexArrays");
    PFNGLBINDVERTEXARRAYPROC bindVertexArray = glFunction<PFNGLBINDVERTEXARRAYPROC>("glBindVertexArray");
    PFNGLGENBUFFERSPROC genBuffers = glFunction<PFNGLGENBUFFERSPROC>("glGenBuffers");
    PFNGLDELETEBUFFERSPROC deleteBuffers = glFunction<PFNGLDELETEBUFFERSPROC>("glDeleteBuffers");
    PFNGLBINDBUFFERPROC bindBuffer = glFunction<PFNGLBINDBUFFERPROC>("glBindBuffer");
    PFNGLBUFFERDATAPROC bufferData = glFunction<PFNGLBUFFERDATAPROC>("glBufferData");
    PFNGLENABLEVERTEXATTRIBARRAYPROC enableAttribute = glFunction<PFNGLENABLEVERTEXATTRIBARRAYPROC>("glEnableVertexAttribArray");
    PFNGLVERTEXATTRIBPOINTERPROC attributePointer = glFunction<PFNGLVERTEXATTRIBPOINTERPROC>("glVertexAttribPointer");
    PFNGLCREATESHADERPROC createShader = glFunction<PFNGLCREATESHADERPROC>("glCreateShader");
    PFNGLDELETESHADERPROC deleteShader = glFunction<PFNGLDELETESHADERPROC>("glDeleteShader");
    PFNGLSHADERSOURCEPROC shaderSource = glFunction<PFNGLSHADERSOURCEPROC>("glShaderSource");
    PFNGLCOMPILESHADERPROC compileShader = glFunction<PFNGLCOMPILESHADERPROC>("glCompileShader");
    PFNGLGETSHADERIVPROC shaderStatus = glFunction<PFNGLGETSHADERIVPROC>("glGetShaderiv");
    PFNGLGETSHADERINFOLOGPROC shaderLog = glFunction<PFNGLGETSHADERINFOLOGPROC>("glGetShaderInfoLog");
    PFNGLCREATEPROGRAMPROC createProgram = glFunction<PFNGLCREATEPROGRAMPROC>("glCreateProgram");
    PFNGLDELETEPROGRAMPROC deleteProgram = glFunction<PFNGLDELETEPROGRAMPROC>("glDeleteProgram");
    PFNGLATTACHSHADERPROC attachShader = glFunction<PFNGLATTACHSHADERPROC>("glAttachShader");
    PFNGLLINKPROGRAMPROC linkProgram = glFunction<PFNGLLINKPROGRAMPROC>("glLinkProgram");
    PFNGLGETPROGRAMIVPROC programStatus = glFunction<PFNGLGETPROGRAMIVPROC>("glGetProgramiv");
    PFNGLGETPROGRAMINFOLOGPROC programLog = glFunction<PFNGLGETPROGRAMINFOLOGPROC>("glGetProgramInfoLog");
    PFNGLUSEPROGRAMPROC useProgram = glFunction<PFNGLUSEPROGRAMPROC>("glUseProgram");
    PFNGLGETUNIFORMLOCATIONPROC uniform = glFunction<PFNGLGETUNIFORMLOCATIONPROC>("glGetUniformLocation");
    PFNGLUNIFORMMATRIX4FVPROC mat4 = glFunction<PFNGLUNIFORMMATRIX4FVPROC>("glUniformMatrix4fv");
    PFNGLUNIFORMMATRIX3FVPROC mat3 = glFunction<PFNGLUNIFORMMATRIX3FVPROC>("glUniformMatrix3fv");
    PFNGLUNIFORM4FVPROC vec4 = glFunction<PFNGLUNIFORM4FVPROC>("glUniform4fv");
    PFNGLUNIFORM3FVPROC vec3 = glFunction<PFNGLUNIFORM3FVPROC>("glUniform3fv");
    PFNGLUNIFORM1FPROC scalar = glFunction<PFNGLUNIFORM1FPROC>("glUniform1f");
    PFNGLUNIFORM1IPROC integer = glFunction<PFNGLUNIFORM1IPROC>("glUniform1i");
    PFNGLGENERATEMIPMAPPROC generateMipmap = glFunction<PFNGLGENERATEMIPMAPPROC>("glGenerateMipmap");
    PFNGLACTIVETEXTUREPROC activeTexture = glFunction<PFNGLACTIVETEXTUREPROC>("glActiveTexture");
    PFNGLGENSAMPLERSPROC genSamplers = glFunction<PFNGLGENSAMPLERSPROC>("glGenSamplers");
    PFNGLDELETESAMPLERSPROC deleteSamplers = glFunction<PFNGLDELETESAMPLERSPROC>("glDeleteSamplers");
    PFNGLBINDSAMPLERPROC bindSampler = glFunction<PFNGLBINDSAMPLERPROC>("glBindSampler");
    PFNGLSAMPLERPARAMETERIPROC samplerParameter = glFunction<PFNGLSAMPLERPARAMETERIPROC>("glSamplerParameteri");
};

class Program {
public:
    Program(GL& gl, const char* vertex, const char* fragment) : gl_(gl)
    {
        const GLuint vs = compile(GL_VERTEX_SHADER, vertex);
        GLuint fs = 0;
        try {
            fs = compile(GL_FRAGMENT_SHADER, fragment);
        } catch (...) {
            gl_.deleteShader(vs);
            throw;
        }
        id = gl_.createProgram();
        gl_.attachShader(id, vs);
        gl_.attachShader(id, fs);
        gl_.linkProgram(id);
        gl_.deleteShader(vs);
        gl_.deleteShader(fs);
        GLint linked = 0;
        gl_.programStatus(id, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[1024]{};
            gl_.programLog(id, sizeof(log), nullptr, log);
            gl_.deleteProgram(id);
            throw std::runtime_error(std::string("Shader linking failed: ") + log);
        }
    }
    ~Program() { gl_.deleteProgram(id); }
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;

    GLint location(const char* name) const { return gl_.uniform(id, name); }
    GLuint id = 0;

private:
    GLuint compile(GLenum type, const char* text)
    {
        const GLuint shader = gl_.createShader(type);
        gl_.shaderSource(shader, 1, &text, nullptr);
        gl_.compileShader(shader);
        GLint compiled = 0;
        gl_.shaderStatus(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            char log[1024]{};
            gl_.shaderLog(shader, sizeof(log), nullptr, log);
            gl_.deleteShader(shader);
            throw std::runtime_error(std::string("Shader compilation failed: ") + log);
        }
        return shader;
    }

    GL& gl_;
};

constexpr const char* meshVertex = R"GLSL(#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 color;
layout(location = 4) in vec2 emissiveUv;
uniform mat4 viewProjection;
uniform mat4 model;
uniform mat3 normalMatrix;
out vec3 worldPosition;
out vec3 worldNormal;
out vec2 texcoord;
out vec2 emissiveTexcoord;
out vec4 vertexColor;
void main() {
    vec4 point = model * vec4(position, 1.0);
    worldPosition = point.xyz;
    worldNormal = normalMatrix * normal;
    texcoord = uv;
    emissiveTexcoord = emissiveUv;
    vertexColor = color;
    gl_Position = viewProjection * point;
}
)GLSL";

constexpr const char* meshFragment = R"GLSL(#version 330 core
in vec3 worldPosition;
in vec3 worldNormal;
in vec2 texcoord;
in vec2 emissiveTexcoord;
in vec4 vertexColor;
out vec4 fragment;
uniform vec4 baseColor;
uniform vec3 emission;
uniform vec3 eye;
uniform float metallic;
uniform float roughness;
uniform float cutoff;
uniform int alphaMode;
uniform bool useTexture;
uniform bool useEmissiveTexture;
uniform bool useNormals;
uniform bool unlit;
uniform bool wire;
uniform bool selected;
uniform sampler2D baseTexture;
uniform sampler2D emissiveTexture;

vec3 srgb(vec3 linear) {
    linear = max(linear, vec3(0.0));
    return mix(12.92 * linear, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055,
               step(vec3(0.0031308), linear));
}
void main() {
    if (wire) {
        fragment = vec4(selected ? vec3(0.48, 0.90, 1.0) : vec3(0.41, 0.66, 0.87), 1.0);
        return;
    }
    vec4 base = baseColor * vertexColor;
    if (useTexture) base *= texture(baseTexture, texcoord);
    if (alphaMode == 1 && base.a < cutoff) discard;
    vec3 lit = base.rgb;
    if (!unlit) {
        vec3 flatNormal = cross(dFdx(worldPosition), dFdy(worldPosition));
        vec3 n = useNormals && dot(worldNormal, worldNormal) > 1e-12
               ? normalize(worldNormal) * (gl_FrontFacing ? 1.0 : -1.0)
               : normalize(flatNormal);
        vec3 view = normalize(eye - worldPosition);
        vec3 key = normalize(vec3(0.45, 0.85, 0.60));
        vec3 fill = normalize(vec3(-0.7, 0.36, -0.7));
        float diffuse = 0.32 + 0.66 * max(dot(n, key), 0.0) + 0.22 * max(dot(n, fill), 0.0);
        float gloss = mix(140.0, 4.0, clamp(roughness, 0.0, 1.0));
        float specular = pow(max(dot(n, normalize(key + view)), 0.0), gloss) * max(dot(n, key), 0.0);
        vec3 reflectance = mix(vec3(0.06), base.rgb, clamp(metallic, 0.0, 1.0));
        vec3 emitted = emission;
        if (useEmissiveTexture) emitted *= texture(emissiveTexture, emissiveTexcoord).rgb;
        lit = base.rgb * diffuse * mix(1.0, 0.68, metallic) + reflectance * specular + emitted;
    }
    if (selected) lit = mix(lit, vec3(0.12, 0.57, 1.0), 0.28);
    fragment = vec4(srgb(lit), alphaMode == 2 ? base.a : 1.0);
}
)GLSL";

constexpr const char* gridVertex = R"GLSL(#version 330 core
uniform mat4 viewProjection;
uniform float floorY;
uniform float cell;
out vec2 gridPosition;
flat out vec4 lineColor;
void main() {
    int line = gl_VertexID / 2;
    int tick = line % 81 - 40;
    bool alongX = line < 81;
    float end = gl_VertexID % 2 == 0 ? -40.0 : 40.0;
    gridPosition = (alongX ? vec2(end, tick) : vec2(tick, end)) * cell;
    lineColor = tick % 5 == 0 ? vec4(0.34, 0.40, 0.49, 0.40) : vec4(0.27, 0.33, 0.42, 0.22);
    if (tick == 0) lineColor = alongX ? vec4(0.65, 0.32, 0.38, 0.65) : vec4(0.32, 0.50, 0.70, 0.65);
    gl_Position = viewProjection * vec4(gridPosition.x, floorY, gridPosition.y, 1.0);
}
)GLSL";

constexpr const char* gridFragment = R"GLSL(#version 330 core
in vec2 gridPosition;
flat in vec4 lineColor;
out vec4 fragment;
uniform vec3 eye;
uniform float cell;
void main() {
    float fade = 1.0 - smoothstep(3.0, 36.0, length(gridPosition) / cell);
    float distanceFade = 1.0 - smoothstep(4.0, 22.0, distance(gridPosition, eye.xz));
    fragment = vec4(lineColor.rgb, lineColor.a * fade * distanceFade);
}
)GLSL";

struct MeshUniforms {
    GLint viewProjection, model, normal, color, emission, eye, metallic, roughness, cutoff,
          alpha, useTexture, useEmissiveTexture, useNormals, unlit, wire, selected, texture, emissiveTexture;

    explicit MeshUniforms(const Program& program)
        : viewProjection(program.location("viewProjection")), model(program.location("model")),
          normal(program.location("normalMatrix")), color(program.location("baseColor")),
          emission(program.location("emission")), eye(program.location("eye")),
          metallic(program.location("metallic")), roughness(program.location("roughness")),
          cutoff(program.location("cutoff")), alpha(program.location("alphaMode")),
          useTexture(program.location("useTexture")), useEmissiveTexture(program.location("useEmissiveTexture")),
          useNormals(program.location("useNormals")),
          unlit(program.location("unlit")), wire(program.location("wire")),
          selected(program.location("selected")), texture(program.location("baseTexture")),
          emissiveTexture(program.location("emissiveTexture")) {}
};

struct GpuMesh {
    GLuint vao = 0;
    std::array<GLuint, 2> buffers{};
};

struct GpuDraw {
    glm::mat4 transform;
    glm::mat3 normal;
    glm::vec3 center;
    GLenum winding;
};

struct Scene {
    explicit Scene(GL& api) : gl(api) {}
    ~Scene()
    {
        for (const auto& mesh : meshes) {
            gl.deleteVertexArrays(1, &mesh.vao);
            gl.deleteBuffers(static_cast<GLsizei>(mesh.buffers.size()), mesh.buffers.data());
        }
        for (auto texture : textures) glDeleteTextures(1, &texture);
        for (const auto& sampler : samplers) gl.deleteSamplers(1, &sampler.second);
    }
    GL& gl;
    std::vector<GpuMesh> meshes;
    std::vector<GLuint> textures;
    std::vector<std::pair<Sampler, GLuint>> samplers;
    std::vector<std::array<GLuint, 2>> materialSamplers;
    std::vector<GpuDraw> draws;
    std::vector<std::size_t> opaque;
    std::vector<std::size_t> transparent, visibleTransparent;
    std::vector<float> distances;
    float floor = -0.75f;
    float cell = 0.25f;
};

constexpr std::array<GLenum, 7> modes{
    GL_POINTS, GL_LINES, GL_LINE_LOOP, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN
};

} // namespace

struct Renderer::State {
    GL gl;
    Program mesh{gl, meshVertex, meshFragment};
    Program grid{gl, gridVertex, gridFragment};
    MeshUniforms uniforms{mesh};
    GLint gridView = grid.location("viewProjection");
    GLint gridFloor = grid.location("floorY");
    GLint gridCell = grid.location("cell");
    GLint gridEye = grid.location("eye");
    GLuint gridVao = 0;
    std::unique_ptr<Scene> scene;

    State() { gl.genVertexArrays(1, &gridVao); }
    ~State() { gl.deleteVertexArrays(1, &gridVao); }
};

glm::vec3 Camera::eye() const
{
    return target + distance * glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                         std::cos(pitch) * std::cos(yaw));
}

void Camera::frame(float aspect)
{
    target = glm::vec3(0.0f);
    const auto halfAngle = std::min(fov * 0.5f, std::atan(std::tan(fov * 0.5f) * std::max(aspect, 0.1f)));
    distance = 1.12f / std::sin(halfAngle);
}

void Camera::orbit(float dx, float dy)
{
    yaw = std::remainder(yaw - dx * 0.008f, glm::two_pi<float>());
    pitch = std::clamp(pitch + dy * 0.008f, -1.54f, 1.54f);
}

void Camera::pan(float dx, float dy, float height)
{
    const auto forward = glm::normalize(target - eye());
    const auto right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const auto up = glm::cross(right, forward);
    const float scale = 2.0f * distance * std::tan(fov * 0.5f) / std::max(height, 1.0f);
    target += (-dx * right + dy * up) * scale;
}

void Camera::zoom(float amount)
{
    distance = std::clamp(distance * std::exp(-0.13f * amount), 0.035f, 200.0f);
}

Renderer::Renderer() : state_(std::make_unique<State>()) {}
Renderer::~Renderer() = default;

void Renderer::upload(const Model& model)
{
    const auto& gl = state_->gl;
    auto scene = std::make_unique<Scene>(state_->gl);
    const float radius = std::max(glm::length(model.bounds.size() * 0.5f), 1e-12f);
    if (!std::isfinite(radius)) throw std::runtime_error("The model has invalid scene bounds.");
    const auto scale = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / radius));
    const auto normalize = glm::translate(scale, -model.bounds.center());
    scene->floor = (model.bounds.min.y - model.bounds.center().y) / radius - 0.025f;
    const auto naturalCell = std::pow(10.0f, std::floor(std::log10(radius))) / radius;
    scene->cell = naturalCell > 0.42f ? naturalCell * 0.2f : naturalCell;

    while (glGetError() != GL_NO_ERROR) {}
    scene->meshes.resize(model.geometry.size());
    for (std::size_t i = 0; i < model.geometry.size(); ++i) {
        const auto& geometry = model.geometry[i];
        auto& mesh = scene->meshes[i];
        if (geometry.indices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
            throw std::runtime_error("A mesh exceeds the graphics driver's index limit.");
        }
        gl.genVertexArrays(1, &mesh.vao);
        gl.bindVertexArray(mesh.vao);
        gl.genBuffers(static_cast<GLsizei>(mesh.buffers.size()), mesh.buffers.data());
        gl.bindBuffer(GL_ARRAY_BUFFER, mesh.buffers[0]);
        gl.bufferData(GL_ARRAY_BUFFER, geometry.vertices.size() * sizeof(Vertex), geometry.vertices.data(), GL_STATIC_DRAW);
        gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.buffers[1]);
        gl.bufferData(GL_ELEMENT_ARRAY_BUFFER, geometry.indices.size() * sizeof(std::uint32_t), geometry.indices.data(), GL_STATIC_DRAW);
        constexpr std::array<GLint, 5> sizes{3, 3, 2, 4, 2};
        constexpr std::array<std::size_t, 5> offsets{offsetof(Vertex, position), offsetof(Vertex, normal),
                                                     offsetof(Vertex, uv), offsetof(Vertex, color), offsetof(Vertex, emissiveUv)};
        for (GLuint j = 0; j < sizes.size(); ++j) {
            gl.enableAttribute(j);
            gl.attributePointer(j, sizes[j], GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsets[j]));
        }
    }
    gl.bindVertexArray(0);
    gl.bindBuffer(GL_ARRAY_BUFFER, 0);

    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    scene->textures.resize(model.images.size());
    gl.activeTexture(GL_TEXTURE0);
    for (std::size_t i = 0; i < model.images.size(); ++i) {
        const auto& image = model.images[i];
        if (image.width > maxTextureSize || image.height > maxTextureSize) {
            throw std::runtime_error("A texture exceeds the graphics driver's size limit.");
        }
        glGenTextures(1, &scene->textures[i]);
        glBindTexture(GL_TEXTURE_2D, scene->textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image.width, image.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.get());
        gl.generateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    const auto samplerId = [&](std::size_t image, const Sampler& settings) {
        if (image == none) return 0u;
        const auto found = std::find_if(scene->samplers.begin(), scene->samplers.end(),
                                       [&](const auto& entry) { return entry.first == settings; });
        if (found != scene->samplers.end()) return found->second;

        auto& [sampler, id] = scene->samplers.emplace_back(settings, 0u);
        gl.genSamplers(1, &id);
        gl.samplerParameter(id, GL_TEXTURE_MIN_FILTER, sampler.minFilter);
        gl.samplerParameter(id, GL_TEXTURE_MAG_FILTER, sampler.magFilter);
        gl.samplerParameter(id, GL_TEXTURE_WRAP_S, sampler.wrapS);
        gl.samplerParameter(id, GL_TEXTURE_WRAP_T, sampler.wrapT);
        return id;
    };
    scene->materialSamplers.reserve(model.materials.size());
    for (const auto& material : model.materials) {
        scene->materialSamplers.push_back({samplerId(material.image, material.sampler),
                                          samplerId(material.emissiveImage, material.emissiveSampler)});
    }

    scene->draws.reserve(model.draws.size());
    for (std::size_t i = 0; i < model.draws.size(); ++i) {
        const auto& draw = model.draws[i];
        const auto transform = normalize * draw.transform;
        const auto linear = glm::mat3(transform);
        const float determinant = glm::determinant(linear);
        const auto normal = determinant != 0.0f ? glm::transpose(glm::inverse(linear)) : glm::mat3(0.0f);
        const auto center = glm::vec3(transform * glm::vec4(model.geometry[draw.geometry].bounds.center(), 1.0f));
        scene->draws.push_back({transform, normal, center, static_cast<GLenum>(determinant < 0.0f ? GL_CW : GL_CCW)});
        if (draw.material != none && model.materials[draw.material].alpha == AlphaMode::Blend) {
            scene->transparent.push_back(i);
        } else {
            scene->opaque.push_back(i);
        }
    }
    if (!scene->transparent.empty()) {
        scene->distances.resize(model.draws.size());
        scene->visibleTransparent.reserve(scene->transparent.size());
    }
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("The graphics driver could not upload this model.");
    state_->scene = std::move(scene);
}

void Renderer::paint(const Model* model, const Camera& camera, Viewport stage, int width, int height,
                     bool grid, bool wireframe, const std::vector<std::uint8_t>& visibleDraws, DrawRange selection)
{
    auto& state = *state_;
    const auto& gl = state.gl;
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(0.045f, 0.055f, 0.076f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (stage.width <= 0 || stage.height <= 0) return;

    glViewport(stage.x, height - stage.y - stage.height, stage.width, stage.height);
    glScissor(stage.x, height - stage.y - stage.height, stage.width, stage.height);
    glEnable(GL_SCISSOR_TEST);
    glClearColor(0.071f, 0.087f, 0.113f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    const auto eye = camera.eye();
    const auto view = glm::lookAt(eye, camera.target, glm::vec3(0.0f, 1.0f, 0.0f));
    const float aspect = static_cast<float>(stage.width) / static_cast<float>(stage.height);
    const float extent = std::max(16.0f, camera.distance + glm::length(camera.target) + 16.0f);
    const float halfHeight = camera.distance * std::tan(fov * 0.5f);
    const auto projection = camera.orthographic
        ? glm::ortho(-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight, -extent, extent)
        : glm::perspective(fov, aspect, std::max(0.001f, (camera.distance - 4.0f) * 0.05f), extent);
    const auto vp = projection * view;

    auto* scene = state.scene.get();
    if (grid) {
        gl.useProgram(state.grid.id);
        gl.mat4(state.gridView, 1, GL_FALSE, glm::value_ptr(vp));
        gl.scalar(state.gridFloor, scene ? scene->floor : -0.68f);
        gl.scalar(state.gridCell, scene ? scene->cell : 0.22f);
        gl.vec3(state.gridEye, 1, glm::value_ptr(eye));
        gl.bindVertexArray(state.gridVao);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_LINES, 0, 324);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    if (model && scene) {
        const auto& u = state.uniforms;
        gl.useProgram(state.mesh.id);
        gl.mat4(u.viewProjection, 1, GL_FALSE, glm::value_ptr(vp));
        gl.vec3(u.eye, 1, glm::value_ptr(eye));
        gl.integer(u.texture, 0);
        gl.integer(u.emissiveTexture, 1);
        gl.integer(u.wire, wireframe);
        gl.activeTexture(GL_TEXTURE0);
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
        glPointSize(3.0f);
        static const Material fallback = [] {
            Material material;
            material.color = {0.50f, 0.60f, 0.72f, 1.0f};
            material.doubleSided = true;
            return material;
        }();

        const auto drawMesh = [&](std::size_t i) {
            const auto& draw = model->draws[i];
            const auto& mesh = model->geometry[draw.geometry];
            const auto& gpu = scene->draws[i];
            const auto& material = draw.material == none ? fallback : model->materials[draw.material];
            const bool textured = !wireframe && material.image != none;
            const bool emissive = !wireframe && material.emissiveImage != none;
            glFrontFace(gpu.winding);
            if (material.doubleSided || wireframe) glDisable(GL_CULL_FACE);
            else glEnable(GL_CULL_FACE);
            gl.mat4(u.model, 1, GL_FALSE, glm::value_ptr(gpu.transform));
            gl.mat3(u.normal, 1, GL_FALSE, glm::value_ptr(gpu.normal));
            gl.vec4(u.color, 1, glm::value_ptr(material.color));
            gl.vec3(u.emission, 1, glm::value_ptr(material.emission));
            gl.scalar(u.metallic, material.metallic);
            gl.scalar(u.roughness, material.roughness);
            gl.scalar(u.cutoff, material.cutoff);
            gl.integer(u.alpha, static_cast<int>(material.alpha));
            gl.integer(u.useTexture, textured);
            gl.integer(u.useEmissiveTexture, emissive);
            gl.integer(u.useNormals, mesh.hasNormals);
            gl.integer(u.unlit, material.unlit || mesh.topology < Topology::Triangles);
            gl.integer(u.selected, i >= selection.first && i < selection.end);
            if (textured) {
                gl.activeTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, scene->textures[material.image]);
                gl.bindSampler(0, scene->materialSamplers[draw.material][0]);
            }
            if (emissive) {
                gl.activeTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, scene->textures[material.emissiveImage]);
                gl.bindSampler(1, scene->materialSamplers[draw.material][1]);
            }
            gl.bindVertexArray(scene->meshes[draw.geometry].vao);
            glDrawElements(modes[static_cast<std::size_t>(mesh.topology)], static_cast<GLsizei>(draw.indexCount),
                           GL_UNSIGNED_INT, reinterpret_cast<const void*>(draw.firstIndex * sizeof(std::uint32_t)));
        };

        for (auto i : scene->opaque) {
            if (visibleDraws[i]) drawMesh(i);
        }
        if (!scene->transparent.empty()) {
            auto& visible = scene->visibleTransparent;
            visible.clear();
            for (auto i : scene->transparent) {
                if (visibleDraws[i]) visible.push_back(i);
            }
            if (!visible.empty() && !wireframe) {
                const auto direction = glm::normalize(camera.target - eye);
                for (auto i : visible) scene->distances[i] = glm::dot(scene->draws[i].center, direction);
                std::sort(visible.begin(), visible.end(), [&](auto a, auto b) {
                    return scene->distances[a] > scene->distances[b];
                });
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            }
            for (auto i : visible) drawMesh(i);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
        gl.activeTexture(GL_TEXTURE1);
        gl.bindSampler(1, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.activeTexture(GL_TEXTURE0);
        gl.bindSampler(0, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glFrontFace(GL_CCW);
    }
    gl.bindVertexArray(0);
    gl.useProgram(0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
}

} // namespace viewer

#include "components.hpp"
#include "model.hpp"
#include "renderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <future>
#include <stdexcept>

namespace viewer {

Model loadDesktopModel(const std::filesystem::path& path, const std::atomic_bool& canceled);

namespace {

constexpr ImVec4 ink{0.87f, 0.91f, 0.96f, 1.0f};
constexpr ImVec4 muted{0.55f, 0.63f, 0.72f, 1.0f};
constexpr ImVec4 accent{0.46f, 0.76f, 1.0f, 1.0f};
constexpr ImVec4 warning{1.0f, 0.70f, 0.42f, 1.0f};
constexpr ImU32 chrome = IM_COL32(20, 26, 36, 255);
constexpr ImU32 panel = IM_COL32(23, 30, 42, 255);
constexpr ImU32 line = IM_COL32(49, 61, 77, 255);

Uint32 dialogEvent = 0;

void SDLCALL fileChosen(void*, const char* const* files, int)
{
    SDL_Event event{};
    event.type = dialogEvent;
    event.user.code = files == nullptr;
    event.user.data1 = files && *files ? SDL_strdup(*files) : nullptr;
    if (!SDL_PushEvent(&event)) SDL_free(event.user.data1);
}

struct Layout {
    float scale;
    float width;
    float height;
    float inspector;
    float toolbar;
    float footer;

    float stageWidth() const { return width - inspector; }
    float stageHeight() const { return height - toolbar - footer; }
    bool stageContains(float x, float y) const
    {
        return x >= 0 && x < stageWidth() && y >= toolbar && y < height - footer;
    }
};

Layout layout(SDL_Window* window, bool showInspector)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    const float scale = std::clamp(SDL_GetWindowDisplayScale(window) /
                                   std::max(SDL_GetWindowPixelDensity(window), 1.0f), 1.0f, 3.0f);
    const float inspector = showInspector ? std::min(width * 0.42f, scale * 316.0f) : 0.0f;
    return {scale, static_cast<float>(width), static_cast<float>(height), inspector, scale * 57.0f, scale * 34.0f};
}

void theme(float scale)
{
    auto& style = ImGui::GetStyle();
    style = ImGuiStyle{};
    style.FontSizeBase = 15.0f;
    style.FontScaleDpi = scale;
    style.WindowPadding = {0.0f, 0.0f};
    style.FramePadding = {11.0f, 7.0f};
    style.ItemSpacing = {9.0f, 8.0f};
    style.ScrollbarSize = 10.0f;
    style.FrameRounding = 5.0f;
    style.TabRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.Colors[ImGuiCol_Text] = ink;
    style.Colors[ImGuiCol_TextDisabled] = muted;
    style.Colors[ImGuiCol_Border] = {0.19f, 0.24f, 0.30f, 1.0f};
    style.Colors[ImGuiCol_Separator] = style.Colors[ImGuiCol_Border];
    style.Colors[ImGuiCol_Button] = {0.16f, 0.21f, 0.29f, 1.0f};
    style.Colors[ImGuiCol_ButtonHovered] = {0.23f, 0.31f, 0.42f, 1.0f};
    style.Colors[ImGuiCol_ButtonActive] = {0.20f, 0.37f, 0.54f, 1.0f};
    style.Colors[ImGuiCol_Header] = {0.15f, 0.26f, 0.37f, 1.0f};
    style.Colors[ImGuiCol_HeaderHovered] = {0.20f, 0.31f, 0.43f, 1.0f};
    style.Colors[ImGuiCol_HeaderActive] = style.Colors[ImGuiCol_ButtonActive];
    style.Colors[ImGuiCol_Tab] = {0.12f, 0.16f, 0.23f, 1.0f};
    style.Colors[ImGuiCol_TabSelected] = {0.18f, 0.29f, 0.41f, 1.0f};
    style.Colors[ImGuiCol_TabHovered] = style.Colors[ImGuiCol_HeaderHovered];
    style.Colors[ImGuiCol_TabSelectedOverline] = accent;
    style.ScaleAllSizes(scale);
}

bool button(const char* label, bool active = false)
{
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.29f, 0.43f, 1.0f));
    const bool pressed = ImGui::Button(label);
    if (active) ImGui::PopStyleColor();
    return pressed;
}

std::string bytes(std::uintmax_t size)
{
    if (size < 1024) return std::to_string(size) + " B";
    char text[32];
    if (size < 1024 * 1024) std::snprintf(text, sizeof(text), "%.1f KiB", static_cast<double>(size) / 1024.0);
    else std::snprintf(text, sizeof(text), "%.1f MiB", static_cast<double>(size) / (1024.0 * 1024.0));
    return text;
}

std::string count(std::size_t value)
{
    auto text = std::to_string(value);
    for (auto position = text.size(); position > 3;) {
        position -= 3;
        text.insert(position, ",");
    }
    return text;
}

void property(const char* label, const std::string& value)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextColored(muted, "%s", label);
    ImGui::TableNextColumn();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
}

bool properties(const char* id, float scale)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 103.0f * scale);
    ImGui::TableSetupColumn("Value");
    return true;
}

void overview(const Model& model, float scale)
{
    ImGui::TextColored(accent, "ASSET");
    ImGui::Spacing();
    if (properties("asset-properties", scale)) {
        property("Format", model.format);
        property("File size", bytes(model.fileSize));
        property("Scene", model.sceneName);
        if (!model.creator.empty()) property("Creator", model.creator);
        char time[32];
        std::snprintf(time, sizeof(time), "%.1f ms", model.loadMilliseconds);
        property("CPU import", time);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(accent, "GEOMETRY");
    ImGui::Spacing();
    if (properties("geometry-properties", scale)) {
        property("Nodes", count(model.nodes.size()));
        property("Meshes", count(model.meshes));
        property("Draws", count(model.draws.size()));
        property("Vertices", count(model.vertices));
        property("Triangles", count(model.triangles));
        property("Materials", count(model.materials.size()));
        property("Textures", count(model.images.size()));
        property("Animations", count(model.animations));
        property("Skins", count(model.skins));
        const auto size = model.bounds.size();
        char dimensions[96];
        std::snprintf(dimensions, sizeof(dimensions), "%.4g x %.4g x %.4g %s", size.x, size.y, size.z, model.units.c_str());
        property("XYZ bounds", dimensions);
        ImGui::EndTable();
    }
    if (model.textureWarnings) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(warning, "%zu texture(s) unavailable.", model.textureWarnings);
        ImGui::PopTextWrapPos();
    }
}

std::string componentName(const Model& model, Component component)
{
    if (component.draw == none) return model.nodes[component.node].name;
    const auto& draw = model.draws[component.draw];
    const auto& node = model.nodes[component.node];
    auto name = model.geometry[draw.geometry].name;
    if (draw.material != none) name += " / " + model.materials[draw.material].name;
    if (node.drawCount > 1) name += " #" + std::to_string(component.draw - node.firstDraw + 1);
    return name;
}

void componentDetails(const Model& model, Component component, float scale, std::size_t& selected)
{
    const auto& node = model.nodes[component.node];
    ImGui::Separator();
    ImGui::TextColored(accent, "SELECTION");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) selected = none;
    if (!properties("component-properties", scale)) return;

    if (component.draw != none) {
        const auto& draw = model.draws[component.draw];
        const auto& mesh = model.geometry[draw.geometry];
        property("Mesh", mesh.name);
        property("Node", node.name);
        property("Vertices", count(mesh.vertexCount));
        property("Triangles", count(triangleCount(mesh.topology, draw.indexCount)));
        if (draw.material != none) property("Material", model.materials[draw.material].name);
    } else {
        property("Node", node.name);
        if (node.parent != none) property("Parent", model.nodes[node.parent].name);
        property("Own parts", count(node.drawCount));
        const auto position = glm::vec3(node.transform[3]);
        char value[96];
        std::snprintf(value, sizeof(value), "%.4g, %.4g, %.4g %s", position.x, position.y, position.z, model.units.c_str());
        property("Position", value);
        if (node.drawCount == 1) {
            const auto& draw = model.draws[node.firstDraw];
            const auto& mesh = model.geometry[draw.geometry];
            property("Mesh", mesh.name);
            property("Triangles", count(triangleCount(mesh.topology, draw.indexCount)));
        }
    }
    ImGui::EndTable();
}

void componentList(const Model& model, ComponentVisibility& visibility, float scale, std::size_t& selected)
{
    if (ImGui::SmallButton("Show all")) visibility.showAll(model);
    ImGui::SameLine();
    ImGui::TextColored(muted, "%s / %s parts visible", count(visibility.visibleDrawCount).c_str(),
                       count(model.draws.size()).c_str());

    const float available = ImGui::GetContentRegionAvail().y;
    const float listHeight = selected == none ? 0.0f : std::max(72.0f * scale, available * 0.54f);
    ImGui::BeginChild("component-list", {0.0f, listHeight});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4.0f * scale, 3.0f * scale});
    const float rowHeight = ImGui::GetFrameHeight();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visibility.rows.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto index = static_cast<std::size_t>(i);
            const auto component = visibility.rows[index];
            const auto& node = model.nodes[component.node];
            const bool isNode = component.draw == none;
            const bool canToggle = !isNode || visibility.subtreeEnd[component.node] > node.firstDraw;
            auto& enabled = isNode ? visibility.nodeEnabled[component.node] : visibility.drawEnabled[component.draw];
            bool checked = enabled != 0;
            const bool parentVisible = isNode ? (node.parent == none || visibility.nodeVisible[node.parent])
                                              : visibility.nodeVisible[component.node];

            ImGui::PushID(i);
            const auto depth = std::min<std::size_t>(node.depth + (isNode ? 0u : 1u), 12);
            const auto indent = static_cast<float>(depth) * 9.0f * scale;
            if (indent) ImGui::Indent(indent);
            ImGui::BeginDisabled(!canToggle);
            if (ImGui::Checkbox("##visibility", &checked)) {
                enabled = checked;
                visibility.update(model);
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (!canToggle) ImGui::SetTooltip("This node has no geometry.");
                else if (!parentVisible) ImGui::SetTooltip("A parent is hidden. This setting applies when it is shown.");
                else if (isNode) ImGui::SetTooltip("Toggle this branch. Child visibility settings are retained.");
                else ImGui::SetTooltip("Toggle this mesh part.");
            }

            ImGui::SameLine();
            const auto labelStart = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##component", index == selected, 0, {0.0f, rowHeight})) {
                selected = index == selected ? none : index;
            }
            const auto name = componentName(model, component);
            auto* draw = ImGui::GetWindowDrawList();
            draw->PushClipRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true);
            draw->AddText({labelStart.x + 2.0f * scale, labelStart.y + 3.0f * scale},
                          ImGui::GetColorU32(canToggle && checked && parentVisible ? ink : muted), name.c_str());
            draw->PopClipRect();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name.c_str());
            if (indent) ImGui::Unindent(indent);
            ImGui::PopID();
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    if (selected != none) componentDetails(model, visibility.rows[selected], scale, selected);
}

void materials(const Model& model, float scale, std::size_t& selected)
{
    if (model.materials.empty()) {
        ImGui::TextColored(muted, "This model has no named materials.");
        return;
    }
    if (selected != none) {
        const auto& material = model.materials[selected];
        const auto toSrgb = [](float value) {
            return value <= 0.0031308f ? 12.92f * value : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        };
        ImGui::ColorButton("##color", {toSrgb(material.color.r), toSrgb(material.color.g),
                                      toSrgb(material.color.b), material.color.a});
        ImGui::SameLine();
        ImGui::TextWrapped("%s", material.name.c_str());
        if (properties("material-properties", scale)) {
            const auto number = [](float value) {
                char text[20];
                std::snprintf(text, sizeof(text), "%.3f", value);
                return std::string(text);
            };
            property("Metallic", number(material.metallic));
            property("Roughness", number(material.roughness));
            property("Alpha", material.alpha == AlphaMode::Blend ? "Blend" :
                              (material.alpha == AlphaMode::Mask ? "Mask" : "Opaque"));
            property("Double-sided", material.doubleSided ? "Yes" : "No");
            if (material.image != none) {
                const auto& image = model.images[material.image];
                property("Base color", image.name);
                property("Texture size", std::to_string(image.width) + " x " + std::to_string(image.height));
            }
            if (material.emissiveImage != none) property("Emissive", model.images[material.emissiveImage].name);
            ImGui::EndTable();
        }
        ImGui::Separator();
    }
    ImGui::BeginChild("material-list");
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(model.materials.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto index = static_cast<std::size_t>(i);
            const auto& material = model.materials[index];
            ImGui::PushID(i);
            if (ImGui::Selectable("##material", index == selected)) selected = index == selected ? none : index;
            auto* draw = ImGui::GetWindowDrawList();
            const auto start = ImGui::GetItemRectMin();
            const auto end = ImGui::GetItemRectMax();
            draw->PushClipRect(start, end, true);
            draw->AddText({start.x + 4.0f * scale, start.y + 3.0f * scale},
                          ImGui::GetColorU32(ink), material.name.c_str());
            draw->PopClipRect();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", material.name.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

class App {
public:
    explicit App(SDL_Window* window) : window_(window) {}
    ~App() { canceled_.store(true); }

    void open(const char* file)
    {
        if (!file || !*file) return;
        if (loading_.valid()) {
            pending_ = file;
            return;
        }
        error_.clear();
        loadingName_ = std::filesystem::u8path(file).filename().u8string();
        loading_ = std::async(std::launch::async, [this, path = std::filesystem::u8path(file)] {
            return loadDesktopModel(path, canceled_);
        });
    }

    void run()
    {
        unsigned frames = 2;
        while (running_) {
            SDL_Event event;
            if (!frames) {
                const bool received = loading_.valid() ? SDL_WaitEventTimeout(&event, 30) : SDL_WaitEvent(&event);
                if (received) {
                    handle(event);
                    frames = 2;
                }
            }
            while (SDL_PollEvent(&event)) {
                handle(event);
                frames = 2;
            }
            if (loading_.valid() && loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                finishLoad();
                frames = 2;
            }
            if (frames) {
                --frames;
                if (running_ && !(SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED)) draw();
            }
        }
    }

private:
    void frame()
    {
        const auto ui = layout(window_, showInspector_);
        camera_.frame(ui.stageWidth() / std::max(ui.stageHeight(), 1.0f));
    }

    void chooseFile()
    {
        if (dialogOpen_) return;
        static const SDL_DialogFileFilter filters[] = {
            {"3D models", "gltf;glb;fbx;3ds;blend"},
            {"glTF", "gltf;glb"},
            {"Autodesk FBX", "fbx"},
            {"3D Studio", "3ds"},
            {"Blender", "blend"},
            {"All files", "*"}
        };
        dialogOpen_ = true;
        SDL_ShowOpenFileDialog(fileChosen, nullptr, window_, filters,
                               static_cast<int>(std::size(filters)), nullptr, false);
    }

    void finishLoad()
    {
        try {
            auto model = loading_.get();
            if (pending_.empty()) {
                ComponentVisibility visibility(model);
                renderer_.upload(model);
                for (auto& geometry : model.geometry) {
                    geometry.vertexCount = geometry.vertices.size();
                    std::vector<Vertex>().swap(geometry.vertices);
                    std::vector<std::uint32_t>().swap(geometry.indices);
                }
                for (auto& image : model.images) image.pixels.reset();
                model_ = std::move(model);
                camera_ = Camera{};
                frame();
                visibility_ = std::move(visibility);
                selectedComponent_ = selectedMaterial_ = none;
                SDL_SetWindowTitle(window_, ("Model Viewer - " + model_->path.filename().u8string()).c_str());
            }
        } catch (const std::exception& error) {
            error_ = error.what();
        }
        if (!pending_.empty()) {
            auto next = std::move(pending_);
            pending_.clear();
            open(next.c_str());
        }
    }

    void handle(const SDL_Event& event)
    {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == dialogEvent) {
            dialogOpen_ = false;
            const std::unique_ptr<char, decltype(&SDL_free)> path(static_cast<char*>(event.user.data1), &SDL_free);
            if (event.user.code) error_ = "The file picker could not be opened. You can drop a model into the window.";
            open(path.get());
            return;
        }

        const auto ui = layout(window_, showInspector_);
        switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED: running_ = false; break;
            case SDL_EVENT_DROP_FILE: open(event.drop.data); break;
            case SDL_EVENT_WINDOW_FOCUS_LOST: dragButton_ = 0; break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (model_ && !dragButton_ && ui.stageContains(event.button.x, event.button.y) &&
                    (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT ||
                     event.button.button == SDL_BUTTON_MIDDLE)) {
                    dragButton_ = event.button.button;
                    panning_ = dragButton_ != SDL_BUTTON_LEFT || (SDL_GetModState() & SDL_KMOD_SHIFT);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == dragButton_) dragButton_ = 0;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (dragButton_ && panning_) camera_.pan(event.motion.xrel, event.motion.yrel, ui.stageHeight());
                if (dragButton_ && !panning_) camera_.orbit(event.motion.xrel, event.motion.yrel);
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (model_ && ui.stageContains(event.wheel.mouse_x, event.wheel.mouse_y)) {
                    camera_.zoom(event.wheel.y * (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f));
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.repeat) break;
                if (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) {
                    if (event.key.key == SDLK_O) chooseFile();
                } else {
                    switch (event.key.key) {
                        case SDLK_F: frame(); break;
                        case SDLK_G: grid_ = !grid_; break;
                        case SDLK_W: wireframe_ = !wireframe_; break;
                        case SDLK_P: camera_.orthographic = !camera_.orthographic; break;
                        case SDLK_I: showInspector_ = !showInspector_; break;
                        case SDLK_ESCAPE: selectedComponent_ = none; break;
                        default: break;
                    }
                }
                break;
            default: break;
        }
    }

    void toolbar(const Layout& ui)
    {
        const float s = ui.scale;
        ImGui::SetCursorPos({15.0f * s, 14.0f * s});
        if (button("Open...", true)) chooseFile();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open a model (Ctrl or Command + O)");
        if (ui.width >= 860.0f * s) {
            ImGui::SameLine(0.0f, 14.0f * s);
            ImGui::TextColored(muted, "MODEL VIEWER");
        }
        ImGui::SameLine(0.0f, 16.0f * s);
        if (button("Frame")) frame();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Frame the entire model (F)");
        ImGui::SameLine();
        if (button(camera_.orthographic ? "Ortho" : "Perspective", camera_.orthographic)) {
            camera_.orthographic = !camera_.orthographic;
        }
        ImGui::SameLine();
        if (button("Grid", grid_)) grid_ = !grid_;
        ImGui::SameLine();
        if (button("Wireframe", wireframe_)) wireframe_ = !wireframe_;
        ImGui::SameLine();
        if (button("Inspector", showInspector_)) showInspector_ = !showInspector_;
    }

    void inspector(const Layout& ui)
    {
        if (!ui.inspector) return;
        const float s = ui.scale;
        ImGui::SetCursorPos({ui.stageWidth() + 15.0f * s, ui.toolbar + 17.0f * s});
        ImGui::BeginChild("inspector", {ui.inspector - 29.0f * s, ui.stageHeight() - 30.0f * s});
        ImGui::TextColored(accent, "INSPECTOR");
        if (model_) {
            const auto filename = model_->path.filename().u8string();
            ImGui::PushFont(nullptr, 18.0f);
            ImGui::TextWrapped("%s", filename.c_str());
            ImGui::PopFont();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", model_->path.u8string().c_str());
            ImGui::Spacing();
            if (ImGui::BeginTabBar("inspector-tabs")) {
                if (ImGui::BeginTabItem("Overview")) {
                    ImGui::BeginChild("overview");
                    ImGui::Spacing();
                    overview(*model_, s);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Components")) {
                    ImGui::Spacing();
                    componentList(*model_, visibility_, s, selectedComponent_);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Materials")) {
                    ImGui::Spacing();
                    materials(*model_, s, selectedMaterial_);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        } else {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(muted, "Open a model to inspect its geometry, scene nodes, and materials.");
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
    }

    void viewportOverlay(const Layout& ui)
    {
        const float s = ui.scale;
        if (!model_ && !loading_.valid()) {
            const ImVec2 middle{ui.stageWidth() * 0.5f, ui.toolbar + ui.stageHeight() * 0.41f};
            auto* draw = ImGui::GetWindowDrawList();
            const ImVec2 square{middle.x, middle.y - 53.0f * s};
            const ImVec2 a{square.x, square.y - 24.0f * s};
            const ImVec2 b{square.x + 22.0f * s, square.y - 11.0f * s};
            const ImVec2 c{square.x + 22.0f * s, square.y + 14.0f * s};
            const ImVec2 d{square.x, square.y + 26.0f * s};
            const ImVec2 e{square.x - 22.0f * s, square.y + 14.0f * s};
            const ImVec2 f{square.x - 22.0f * s, square.y - 11.0f * s};
            const ImVec2 g{square.x, square.y + 2.0f * s};
            const ImVec2 outline[] = {a, b, c, d, e, f};
            const auto stroke = ImGui::GetColorU32(accent);
            draw->AddConvexPolyFilled(outline, 6, IM_COL32(30, 52, 71, 230));
            draw->AddPolyline(outline, 6, stroke, ImDrawFlags_Closed, 1.65f * s);
            draw->AddLine(f, g, stroke, 1.65f * s);
            draw->AddLine(b, g, stroke, 1.65f * s);
            draw->AddLine(d, g, stroke, 1.65f * s);

            constexpr const char* title = "Open a 3D model";
            ImGui::PushFont(nullptr, 27.0f);
            ImGui::SetCursorPos({middle.x - ImGui::CalcTextSize(title).x * 0.5f, middle.y - 10.0f * s});
            ImGui::TextUnformatted(title);
            ImGui::PopFont();
            constexpr const char* formats = "glTF  /  GLB  /  FBX  /  3DS  /  BLEND*";
            ImGui::SetCursorPos({middle.x - ImGui::CalcTextSize(formats).x * 0.5f, middle.y + 28.0f * s});
            ImGui::TextColored(muted, "%s", formats);
            ImGui::SetCursorPos({middle.x - 66.0f * s, middle.y + 60.0f * s});
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.36f, 0.55f, 1.0f));
            if (ImGui::Button("Browse files...", {132.0f * s, 36.0f * s})) chooseFile();
            ImGui::PopStyleColor();
            constexpr const char* hint = "or drop a file here";
            ImGui::SetCursorPos({middle.x - ImGui::CalcTextSize(hint).x * 0.5f, middle.y + 110.0f * s});
            ImGui::TextColored(muted, "%s", hint);
            constexpr const char* blendHint = "* Blender files require Blender";
            ImGui::SetCursorPos({middle.x - ImGui::CalcTextSize(blendHint).x * 0.5f, middle.y + 133.0f * s});
            ImGui::TextColored(muted, "%s", blendHint);
        }
        if (!error_.empty() || loading_.valid()) {
            ImGui::SetCursorPos({16.0f * s, ui.toolbar + 14.0f * s});
            ImGui::PushTextWrapPos(ui.stageWidth() - 16.0f * s);
            if (loading_.valid()) ImGui::TextColored(accent, "Opening %s ...", loadingName_.c_str());
            else ImGui::TextColored(warning, "%s", error_.c_str());
            ImGui::PopTextWrapPos();
        }
    }

    void draw()
    {
        const auto ui = layout(window_, showInspector_);
        if (ui.scale != scale_) {
            scale_ = ui.scale;
            theme(scale_);
            SDL_SetWindowMinimumSize(window_, static_cast<int>(620.0f * scale_), static_cast<int>(430.0f * scale_));
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({ui.width, ui.height});
        ImGui::Begin("Model Viewer##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);
        auto* canvas = ImGui::GetWindowDrawList();
        canvas->AddRectFilled({0.0f, 0.0f}, {ui.width, ui.toolbar}, chrome);
        canvas->AddRectFilled({ui.stageWidth(), ui.toolbar}, {ui.width, ui.height - ui.footer}, panel);
        canvas->AddRectFilled({0.0f, ui.height - ui.footer}, {ui.width, ui.height}, chrome);
        canvas->AddLine({0.0f, ui.toolbar}, {ui.width, ui.toolbar}, line);
        canvas->AddLine({ui.stageWidth(), ui.toolbar}, {ui.stageWidth(), ui.height - ui.footer}, line);
        canvas->AddLine({0.0f, ui.height - ui.footer}, {ui.width, ui.height - ui.footer}, line);

        toolbar(ui);
        viewportOverlay(ui);
        inspector(ui);
        const float s = ui.scale;
        ImGui::SetCursorPos({15.0f * s, ui.height - ui.footer + 8.0f * s});
        ImGui::TextColored(muted, model_ ? "Drag to orbit   |   Shift + drag to pan   |   Scroll to zoom"
                                         : "Ready for a model");
        if (model_ && ui.width >= 610.0f * s) {
            const auto info = model_->format + "   /   " + count(model_->triangles) + " triangles";
            ImGui::SetCursorPos({ui.width - ImGui::CalcTextSize(info.c_str()).x - 15.0f * s,
                                 ui.height - ui.footer + 8.0f * s});
            ImGui::TextColored(muted, "%s", info.c_str());
        }
        ImGui::End();
        ImGui::Render();

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        if (width <= 0 || height <= 0 || ui.width <= 0 || ui.height <= 0) return;
        const auto y = static_cast<int>(std::lround(ui.toolbar * height / ui.height));
        const auto bottom = static_cast<int>(std::lround((ui.height - ui.footer) * height / ui.height));
        DrawRange selection;
        if (selectedComponent_ != none) {
            const auto component = visibility_.rows[selectedComponent_];
            selection = component.draw == none
                ? DrawRange{model_->nodes[component.node].firstDraw, visibility_.subtreeEnd[component.node]}
                : DrawRange{component.draw, component.draw + 1};
        }
        renderer_.paint(model_ ? &*model_ : nullptr, camera_,
                        {0, y, static_cast<int>(std::lround(ui.stageWidth() * width / ui.width)), bottom - y},
                        width, height, grid_, wireframe_, visibility_.drawVisible, selection);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window_);
    }

    SDL_Window* window_;
    Renderer renderer_;
    Camera camera_;
    std::optional<Model> model_;
    std::atomic_bool canceled_{false};
    std::future<Model> loading_;
    std::string pending_, loadingName_, error_;
    ComponentVisibility visibility_;
    std::size_t selectedComponent_ = none;
    std::size_t selectedMaterial_ = none;
    float scale_ = 0.0f;
    Uint8 dragButton_ = 0;
    bool running_ = true;
    bool showInspector_ = true;
    bool grid_ = true;
    bool wireframe_ = false;
    bool panning_ = false;
    bool dialogOpen_ = false;
};

class Desktop {
public:
    ~Desktop()
    {
        if (rendererReady) ImGui_ImplOpenGL3_Shutdown();
        if (platformReady) ImGui_ImplSDL3_Shutdown();
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        if (context) SDL_GL_DestroyContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void initialize()
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        dialogEvent = SDL_RegisterEvents(1);
        if (!dialogEvent) throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        window = SDL_CreateWindow("Model Viewer", 1216, 770,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window) throw std::runtime_error(SDL_GetError());
        SDL_SetWindowMinimumSize(window, 620, 430);
        context = SDL_GL_CreateContext(window);
        if (!context) throw std::runtime_error(std::string("An OpenGL 3.3-capable graphics driver is required. ") + SDL_GetError());
        SDL_GL_SetSwapInterval(1);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImFontConfig fontConfig;
        fontConfig.RasterizerDensity = std::max(1.0f, SDL_GetWindowPixelDensity(window));
#ifdef __APPLE__
        const auto font = std::filesystem::path("/System/Library/Fonts/Supplemental/Arial.ttf");
#elif defined(_WIN32)
        const char* windows = SDL_getenv("WINDIR");
        const auto font = std::filesystem::u8path(windows ? windows : "C:\\Windows") / "Fonts" / "segoeui.ttf";
#else
        const auto font = std::filesystem::path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
#endif
        std::error_code fontError;
        if (std::filesystem::is_regular_file(font, fontError)) {
            ImGui::GetIO().Fonts->AddFontFromFileTTF(font.u8string().c_str(), 0.0f, &fontConfig);
        }
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        platformReady = ImGui_ImplSDL3_InitForOpenGL(window, context);
        rendererReady = platformReady && ImGui_ImplOpenGL3_Init("#version 330 core");
        if (!rendererReady) throw std::runtime_error("The viewer interface could not initialize.");
    }

    SDL_Window* window = nullptr;

private:
    SDL_GLContext context = nullptr;
    bool platformReady = false;
    bool rendererReady = false;
};

} // namespace
} // namespace viewer

int main(int argc, char** argv)
{
    viewer::Desktop desktop;
    try {
        desktop.initialize();
        viewer::App app(desktop.window);
        if (argc > 1) app.open(argv[1]);
        app.run();
    } catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Model Viewer", error.what(), desktop.window);
        return 1;
    }
    return 0;
}

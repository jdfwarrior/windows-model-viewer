#pragma once

#include "model.hpp"

namespace viewer {

struct Viewport {
    int x;
    int y;
    int width;
    int height;
};

struct DrawRange {
    std::size_t first = 0;
    std::size_t end = 0;
};

struct Camera {
    glm::vec3 target{};
    float yaw = 0.66f;
    float pitch = 0.36f;
    float distance = 4.0f;
    bool orthographic = false;

    glm::vec3 eye() const;
    void frame(float aspect);
    void orbit(float dx, float dy);
    void pan(float dx, float dy, float height);
    void zoom(float amount);
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    void upload(const Model& model);
    void paint(const Model* model, const Camera& camera, Viewport stage, int width, int height,
               bool grid, bool wireframe, const std::vector<std::uint8_t>& visibleDraws, DrawRange selection);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace viewer

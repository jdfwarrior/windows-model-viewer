#pragma once

#include "model.hpp"

#include <algorithm>

namespace viewer {

struct Component {
    std::size_t node = none;
    std::size_t draw = none;
};

struct ComponentVisibility {
    ComponentVisibility() = default;

    explicit ComponentVisibility(const Model& model)
        : subtreeEnd(model.nodes.size()), nodeEnabled(model.nodes.size(), 1),
          nodeVisible(model.nodes.size()), drawEnabled(model.draws.size(), 1),
          drawVisible(model.draws.size())
    {
        // Draws are stored in depth-first node order, so each branch owns a contiguous range.
        for (std::size_t i = model.nodes.size(); i > 0; --i) {
            const auto& node = model.nodes[i - 1];
            auto& end = subtreeEnd[i - 1];
            end = std::max(end, node.firstDraw + node.drawCount);
            if (node.parent != none) subtreeEnd[node.parent] = std::max(subtreeEnd[node.parent], end);
        }
        rows.reserve(model.nodes.size());
        for (std::size_t i = 0; i < model.nodes.size(); ++i) {
            const auto& node = model.nodes[i];
            rows.push_back({i, none});
            // Give a node's own mesh separate controls when its checkbox also covers other draws.
            if (subtreeEnd[i] - node.firstDraw > 1) {
                for (std::size_t draw = node.firstDraw; draw < node.firstDraw + node.drawCount; ++draw) {
                    rows.push_back({i, draw});
                }
            }
        }
        update(model);
    }

    void update(const Model& model)
    {
        for (std::size_t i = 0; i < model.nodes.size(); ++i) {
            const auto parent = model.nodes[i].parent;
            nodeVisible[i] = nodeEnabled[i] && (parent == none || nodeVisible[parent]);
        }
        visibleDrawCount = 0;
        for (std::size_t i = 0; i < model.draws.size(); ++i) {
            drawVisible[i] = drawEnabled[i] && nodeVisible[model.draws[i].node];
            visibleDrawCount += drawVisible[i];
        }
    }

    void showAll(const Model& model)
    {
        std::fill(nodeEnabled.begin(), nodeEnabled.end(), 1);
        std::fill(drawEnabled.begin(), drawEnabled.end(), 1);
        update(model);
    }

    std::vector<Component> rows;
    std::vector<std::size_t> subtreeEnd;
    std::vector<std::uint8_t> nodeEnabled, nodeVisible;
    std::vector<std::uint8_t> drawEnabled, drawVisible;
    std::size_t visibleDrawCount = 0;
};

} // namespace viewer

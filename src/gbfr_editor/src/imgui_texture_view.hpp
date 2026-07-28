#pragma once

#include <imgui.h>

#include <algorithm>

namespace gbfr::editor {

inline void draw_checkerboard(ImVec2 minimum, ImVec2 maximum, float cell_size = 12.0f) {
    auto* draw_list = ImGui::GetWindowDrawList();
    const auto dark = ImGui::GetColorU32(ImVec4(0.28f, 0.29f, 0.31f, 1.0f));
    const auto light = ImGui::GetColorU32(ImVec4(0.48f, 0.49f, 0.52f, 1.0f));
    draw_list->AddRectFilled(minimum, maximum, dark);
    const int columns = static_cast<int>((maximum.x - minimum.x + cell_size - 1.0f) / cell_size);
    const int rows = static_cast<int>((maximum.y - minimum.y + cell_size - 1.0f) / cell_size);
    for (int row = 0; row < rows; ++row) {
        for (int column = row & 1; column < columns; column += 2) {
            const ImVec2 cell_min{minimum.x + column * cell_size, minimum.y + row * cell_size};
            const ImVec2 cell_max{std::min(cell_min.x + cell_size, maximum.x),
                                  std::min(cell_min.y + cell_size, maximum.y)};
            draw_list->AddRectFilled(cell_min, cell_max, light);
        }
    }
}

inline void image_on_checkerboard(ImTextureID texture, ImVec2 size, float cell_size = 12.0f) {
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const ImVec2 maximum{minimum.x + size.x, minimum.y + size.y};
    draw_checkerboard(minimum, maximum, cell_size);
    ImGui::Image(texture, size);
}

}

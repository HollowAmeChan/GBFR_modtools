#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace gbfr {
std::vector<std::filesystem::path> adjacent_mmat_variant_jsons(
    const std::filesystem::path& current);

std::size_t propagate_mmat_material_render_settings(
    const std::filesystem::path& current, std::size_t material_index);
}

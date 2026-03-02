#include "appearance.hpp"
#include "color_palette.hpp"

namespace dune3d {

Appearance::Appearance()
{
    // Fallback palette if no preset is available: flat light background, Adwaita-like accents.
    colors[ColorP::BACKGROUND_BOTTOM] = {0.965, 0.961, 0.957};
    colors[ColorP::BACKGROUND_TOP] = {0.965, 0.961, 0.957};
    colors[ColorP::CONSTRAINT] = {0.110, 0.443, 0.847};
    colors[ColorP::CONSTRUCTION_ENTITY] = {0.450, 0.450, 0.450};
    colors[ColorP::CONSTRUCTION_POINT] = {0.290, 0.720, 0.520};
    colors[ColorP::ENTITY] = {0.180, 0.204, 0.212};
    colors[ColorP::HOVER] = {0.900, 0.380, 0.000};
    colors[ColorP::INACTIVE_ENTITY] = {0.600, 0.600, 0.590};
    colors[ColorP::INACTIVE_POINT] = {0.560, 0.790, 0.660};
    colors[ColorP::POINT] = {0.180, 0.760, 0.490};
    colors[ColorP::SELECTED] = {0.210, 0.520, 0.890};
    colors[ColorP::SELECTED_HOVER] = {0.360, 0.620, 0.930};
    colors[ColorP::SOLID_MODEL] = {0.900, 0.900, 0.900};
    colors[ColorP::OTHER_BODY_SOLID_MODEL] = {0.850, 0.850, 0.850};
    colors[ColorP::HIGHLIGHT] = {0.960, 0.830, 0.180};
    colors[ColorP::SELECTION_BOX] = {0.210, 0.520, 0.890};
    colors[ColorP::ERROR_OVERLAY] = {1, 0, 0};
}

const Color &Appearance::get_color(const ColorP &color) const
{
    static const Color default_color{1, 0, 1};
    if (colors.contains(color))
        return colors.at(color);
    else
        return default_color;
}

} // namespace dune3d

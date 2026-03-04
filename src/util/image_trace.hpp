#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace dune3d {

class PictureData;

struct ImageTraceSettings {
    int threshold = 160;
    double smoothness = 1.2;
    int noise_ignore = 16;
    double opt_tolerance = 0.38;
    int blur = 1;
    bool anti_stair = true;
    double anti_stair_strength = 0.65;
    double detail_preserve = 0.65;
    bool outline_mode = false;
    bool outline_with_trace = false;
    double outline_offset_mm = 0.0;
    bool curve_fit = true;
    double curve_strength = 0.85;
};

struct ImageTraceResult {
    bool ok = false;
    std::string error;
    unsigned int width = 0;
    unsigned int height = 0;
    bool outline_mode = false;
    std::vector<std::vector<glm::dvec2>> contours;
    std::string svg;
};

ImageTraceResult trace_picture_to_svg(const PictureData &picture, const ImageTraceSettings &settings);

} // namespace dune3d

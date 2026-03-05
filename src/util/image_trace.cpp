#include "image_trace.hpp"
#include "picture_data.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <sstream>

namespace dune3d {
namespace {
struct IntPoint {
    int x = 0;
    int y = 0;

    bool operator==(const IntPoint &other) const = default;
    bool operator<(const IntPoint &other) const
    {
        if (x != other.x)
            return x < other.x;
        return y < other.y;
    }
};

struct Edge {
    IntPoint a;
    IntPoint b;
    bool used = false;
};

static double point_line_distance(const glm::dvec2 &p, const glm::dvec2 &a, const glm::dvec2 &b)
{
    const auto ab = b - a;
    const auto len = glm::length(ab);
    if (len < 1e-12)
        return glm::length(p - a);
    const auto area = std::abs((p.x - a.x) * ab.y - (p.y - a.y) * ab.x);
    return area / len;
}

static void rdp_impl(const std::vector<glm::dvec2> &points, std::size_t begin, std::size_t end, double epsilon,
                     std::vector<bool> &keep)
{
    if (end <= begin + 1)
        return;

    double max_dist = -1;
    std::size_t index = begin;
    for (std::size_t i = begin + 1; i < end; i++) {
        const auto d = point_line_distance(points.at(i), points.at(begin), points.at(end));
        if (d > max_dist) {
            max_dist = d;
            index = i;
        }
    }

    if (max_dist > epsilon) {
        keep.at(index) = true;
        rdp_impl(points, begin, index, epsilon, keep);
        rdp_impl(points, index, end, epsilon, keep);
    }
}

static std::vector<glm::dvec2> simplify_closed_polygon(const std::vector<glm::dvec2> &poly, double epsilon)
{
    if (poly.size() < 4 || epsilon <= 1e-9)
        return poly;

    std::vector<glm::dvec2> open = poly;
    open.push_back(poly.front());

    std::vector<bool> keep(open.size(), false);
    keep.front() = true;
    keep.back() = true;
    rdp_impl(open, 0, open.size() - 1, epsilon, keep);

    std::vector<glm::dvec2> out;
    out.reserve(open.size());
    for (std::size_t i = 0; i + 1 < open.size(); i++) {
        if (keep.at(i))
            out.push_back(open.at(i));
    }

    if (out.size() < 3)
        return poly;
    return out;
}

static std::vector<glm::dvec2> remove_collinear(const std::vector<glm::dvec2> &poly)
{
    if (poly.size() < 4)
        return poly;

    std::vector<glm::dvec2> out;
    out.reserve(poly.size());
    for (std::size_t i = 0; i < poly.size(); i++) {
        const auto &prev = poly.at((i + poly.size() - 1) % poly.size());
        const auto &cur = poly.at(i);
        const auto &next = poly.at((i + 1) % poly.size());
        const auto v1 = cur - prev;
        const auto v2 = next - cur;
        const auto cross = v1.x * v2.y - v1.y * v2.x;
        if (std::abs(cross) > 1e-9)
            out.push_back(cur);
    }

    if (out.size() < 3)
        return poly;
    return out;
}

static std::vector<glm::dvec2> chaikin_closed(const std::vector<glm::dvec2> &poly, int iterations)
{
    if (poly.size() < 3 || iterations <= 0)
        return poly;

    std::vector<glm::dvec2> out = poly;
    for (int it = 0; it < iterations; it++) {
        std::vector<glm::dvec2> next;
        next.reserve(out.size() * 2);
        for (std::size_t i = 0; i < out.size(); i++) {
            const auto &a = out.at(i);
            const auto &b = out.at((i + 1) % out.size());
            const auto q = a * 0.75 + b * 0.25;
            const auto r = a * 0.25 + b * 0.75;
            next.push_back(q);
            next.push_back(r);
        }
        out = std::move(next);
    }
    return out;
}

static std::vector<glm::dvec2> smooth_closed_preserve_corners(const std::vector<glm::dvec2> &poly, int iterations,
                                                               double corner_keep_deg)
{
    if (poly.size() < 5 || iterations <= 0)
        return poly;

    std::vector<glm::dvec2> out = poly;
    const double corner_keep_rad = std::clamp(corner_keep_deg, 5.0, 175.0) * (M_PI / 180.0);

    for (int it = 0; it < iterations; it++) {
        std::vector<glm::dvec2> next = out;
        for (std::size_t i = 0; i < out.size(); i++) {
            const auto &prev = out.at((i + out.size() - 1) % out.size());
            const auto &cur = out.at(i);
            const auto &nxt = out.at((i + 1) % out.size());

            const auto v1 = prev - cur;
            const auto v2 = nxt - cur;
            const auto l1 = glm::length(v1);
            const auto l2 = glm::length(v2);
            if (l1 < 1e-9 || l2 < 1e-9)
                continue;

            const auto dot = std::clamp(glm::dot(v1 / l1, v2 / l2), -1.0, 1.0);
            const auto angle = std::acos(dot);
            if (angle < corner_keep_rad)
                continue;

            next.at(i) = (prev + cur * 2.0 + nxt) * 0.25;
        }
        out = std::move(next);
    }
    return out;
}

static std::vector<glm::dvec2> suppress_micro_waves(const std::vector<glm::dvec2> &poly, double max_dev,
                                                     double max_extra_path, int passes)
{
    if (poly.size() < 6 || passes <= 0 || max_dev <= 1e-9 || max_extra_path <= 1e-9)
        return poly;

    std::vector<glm::dvec2> out = poly;
    const double straight_angle_min = 2.30; // ~132 deg, preserve sharper corners

    for (int pass = 0; pass < passes; pass++) {
        if (out.size() < 6)
            break;

        std::vector<glm::dvec2> next;
        next.reserve(out.size());

        for (std::size_t i = 0; i < out.size(); i++) {
            const auto &prev = out.at((i + out.size() - 1) % out.size());
            const auto &cur = out.at(i);
            const auto &nxt = out.at((i + 1) % out.size());

            const auto v_prev = cur - prev;
            const auto v_next = nxt - cur;
            const auto l_prev = glm::length(v_prev);
            const auto l_next = glm::length(v_next);
            const auto l_chord = glm::length(nxt - prev);

            bool remove = false;
            if (l_prev > 1e-9 && l_next > 1e-9 && l_chord > 1e-9) {
                const auto dot = std::clamp(glm::dot(v_prev / l_prev, v_next / l_next), -1.0, 1.0);
                const auto angle = std::acos(dot);
                const double dev = point_line_distance(cur, prev, nxt);
                const double extra_path = (l_prev + l_next) - l_chord;

                if (angle >= straight_angle_min && dev <= max_dev && extra_path <= max_extra_path) {
                    remove = true;
                }
            }

            if (!remove)
                next.push_back(cur);
        }

        if (next.size() < 3)
            break;
        if (next.size() == out.size())
            break;
        out = std::move(next);
    }

    return out;
}

static double polygon_area_abs(const std::vector<glm::dvec2> &poly)
{
    if (poly.size() < 3)
        return 0;
    double sum = 0;
    for (std::size_t i = 0; i < poly.size(); i++) {
        const auto &a = poly.at(i);
        const auto &b = poly.at((i + 1) % poly.size());
        sum += a.x * b.y - b.x * a.y;
    }
    return std::abs(sum) * 0.5;
}

static double polygon_area_signed(const std::vector<glm::dvec2> &poly)
{
    if (poly.size() < 3)
        return 0;
    double sum = 0;
    for (std::size_t i = 0; i < poly.size(); i++) {
        const auto &a = poly.at(i);
        const auto &b = poly.at((i + 1) % poly.size());
        sum += a.x * b.y - b.x * a.y;
    }
    return sum * 0.5;
}

static std::vector<glm::dvec2> offset_closed_polygon(const std::vector<glm::dvec2> &poly, double distance)
{
    if (poly.size() < 3 || std::abs(distance) < 1e-9)
        return poly;

    const double signed_area = polygon_area_signed(poly);
    const double orientation = (signed_area >= 0.0) ? 1.0 : -1.0;
    std::vector<glm::dvec2> out(poly.size());

    for (std::size_t i = 0; i < poly.size(); i++) {
        const auto &prev = poly.at((i + poly.size() - 1) % poly.size());
        const auto &cur = poly.at(i);
        const auto &next = poly.at((i + 1) % poly.size());

        auto e1 = cur - prev;
        auto e2 = next - cur;
        const auto l1 = glm::length(e1);
        const auto l2 = glm::length(e2);
        if (l1 < 1e-9 || l2 < 1e-9) {
            out.at(i) = cur;
            continue;
        }
        e1 /= l1;
        e2 /= l2;

        glm::dvec2 n1;
        glm::dvec2 n2;
        if (orientation > 0) {
            n1 = {e1.y, -e1.x};
            n2 = {e2.y, -e2.x};
        }
        else {
            n1 = {-e1.y, e1.x};
            n2 = {-e2.y, e2.x};
        }

        const auto bis = n1 + n2;
        const auto bis_len = glm::length(bis);
        if (bis_len < 1e-9) {
            out.at(i) = cur + n1 * distance;
            continue;
        }
        const auto bis_n = bis / bis_len;
        const auto denom = std::max(0.15, std::abs(glm::dot(bis_n, n2)));
        const auto miter = std::clamp(distance / denom, -4.0 * std::abs(distance), 4.0 * std::abs(distance));
        out.at(i) = cur + bis_n * miter;
    }

    return out;
}

static std::vector<glm::dvec2> offset_outer_polygon(const std::vector<glm::dvec2> &poly, double distance)
{
    if (poly.size() < 3 || std::abs(distance) < 1e-9)
        return poly;

    const auto candidate_a = offset_closed_polygon(poly, distance);
    const auto candidate_b = offset_closed_polygon(poly, -distance);
    const auto base_area = polygon_area_abs(poly);
    const auto area_a = polygon_area_abs(candidate_a);
    const auto area_b = polygon_area_abs(candidate_b);

    if (distance > 0) {
        if (area_a >= area_b && area_a >= base_area)
            return candidate_a;
        if (area_b >= base_area)
            return candidate_b;
    }
    else {
        if (area_a <= area_b && area_a <= base_area)
            return candidate_a;
        if (area_b <= base_area)
            return candidate_b;
    }
    return candidate_a;
}

static void blur_luma(std::vector<float> &luma, unsigned int w, unsigned int h, int iterations)
{
    if (iterations <= 0 || w == 0 || h == 0)
        return;

    std::vector<float> tmp(luma.size());
    for (int it = 0; it < iterations; it++) {
        for (unsigned int y = 0; y < h; y++) {
            for (unsigned int x = 0; x < w; x++) {
                float sum = 0;
                int count = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        const int xx = static_cast<int>(x) + dx;
                        const int yy = static_cast<int>(y) + dy;
                        if (xx < 0 || yy < 0 || xx >= static_cast<int>(w) || yy >= static_cast<int>(h))
                            continue;
                        sum += luma.at(static_cast<std::size_t>(yy) * w + static_cast<std::size_t>(xx));
                        count++;
                    }
                }
                tmp.at(static_cast<std::size_t>(y) * w + x) = (count > 0) ? (sum / static_cast<float>(count)) : 255.f;
            }
        }
        luma.swap(tmp);
    }
}

static std::vector<uint8_t> build_mask(const PictureData &picture, const ImageTraceSettings &settings)
{
    const auto w = picture.m_width;
    const auto h = picture.m_height;
    std::vector<float> luma(static_cast<std::size_t>(w) * h, 255.f);
    std::vector<uint8_t> alpha(static_cast<std::size_t>(w) * h, 255);

    for (unsigned int y = 0; y < h; y++) {
        for (unsigned int x = 0; x < w; x++) {
            const auto px = picture.m_data.at(static_cast<std::size_t>(y) * w + x);
            const int b = px & 0xff;
            const int g = (px >> 8) & 0xff;
            const int r = (px >> 16) & 0xff;
            const int a = (px >> 24) & 0xff;
            luma.at(static_cast<std::size_t>(y) * w + x) =
                    0.2126f * static_cast<float>(r) + 0.7152f * static_cast<float>(g) + 0.0722f * static_cast<float>(b);
            alpha.at(static_cast<std::size_t>(y) * w + x) = static_cast<uint8_t>(a);
        }
    }

    const int blur_iterations = std::clamp(settings.blur, 0, 5);
    blur_luma(luma, w, h, blur_iterations);

    const int threshold = std::clamp(settings.threshold, 0, 255);
    std::vector<uint8_t> mask(static_cast<std::size_t>(w) * h, 0);
    for (unsigned int y = 0; y < h; y++) {
        for (unsigned int x = 0; x < w; x++) {
            const auto idx = static_cast<std::size_t>(y) * w + x;
            if (alpha.at(idx) < 16)
                continue;
            const bool fg = luma.at(idx) <= threshold;
            mask.at(idx) = fg ? 1 : 0;
        }
    }

    return mask;
}

static void remove_small_components(std::vector<uint8_t> &mask, unsigned int w, unsigned int h, int min_area)
{
    if (min_area <= 1)
        return;

    std::vector<uint8_t> visited(mask.size(), 0);
    constexpr std::array<int, 8> dx = {-1, 0, 1, -1, 1, -1, 0, 1};
    constexpr std::array<int, 8> dy = {-1, -1, -1, 0, 0, 1, 1, 1};

    for (unsigned int y = 0; y < h; y++) {
        for (unsigned int x = 0; x < w; x++) {
            const auto idx = static_cast<std::size_t>(y) * w + x;
            if (!mask.at(idx) || visited.at(idx))
                continue;

            std::deque<IntPoint> queue;
            std::vector<std::size_t> component;
            queue.push_back({static_cast<int>(x), static_cast<int>(y)});
            visited.at(idx) = 1;

            while (!queue.empty()) {
                const auto p = queue.front();
                queue.pop_front();
                const auto p_idx = static_cast<std::size_t>(p.y) * w + static_cast<std::size_t>(p.x);
                component.push_back(p_idx);

                for (std::size_t i = 0; i < dx.size(); i++) {
                    const int xx = p.x + dx.at(i);
                    const int yy = p.y + dy.at(i);
                    if (xx < 0 || yy < 0 || xx >= static_cast<int>(w) || yy >= static_cast<int>(h))
                        continue;
                    const auto n_idx = static_cast<std::size_t>(yy) * w + static_cast<std::size_t>(xx);
                    if (!mask.at(n_idx) || visited.at(n_idx))
                        continue;
                    visited.at(n_idx) = 1;
                    queue.push_back({xx, yy});
                }
            }

            if (static_cast<int>(component.size()) < min_area) {
                for (const auto p_idx : component)
                    mask.at(p_idx) = 0;
            }
        }
    }
}

static std::vector<std::vector<glm::dvec2>> extract_contours(const std::vector<uint8_t> &mask, unsigned int w,
                                                             unsigned int h, double simplify_epsilon)
{
    std::vector<Edge> edges;
    edges.reserve(mask.size() * 2);

    auto is_fg = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= static_cast<int>(w) || y >= static_cast<int>(h))
            return false;
        const auto idx = static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x);
        return mask.at(idx) != 0;
    };

    for (int y = 0; y < static_cast<int>(h); y++) {
        for (int x = 0; x < static_cast<int>(w); x++) {
            if (!is_fg(x, y))
                continue;

            if (!is_fg(x, y - 1))
                edges.push_back({{x, y}, {x + 1, y}});
            if (!is_fg(x + 1, y))
                edges.push_back({{x + 1, y}, {x + 1, y + 1}});
            if (!is_fg(x, y + 1))
                edges.push_back({{x + 1, y + 1}, {x, y + 1}});
            if (!is_fg(x - 1, y))
                edges.push_back({{x, y + 1}, {x, y}});
        }
    }

    std::map<IntPoint, std::vector<std::size_t>> adjacency;
    for (std::size_t i = 0; i < edges.size(); i++)
        adjacency[edges.at(i).a].push_back(i);

    std::vector<std::vector<glm::dvec2>> contours;
    for (std::size_t i = 0; i < edges.size(); i++) {
        if (edges.at(i).used)
            continue;

        std::vector<glm::dvec2> poly;
        auto first = edges.at(i).a;
        auto current = edges.at(i).a;
        auto next = edges.at(i).b;

        edges.at(i).used = true;
        poly.push_back({static_cast<double>(current.x), static_cast<double>(current.y)});

        for (std::size_t guard = 0; guard < edges.size() + 8; guard++) {
            poly.push_back({static_cast<double>(next.x), static_cast<double>(next.y)});
            current = next;
            if (current == first)
                break;

            auto it = adjacency.find(current);
            if (it == adjacency.end())
                break;

            bool found = false;
            for (const auto edge_idx : it->second) {
                auto &edge = edges.at(edge_idx);
                if (edge.used)
                    continue;
                edge.used = true;
                next = edge.b;
                found = true;
                break;
            }
            if (!found)
                break;
        }

        if (poly.size() < 4)
            continue;
        if (glm::distance(poly.front(), poly.back()) > 1e-9)
            continue;

        poly.pop_back();
        poly = remove_collinear(poly);
        poly = simplify_closed_polygon(poly, std::max(0.0, simplify_epsilon));
        if (poly.size() < 3)
            continue;
        if (polygon_area_abs(poly) < 1.0)
            continue;

        contours.push_back(std::move(poly));
    }

    return contours;
}

static std::string contours_to_svg(const std::vector<std::vector<glm::dvec2>> &contours, unsigned int w, unsigned int h,
                                   const ImageTraceSettings &settings)
{
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(2);
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\"0 0 " << w << " " << h
       << "\">\n";
    for (const auto &poly : contours) {
        if (poly.size() < 3)
            continue;
        ss << "  <path d=\"";
        if (!settings.curve_fit || poly.size() < 4) {
            ss << "M " << poly.front().x << " " << poly.front().y;
            for (std::size_t i = 1; i < poly.size(); i++) {
                ss << " L " << poly.at(i).x << " " << poly.at(i).y;
            }
        }
        else {
            const double k = std::clamp(settings.curve_strength, 0.0, 1.5) / 6.0;
            ss << "M " << poly.front().x << " " << poly.front().y;
            for (std::size_t i = 0; i < poly.size(); i++) {
                const auto &p_prev = poly.at((i + poly.size() - 1) % poly.size());
                const auto &p0 = poly.at(i);
                const auto &p1 = poly.at((i + 1) % poly.size());
                const auto &p2 = poly.at((i + 2) % poly.size());

                const auto c1 = p0 + (p1 - p_prev) * k;
                const auto c2 = p1 - (p2 - p0) * k;
                ss << " C " << c1.x << " " << c1.y << " " << c2.x << " " << c2.y << " " << p1.x << " " << p1.y;
            }
        }
        ss << " Z\" fill=\"none\" stroke=\"#000000\" stroke-width=\"1\"/>\n";
    }
    ss << "</svg>\n";
    return ss.str();
}
} // namespace

ImageTraceResult trace_picture_to_svg(const PictureData &picture, const ImageTraceSettings &settings)
{
    ImageTraceResult result;
    result.width = picture.m_width;
    result.height = picture.m_height;

    if (picture.m_width == 0 || picture.m_height == 0) {
        result.error = "Image is empty";
        return result;
    }

    auto mask = build_mask(picture, settings);
    remove_small_components(mask, picture.m_width, picture.m_height, std::clamp(settings.noise_ignore, 0, 100000));

    const double anti_stair_strength = settings.anti_stair ? std::clamp(settings.anti_stair_strength, 0.0, 1.0) : 0.0;
    const double detail_preserve = std::clamp(settings.detail_preserve, 0.0, 1.0);
    const double opt_tolerance = std::clamp(settings.opt_tolerance, 0.0, 1.0);

    double simplify_epsilon = 0.0;
    if (settings.curve_fit) {
        const double base = 0.07 + std::max(0.0, settings.smoothness) * 0.05;
        const double extra = (0.08 + std::max(0.0, settings.smoothness) * 0.06) * anti_stair_strength;
        const double opt_extra = opt_tolerance * (0.05 + std::max(0.0, settings.smoothness) * 0.05);
        simplify_epsilon = base + extra * (1.0 - 0.55 * detail_preserve) + opt_extra;
    }
    else {
        simplify_epsilon = std::max(0.0, settings.smoothness) * 0.30;
    }

    result.contours = extract_contours(mask, picture.m_width, picture.m_height, simplify_epsilon);

    if (result.contours.empty()) {
        result.error = "No contours found. Try lowering threshold or noise filter.";
        return result;
    }

    if (settings.curve_fit) {
        const int denoise_base =
                std::clamp(static_cast<int>(std::lround(settings.smoothness * (0.7 + opt_tolerance * 0.5))), 0, 6);
        const int denoise_iterations =
                std::clamp(static_cast<int>(std::lround(denoise_base * anti_stair_strength * (1.0 - 0.40 * detail_preserve))),
                           0, 6);
        int iterations = std::clamp(static_cast<int>(std::lround(settings.smoothness * 0.65)), 0, 5);
        if (settings.smoothness > 0.2)
            iterations = std::max(iterations, 1);
        const int adjusted_iterations =
                std::clamp(static_cast<int>(std::lround(iterations * (1.05 - 0.35 * detail_preserve))), 1, 6);
        const double corner_keep_deg = 26.0 + detail_preserve * 44.0 - opt_tolerance * 6.0;
        const double post_simplify =
                (0.03 + anti_stair_strength * 0.12 + opt_tolerance * 0.10) * (1.0 - 0.55 * detail_preserve);
        const int anti_stair_passes =
                std::clamp(static_cast<int>(std::lround(1.0 + anti_stair_strength * 2.0 + settings.smoothness * 0.20)),
                           1, 4);
        const double anti_stair_dev =
                0.08 + anti_stair_strength * 0.42 + opt_tolerance * 0.10 + std::max(0.0, settings.smoothness) * 0.08;
        const double anti_stair_extra = anti_stair_dev * (1.15 + (1.0 - detail_preserve) * 0.65);
        for (auto &poly : result.contours) {
            if (denoise_iterations > 0)
                poly = smooth_closed_preserve_corners(poly, denoise_iterations, corner_keep_deg);
            poly = chaikin_closed(poly, adjusted_iterations);
            if (poly.size() < 3)
                continue;
            if (settings.anti_stair)
                poly = suppress_micro_waves(poly, anti_stair_dev, anti_stair_extra, anti_stair_passes);
            if (post_simplify > 1e-9)
                poly = simplify_closed_polygon(poly, post_simplify);
            poly = remove_collinear(poly);
        }
    }

    result.outline_mode = settings.outline_mode;
    if (settings.outline_mode) {
        std::size_t best_idx = 0;
        double best_area = -1.0;
        for (std::size_t i = 0; i < result.contours.size(); i++) {
            const auto area = polygon_area_abs(result.contours.at(i));
            if (area > best_area) {
                best_area = area;
                best_idx = i;
            }
        }

        auto outline = result.contours.at(best_idx);
        const double offset_px = settings.outline_offset_mm * (96.0 / 25.4);
        outline = offset_outer_polygon(outline, offset_px);
        outline = remove_collinear(outline);
        if (outline.size() < 3) {
            result.error = "Outline failed. Try lowering offset.";
            return result;
        }
        if (settings.outline_with_trace) {
            result.contours.push_back(std::move(outline));
        }
        else {
            result.contours.clear();
            result.contours.push_back(std::move(outline));
        }
    }

    result.svg = contours_to_svg(result.contours, result.width, result.height, settings);
    result.ok = true;
    return result;
}

} // namespace dune3d

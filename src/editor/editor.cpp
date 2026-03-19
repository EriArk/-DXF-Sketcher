#include "editor.hpp"
#include "dune3d_appwindow.hpp"
#include "core/tool_id.hpp"
#include "widgets/constraints_box.hpp"
#include "action/action_id.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include "in_tool_action/in_tool_action_catalog.hpp"
#include "canvas/canvas.hpp"
#include "document/entity/entity.hpp"
#include "document/entity/entity_document.hpp"
#include "document/group/group_sketch.hpp"
#include "document/group/group_reference.hpp"
#include "tool_popover.hpp"
#include "dune3d_application.hpp"
#include "util/selection_util.hpp"
#include "group_editor/group_editor.hpp"
#include "render/renderer.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_bezier2d.hpp"
#include "document/entity/entity_point2d.hpp"
#include "document/entity/entity_cluster.hpp"
#include "document/entity/entity_text.hpp"
#include "document/entity/ientity_in_workplane.hpp"
#include "document/entity/ientity_in_workplane_set.hpp"
#include "logger/logger.hpp"
#include "document/constraint/constraint.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "util/fs_util.hpp"
#include "util/sketch_theme.hpp"
#include "util/util.hpp"
#include "selection_editor.hpp"
#include "preferences/color_presets.hpp"
#include "workspace_browser.hpp"
#include "document/constraint/iconstraint_workplane.hpp"
#include "document/export_dxf.hpp"
#include "document/export_paths.hpp"
#include "import_dxf/dxf_importer.hpp"
#include "widgets/clipping_plane_window.hpp"
#include "widgets/selection_filter_window.hpp"
#include "dialogs/image_trace_dialog.hpp"
#include "core/tools/tool_draw_regular_polygon.hpp"
#include "core/tools/tool_draw_rectangle.hpp"
#include "core/tools/tool_draw_circle_2d.hpp"
#include "core/tools/tool_draw_text.hpp"
#include "core/tool_data_path.hpp"
#include "system/system.hpp"
#include "logger/log_util.hpp"
#include "util/text_render.hpp"
#include "util/uuid.hpp"
#include "nanosvg.h"
#include "nlohmann/json.hpp"
#include "buffer.hpp"
#include "icon_texture_id.hpp"
#include <iostream>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <vector>
#include <array>
#include <filesystem>
#include <cmath>
#include <iomanip>
#include <functional>
#include <sstream>
#include <functional>
#include <limits>
#include <memory>
#include <fstream>
#include <cstdint>
#include <chrono>
#include <glib.h>
#include <glm/gtx/quaternion.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace dune3d {
namespace {
std::filesystem::path normalize_group_path(const std::filesystem::path &path)
{
    try {
        return std::filesystem::absolute(path).lexically_normal();
    }
    catch (...) {
        return path.lexically_normal();
    }
}

std::string format_line_width_multiplier(double line_width)
{
    constexpr double base_line_width = 2.5;
    const double multiplier = line_width / base_line_width;
    std::ostringstream ss;
    if (std::abs(multiplier - std::round(multiplier)) < 0.05) {
        ss << static_cast<int>(std::round(multiplier));
    }
    else {
        ss << std::fixed << std::setprecision(1) << multiplier;
    }
    ss << "x";
    return ss.str();
}

constexpr std::array<CanvasPreferences::ThemeVariant, 5> kSketchThemeOrder = {
        CanvasPreferences::ThemeVariant::LIGHT,
        CanvasPreferences::ThemeVariant::MIX,
        CanvasPreferences::ThemeVariant::DARK,
        CanvasPreferences::ThemeVariant::HEAVEN,
        CanvasPreferences::ThemeVariant::DARK_BLUE,
};

constexpr std::array<CanvasPreferences::AccentVariant, 5> kSketchAccentOrder = {
        CanvasPreferences::AccentVariant::BLUE,
        CanvasPreferences::AccentVariant::ORANGE,
        CanvasPreferences::AccentVariant::TEAL,
        CanvasPreferences::AccentVariant::PINK,
        CanvasPreferences::AccentVariant::LIME,
};

const char *sketch_accent_css_class(CanvasPreferences::AccentVariant accent)
{
    switch (accent) {
    case CanvasPreferences::AccentVariant::BLUE:
        return "sketch-accent-blue";
    case CanvasPreferences::AccentVariant::ORANGE:
        return "sketch-accent-orange";
    case CanvasPreferences::AccentVariant::TEAL:
        return "sketch-accent-teal";
    case CanvasPreferences::AccentVariant::PINK:
        return "sketch-accent-pink";
    case CanvasPreferences::AccentVariant::LIME:
        return "sketch-accent-lime";
    default:
        return "sketch-accent-blue";
    }
}

CanvasPreferences::ThemeVariant normalize_sketch_theme_variant(CanvasPreferences::ThemeVariant variant)
{
    switch (variant) {
    case CanvasPreferences::ThemeVariant::DARK:
    case CanvasPreferences::ThemeVariant::LIGHT:
    case CanvasPreferences::ThemeVariant::MIX:
    case CanvasPreferences::ThemeVariant::DARK_BLUE:
    case CanvasPreferences::ThemeVariant::HEAVEN:
        return variant;
    case CanvasPreferences::ThemeVariant::AUTO:
    default:
        return CanvasPreferences::ThemeVariant::LIGHT;
    }
}

CanvasPreferences::ThemeVariant cycle_sketch_theme_variant(CanvasPreferences::ThemeVariant current, int dir)
{
    const auto normalized = normalize_sketch_theme_variant(current);
    auto it = std::find(kSketchThemeOrder.begin(), kSketchThemeOrder.end(), normalized);
    if (it == kSketchThemeOrder.end())
        it = kSketchThemeOrder.begin();
    const auto idx = static_cast<int>(std::distance(kSketchThemeOrder.begin(), it));
    const auto size = static_cast<int>(kSketchThemeOrder.size());
    const auto next_idx = (idx + (dir >= 0 ? 1 : -1) + size) % size;
    return kSketchThemeOrder.at(static_cast<std::size_t>(next_idx));
}

const char *sketch_theme_variant_name(CanvasPreferences::ThemeVariant variant)
{
    switch (normalize_sketch_theme_variant(variant)) {
    case CanvasPreferences::ThemeVariant::LIGHT:
        return "Light";
    case CanvasPreferences::ThemeVariant::MIX:
        return "Mix";
    case CanvasPreferences::ThemeVariant::DARK:
        return "Dark";
    case CanvasPreferences::ThemeVariant::DARK_BLUE:
        return "Blue";
    case CanvasPreferences::ThemeVariant::HEAVEN:
        return "Light-Blue";
    case CanvasPreferences::ThemeVariant::AUTO:
    default:
        return "Light";
    }
}

[[maybe_unused]] bool group_has_user_entities(const Document &doc, const UUID &group_uu)
{
    for (const auto &[en_uu, en] : doc.m_entities) {
        if (en->m_group != group_uu)
            continue;
        if (en->m_kind != ItemKind::USER)
            continue;
        if (en->of_type(Entity::Type::WORKPLANE))
            continue;
        return true;
    }
    return false;
}

std::optional<UUID> selected_circle_entity_in_group(const Document &doc, const std::set<SelectableRef> &sel,
                                                    const UUID &group_uu)
{
    for (const auto &sr : entities_from_selection(sel)) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        auto en = doc.get_entity_ptr(sr.item);
        if (!en)
            continue;
        if (en->m_group != group_uu)
            continue;
        if (!en->of_type(Entity::Type::CIRCLE_2D, Entity::Type::ARC_2D))
            continue;
        return sr.item;
    }
    return {};
}

std::vector<UUID> selected_line_entities_in_group(const Document &doc, const std::set<SelectableRef> &sel,
                                                  const UUID &group_uu)
{
    std::vector<UUID> lines;
    for (const auto &sr : entities_from_selection(sel)) {
        if (sr.type != SelectableRef::Type::ENTITY || sr.point != 0)
            continue;
        auto en = doc.get_entity_ptr(sr.item);
        if (!en)
            continue;
        if (en->m_group != group_uu)
            continue;
        if (en->get_type() != Entity::Type::LINE_2D)
            continue;
        if (std::find(lines.begin(), lines.end(), sr.item) == lines.end())
            lines.push_back(sr.item);
    }
    return lines;
}

struct LoopEndpointKey {
    long long x = 0;
    long long y = 0;
    auto operator<=>(const LoopEndpointKey &) const = default;
};

LoopEndpointKey make_loop_endpoint_key(const glm::dvec2 &p)
{
    constexpr double eps = 1e-4;
    return {static_cast<long long>(std::llround(p.x / eps)), static_cast<long long>(std::llround(p.y / eps))};
}

bool get_loop_edge_endpoints(const Entity &en, UUID &wrkpl, glm::dvec2 &p1, glm::dvec2 &p2)
{
    if (const auto *line = dynamic_cast<const EntityLine2D *>(&en)) {
        wrkpl = line->m_wrkpl;
        p1 = line->m_p1;
        p2 = line->m_p2;
        return true;
    }
    if (const auto *arc = dynamic_cast<const EntityArc2D *>(&en)) {
        wrkpl = arc->m_wrkpl;
        p1 = arc->m_from;
        p2 = arc->m_to;
        return true;
    }
    if (const auto *bez = dynamic_cast<const EntityBezier2D *>(&en)) {
        wrkpl = bez->m_wrkpl;
        p1 = bez->m_p1;
        p2 = bez->m_p2;
        return true;
    }
    return false;
}

bool collect_closed_loop_component(const Document &doc, const UUID &group_uu, const UUID &wrkpl_uu, const UUID &seed_edge_uu,
                                   std::set<UUID> &out_edges)
{
    struct Edge {
        UUID uu;
        LoopEndpointKey a;
        LoopEndpointKey b;
    };

    std::vector<Edge> edges;
    std::map<UUID, size_t> index_by_uuid;
    std::map<LoopEndpointKey, std::vector<size_t>> node_to_edges;

    for (const auto &[uu, en] : doc.m_entities) {
        if (en->m_group != group_uu || en->m_kind != ItemKind::USER)
            continue;
        UUID en_wrkpl;
        glm::dvec2 p1, p2;
        if (!get_loop_edge_endpoints(*en, en_wrkpl, p1, p2))
            continue;
        if (en_wrkpl != wrkpl_uu)
            continue;

        const auto k1 = make_loop_endpoint_key(p1);
        const auto k2 = make_loop_endpoint_key(p2);
        if (k1 == k2)
            continue;
        const auto idx = edges.size();
        edges.push_back({uu, k1, k2});
        index_by_uuid.emplace(uu, idx);
        node_to_edges[k1].push_back(idx);
        node_to_edges[k2].push_back(idx);
    }

    if (!index_by_uuid.contains(seed_edge_uu))
        return false;

    std::set<size_t> component_edges;
    std::vector<size_t> stack = {index_by_uuid.at(seed_edge_uu)};
    while (!stack.empty()) {
        const auto idx = stack.back();
        stack.pop_back();
        if (!component_edges.insert(idx).second)
            continue;
        const auto &e = edges.at(idx);
        for (const auto &k : {e.a, e.b}) {
            if (!node_to_edges.contains(k))
                continue;
            for (const auto nidx : node_to_edges.at(k)) {
                if (!component_edges.contains(nidx))
                    stack.push_back(nidx);
            }
        }
    }

    std::map<LoopEndpointKey, int> degree;
    for (const auto idx : component_edges) {
        const auto &e = edges.at(idx);
        degree[e.a]++;
        degree[e.b]++;
    }

    if (degree.empty())
        return false;
    for (const auto &[k, d] : degree) {
        (void)k;
        if (d < 2)
            return false;
    }

    for (const auto idx : component_edges)
        out_edges.insert(edges.at(idx).uu);
    return true;
}

std::optional<double> infer_line_side_toward_closed_loop_center(const Document &doc, const UUID &group_uu, const UUID &wrkpl_uu,
                                                                const UUID &seed_edge_uu, const glm::dvec2 &p1,
                                                                const glm::dvec2 &p2)
{
    std::set<UUID> loop_edges;
    if (!collect_closed_loop_component(doc, group_uu, wrkpl_uu, seed_edge_uu, loop_edges))
        return {};

    glm::dvec2 sum{0, 0};
    size_t count = 0;
    for (const auto &uu : loop_edges) {
        const auto *en = doc.get_entity_ptr(uu);
        if (!en)
            continue;
        UUID en_wrkpl;
        glm::dvec2 a, b;
        if (!get_loop_edge_endpoints(*en, en_wrkpl, a, b) || en_wrkpl != wrkpl_uu)
            continue;
        sum += a;
        sum += b;
        count += 2;
    }
    if (count == 0)
        return {};

    const auto center = sum / static_cast<double>(count);
    const auto delta = p2 - p1;
    const auto len = glm::length(delta);
    if (len < 1e-6)
        return {};
    const auto tangent = delta / len;
    const auto raw_normal = glm::dvec2{-tangent.y, tangent.x};
    const auto raw_normal_len = glm::length(raw_normal);
    if (raw_normal_len < 1e-9)
        return {};
    const auto normal = raw_normal / raw_normal_len;
    const auto midpoint = (p1 + p2) * 0.5;
    return glm::dot(normal, center - midpoint) >= 0 ? 1.0 : -1.0;
}

glm::dvec2 normalize_dir(const glm::dvec2 &v)
{
    const auto len = glm::length(v);
    if (len < 1e-9)
        return {1, 0};
    return v / len;
}

std::pair<glm::dvec2, glm::dvec2> center_trim_segment(const glm::dvec2 &p1, const glm::dvec2 &p2, double target_length)
{
    const auto delta = p2 - p1;
    const auto len = glm::length(delta);
    if (len < 1e-9 || target_length <= 1e-9 || target_length >= len)
        return {p1, p2};
    const auto dir = delta / len;
    const auto trim = (len - target_length) * 0.5;
    return {p1 + dir * trim, p2 - dir * trim};
}

glm::dvec2 reflect_point_2d(const glm::dvec2 &p, const glm::dvec2 &axis_point, const glm::dvec2 &axis_dir)
{
    const auto d = normalize_dir(axis_dir);
    const glm::dvec2 n{-d.y, d.x};
    const auto dist = glm::dot(p - axis_point, n);
    return p - 2.0 * dist * n;
}

glm::dvec2 rotate_point_2d(const glm::dvec2 &p, const glm::dvec2 &center, double angle_rad)
{
    const auto pc = p - center;
    const auto cs = std::cos(angle_rad);
    const auto sn = std::sin(angle_rad);
    return center + glm::dvec2(pc.x * cs - pc.y * sn, pc.x * sn + pc.y * cs);
}

glm::dvec2 rotate_dir_2d(const glm::dvec2 &dir, double angle_rad)
{
    const auto cs = std::cos(angle_rad);
    const auto sn = std::sin(angle_rad);
    return normalize_dir(glm::dvec2(dir.x * cs - dir.y * sn, dir.x * sn + dir.y * cs));
}

double radial_rotation_deg_to_rad(double deg)
{
    return deg * M_PI / 180.0;
}

constexpr int sketch_popover_content_width = 193;
constexpr int sketch_popover_total_width = sketch_popover_content_width + 24;
constexpr int edge_features_popover_content_width = 128;
constexpr int edge_features_popover_total_width = edge_features_popover_content_width + 24;

std::string format_text_font_label(const Pango::FontDescription &desc)
{
    auto family = desc.get_family();
    if (family.empty())
        family = desc.to_string();
    return family;
}

const std::array<std::pair<int, const char *>, 12> kSketchLayerColorPalette = {{
        {1, "#e53935"},  // red
        {2, "#fbc02d"},  // yellow
        {3, "#43a047"},  // green
        {4, "#00acc1"},  // cyan
        {5, "#1e88e5"},  // blue
        {6, "#8e24aa"},  // magenta
        {7, "#111111"},  // black
        {30, "#f57c00"}, // orange
        {140, "#6d4c41"},
        {171, "#5e35b1"},
        {210, "#546e7a"},
        {250, "#9e9e9e"},
}};

const char *process_icon_name(GroupSketch::SketchLayerProcess process)
{
    switch (process) {
    case GroupSketch::SketchLayerProcess::LINE_ENGRAVING:
        return "action-draw-contour-symbolic";
    case GroupSketch::SketchLayerProcess::FILL_ENGRAVING:
        return "tool-brush-symbolic";
    case GroupSketch::SketchLayerProcess::LINE_CUTTING:
        return "edit-cut-symbolic";
    case GroupSketch::SketchLayerProcess::IMAGE_ENGRAVING:
        return "image-x-generic-symbolic";
    default:
        return "applications-graphics-symbolic";
    }
}

const char *process_label(GroupSketch::SketchLayerProcess process)
{
    switch (process) {
    case GroupSketch::SketchLayerProcess::LINE_ENGRAVING:
        return "Line engraving";
    case GroupSketch::SketchLayerProcess::FILL_ENGRAVING:
        return "Fill engraving";
    case GroupSketch::SketchLayerProcess::LINE_CUTTING:
        return "Line cutting";
    case GroupSketch::SketchLayerProcess::IMAGE_ENGRAVING:
        return "Image engraving";
    default:
        return "Layer process";
    }
}

std::string aci_color_hex(int aci)
{
    for (const auto &[palette_aci, hex] : kSketchLayerColorPalette) {
        if (palette_aci == aci)
            return hex;
    }
    return "#111111";
}

uint8_t aci_layer_color_slot(int aci)
{
    for (size_t i = 0; i < kSketchLayerColorPalette.size(); i++) {
        if (kSketchLayerColorPalette.at(i).first == aci)
            return static_cast<uint8_t>(i + 1);
    }
    return 7; // fallback to black-ish glow
}

double wrap_angle_0_2pi(double value)
{
    while (value < 0)
        value += 2 * M_PI;
    while (value >= 2 * M_PI)
        value -= 2 * M_PI;
    return value;
}

struct GearGeneratorParams {
    double module = 2.0;
    int teeth = 24;
    double pressure_angle_deg = 20.0;
    double backlash_mm = 0.0;
    int involute_segments = 12;
    double bore_diameter_mm = 5.0;
};

struct JointGeneratorParams {
    double finger_width_mm = 6.0;
    double space_width_mm = 6.0;
    double hole_width_mm = 3.0;
    double edge_width_mm = 3.0;
    double surroundingspaces = 2.0;
    double play_mm = 0.0;
    double extra_length_mm = 0.0;
    double thickness_mm = 3.0;
    double burn_mm = 0.1;
};

enum class JointFamilyPreset {
    FINGER = 0,
};

enum class JointFingerVariantPreset {
    RECTANGULAR = 0,
    SPRINGS = 1,
    BARBS = 2,
    SNAP = 3,
};

enum class JointRolePreset {
    EDGE = 0,
    COUNTERPART = 1,
    HOLES = 2,
    PAIR_EDGE_COUNTERPART = 3,
    PAIR_COUNTERPART_EDGE = 4,
    PAIR_EDGE_HOLES = 5,
    PAIR_HOLES_EDGE = 6,
    LEGACY_INTERLOCK = 7,
};

const char *joint_family_label(JointFamilyPreset family)
{
    switch (family) {
    case JointFamilyPreset::FINGER:
    default:
        return "Finger";
    }
}

const char *joint_finger_variant_label(JointFingerVariantPreset variant)
{
    switch (variant) {
    case JointFingerVariantPreset::SPRINGS:
        return "Springs";
    case JointFingerVariantPreset::BARBS:
        return "Barbs";
    case JointFingerVariantPreset::SNAP:
        return "Snap";
    case JointFingerVariantPreset::RECTANGULAR:
    default:
        return "Rectangular";
    }
}

const char *joint_role_label(JointRolePreset role)
{
    switch (role) {
    case JointRolePreset::COUNTERPART:
        return "Counterpart";
    case JointRolePreset::HOLES:
        return "Holes";
    case JointRolePreset::PAIR_EDGE_COUNTERPART:
        return "Pair: Edge / Counterpart";
    case JointRolePreset::PAIR_COUNTERPART_EDGE:
        return "Pair: Counterpart / Edge";
    case JointRolePreset::PAIR_EDGE_HOLES:
        return "Pair: Edge / Holes";
    case JointRolePreset::PAIR_HOLES_EDGE:
        return "Pair: Holes / Edge";
    case JointRolePreset::LEGACY_INTERLOCK:
        return "Pair: Legacy Interlock";
    case JointRolePreset::EDGE:
    default:
        return "Edge";
    }
}

JointFamilyPreset joint_family_from_index(unsigned int idx)
{
    switch (idx) {
    case 0:
    default:
        return JointFamilyPreset::FINGER;
    }
}

JointFingerVariantPreset joint_finger_variant_from_index(unsigned int idx)
{
    switch (idx) {
    case 1:
        return JointFingerVariantPreset::SPRINGS;
    case 2:
        return JointFingerVariantPreset::BARBS;
    case 3:
        return JointFingerVariantPreset::SNAP;
    case 0:
    default:
        return JointFingerVariantPreset::RECTANGULAR;
    }
}

JointRolePreset joint_role_from_index(unsigned int idx)
{
    switch (idx) {
    case 1:
        return JointRolePreset::COUNTERPART;
    case 2:
        return JointRolePreset::HOLES;
    case 3:
        return JointRolePreset::PAIR_EDGE_COUNTERPART;
    case 4:
        return JointRolePreset::PAIR_COUNTERPART_EDGE;
    case 5:
        return JointRolePreset::PAIR_EDGE_HOLES;
    case 6:
        return JointRolePreset::PAIR_HOLES_EDGE;
    case 7:
        return JointRolePreset::LEGACY_INTERLOCK;
    case 0:
    default:
        return JointRolePreset::EDGE;
    }
}

bool joint_role_requires_pair(JointRolePreset role)
{
    switch (role) {
    case JointRolePreset::PAIR_EDGE_COUNTERPART:
    case JointRolePreset::PAIR_COUNTERPART_EDGE:
    case JointRolePreset::PAIR_EDGE_HOLES:
    case JointRolePreset::PAIR_HOLES_EDGE:
    case JointRolePreset::LEGACY_INTERLOCK:
        return true;
    case JointRolePreset::EDGE:
    case JointRolePreset::COUNTERPART:
    case JointRolePreset::HOLES:
    default:
        return false;
    }
}

bool joint_role_uses_holes(JointRolePreset role)
{
    switch (role) {
    case JointRolePreset::HOLES:
    case JointRolePreset::PAIR_EDGE_HOLES:
    case JointRolePreset::PAIR_HOLES_EDGE:
        return true;
    default:
        return false;
    }
}

JointGeneratorParams resolve_joint_generator_params(double thickness_mm, double burn_mm, double finger_mm, double space_mm,
                                                    double hole_width_mm, double edge_width_mm, double surroundingspaces,
                                                    double play_mm, double extra_length_mm, bool auto_size)
{
    JointGeneratorParams params;
    params.thickness_mm = std::max(0.1, thickness_mm);
    params.burn_mm = std::max(0.0, burn_mm);
    params.extra_length_mm = std::max(0.0, extra_length_mm);

    if (auto_size) {
        const auto base = params.thickness_mm * 2.0;
        params.finger_width_mm = std::max(0.1, base);
        params.space_width_mm = std::max(0.0, base);
        params.hole_width_mm = std::max(0.1, params.thickness_mm);
        params.edge_width_mm = std::max(0.0, params.thickness_mm);
        params.surroundingspaces = 2.0;
        params.play_mm = 0.0;
    }
    else {
        params.finger_width_mm = std::max(0.1, finger_mm);
        params.space_width_mm = std::max(0.0, space_mm);
        params.hole_width_mm = std::max(0.1, hole_width_mm);
        params.edge_width_mm = std::max(0.0, edge_width_mm);
        params.surroundingspaces = std::max(0.0, surroundingspaces);
        params.play_mm = std::max(0.0, play_mm);
    }

    return params;
}

struct GearProfileSource {
    enum class Kind {
        CIRCLE,
        ARC,
        BEZIER_CHAIN,
    };

    Kind kind = Kind::CIRCLE;
    UUID layer;
    UUID wrkpl;
    std::vector<UUID> source_entities;
    glm::dvec2 center = {0, 0};
    double radius = 0.0;
    double start_angle = 0.0;
    double sweep_angle = 0.0;
    std::vector<glm::dvec2> polyline;
    bool closed = false;
};

struct PolylineSample {
    std::vector<glm::dvec2> points;
    std::vector<double> acc;
    bool closed = false;
    double total = 0.0;
};

struct BoxesTemplateDef {
    enum class ArgKind {
        FLOAT,
        INT,
        BOOL,
        CHOICE,
        STRING,
    };

    struct ArgDef {
        std::string dest;
        std::string option;
        std::string label;
        std::string help;
        std::string group;
        ArgKind kind = ArgKind::STRING;
        std::string default_string;
        double default_float = 0.0;
        int default_int = 0;
        bool default_bool = false;
        std::vector<std::string> choices;
    };

    struct ArgGroupDef {
        std::string title;
        std::vector<std::string> args;
    };

    std::string id;
    std::string label;
    std::string generator;
    std::string category;
    std::string short_description;
    std::string description;
    std::string sample_image;
    std::string sample_thumbnail;
    std::vector<ArgDef> args;
    std::vector<ArgGroupDef> arg_groups;
};

struct BoxesCategoryDef {
    std::string id;
    std::string title;
    std::string description;
    std::string sample_image;
    std::string sample_thumbnail;
};

using SettingsArgKind = BoxesTemplateDef::ArgKind;
using SettingsArgDef = BoxesTemplateDef::ArgDef;
using SettingsArgGroupDef = BoxesTemplateDef::ArgGroupDef;

struct JointEdgeDef {
    std::string id;
    std::string label;
    std::string side_mode;
    std::vector<SettingsArgDef> extra_args;
};

struct JointRoleDef {
    std::string id;
    std::string label;
    bool pair = false;
    std::string line0_edge;
    std::string line1_edge;
};

struct JointFamilyDef {
    std::string id;
    std::string label;
    std::string description;
    std::vector<SettingsArgDef> args;
    std::vector<SettingsArgGroupDef> arg_groups;
    std::vector<JointEdgeDef> edges;
    std::vector<JointRoleDef> roles;
};

struct JointUiCategoryDef {
    std::string id;
    std::string label;
    std::string description;
    bool quick_popover_supported = true;
};

std::vector<BoxesTemplateDef> g_boxes_templates;
std::vector<BoxesCategoryDef> g_boxes_categories;
std::vector<JointFamilyDef> g_joint_families;
const JointEdgeDef *find_joint_edge(const JointFamilyDef &family, const std::string &edge_id);

const std::vector<JointUiCategoryDef> &joint_ui_categories()
{
    static const std::vector<JointUiCategoryDef> categories = {
            {"joinery", "Joinery", "Finger joints, dovetails, grooves, click joints, and stackable edges.", true},
            {"motion", "Motion", "Hinges and sliding lid features that usually need guided edge pairing.", false},
            {"utility", "Utility", "Functional cuts such as mounting slots and flex patterns.", true},
            {"shape", "Shape", "Handle and profile features for selected edges.", true},
    };
    return categories;
}

const JointUiCategoryDef &joint_ui_category_for_family_id(const std::string &family_id)
{
    const auto &categories = joint_ui_categories();
    const auto pick = [&](size_t idx) -> const JointUiCategoryDef & { return categories.at(std::min(idx, categories.size() - 1)); };

    if (family_id == "finger" || family_id == "stackable" || family_id == "click" || family_id == "dovetail"
        || family_id == "grooved")
        return pick(0);
    if (family_id == "hinge" || family_id == "chest_hinge" || family_id == "cabinet_hinge" || family_id == "slide_on_lid")
        return pick(1);
    if (family_id == "mounting" || family_id == "flex")
        return pick(2);
    if (family_id == "rounded_triangle" || family_id == "handle")
        return pick(3);
    return pick(0);
}

const JointFamilyDef *get_selected_joint_family(const Gtk::DropDown *family_dropdown, const std::vector<unsigned int> &visible_family_indices)
{
    if (!family_dropdown || visible_family_indices.empty() || g_joint_families.empty())
        return nullptr;
    const auto idx =
            static_cast<size_t>(std::min<unsigned int>(family_dropdown->get_selected(), visible_family_indices.size() - 1));
    return &g_joint_families.at(visible_family_indices.at(idx));
}

const JointRoleDef *get_selected_joint_role(const JointFamilyDef &family, const Gtk::DropDown *role_dropdown)
{
    if (family.roles.empty())
        return nullptr;
    const auto idx =
            static_cast<size_t>(std::min<unsigned int>(role_dropdown ? role_dropdown->get_selected() : 0, family.roles.size() - 1));
    return &family.roles.at(idx);
}

bool joint_family_quick_popover_supported(const JointFamilyDef &family)
{
    return joint_ui_category_for_family_id(family.id).quick_popover_supported;
}

enum class JointSideMode {
    AUTO = 0,
    INSIDE = 1,
    OUTSIDE = 2,
};

JointSideMode joint_side_mode_from_index(unsigned int index)
{
    switch (index) {
    case 1:
        return JointSideMode::INSIDE;
    case 2:
        return JointSideMode::OUTSIDE;
    case 0:
    default:
        return JointSideMode::AUTO;
    }
}

const char *joint_side_mode_label(JointSideMode mode)
{
    switch (mode) {
    case JointSideMode::INSIDE:
        return "Inside";
    case JointSideMode::OUTSIDE:
        return "Outside";
    case JointSideMode::AUTO:
    default:
        return "Auto";
    }
}

const JointRoleDef *find_swapped_joint_role(const JointFamilyDef &family, const JointRoleDef &role)
{
    if (!role.pair)
        return nullptr;
    const auto it = std::find_if(family.roles.begin(), family.roles.end(), [&](const auto &candidate) {
        return candidate.pair && candidate.line0_edge == role.line1_edge && candidate.line1_edge == role.line0_edge;
    });
    return it != family.roles.end() ? &*it : nullptr;
}

std::string joint_selection_hint(const JointFamilyDef &family, const JointRoleDef &role)
{
    const auto *edge0 = find_joint_edge(family, role.line0_edge);
    const auto *edge1 = role.pair ? find_joint_edge(family, role.line1_edge) : nullptr;
    const auto edge0_label = edge0 ? edge0->label : role.label;
    const auto edge1_label = edge1 ? edge1->label : std::string{};

    if (role.pair) {
        return "Select 2 lines in order on the same workplane. The first edge becomes " + edge0_label
               + ", the second becomes " + edge1_label + ". Use Swap roles to flip them.";
    }

    return "Select one or more lines. " + edge0_label + " is applied to each selected edge independently.";
}

struct JointSelectionState {
    bool ready = false;
    std::string text;
};

JointSelectionState evaluate_joint_selection_state(const Document &doc, const UUID &group_uu, const std::set<SelectableRef> &selection,
                                                   const JointFamilyDef &family, const JointRoleDef &role)
{
    const auto lines = selected_line_entities_in_group(doc, selection, group_uu);
    if (role.pair) {
        if (lines.empty())
            return {false, "Select the first edge."};
        if (lines.size() == 1)
            return {false, "Select the matching second edge."};
        if (lines.size() != 2)
            return {false, "Select exactly 2 lines for this operation."};
        const auto *line0 = doc.get_entity_ptr<EntityLine2D>(lines.at(0));
        const auto *line1 = doc.get_entity_ptr<EntityLine2D>(lines.at(1));
        if (!line0 || !line1)
            return {false, "Selection must contain 2 line entities."};
        if (line0->m_wrkpl != line1->m_wrkpl)
            return {false, "Both edges must be on the same workplane."};
        const auto len0 = glm::length(line0->m_p2 - line0->m_p1);
        const auto len1 = glm::length(line1->m_p2 - line1->m_p1);
        if (len0 < 1e-6 || len1 < 1e-6)
            return {false, "Selected edges are too short."};
        return {true, "Ready: edge pair selected."};
    }

    if (lines.empty())
        return {false, "Select one or more lines."};
    return {true, "Ready: " + std::to_string(lines.size()) + " edge(s) selected."};
}

const BoxesTemplateDef &get_boxes_template(int index)
{
    return g_boxes_templates.at(static_cast<size_t>(std::clamp(index, 0, static_cast<int>(g_boxes_templates.size()) - 1)));
}

const JointFamilyDef &get_joint_family(unsigned int index)
{
    return g_joint_families.at(static_cast<size_t>(std::clamp(index, 0u, static_cast<unsigned int>(g_joint_families.size() - 1))));
}

const JointEdgeDef *find_joint_edge(const JointFamilyDef &family, const std::string &edge_id)
{
    const auto it = std::find_if(family.edges.begin(), family.edges.end(),
                                 [&edge_id](const auto &edge) { return edge.id == edge_id; });
    return it != family.edges.end() ? &*it : nullptr;
}

const JointRoleDef *find_joint_role(const JointFamilyDef &family, const std::string &role_id)
{
    const auto it = std::find_if(family.roles.begin(), family.roles.end(),
                                 [&role_id](const auto &role) { return role.id == role_id; });
    return it != family.roles.end() ? &*it : nullptr;
}

constexpr double kJointMinEdgeStubMm = 2.0;

glm::dvec2 polar_2d(double r, double angle)
{
    return {r * std::cos(angle), r * std::sin(angle)};
}

void append_point_if_new(std::vector<glm::dvec2> &points, const glm::dvec2 &p);
std::vector<std::filesystem::path> get_boxes_runner_candidates();
std::vector<std::string> get_boxes_python_candidates(const std::filesystem::path &runner_path);
bool ensure_boxes_catalog_loaded(std::string &error);
bool ensure_joint_families_loaded(std::string &error);
std::vector<std::filesystem::path> get_boxes_sample_candidates(const std::string &sample_image);
std::optional<std::filesystem::path> get_boxes_sample_path(const BoxesTemplateDef &template_def);
std::optional<std::filesystem::path> get_boxes_sample_path(const std::string &sample_image);

int normalize_gear_hole_mode_index(int idx)
{
    constexpr int kCount = 3;
    idx %= kCount;
    if (idx < 0)
        idx += kCount;
    return idx;
}

const char *gear_hole_mode_name_by_index(int idx)
{
    switch (normalize_gear_hole_mode_index(idx)) {
    case 1:
        return "Cross";
    case 2:
        return "Slot";
    case 0:
    default:
        return "Circle";
    }
}

void append_closed_polyline(std::vector<std::vector<glm::dvec2>> &polylines, std::vector<glm::dvec2> pts)
{
    if (pts.size() < 3)
        return;
    if (glm::length(pts.front() - pts.back()) < 1e-6)
        pts.pop_back();
    if (pts.size() < 3)
        return;
    polylines.push_back(std::move(pts));
}

void append_gear_hole_polylines(std::vector<std::vector<glm::dvec2>> &polylines, int hole_mode_index, double bore_diameter,
                                double material_thickness, const glm::dvec2 &center, double rotation_rad, int circle_segments)
{
    const auto bore = std::max(0.0, bore_diameter);
    if (bore <= 1e-6)
        return;

    const auto thick = std::clamp(material_thickness, 0.1, std::max(0.1, bore));
    const auto mode = normalize_gear_hole_mode_index(hole_mode_index);
    auto rot = [&](const glm::dvec2 &p) { return rotate_point_2d(p, {0, 0}, rotation_rad) + center; };

    if (mode == 0) {
        const auto r = bore * 0.5;
        const auto segs = std::max(24, circle_segments);
        std::vector<glm::dvec2> hole;
        hole.reserve(static_cast<size_t>(segs));
        for (int i = 0; i < segs; i++) {
            const auto a = 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(segs);
            hole.push_back(rot(polar_2d(r, a)));
        }
        append_closed_polyline(polylines, std::move(hole));
        return;
    }

    const auto half_l = bore * 0.5;
    const auto half_w = thick * 0.5;

    if (mode == 2) {
        std::vector<glm::dvec2> slot = {
                rot({-half_l, -half_w}),
                rot({half_l, -half_w}),
                rot({half_l, half_w}),
                rot({-half_l, half_w}),
        };
        append_closed_polyline(polylines, std::move(slot));
        return;
    }

    std::vector<glm::dvec2> cross = {
            rot({-half_w, -half_l}),
            rot({half_w, -half_l}),
            rot({half_w, -half_w}),
            rot({half_l, -half_w}),
            rot({half_l, half_w}),
            rot({half_w, half_w}),
            rot({half_w, half_l}),
            rot({-half_w, half_l}),
            rot({-half_w, half_w}),
            rot({-half_l, half_w}),
            rot({-half_l, -half_w}),
            rot({-half_w, -half_w}),
    };
    append_closed_polyline(polylines, std::move(cross));
}

double signed_area_polyline(const std::vector<glm::dvec2> &pts)
{
    if (pts.size() < 3)
        return 0.0;
    double area2 = 0.0;
    for (size_t i = 0; i < pts.size(); i++) {
        const auto &a = pts.at(i);
        const auto &b = pts.at((i + 1) % pts.size());
        area2 += a.x * b.y - b.x * a.y;
    }
    return area2 * 0.5;
}

std::vector<UUID> selected_entities_in_group_unique(const Document &doc, const std::set<SelectableRef> &sel, const UUID &group_uu)
{
    std::vector<UUID> out;
    for (const auto &sr : entities_from_selection(sel)) {
        if (sr.type != SelectableRef::Type::ENTITY || sr.point != 0)
            continue;
        const auto *en = doc.get_entity_ptr(sr.item);
        if (!en || en->m_group != group_uu)
            continue;
        if (std::find(out.begin(), out.end(), sr.item) == out.end())
            out.push_back(sr.item);
    }
    return out;
}

void sample_bezier_points(const EntityBezier2D &bez, bool reversed, int steps, std::vector<glm::dvec2> &out)
{
    if (steps < 1)
        steps = 1;
    for (int i = 0; i <= steps; i++) {
        const auto t = static_cast<double>(i) / static_cast<double>(steps);
        const auto u = reversed ? (1.0 - t) : t;
        const auto p = bez.get_interpolated(u);
        if (out.empty() || glm::length(out.back() - p) > 1e-6)
            out.push_back(p);
    }
}

bool build_polyline_sample(const std::vector<glm::dvec2> &path, bool closed, PolylineSample &sample)
{
    sample = {};
    sample.points = path;
    sample.closed = closed;
    if (sample.points.size() < 2)
        return false;
    if (sample.closed && glm::length(sample.points.front() - sample.points.back()) < 1e-6)
        sample.points.pop_back();
    if (sample.points.size() < 2)
        return false;

    const auto seg_count = sample.points.size() - 1 + (sample.closed ? 1 : 0);
    if (seg_count == 0)
        return false;

    sample.acc.clear();
    sample.acc.reserve(seg_count + 1);
    sample.acc.push_back(0.0);
    double total = 0.0;
    for (size_t i = 0; i < seg_count; i++) {
        const auto &a = sample.points.at(i);
        const auto &b = sample.points.at((i + 1) % sample.points.size());
        const auto len = glm::length(b - a);
        if (len <= 1e-9)
            continue;
        total += len;
        sample.acc.push_back(total);
    }

    sample.total = total;
    return sample.total > 1e-6 && sample.acc.size() >= 2;
}

bool polyline_eval(const PolylineSample &sample, double s, glm::dvec2 &p, glm::dvec2 &tangent)
{
    if (sample.points.size() < 2 || sample.acc.size() < 2 || sample.total <= 1e-6)
        return false;
    s = std::clamp(s, 0.0, sample.total);

    auto it = std::upper_bound(sample.acc.begin(), sample.acc.end(), s);
    size_t seg_idx = 0;
    if (it == sample.acc.begin())
        seg_idx = 0;
    else if (it == sample.acc.end())
        seg_idx = sample.acc.size() - 2;
    else
        seg_idx = static_cast<size_t>(std::distance(sample.acc.begin(), it) - 1);

    const auto seg_start = sample.acc.at(seg_idx);
    const auto seg_end = sample.acc.at(seg_idx + 1);
    const auto seg_len = std::max(1e-9, seg_end - seg_start);
    const auto u = std::clamp((s - seg_start) / seg_len, 0.0, 1.0);

    const auto i0 = seg_idx % sample.points.size();
    const auto i1 = (seg_idx + 1) % sample.points.size();
    const auto &a = sample.points.at(i0);
    const auto &b = sample.points.at(i1);
    p = a + (b - a) * u;
    tangent = normalize_dir(b - a);
    return true;
}

std::vector<glm::dvec2> build_radial_teeth_profile(const glm::dvec2 &center, double radius, double start_angle, double sweep_angle,
                                                   bool closed, double module_mm, bool inward)
{
    std::vector<glm::dvec2> profile;
    if (radius <= 1e-6)
        return profile;

    const auto arc_len = std::abs(sweep_angle) * radius;
    const auto pitch = std::max(0.1, M_PI * std::max(0.05, module_mm));
    int tooth_count = 0;
    if (closed)
        tooth_count = std::max(3, static_cast<int>(std::lround(arc_len / pitch)));
    else
        tooth_count = std::max(1, static_cast<int>(std::floor((arc_len + pitch * 0.5) / pitch)));
    tooth_count = std::clamp(tooth_count, 1, 2000);

    const auto cell = sweep_angle / static_cast<double>(tooth_count);
    const auto depth = std::max(0.1, module_mm);
    const auto root_r = radius;
    const auto tip_r = inward ? std::max(0.05, radius - depth) : (radius + depth);

    auto point_at = [&](double r, double a) { return center + glm::dvec2(std::cos(a) * r, std::sin(a) * r); };

    profile.reserve(static_cast<size_t>(tooth_count) * 8 + 2);
    profile.push_back(point_at(root_r, start_angle));
    for (int i = 0; i < tooth_count; i++) {
        const auto a0 = start_angle + cell * static_cast<double>(i);
        const auto a_root_start = a0 + cell * 0.14;
        const auto a_flank_in = a0 + cell * 0.26;
        const auto a_tooth_start = a0 + cell * 0.36;
        const auto a_tooth_end = a0 + cell * 0.64;
        const auto a_flank_out = a0 + cell * 0.74;
        const auto a_root_end = a0 + cell * 0.86;
        const auto a1 = a0 + cell;
        const auto mid_r = root_r + (tip_r - root_r) * 0.55;

        append_point_if_new(profile, point_at(root_r, a_root_start));
        append_point_if_new(profile, point_at(mid_r, a_flank_in));
        append_point_if_new(profile, point_at(tip_r, a_tooth_start));
        append_point_if_new(profile, point_at(tip_r, a_tooth_end));
        append_point_if_new(profile, point_at(mid_r, a_flank_out));
        append_point_if_new(profile, point_at(root_r, a_root_end));
        append_point_if_new(profile, point_at(root_r, a1));
    }

    if (!closed)
        append_point_if_new(profile, point_at(root_r, start_angle + sweep_angle));
    return profile;
}

std::vector<glm::dvec2> build_polyline_teeth_profile(const std::vector<glm::dvec2> &path, bool closed, double module_mm, bool inward)
{
    std::vector<glm::dvec2> profile;
    PolylineSample sample;
    if (!build_polyline_sample(path, closed, sample))
        return profile;

    const auto pitch = std::max(0.1, M_PI * std::max(0.05, module_mm));
    const auto tooth_w = pitch * 0.5;
    const auto depth = std::max(0.1, module_mm);
    const auto area = signed_area_polyline(sample.points);
    const auto outside_sign = (area >= 0.0) ? -1.0 : 1.0;
    const auto side_sign = closed ? (inward ? -outside_sign : outside_sign) : (inward ? -1.0 : 1.0);

    auto normal_at = [&](const glm::dvec2 &tan) { return normalize_dir(glm::dvec2(-tan.y, tan.x)) * side_sign; };
    auto point_at = [&](double s) {
        glm::dvec2 p{0, 0};
        glm::dvec2 t{1, 0};
        polyline_eval(sample, s, p, t);
        return std::pair<glm::dvec2, glm::dvec2>{p, t};
    };
    auto point_with_offset = [&](double s, double offset) {
        const auto [p, t] = point_at(s);
        return p + normal_at(t) * offset;
    };
    auto emit_flat = [&](double s0, double s1) {
        if (s1 <= s0 + 1e-6)
            return;
        const auto [p0, t0] = point_at(s0);
        const auto [p1, t1] = point_at(s1);
        if (profile.empty())
            profile.push_back(p0);
        else
            append_point_if_new(profile, p0);
        append_point_if_new(profile, p1);
    };
    auto emit_tooth = [&](double s0, double s1) {
        if (s1 <= s0 + 1e-6)
            return;
        const auto len = s1 - s0;
        const auto s_root_start = s0 + len * 0.14;
        const auto s_flank_in = s0 + len * 0.26;
        const auto s_tooth_start = s0 + len * 0.36;
        const auto s_tooth_end = s0 + len * 0.64;
        const auto s_flank_out = s0 + len * 0.74;
        const auto s_root_end = s0 + len * 0.86;
        emit_flat(s0, s_root_start);
        append_point_if_new(profile, point_with_offset(s_flank_in, depth * 0.55));
        append_point_if_new(profile, point_with_offset(s_tooth_start, depth));
        append_point_if_new(profile, point_with_offset(s_tooth_end, depth));
        append_point_if_new(profile, point_with_offset(s_flank_out, depth * 0.55));
        append_point_if_new(profile, point_with_offset(s_root_end, 0.0));
        emit_flat(s_root_end, s1);
    };

    if (closed) {
        int tooth_count = std::max(3, static_cast<int>(std::lround(sample.total / pitch)));
        tooth_count = std::clamp(tooth_count, 3, 2000);
        const auto cell = sample.total / static_cast<double>(tooth_count);
        for (int i = 0; i < tooth_count; i++) {
            const auto s0 = cell * static_cast<double>(i);
            const auto s1 = s0 + cell;
            emit_tooth(s0, s1);
        }
    }
    else {
        int teeth = std::max(1, static_cast<int>(std::floor((sample.total + tooth_w) / (2.0 * tooth_w))));
        teeth = std::clamp(teeth, 1, 2000);
        const auto gaps = std::max(0, teeth - 1);
        std::vector<double> gap_widths(static_cast<size_t>(gaps), tooth_w);
        const auto used = tooth_w * static_cast<double>(std::max(1, 2 * teeth - 1));
        auto remainder = std::max(0.0, sample.total - used);
        if (gaps > 0 && remainder > 1e-9) {
            if ((gaps % 2) == 1) {
                gap_widths.at(static_cast<size_t>(gaps / 2)) += remainder;
            }
            else {
                const auto mid_right = static_cast<size_t>(gaps / 2);
                const auto mid_left = mid_right - 1;
                gap_widths.at(mid_left) += remainder * 0.5;
                gap_widths.at(mid_right) += remainder * 0.5;
            }
        }
        double cursor = 0.0;
        for (int i = 0; i < teeth; i++) {
            const auto tooth_end = std::min(sample.total, cursor + tooth_w);
            if (inward)
                emit_flat(cursor, tooth_end);
            else
                emit_tooth(cursor, tooth_end);
            cursor = tooth_end;
            if (i + 1 < teeth) {
                const auto gap_end = std::min(sample.total, cursor + gap_widths.at(static_cast<size_t>(i)));
                if (inward)
                    emit_tooth(cursor, gap_end);
                else
                    emit_flat(cursor, gap_end);
                cursor = gap_end;
            }
        }
        emit_flat(cursor, sample.total);
    }

    if (closed && !profile.empty() && glm::length(profile.front() - profile.back()) < 1e-6)
        profile.pop_back();
    return profile;
}

bool collect_selected_gear_profile_source(const Document &doc, const std::set<SelectableRef> &sel, const UUID &group_uu,
                                          GearProfileSource &out, std::string *error = nullptr)
{
    const auto selected = selected_entities_in_group_unique(doc, sel, group_uu);
    if (selected.empty()) {
        if (error)
            *error = "Select a circle, arc, or oval (Bezier chain)";
        return false;
    }

    if (selected.size() == 1) {
        const auto uu = selected.front();
        if (const auto *circle = doc.get_entity_ptr<EntityCircle2D>(uu)) {
            out = {};
            out.kind = GearProfileSource::Kind::CIRCLE;
            out.layer = circle->m_layer;
            out.wrkpl = circle->m_wrkpl;
            out.source_entities = {uu};
            out.center = circle->m_center;
            out.radius = std::max(0.0, circle->m_radius);
            out.start_angle = 0.0;
            out.sweep_angle = 2.0 * M_PI;
            out.closed = true;
            return out.radius > 1e-6;
        }
        if (const auto *arc = doc.get_entity_ptr<EntityArc2D>(uu)) {
            out = {};
            out.kind = GearProfileSource::Kind::ARC;
            out.layer = arc->m_layer;
            out.wrkpl = arc->m_wrkpl;
            out.source_entities = {uu};
            out.center = arc->m_center;
            out.radius = glm::length(arc->m_from - arc->m_center);
            out.start_angle = std::atan2(arc->m_from.y - arc->m_center.y, arc->m_from.x - arc->m_center.x);
            auto a1 = std::atan2(arc->m_to.y - arc->m_center.y, arc->m_to.x - arc->m_center.x);
            out.sweep_angle = a1 - out.start_angle;
            while (out.sweep_angle <= 0)
                out.sweep_angle += 2.0 * M_PI;
            out.closed = false;
            return out.radius > 1e-6 && out.sweep_angle > 1e-6;
        }
    }

    std::vector<const EntityBezier2D *> beziers;
    beziers.reserve(selected.size());
    UUID layer;
    UUID wrkpl;
    bool first = true;
    for (const auto &uu : selected) {
        const auto *bez = doc.get_entity_ptr<EntityBezier2D>(uu);
        if (!bez) {
            if (error)
                *error = "Gears quick mode supports circle, arc, or Bezier-only selection";
            return false;
        }
        if (first) {
            layer = bez->m_layer;
            wrkpl = bez->m_wrkpl;
            first = false;
        }
        else if (bez->m_wrkpl != wrkpl) {
            if (error)
                *error = "Selected Beziers must be on the same workplane";
            return false;
        }
        beziers.push_back(bez);
    }
    if (beziers.empty())
        return false;

    struct ChainItem {
        const EntityBezier2D *bez = nullptr;
        UUID uu;
        bool used = false;
    };
    std::vector<ChainItem> chain;
    chain.reserve(beziers.size());
    for (size_t i = 0; i < selected.size(); i++)
        chain.push_back({beziers.at(i), selected.at(i), false});

    std::vector<glm::dvec2> path;
    constexpr int bez_steps = 24;
    chain.front().used = true;
    sample_bezier_points(*chain.front().bez, false, bez_steps, path);
    for (size_t used_count = 1; used_count < chain.size(); used_count++) {
        const auto tail = path.back();
        double best_dist = std::numeric_limits<double>::max();
        size_t best_idx = chain.size();
        bool best_reversed = false;
        for (size_t i = 0; i < chain.size(); i++) {
            if (chain.at(i).used)
                continue;
            const auto d_start = glm::length(chain.at(i).bez->m_p1 - tail);
            if (d_start < best_dist) {
                best_dist = d_start;
                best_idx = i;
                best_reversed = false;
            }
            const auto d_end = glm::length(chain.at(i).bez->m_p2 - tail);
            if (d_end < best_dist) {
                best_dist = d_end;
                best_idx = i;
                best_reversed = true;
            }
        }
        if (best_idx >= chain.size())
            break;
        chain.at(best_idx).used = true;
        sample_bezier_points(*chain.at(best_idx).bez, best_reversed, bez_steps, path);
    }

    if (path.size() < 2) {
        if (error)
            *error = "Couldn't build a path from selected Beziers";
        return false;
    }
    bool closed = glm::length(path.front() - path.back()) < 1e-2;
    if (closed)
        path.pop_back();

    out = {};
    out.kind = GearProfileSource::Kind::BEZIER_CHAIN;
    out.layer = layer;
    out.wrkpl = wrkpl;
    out.source_entities = selected;
    out.polyline = std::move(path);
    out.closed = closed;
    return out.polyline.size() >= 2;
}

void append_point_if_new(std::vector<glm::dvec2> &points, const glm::dvec2 &p)
{
    if (points.empty() || glm::length(points.back() - p) > 1e-6)
        points.push_back(p);
}

void append_arc_ccw(std::vector<glm::dvec2> &points, double radius, double from_angle, double to_angle, int steps)
{
    if (steps < 1)
        return;
    auto delta = to_angle - from_angle;
    while (delta <= 0)
        delta += 2 * M_PI;
    for (int i = 1; i <= steps; i++) {
        const auto angle = from_angle + delta * static_cast<double>(i) / static_cast<double>(steps);
        append_point_if_new(points, polar_2d(radius, angle));
    }
}

bool build_involute_gear_outline(const GearGeneratorParams &params, std::vector<glm::dvec2> &outline)
{
    outline.clear();
    if (params.teeth < 3 || params.module <= 0)
        return false;

    const auto z = static_cast<double>(params.teeth);
    const auto pressure_angle = std::clamp(params.pressure_angle_deg, 5.0, 45.0) * M_PI / 180.0;
    const auto pitch_radius = params.module * z * 0.5;
    const auto base_radius = pitch_radius * std::cos(pressure_angle);
    const auto addendum_radius = pitch_radius + params.module;
    const auto dedendum_radius = std::max(0.05, pitch_radius - params.module * 1.25);
    if (addendum_radius <= dedendum_radius || base_radius <= 1e-6)
        return false;

    const auto tooth_pitch_angle = 2.0 * M_PI / z;
    const auto backlash = std::clamp(std::max(0.0, params.backlash_mm), 0.0, params.module * 0.4);
    auto half_tooth_pitch_angle = tooth_pitch_angle * 0.25 - backlash / (4.0 * std::max(1e-6, pitch_radius));
    half_tooth_pitch_angle = std::clamp(half_tooth_pitch_angle, tooth_pitch_angle * 0.05, tooth_pitch_angle * 0.45);

    const auto involute_angle_for_radius = [base_radius](double radius) {
        if (radius <= base_radius)
            return 0.0;
        const auto t = std::sqrt(std::max(0.0, (radius * radius) / (base_radius * base_radius) - 1.0));
        return t - std::atan(t);
    };

    const auto involute_t_for_radius = [base_radius](double radius) {
        if (radius <= base_radius)
            return 0.0;
        return std::sqrt(std::max(0.0, (radius * radius) / (base_radius * base_radius) - 1.0));
    };

    const auto r_start = std::max(base_radius, dedendum_radius);
    const auto t_start = involute_t_for_radius(r_start);
    const auto t_tip = involute_t_for_radius(addendum_radius);
    const auto inv_pressure = involute_angle_for_radius(pitch_radius);
    const auto inv_start = involute_angle_for_radius(r_start);
    const auto inv_tip = involute_angle_for_radius(addendum_radius);

    const auto flank_steps = std::max(4, params.involute_segments);
    const auto root_arc_steps = std::max(2, params.involute_segments / 3);
    const auto tip_arc_steps = std::max(2, params.involute_segments / 3);

    auto tooth_angles = [&](int tooth_idx) {
        const auto center_angle = tooth_pitch_angle * static_cast<double>(tooth_idx);
        const auto right_base = center_angle - half_tooth_pitch_angle - inv_pressure;
        const auto left_base = center_angle + half_tooth_pitch_angle + inv_pressure;
        const auto right_start = right_base + inv_start;
        const auto right_tip = right_base + inv_tip;
        const auto left_tip = left_base - inv_tip;
        const auto left_start = left_base - inv_start;
        return std::array<double, 6>{right_base, left_base, right_start, right_tip, left_tip, left_start};
    };

    const auto first_angles = tooth_angles(0);
    const auto first_right_start = first_angles[2];
    append_point_if_new(outline, polar_2d(dedendum_radius, first_right_start));
    if (r_start > dedendum_radius + 1e-6)
        append_point_if_new(outline, polar_2d(r_start, first_right_start));
    auto previous_left_start = first_right_start;

    for (int tooth_idx = 0; tooth_idx < params.teeth; tooth_idx++) {
        const auto angles = tooth_angles(tooth_idx);
        const auto right_base = angles[0];
        const auto left_base = angles[1];
        const auto right_start = angles[2];
        const auto right_tip = angles[3];
        const auto left_tip = angles[4];
        const auto left_start = angles[5];

        if (tooth_idx > 0) {
            append_arc_ccw(outline, dedendum_radius, previous_left_start, right_start, root_arc_steps);
            if (r_start > dedendum_radius + 1e-6)
                append_point_if_new(outline, polar_2d(r_start, right_start));
        }

        for (int i = 1; i <= flank_steps; i++) {
            const auto t = t_start + (t_tip - t_start) * static_cast<double>(i) / static_cast<double>(flank_steps);
            const auto inv = t - std::atan(t);
            const auto radius = base_radius * std::sqrt(1.0 + t * t);
            append_point_if_new(outline, polar_2d(radius, right_base + inv));
        }

        append_arc_ccw(outline, addendum_radius, right_tip, left_tip, tip_arc_steps);

        for (int i = 1; i <= flank_steps; i++) {
            const auto t = t_tip + (t_start - t_tip) * static_cast<double>(i) / static_cast<double>(flank_steps);
            const auto inv = t - std::atan(t);
            const auto radius = base_radius * std::sqrt(1.0 + t * t);
            append_point_if_new(outline, polar_2d(radius, left_base - inv));
        }

        if (r_start > dedendum_radius + 1e-6)
            append_point_if_new(outline, polar_2d(dedendum_radius, left_start));
        previous_left_start = left_start;
    }

    append_arc_ccw(outline, dedendum_radius, previous_left_start, first_right_start, root_arc_steps);
    return outline.size() >= static_cast<size_t>(params.teeth * 6);
}

struct JointPatternData {
    int fingers = 0;
    double finger_width = 0.0;
    double space_width = 0.0;
    double left_margin = 0.0;
    double right_margin = 0.0;
};

JointPatternData build_joint_pattern_data(double length, const JointGeneratorParams &params, bool counterpart)
{
    JointPatternData pattern;
    if (length <= 1e-6)
        return pattern;

    const auto base_finger = std::max(0.0, params.finger_width_mm);
    const auto base_space = std::max(0.0, params.space_width_mm);
    const auto play = std::max(0.0, params.play_mm);
    const auto surroundingspaces = std::max(0.0, params.surroundingspaces);
    const auto pitch = base_space + base_finger;
    double leftover = length;

    if (pitch >= 0.1 && base_finger > 1e-6) {
        pattern.fingers = static_cast<int>(std::floor((length - (surroundingspaces - 1.0) * base_space) / pitch));
        if (pattern.fingers < 0)
            pattern.fingers = 0;
        leftover = length - static_cast<double>(pattern.fingers) * pitch + base_space;
        if (pattern.fingers <= 0)
            leftover = length;
    }

    pattern.finger_width = base_finger;
    pattern.space_width = base_space;

    if (pattern.fingers == 0 && pattern.finger_width > 1e-6 && leftover > 0.75 * params.thickness_mm && leftover > 4.0 * play) {
        pattern.fingers = 1;
        pattern.finger_width = leftover * 0.5;
        pattern.space_width = 0.0;
        leftover = pattern.finger_width;
    }

    if (counterpart) {
        pattern.finger_width += play;
        pattern.space_width = std::max(0.0, pattern.space_width - play);
        leftover = std::max(0.0, leftover - play);
    }

    pattern.left_margin = leftover * 0.5;
    pattern.right_margin = leftover * 0.5;
    return pattern;
}

std::pair<double, double> apply_joint_kerf(double x0, double x1, double kerf)
{
    auto xa = x0 + kerf * 0.5;
    auto xb = x1 - kerf * 0.5;
    if ((xa - x0) <= kJointMinEdgeStubMm)
        xa = x0;
    if ((x1 - xb) <= kJointMinEdgeStubMm)
        xb = x1;
    if (xb <= xa) {
        xa = x0;
        xb = x1;
    }
    return {xa, xb};
}

struct JointTurtlePoint {
    double x = 0.0;
    double y = 0.0;
};

struct JointTurtle {
    std::vector<JointTurtlePoint> points;
    JointTurtlePoint pos{0.0, 0.0};
    double heading_rad = 0.0;

    void add_point(const JointTurtlePoint &p)
    {
        if (points.empty() || std::abs(points.back().x - p.x) > 1e-6 || std::abs(points.back().y - p.y) > 1e-6)
            points.push_back(p);
        pos = p;
    }

    void edge(double length)
    {
        add_point({pos.x + std::cos(heading_rad) * length, pos.y + std::sin(heading_rad) * length});
    }

    void corner(double degrees, double radius = 0.0)
    {
        if (std::abs(degrees) < 1e-6)
            return;
        if (radius <= 1e-6) {
            heading_rad += glm::radians(degrees);
            return;
        }

        const auto heading = heading_rad;
        const glm::dvec2 left{-std::sin(heading), std::cos(heading)};
        glm::dvec2 center;
        double start_angle = 0.0;
        if (degrees > 0.0) {
            center = {pos.x, pos.y};
            center += left * radius;
            start_angle = heading - M_PI_2;
        }
        else {
            center = {pos.x, pos.y};
            center -= left * radius;
            start_angle = heading + M_PI_2;
        }

        const auto sweep = glm::radians(degrees);
        const auto steps = std::max(2, static_cast<int>(std::ceil(std::abs(degrees) / 18.0)));
        for (int i = 1; i <= steps; i++) {
            const auto angle = start_angle + sweep * static_cast<double>(i) / static_cast<double>(steps);
            add_point({center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius});
        }
        heading_rad += sweep;
    }
};

void append_joint_rect_segment(std::vector<glm::dvec2> &profile, const auto &point_at, double x0, double x1, double depth, double burn)
{
    if (x1 <= x0 + 1e-6)
        return;
    const auto [xa, xb] = apply_joint_kerf(x0, x1, burn);
    append_point_if_new(profile, point_at(xa, 0.0));
    append_point_if_new(profile, point_at(xa, depth));
    append_point_if_new(profile, point_at(xb, depth));
    append_point_if_new(profile, point_at(xb, 0.0));
}

void append_joint_styled_finger_segment(std::vector<glm::dvec2> &profile, const auto &point_at, double x0, double x1, double depth,
                                        double thickness_mm, double burn_mm, JointFingerVariantPreset variant, bool positive,
                                        bool firsthalf)
{
    const auto width = x1 - x0;
    if (width <= 1e-6)
        return;

    if (!positive || variant == JointFingerVariantPreset::RECTANGULAR) {
        append_joint_rect_segment(profile, point_at, x0, x1, depth, burn_mm);
        append_point_if_new(profile, point_at(x1, 0.0));
        return;
    }

    if (variant == JointFingerVariantPreset::BARBS) {
        const auto barb = std::max(0.2 * thickness_mm, 0.12 * width);
        const auto steps = std::max(2, static_cast<int>((depth + 0.1 * thickness_mm) / std::max(0.2, 0.35 * thickness_mm)));
        append_point_if_new(profile, point_at(x0, 0.0));
        for (int i = 0; i < steps; i++) {
            const auto y0 = depth * static_cast<double>(i) / static_cast<double>(steps);
            const auto y1 = depth * static_cast<double>(i + 1) / static_cast<double>(steps);
            append_point_if_new(profile, point_at(x0, y0));
            append_point_if_new(profile, point_at(x0 + barb, y0 + (y1 - y0) * 0.45));
            append_point_if_new(profile, point_at(x0, y1));
        }
        append_point_if_new(profile, point_at(x1, depth));
        for (int i = steps; i-- > 0;) {
            const auto y0 = depth * static_cast<double>(i + 1) / static_cast<double>(steps);
            const auto y1 = depth * static_cast<double>(i) / static_cast<double>(steps);
            append_point_if_new(profile, point_at(x1, y0));
            append_point_if_new(profile, point_at(x1 - barb, y0 - (y0 - y1) * 0.45));
            append_point_if_new(profile, point_at(x1, y1));
        }
        append_point_if_new(profile, point_at(x1, 0.0));
        return;
    }

    JointTurtle turtle;
    turtle.add_point({0.0, 0.0});

    if (variant == JointFingerVariantPreset::SPRINGS) {
        turtle.corner(-90.0);
        turtle.edge(0.8 * depth);
        turtle.corner(90.0, 0.2 * depth);
        turtle.edge(0.1 * depth);
        turtle.corner(90.0);
        turtle.edge(0.9 * depth);
        turtle.corner(-180.0);
        turtle.edge(0.9 * depth);
        turtle.corner(90.0);
        turtle.edge(std::max(0.0, width - 0.6 * depth));
        turtle.corner(90.0);
        turtle.edge(0.9 * depth);
        turtle.corner(-180.0);
        turtle.edge(0.9 * depth);
        turtle.corner(90.0);
        turtle.edge(0.1 * depth);
        turtle.corner(90.0, 0.2 * depth);
        turtle.edge(0.8 * depth);
        turtle.corner(-90.0);
    }
    else if (variant == JointFingerVariantPreset::SNAP) {
        if (width <= 1.9 * thickness_mm) {
            append_joint_rect_segment(profile, point_at, x0, x1, depth, burn_mm);
            append_point_if_new(profile, point_at(x1, 0.0));
            return;
        }
        const auto hook = std::max(thickness_mm * 0.55, depth * 0.3);
        const auto neck = std::max(thickness_mm * 0.35, depth * 0.18);
        const auto left_inner = std::max(thickness_mm * 0.55, width * 0.28);
        const auto right_inner = std::min(width - thickness_mm * 0.55, width * 0.72);

        turtle.corner(-90.0);
        turtle.edge(std::max(0.0, depth - hook));
        turtle.corner(55.0);
        turtle.edge(hook);
        turtle.corner(35.0);
        turtle.edge(std::max(0.0, left_inner));
        turtle.corner(90.0);
        turtle.edge(neck);
        turtle.corner(-90.0);
        turtle.edge(std::max(0.0, right_inner - left_inner));
        turtle.corner(-90.0);
        turtle.edge(neck);
        turtle.corner(90.0);
        turtle.edge(std::max(0.0, width - right_inner));
        turtle.corner(35.0);
        turtle.edge(hook);
        turtle.corner(55.0);
        turtle.edge(std::max(0.0, depth - hook));
        turtle.corner(-90.0);
        if (!firsthalf) {
            for (auto &pt : turtle.points)
                pt.x = width - pt.x;
            std::reverse(turtle.points.begin(), turtle.points.end());
        }
    }

    for (const auto &pt : turtle.points)
        append_point_if_new(profile, point_at(x0 + pt.x, -pt.y));
    append_point_if_new(profile, point_at(x1, 0.0));
}

std::vector<glm::dvec2> build_joint_line_profile(const glm::dvec2 &p1, const glm::dvec2 &p2, const JointGeneratorParams &params,
                                                 bool counterpart, bool protrude_on_fingers, double side_sign,
                                                 JointFingerVariantPreset variant)
{
    std::vector<glm::dvec2> profile;
    const auto delta = p2 - p1;
    const auto length = glm::length(delta);
    if (length < 1e-6)
        return profile;

    const auto tangent = delta / length;
    const auto normal = normalize_dir(glm::dvec2{-tangent.y, tangent.x}) * (side_sign >= 0 ? 1.0 : -1.0);
    const auto pattern = build_joint_pattern_data(length, params, counterpart);
    const auto burn = std::max(0.0, params.burn_mm);
    const auto depth = std::max(0.1, params.thickness_mm + params.extra_length_mm);

    auto point_at = [&](double along, double offset = 0.0) { return p1 + tangent * along + normal * offset; };

    profile.reserve(static_cast<size_t>(pattern.fingers) * 8 + 4);
    profile.push_back(p1);

    auto emit_segment = [&](double x0, double x1, bool protrude) {
        if (x1 <= x0 + 1e-6)
            return;
        if (protrude) {
            append_joint_styled_finger_segment(profile, point_at, x0, x1, depth, params.thickness_mm, burn, variant, !counterpart,
                                               x0 < length * 0.5);
        }
        append_point_if_new(profile, point_at(x1, 0.0));
    };

    double cursor = 0.0;
    emit_segment(cursor, std::min(length, pattern.left_margin), false);
    cursor = std::min(length, pattern.left_margin);
    for (int i = 0; i < pattern.fingers; i++) {
        const auto tooth_end = std::min(length, cursor + pattern.finger_width);
        emit_segment(cursor, tooth_end, protrude_on_fingers);
        cursor = tooth_end;
        if (i + 1 < pattern.fingers) {
            const auto gap_end = std::min(length, cursor + pattern.space_width);
            emit_segment(cursor, gap_end, !protrude_on_fingers);
            cursor = gap_end;
        }
    }
    emit_segment(cursor, length, false);

    append_point_if_new(profile, p2);
    return profile;
}

std::vector<std::vector<glm::dvec2>> build_joint_finger_hole_profiles(const glm::dvec2 &p1, const glm::dvec2 &p2,
                                                                      const JointGeneratorParams &params, double side_sign)
{
    std::vector<std::vector<glm::dvec2>> slots;
    const auto delta = p2 - p1;
    const auto length = glm::length(delta);
    if (length < 1e-6)
        return slots;

    const auto tangent = delta / length;
    const auto normal = normalize_dir(glm::dvec2{-tangent.y, tangent.x}) * (side_sign >= 0 ? 1.0 : -1.0);
    const auto pattern = build_joint_pattern_data(length, params, false);
    const auto slot_center_offset = params.edge_width_mm + params.thickness_mm * 0.5 + params.burn_mm;
    const auto slot_half_height = std::max(0.05, (params.hole_width_mm + params.play_mm) * 0.5);

    auto point_at = [&](double along, double offset = 0.0) { return p1 + tangent * along + normal * offset; };
    auto emit_slot = [&](double center_along, double hole_width) {
        const auto half = std::max(0.05, hole_width * 0.5);
        const auto [xa, xb] = apply_joint_kerf(center_along - half, center_along + half, params.burn_mm);
        if (xb <= xa + 1e-6)
            return;
        std::vector<glm::dvec2> rect;
        rect.reserve(5);
        rect.push_back(point_at(xa, slot_center_offset - slot_half_height));
        rect.push_back(point_at(xb, slot_center_offset - slot_half_height));
        rect.push_back(point_at(xb, slot_center_offset + slot_half_height));
        rect.push_back(point_at(xa, slot_center_offset + slot_half_height));
        rect.push_back(point_at(xa, slot_center_offset - slot_half_height));
        slots.push_back(std::move(rect));
    };

    double cursor = std::min(length, pattern.left_margin);
    for (int i = 0; i < pattern.fingers; i++) {
        emit_slot(cursor + pattern.finger_width * 0.5, pattern.finger_width + params.play_mm);
        cursor = std::min(length, cursor + pattern.finger_width);
        if (i + 1 < pattern.fingers)
            cursor = std::min(length, cursor + pattern.space_width);
    }

    return slots;
}

std::vector<std::vector<glm::dvec2>> build_joint_interlocking_slot_profiles(const glm::dvec2 &p1, const glm::dvec2 &p2,
                                                                             const JointGeneratorParams &params, bool counterpart,
                                                                             bool slots_on_fingers, double side_sign)
{
    std::vector<std::vector<glm::dvec2>> slots;
    const auto delta = p2 - p1;
    const auto length = glm::length(delta);
    if (length < 1e-6)
        return slots;

    const auto tangent = delta / length;
    const auto normal = normalize_dir(glm::dvec2{-tangent.y, tangent.x}) * (side_sign >= 0 ? 1.0 : -1.0);
    const auto pattern = build_joint_pattern_data(length, params, counterpart);
    const auto kerf = std::max(0.0, params.burn_mm);
    const auto depth = std::max(0.1, params.thickness_mm + params.extra_length_mm);

    auto point_at = [&](double along, double offset = 0.0) { return p1 + tangent * along + normal * offset; };
    auto emit_slot = [&](double x0, double x1) {
        if (x1 <= x0 + 1e-6)
            return;
        const auto [xa, xb] = apply_joint_kerf(x0, x1, kerf);
        if (xb <= xa + 1e-6)
            return;
        std::vector<glm::dvec2> rect;
        rect.reserve(5);
        rect.push_back(point_at(xa, depth));
        rect.push_back(point_at(xb, depth));
        rect.push_back(point_at(xb, depth * 2.0));
        rect.push_back(point_at(xa, depth * 2.0));
        rect.push_back(point_at(xa, depth));
        slots.push_back(std::move(rect));
    };

    double cursor = std::min(length, pattern.left_margin);
    for (int i = 0; i < pattern.fingers; i++) {
        const auto tooth_end = std::min(length, cursor + pattern.finger_width);
        if (slots_on_fingers)
            emit_slot(cursor, tooth_end);
        cursor = tooth_end;
        if (i + 1 < pattern.fingers) {
            const auto gap_end = std::min(length, cursor + pattern.space_width);
            if (!slots_on_fingers)
                emit_slot(cursor, gap_end);
            cursor = gap_end;
        }
    }

    return slots;
}

bool spawn_sync_argv(const std::vector<std::string> &argv, std::string &stdout_text, std::string &stderr_text, int &exit_code)
{
    std::vector<gchar *> argv_native;
    argv_native.reserve(argv.size() + 1);
    for (const auto &arg : argv)
        argv_native.push_back(const_cast<gchar *>(arg.c_str()));
    argv_native.push_back(nullptr);

    gchar *stdout_buf = nullptr;
    gchar *stderr_buf = nullptr;
    gint wait_status = 0;
    GError *error = nullptr;
    const auto ok = g_spawn_sync(nullptr, argv_native.data(), nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, &stdout_buf,
                                 &stderr_buf, &wait_status, &error);

    stdout_text = stdout_buf ? stdout_buf : "";
    stderr_text = stderr_buf ? stderr_buf : "";
    if (stdout_buf)
        g_free(stdout_buf);
    if (stderr_buf)
        g_free(stderr_buf);

    if (error) {
        if (!stderr_text.empty())
            stderr_text += "\n";
        stderr_text += error->message;
        g_error_free(error);
    }

    if (!ok) {
        exit_code = -1;
        return false;
    }

    #ifdef _WIN32
    exit_code = wait_status;
    #else
    if (WIFEXITED(wait_status))
        exit_code = WEXITSTATUS(wait_status);
    else if (WIFSIGNALED(wait_status))
        exit_code = 128 + WTERMSIG(wait_status);
    else
        exit_code = wait_status;
    #endif

    return true;
}

struct BoxesSvgSegment {
    enum class Kind {
        LINE,
        BEZIER,
    };

    enum class Layer {
        OUTER_CUT,
        INNER_CUT,
        ETCHING,
        ETCHING_DEEP,
        ANNOTATIONS,
    };

    Kind kind = Kind::LINE;
    Layer layer = Layer::OUTER_CUT;
    glm::dvec2 p1 = {0, 0};
    glm::dvec2 c1 = {0, 0};
    glm::dvec2 c2 = {0, 0};
    glm::dvec2 p2 = {0, 0};
};

struct BoxesSvgPath {
    BoxesSvgSegment::Layer layer = BoxesSvgSegment::Layer::OUTER_CUT;
    std::vector<BoxesSvgSegment> segments;
    glm::dvec2 bbox_min = {0, 0};
    glm::dvec2 bbox_max = {0, 0};
};

struct BoxesSvgGroup {
    std::string id;
    std::vector<BoxesSvgSegment> segments;
    glm::dvec2 bbox_min = {0, 0};
    glm::dvec2 bbox_max = {0, 0};
};

struct BoxesPreviewTransform {
    double scale = 1.0;
    double ox = 0.0;
    double oy = 0.0;
};

struct BoxesValuesSnapshot {
    std::map<std::string, std::string> values;
};

glm::dvec2 map_boxes_svg_point(double x, double y, double svg_height)
{
    return {x, svg_height - y};
}

void expand_boxes_svg_bbox(glm::dvec2 &bbox_min, glm::dvec2 &bbox_max, const glm::dvec2 &p)
{
    bbox_min.x = std::min(bbox_min.x, p.x);
    bbox_min.y = std::min(bbox_min.y, p.y);
    bbox_max.x = std::max(bbox_max.x, p.x);
    bbox_max.y = std::max(bbox_max.y, p.y);
}

double point_line_distance(const glm::dvec2 &p, const glm::dvec2 &a, const glm::dvec2 &b)
{
    const auto ab = b - a;
    const auto len = glm::length(ab);
    if (len < 1e-9)
        return glm::length(p - a);
    return std::abs(ab.x * (a.y - p.y) - (a.x - p.x) * ab.y) / len;
}

bool boxes_svg_bezier_is_effectively_line(const glm::dvec2 &p1, const glm::dvec2 &c1, const glm::dvec2 &c2, const glm::dvec2 &p2)
{
    return point_line_distance(c1, p1, p2) < 1e-3 && point_line_distance(c2, p1, p2) < 1e-3;
}

std::optional<BoxesSvgSegment::Layer> classify_boxes_svg_stroke(const NSVGshape &shape)
{
    if (shape.stroke.type != NSVG_PAINT_COLOR)
        return std::nullopt;

    const auto color = shape.stroke.color;
    const auto r = static_cast<unsigned int>(color & 0xffu);
    const auto g = static_cast<unsigned int>((color >> 8u) & 0xffu);
    const auto b = static_cast<unsigned int>((color >> 16u) & 0xffu);

    if (r == 0u && g == 0u && b == 255u)
        return BoxesSvgSegment::Layer::INNER_CUT;
    if (r == 0u && g == 255u && b == 0u)
        return BoxesSvgSegment::Layer::ETCHING;
    if (r == 0u && g == 255u && b == 255u)
        return BoxesSvgSegment::Layer::ETCHING_DEEP;
    if (r == 255u && g == 0u && b == 0u)
        return BoxesSvgSegment::Layer::ANNOTATIONS;

    if ((r == 0u && g == 0u && b == 0u) || (r == 255u && g == 0u && b == 255u)
        || (r == 255u && g == 255u && b == 0u) || (r == 255u && g == 255u && b == 255u))
        return BoxesSvgSegment::Layer::OUTER_CUT;

    return std::nullopt;
}

double boxes_svg_bbox_area(const glm::dvec2 &bbox_min, const glm::dvec2 &bbox_max)
{
    const auto size = bbox_max - bbox_min;
    return std::max(0.0, size.x) * std::max(0.0, size.y);
}

double boxes_svg_bbox_gap_distance(const glm::dvec2 &a_min, const glm::dvec2 &a_max, const glm::dvec2 &b_min, const glm::dvec2 &b_max)
{
    const auto dx = std::max({a_min.x - b_max.x, b_min.x - a_max.x, 0.0});
    const auto dy = std::max({a_min.y - b_max.y, b_min.y - a_max.y, 0.0});
    return std::hypot(dx, dy);
}

double boxes_svg_bbox_union_area(const glm::dvec2 &a_min, const glm::dvec2 &a_max, const glm::dvec2 &b_min, const glm::dvec2 &b_max)
{
    const auto minp = glm::dvec2{std::min(a_min.x, b_min.x), std::min(a_min.y, b_min.y)};
    const auto maxp = glm::dvec2{std::max(a_max.x, b_max.x), std::max(a_max.y, b_max.y)};
    return boxes_svg_bbox_area(minp, maxp);
}

bool boxes_svg_bbox_contains(const glm::dvec2 &outer_min, const glm::dvec2 &outer_max, const glm::dvec2 &inner_min,
                             const glm::dvec2 &inner_max, double margin = 1e-3)
{
    return inner_min.x >= outer_min.x - margin && inner_min.y >= outer_min.y - margin && inner_max.x <= outer_max.x + margin
           && inner_max.y <= outer_max.y + margin;
}

void repack_boxes_svg_paths(std::vector<BoxesSvgPath> &paths, std::vector<BoxesSvgSegment> &segments, glm::dvec2 &bbox_min,
                            glm::dvec2 &bbox_max)
{
    struct Group {
        std::vector<size_t> path_indices;
        glm::dvec2 bbox_min = {0, 0};
        glm::dvec2 bbox_max = {0, 0};
    };

    if (paths.empty()) {
        segments.clear();
        bbox_min = {0, 0};
        bbox_max = {0, 0};
        return;
    }

    std::vector<size_t> outer_roots;
    outer_roots.reserve(paths.size());
    for (size_t i = 0; i < paths.size(); i++) {
        if (paths.at(i).layer == BoxesSvgSegment::Layer::OUTER_CUT)
            outer_roots.push_back(i);
    }

    std::vector<size_t> roots(paths.size());
    std::iota(roots.begin(), roots.end(), 0);
    for (size_t i = 0; i < paths.size(); i++) {
        if (paths.at(i).layer == BoxesSvgSegment::Layer::OUTER_CUT)
            continue;

        const auto path_area = boxes_svg_bbox_area(paths.at(i).bbox_min, paths.at(i).bbox_max);
        double best_score = std::numeric_limits<double>::infinity();
        std::optional<size_t> best_outer;
        for (const auto j : outer_roots) {
            if (i == j)
                continue;
            const auto outer_area = boxes_svg_bbox_area(paths.at(j).bbox_min, paths.at(j).bbox_max);
            const auto contains =
                    boxes_svg_bbox_contains(paths.at(j).bbox_min, paths.at(j).bbox_max, paths.at(i).bbox_min, paths.at(i).bbox_max,
                                            1e-2);
            const auto gap =
                    boxes_svg_bbox_gap_distance(paths.at(j).bbox_min, paths.at(j).bbox_max, paths.at(i).bbox_min, paths.at(i).bbox_max);
            const auto union_area =
                    boxes_svg_bbox_union_area(paths.at(j).bbox_min, paths.at(j).bbox_max, paths.at(i).bbox_min, paths.at(i).bbox_max);
            const auto area_penalty = std::max(0.0, union_area - outer_area - path_area);
            const auto score = (contains ? 0.0 : 1'000'000.0) + gap * 1000.0 + area_penalty;
            if (score < best_score) {
                best_score = score;
                best_outer = j;
            }
        }

        if (best_outer) {
            const auto gap =
                    boxes_svg_bbox_gap_distance(paths.at(*best_outer).bbox_min, paths.at(*best_outer).bbox_max, paths.at(i).bbox_min,
                                                paths.at(i).bbox_max);
            const auto outer_size = paths.at(*best_outer).bbox_max - paths.at(*best_outer).bbox_min;
            const auto attach_limit = std::max(12.0, std::min(outer_size.x, outer_size.y) * 0.2);
            if (gap <= attach_limit || paths.at(i).layer == BoxesSvgSegment::Layer::ANNOTATIONS)
                roots.at(i) = *best_outer;
        }
    }

    std::map<size_t, Group> groups_by_root;
    for (size_t i = 0; i < paths.size(); i++) {
        auto &group = groups_by_root[roots.at(i)];
        if (group.path_indices.empty()) {
            group.bbox_min = paths.at(i).bbox_min;
            group.bbox_max = paths.at(i).bbox_max;
        }
        else {
            expand_boxes_svg_bbox(group.bbox_min, group.bbox_max, paths.at(i).bbox_min);
            expand_boxes_svg_bbox(group.bbox_min, group.bbox_max, paths.at(i).bbox_max);
        }
        group.path_indices.push_back(i);
    }

    std::vector<Group> groups;
    groups.reserve(groups_by_root.size());
    for (auto &[_, group] : groups_by_root)
        groups.push_back(std::move(group));

    std::stable_sort(groups.begin(), groups.end(), [](const auto &a, const auto &b) {
        const auto as = a.bbox_max - a.bbox_min;
        const auto bs = b.bbox_max - b.bbox_min;
        const auto area_a = as.x * as.y;
        const auto area_b = bs.x * bs.y;
        if (std::abs(area_a - area_b) > 1e-6)
            return area_a > area_b;
        if (std::abs(as.y - bs.y) > 1e-6)
            return as.y > bs.y;
        return as.x > bs.x;
    });

    double total_area = 0.0;
    double max_width = 0.0;
    for (const auto &group : groups) {
        const auto size = group.bbox_max - group.bbox_min;
        total_area += std::max(0.0, size.x) * std::max(0.0, size.y);
        max_width = std::max(max_width, size.x);
    }

    const double gap = 10.0;
    const double row_target_width = std::max(max_width, std::sqrt(std::max(1.0, total_area)) * 1.35);
    double x = 0.0;
    double y = 0.0;
    double row_height = 0.0;

    segments.clear();
    bbox_min = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    bbox_max = {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

    for (const auto &group : groups) {
        const auto size = group.bbox_max - group.bbox_min;
        if (x > 0.0 && x + size.x > row_target_width) {
            x = 0.0;
            y += row_height + gap;
            row_height = 0.0;
        }

        const auto offset = glm::dvec2{x, y} - group.bbox_min;
        for (const auto path_index : group.path_indices) {
            for (const auto &seg : paths.at(path_index).segments) {
                auto packed_seg = seg;
                packed_seg.p1 += offset;
                packed_seg.p2 += offset;
                if (packed_seg.kind == BoxesSvgSegment::Kind::BEZIER) {
                    packed_seg.c1 += offset;
                    packed_seg.c2 += offset;
                }
                expand_boxes_svg_bbox(bbox_min, bbox_max, packed_seg.p1);
                expand_boxes_svg_bbox(bbox_min, bbox_max, packed_seg.p2);
                if (packed_seg.kind == BoxesSvgSegment::Kind::BEZIER) {
                    expand_boxes_svg_bbox(bbox_min, bbox_max, packed_seg.c1);
                    expand_boxes_svg_bbox(bbox_min, bbox_max, packed_seg.c2);
                }
                segments.push_back(std::move(packed_seg));
            }
        }

        x += size.x + gap;
        row_height = std::max(row_height, size.y);
    }
}

bool parse_boxes_svg_image(NSVGimage *image, std::vector<BoxesSvgSegment> &segments, glm::dvec2 &bbox_min, glm::dvec2 &bbox_max,
                           std::string &error, bool repack_paths = true)
{
    if (!image) {
        error = "Couldn't parse generated SVG";
        return false;
    }

    std::vector<BoxesSvgPath> paths;

    for (auto *shape = image->shapes; shape; shape = shape->next) {
        const auto layer = classify_boxes_svg_stroke(*shape);
        if (!layer)
            continue;
        for (auto *path = shape->paths; path; path = path->next) {
            auto &svg_path = paths.emplace_back();
            svg_path.layer = *layer;
            svg_path.bbox_min = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
            svg_path.bbox_max = {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
            for (int i = 0; i < path->npts - 1; i += 3) {
                const auto p1 = map_boxes_svg_point(path->pts[2 * i], path->pts[2 * i + 1], image->height);
                const auto c1 = map_boxes_svg_point(path->pts[2 * (i + 1)], path->pts[2 * (i + 1) + 1], image->height);
                const auto c2 = map_boxes_svg_point(path->pts[2 * (i + 2)], path->pts[2 * (i + 2) + 1], image->height);
                const auto p2 = map_boxes_svg_point(path->pts[2 * (i + 3)], path->pts[2 * (i + 3) + 1], image->height);
                if (glm::length(p2 - p1) < 1e-6)
                    continue;

                expand_boxes_svg_bbox(svg_path.bbox_min, svg_path.bbox_max, p1);
                expand_boxes_svg_bbox(svg_path.bbox_min, svg_path.bbox_max, c1);
                expand_boxes_svg_bbox(svg_path.bbox_min, svg_path.bbox_max, c2);
                expand_boxes_svg_bbox(svg_path.bbox_min, svg_path.bbox_max, p2);

                if (boxes_svg_bezier_is_effectively_line(p1, c1, c2, p2))
                    svg_path.segments.push_back({BoxesSvgSegment::Kind::LINE, *layer, p1, {}, {}, p2});
                else
                    svg_path.segments.push_back({BoxesSvgSegment::Kind::BEZIER, *layer, p1, c1, c2, p2});
            }
            if (svg_path.segments.empty())
                paths.pop_back();
        }
    }

    if (paths.empty()) {
        error = "Generated SVG contains no geometry";
        return false;
    }

    if (repack_paths)
        repack_boxes_svg_paths(paths, segments, bbox_min, bbox_max);
    else {
        segments.clear();
        bbox_min = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        bbox_max = {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
        for (const auto &path : paths) {
            for (const auto &seg : path.segments) {
                segments.push_back(seg);
                expand_boxes_svg_bbox(bbox_min, bbox_max, seg.p1);
                expand_boxes_svg_bbox(bbox_min, bbox_max, seg.p2);
                if (seg.kind == BoxesSvgSegment::Kind::BEZIER) {
                    expand_boxes_svg_bbox(bbox_min, bbox_max, seg.c1);
                    expand_boxes_svg_bbox(bbox_min, bbox_max, seg.c2);
                }
            }
        }
    }
    return true;
}

bool parse_boxes_svg_string(const std::string &svg_text, std::vector<BoxesSvgSegment> &segments, glm::dvec2 &bbox_min,
                            glm::dvec2 &bbox_max, std::string &error)
{
    std::vector<char> buf(svg_text.begin(), svg_text.end());
    buf.push_back('\0');
    auto image = std::unique_ptr<NSVGimage, void (*)(NSVGimage *)>(nsvgParse(buf.data(), "mm", 96.0f), nsvgDelete);
    return parse_boxes_svg_image(image.get(), segments, bbox_min, bbox_max, error, false);
}

bool parse_boxes_svg(const std::filesystem::path &svg_path, std::vector<BoxesSvgSegment> &segments, glm::dvec2 &bbox_min,
                     glm::dvec2 &bbox_max, std::string &error)
{
    auto image = std::unique_ptr<NSVGimage, void (*)(NSVGimage *)>(
            nsvgParseFromFile(path_to_string(svg_path).c_str(), "mm", 96.0f), nsvgDelete);
    return parse_boxes_svg_image(image.get(), segments, bbox_min, bbox_max, error);
}

void repack_boxes_svg_groups(const std::vector<BoxesSvgGroup> &groups, std::vector<BoxesSvgSegment> &segments, glm::dvec2 &bbox_min,
                             glm::dvec2 &bbox_max)
{
    if (groups.empty()) {
        segments.clear();
        bbox_min = {0, 0};
        bbox_max = {0, 0};
        return;
    }

    std::vector<size_t> order(groups.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&groups](size_t ia, size_t ib) {
        const auto as = groups.at(ia).bbox_max - groups.at(ia).bbox_min;
        const auto bs = groups.at(ib).bbox_max - groups.at(ib).bbox_min;
        const auto area_a = as.x * as.y;
        const auto area_b = bs.x * bs.y;
        if (std::abs(area_a - area_b) > 1e-6)
            return area_a > area_b;
        if (std::abs(as.y - bs.y) > 1e-6)
            return as.y > bs.y;
        return as.x > bs.x;
    });

    double total_area = 0.0;
    double max_width = 0.0;
    for (const auto idx : order) {
        const auto size = groups.at(idx).bbox_max - groups.at(idx).bbox_min;
        total_area += std::max(0.0, size.x) * std::max(0.0, size.y);
        max_width = std::max(max_width, size.x);
    }

    const double gap = 10.0;
    const double row_target_width = std::max(max_width, std::sqrt(std::max(1.0, total_area)) * 1.35);
    double x = 0.0;
    double y = 0.0;
    double row_height = 0.0;

    segments.clear();
    bbox_min = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    bbox_max = {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

    for (const auto idx : order) {
        const auto &group = groups.at(idx);
        const auto size = group.bbox_max - group.bbox_min;
        if (x > 0.0 && x + size.x > row_target_width) {
            x = 0.0;
            y += row_height + gap;
            row_height = 0.0;
        }

        const auto offset = glm::dvec2{x, y} - group.bbox_min;
        for (const auto &seg : group.segments) {
            auto packed_seg = seg;
            packed_seg.p1 += offset;
            packed_seg.p2 += offset;
            if (packed_seg.kind == BoxesSvgSegment::Kind::BEZIER) {
                packed_seg.c1 += offset;
                packed_seg.c2 += offset;
            }
            expand_boxes_svg_bbox(bbox_min, bbox_max, packed_seg.p1);
            expand_boxes_svg_bbox(bbox_min, bbox_max, packed_seg.p2);
            if (packed_seg.kind == BoxesSvgSegment::Kind::BEZIER) {
                expand_boxes_svg_bbox(bbox_min, bbox_max, packed_seg.c1);
                expand_boxes_svg_bbox(bbox_min, bbox_max, packed_seg.c2);
            }
            segments.push_back(std::move(packed_seg));
        }

        x += size.x + gap;
        row_height = std::max(row_height, size.y);
    }
}

BoxesPreviewTransform get_boxes_preview_transform(const glm::dvec2 &bbox_min, const glm::dvec2 &bbox_max, int width, int height,
                                                  double zoom, double pan_x, double pan_y)
{
    BoxesPreviewTransform transform;
    const auto bbox_size = bbox_max - bbox_min;
    const auto safe_width = std::max(1.0, bbox_size.x);
    const auto safe_height = std::max(1.0, bbox_size.y);
    const auto fit_w = std::max(1, width) - 32.0;
    const auto fit_h = std::max(1, height) - 32.0;
    const auto fit_scale = std::max(0.01, std::min(fit_w / safe_width, fit_h / safe_height));
    transform.scale = fit_scale * std::clamp(zoom, 0.1, 64.0);
    transform.ox = (static_cast<double>(width) - safe_width * transform.scale) * 0.5 - bbox_min.x * transform.scale + pan_x;
    transform.oy = (static_cast<double>(height) - safe_height * transform.scale) * 0.5 + bbox_max.y * transform.scale + pan_y;
    return transform;
}

int boxes_preview_layer_index(BoxesSvgSegment::Layer layer)
{
    switch (layer) {
    case BoxesSvgSegment::Layer::OUTER_CUT:
        return 0;
    case BoxesSvgSegment::Layer::INNER_CUT:
        return 1;
    case BoxesSvgSegment::Layer::ETCHING:
        return 2;
    case BoxesSvgSegment::Layer::ETCHING_DEEP:
        return 3;
    case BoxesSvgSegment::Layer::ANNOTATIONS:
        return 4;
    }
    return 0;
}

void append_boxes_preview_polyline(auto &polylines, const BoxesSvgSegment &seg)
{
    if (seg.kind == BoxesSvgSegment::Kind::LINE) {
        polylines.push_back({{seg.p1, seg.p2}, boxes_preview_layer_index(seg.layer)});
        return;
    }

    auto &polyline = polylines.emplace_back();
    polyline.layer = boxes_preview_layer_index(seg.layer);
    constexpr int bezier_steps = 18;
    for (int i = 0; i <= bezier_steps; i++) {
        const auto t = static_cast<double>(i) / static_cast<double>(bezier_steps);
        const auto u = 1.0 - t;
        const auto p = u * u * u * seg.p1 + 3.0 * u * u * t * seg.c1 + 3.0 * u * t * t * seg.c2 + t * t * t * seg.p2;
        append_point_if_new(polyline.points, p);
    }
}

BoxesValuesSnapshot capture_boxes_values_snapshot(const BoxesTemplateDef &template_def,
                                                 const std::map<std::string, Gtk::SpinButton *> &spins,
                                                 const std::map<std::string, Gtk::Entry *> &entries,
                                                 const std::map<std::string, Gtk::DropDown *> &dropdowns,
                                                 const std::map<std::string, Gtk::Switch *> &switches)
{
    const auto fmt = [](double value) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(3) << value;
        return ss.str();
    };

    BoxesValuesSnapshot snapshot;

    for (const auto &arg : template_def.args) {
        std::string value = arg.default_string;
        switch (arg.kind) {
        case BoxesTemplateDef::ArgKind::FLOAT:
            if (const auto it = spins.find(arg.dest); it != spins.end() && it->second)
                value = fmt(it->second->get_value());
            else
                value = fmt(arg.default_float);
            break;
        case BoxesTemplateDef::ArgKind::INT:
            if (const auto it = spins.find(arg.dest); it != spins.end() && it->second)
                value = std::to_string(static_cast<int>(std::lround(it->second->get_value())));
            else
                value = std::to_string(arg.default_int);
            break;
        case BoxesTemplateDef::ArgKind::BOOL:
            if (const auto it = switches.find(arg.dest); it != switches.end() && it->second)
                value = it->second->get_active() ? "1" : "0";
            else
                value = arg.default_bool ? "1" : "0";
            break;
        case BoxesTemplateDef::ArgKind::CHOICE:
            if (const auto it = dropdowns.find(arg.dest); it != dropdowns.end() && it->second && !arg.choices.empty()) {
                const auto idx = std::min<size_t>(it->second->get_selected(), arg.choices.size() - 1);
                value = arg.choices.at(idx);
            }
            else if (!arg.choices.empty()) {
                value = arg.choices.front();
            }
            break;
        case BoxesTemplateDef::ArgKind::STRING:
            if (const auto it = entries.find(arg.dest); it != entries.end() && it->second)
                value = it->second->get_text();
            break;
        }
        snapshot.values[arg.dest] = std::move(value);
    }
    return snapshot;
}

std::string joints_value_key(const std::string &family_id, const std::string &dest)
{
    return family_id + ":" + dest;
}

std::vector<SettingsArgDef> collect_joint_visible_args(const JointFamilyDef &family, const JointRoleDef &role)
{
    std::vector<SettingsArgDef> args = family.args;
    const auto append_edge_args = [&args](const JointEdgeDef *edge) {
        if (!edge)
            return;
        for (const auto &arg : edge->extra_args) {
            if (std::none_of(args.begin(), args.end(), [&arg](const auto &existing) { return existing.dest == arg.dest; }))
                args.push_back(arg);
        }
    };
    append_edge_args(find_joint_edge(family, role.line0_edge));
    if (role.pair)
        append_edge_args(find_joint_edge(family, role.line1_edge));
    return args;
}

std::vector<SettingsArgDef> collect_joint_edge_args(const JointFamilyDef &family, const JointEdgeDef &edge)
{
    std::vector<SettingsArgDef> args = family.args;
    for (const auto &arg : edge.extra_args) {
        if (std::none_of(args.begin(), args.end(), [&arg](const auto &existing) { return existing.dest == arg.dest; }))
            args.push_back(arg);
    }
    return args;
}

BoxesValuesSnapshot capture_joint_values_snapshot(const JointFamilyDef &family, const JointRoleDef &role,
                                                  const std::map<std::string, Gtk::SpinButton *> &spins,
                                                  const std::map<std::string, Gtk::Entry *> &entries,
                                                  const std::map<std::string, Gtk::DropDown *> &dropdowns,
                                                  const std::map<std::string, Gtk::Switch *> &switches)
{
    const auto fmt = [](double value) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(3) << value;
        return ss.str();
    };

    BoxesValuesSnapshot snapshot;
    for (const auto &arg : collect_joint_visible_args(family, role)) {
        std::string value = arg.default_string;
        switch (arg.kind) {
        case SettingsArgKind::FLOAT:
            if (const auto it = spins.find(arg.dest); it != spins.end() && it->second)
                value = fmt(it->second->get_value());
            else
                value = fmt(arg.default_float);
            break;
        case SettingsArgKind::INT:
            if (const auto it = spins.find(arg.dest); it != spins.end() && it->second)
                value = std::to_string(static_cast<int>(std::lround(it->second->get_value())));
            else
                value = std::to_string(arg.default_int);
            break;
        case SettingsArgKind::BOOL:
            if (const auto it = switches.find(arg.dest); it != switches.end() && it->second)
                value = it->second->get_active() ? "1" : "0";
            else
                value = arg.default_bool ? "1" : "0";
            break;
        case SettingsArgKind::CHOICE:
            if (const auto it = dropdowns.find(arg.dest); it != dropdowns.end() && it->second && !arg.choices.empty()) {
                const auto idx = std::min<size_t>(it->second->get_selected(), arg.choices.size() - 1);
                value = arg.choices.at(idx);
            }
            else if (!arg.choices.empty()) {
                value = arg.choices.front();
            }
            break;
        case SettingsArgKind::STRING:
            if (const auto it = entries.find(arg.dest); it != entries.end() && it->second)
                value = it->second->get_text();
            break;
        }
        snapshot.values[arg.dest] = std::move(value);
    }
    return snapshot;
}

bool generate_joint_edge_segments(const JointFamilyDef &family, const JointEdgeDef &edge, double length,
                                  const BoxesValuesSnapshot &snapshot, double thickness_mm, double burn_mm,
                                  std::vector<BoxesSvgSegment> &segments, glm::dvec2 &bbox_min, glm::dvec2 &bbox_max,
                                  std::string &error)
{
    const auto fmt = [](double value) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(3) << value;
        return ss.str();
    };

    std::filesystem::path runner_path;
    for (const auto &candidate : get_boxes_runner_candidates()) {
        if (std::filesystem::exists(candidate)) {
            runner_path = candidate;
            break;
        }
    }
    if (runner_path.empty()) {
        error = "Bundled boxes runner is missing";
        return false;
    }

    auto generator_args = std::vector<std::string>{
            "--joint-edge-json",
            family.id,
            edge.id,
            fmt(length),
            "--thickness",
            fmt(thickness_mm),
            "--burn",
            fmt(burn_mm),
    };

    for (const auto &arg : collect_joint_edge_args(family, edge)) {
        const auto it = snapshot.values.find(arg.dest);
        const auto &value = it != snapshot.values.end() ? it->second : arg.default_string;
        if (arg.option.empty())
            generator_args.push_back("--call_" + arg.dest);
        else
            generator_args.push_back(arg.option);
        generator_args.push_back(value);
    }

    std::vector<std::vector<std::string>> commands;
    for (const auto &python_cmd : get_boxes_python_candidates(runner_path)) {
        auto cmd = std::vector<std::string>{python_cmd, path_to_string(runner_path)};
        cmd.insert(cmd.end(), generator_args.begin(), generator_args.end());
        commands.push_back(std::move(cmd));
    }

    std::string last_stdout;
    std::string last_stderr;
    int last_exit_code = -1;
    bool generated = false;
    for (const auto &cmd : commands) {
        std::string out;
        std::string err;
        int exit_code = -1;
        const auto spawned = spawn_sync_argv(cmd, out, err, exit_code);
        last_stdout = std::move(out);
        last_stderr = std::move(err);
        last_exit_code = exit_code;
        if (!spawned || exit_code != 0 || last_stdout.empty())
            continue;
        generated = true;
        break;
    }

    if (!generated) {
        if (last_exit_code == -1)
            error = "Bundled joints helper couldn't start";
        else if (!last_stderr.empty())
            error = last_stderr;
        else if (!last_stdout.empty())
            error = last_stdout;
        else
            error = "Bundled joints helper failed";
        return false;
    }

    try {
        const auto json = nlohmann::json::parse(last_stdout);
        segments.clear();
        bbox_min = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        bbox_max = {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

        auto parse_layer = [](const std::string &name) {
            if (name == "inner_cut")
                return BoxesSvgSegment::Layer::INNER_CUT;
            if (name == "etching")
                return BoxesSvgSegment::Layer::ETCHING;
            if (name == "etching_deep")
                return BoxesSvgSegment::Layer::ETCHING_DEEP;
            if (name == "annotations")
                return BoxesSvgSegment::Layer::ANNOTATIONS;
            return BoxesSvgSegment::Layer::OUTER_CUT;
        };

        for (const auto &seg_item : json.at("segments")) {
            BoxesSvgSegment seg;
            seg.kind = seg_item.value("kind", "line") == "bezier" ? BoxesSvgSegment::Kind::BEZIER : BoxesSvgSegment::Kind::LINE;
            seg.layer = parse_layer(seg_item.value("layer", "outer_cut"));
            const auto p1 = seg_item.at("p1").get<std::vector<double>>();
            const auto p2 = seg_item.at("p2").get<std::vector<double>>();
            seg.p1 = {p1.at(0), p1.at(1)};
            seg.p2 = {p2.at(0), p2.at(1)};
            if (seg.kind == BoxesSvgSegment::Kind::BEZIER) {
                const auto c1 = seg_item.at("c1").get<std::vector<double>>();
                const auto c2 = seg_item.at("c2").get<std::vector<double>>();
                seg.c1 = {c1.at(0), c1.at(1)};
                seg.c2 = {c2.at(0), c2.at(1)};
            }
            segments.push_back(seg);
            expand_boxes_svg_bbox(bbox_min, bbox_max, seg.p1);
            expand_boxes_svg_bbox(bbox_min, bbox_max, seg.p2);
            if (seg.kind == BoxesSvgSegment::Kind::BEZIER) {
                expand_boxes_svg_bbox(bbox_min, bbox_max, seg.c1);
                expand_boxes_svg_bbox(bbox_min, bbox_max, seg.c2);
            }
        }

        if (segments.empty()) {
            error = "Joint helper returned no geometry";
            return false;
        }
    }
    catch (const std::exception &e) {
        error = std::string("Couldn't parse joints geometry: ") + e.what();
        return false;
    }

    return true;
}

bool generate_boxes_svg_segments(const BoxesTemplateDef &template_def, const BoxesValuesSnapshot &snapshot,
                                 std::vector<BoxesSvgSegment> &segments, glm::dvec2 &bbox_min, glm::dvec2 &bbox_max,
                                 std::string &error)
{
    auto generator_args = std::vector<std::string>{
            template_def.generator,
            "--format",
            "svg",
            "--output",
            "-",
    };

    for (const auto &arg : template_def.args) {
        const auto it = snapshot.values.find(arg.dest);
        const auto &value = it != snapshot.values.end() ? it->second : arg.default_string;
        if (arg.option.empty())
            continue;
        generator_args.push_back(arg.option);
        generator_args.push_back(value);
    }

    std::filesystem::path runner_path;
    for (const auto &candidate : get_boxes_runner_candidates()) {
        if (std::filesystem::exists(candidate)) {
            runner_path = candidate;
            break;
        }
    }
    if (runner_path.empty()) {
        error = "Bundled boxes runner is missing";
        return false;
    }

    std::vector<std::vector<std::string>> commands;
    for (const auto &python_cmd : get_boxes_python_candidates(runner_path)) {
        auto cmd = std::vector<std::string>{python_cmd, path_to_string(runner_path)};
        cmd.insert(cmd.end(), generator_args.begin(), generator_args.end());
        commands.push_back(std::move(cmd));
    }

    std::string last_stdout;
    std::string last_stderr;
    int last_exit_code = -1;
    bool generated = false;
    for (const auto &cmd : commands) {
        std::string out;
        std::string err;
        int exit_code = -1;
        const auto spawned = spawn_sync_argv(cmd, out, err, exit_code);
        last_stdout = std::move(out);
        last_stderr = std::move(err);
        last_exit_code = exit_code;
        if (!spawned || exit_code != 0 || last_stdout.empty())
            continue;
        generated = true;
        break;
    }

    if (!generated) {
        if (last_exit_code == -1)
            error = "Bundled boxes runner couldn't start (missing Python runtime?)";
        else if (!last_stderr.empty())
            error = last_stderr;
        else if (!last_stdout.empty())
            error = last_stdout;
        else
            error = "Bundled boxes generation failed; check parameters";
        return false;
    }

    return parse_boxes_svg_string(last_stdout, segments, bbox_min, bbox_max, error);
}

std::vector<std::filesystem::path> get_boxes_runner_candidates()
{
    std::vector<std::filesystem::path> paths;
    const auto get_executable_dir = []() -> std::filesystem::path {
#ifdef _WIN32
        std::wstring buffer(32768, L'\0');
        const auto len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0 || len >= buffer.size())
            return {};
        buffer.resize(len);
        return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            return {};
        return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str())).parent_path();
#else
        std::vector<char> buffer(4096, '\0');
        const auto len = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (len <= 0)
            return {};
        buffer.at(static_cast<size_t>(len)) = '\0';
        return std::filesystem::weakly_canonical(std::filesystem::path(buffer.data())).parent_path();
#endif
    };

    const auto exe_dir = get_executable_dir();
    if (!exe_dir.empty()) {
        paths.emplace_back(exe_dir / "share" / "dxfsketcher" / "pyvendor" / "boxes_runner.py");
        paths.emplace_back(exe_dir.parent_path() / "share" / "dxfsketcher" / "pyvendor" / "boxes_runner.py");
        paths.emplace_back(exe_dir.parent_path() / "Resources" / "share" / "dxfsketcher" / "pyvendor" / "boxes_runner.py");
    }
    paths.emplace_back(std::filesystem::path(DUNE_SOURCE_ROOT) / "3rd_party" / "pyvendor" / "boxes_runner.py");
    paths.emplace_back(std::filesystem::path(DUNE_PKGDATADIR) / "pyvendor" / "boxes_runner.py");
    return paths;
}

std::vector<std::string> get_boxes_python_candidates(const std::filesystem::path &runner_path)
{
    std::vector<std::string> commands;
    if (const auto *env_python = std::getenv("DUNE_BOXES_PYTHON")) {
        if (*env_python)
            commands.emplace_back(env_python);
    }

    const auto root = runner_path.parent_path();
    for (const auto &candidate : {
                 root / "python" / "bin" / "python3",
                 root / "python" / "bin" / "python",
                 root / "python" / "Scripts" / "python.exe",
                 root / "python" / "python.exe",
         }) {
        if (std::filesystem::exists(candidate))
            commands.push_back(path_to_string(candidate));
    }

    commands.push_back("python3");
    commands.push_back("python");
    return commands;
}

bool ensure_boxes_catalog_loaded(std::string &error)
{
    if (!g_boxes_templates.empty() && !g_boxes_categories.empty())
        return true;

    std::filesystem::path runner_path;
    for (const auto &candidate : get_boxes_runner_candidates()) {
        if (std::filesystem::exists(candidate)) {
            runner_path = candidate;
            break;
        }
    }
    if (runner_path.empty()) {
        error = "Bundled boxes runner is missing";
        return false;
    }

    std::vector<std::vector<std::string>> commands;
    for (const auto &python_cmd : get_boxes_python_candidates(runner_path))
        commands.push_back({python_cmd, path_to_string(runner_path), "--catalog-json"});

    std::string last_stdout;
    std::string last_stderr;
    int last_exit_code = -1;
    bool loaded = false;
    for (const auto &cmd : commands) {
        std::string out;
        std::string err;
        int exit_code = -1;
        const auto spawned = spawn_sync_argv(cmd, out, err, exit_code);
        last_stdout = std::move(out);
        last_stderr = std::move(err);
        last_exit_code = exit_code;
        if (!spawned || exit_code != 0 || last_stdout.empty())
            continue;

        try {
            const auto json = nlohmann::json::parse(last_stdout);
            const auto json_string = [](const nlohmann::json &obj, const char *key, std::string fallback = {}) {
                const auto it = obj.find(key);
                if (it == obj.end() || it->is_null())
                    return fallback;
                if (it->is_string())
                    return it->get<std::string>();
                return it->dump();
            };
            g_boxes_categories.clear();
            g_boxes_templates.clear();

            for (const auto &item : json.at("categories")) {
                BoxesCategoryDef category;
                category.id = json_string(item, "id");
                category.title = json_string(item, "title", category.id);
                category.description = json_string(item, "description");
                category.sample_image = json_string(item, "sample_image");
                category.sample_thumbnail = json_string(item, "sample_thumbnail");
                g_boxes_categories.push_back(std::move(category));
            }

            for (const auto &item : json.at("generators")) {
                BoxesTemplateDef def;
                def.id = json_string(item, "id");
                def.label = json_string(item, "label", def.id);
                def.generator = def.id;
                def.category = json_string(item, "group", "Misc");
                def.short_description = json_string(item, "short_description");
                def.description = json_string(item, "description");
                def.sample_image = json_string(item, "sample_image");
                def.sample_thumbnail = json_string(item, "sample_thumbnail");

                for (const auto &arg_item : item.at("args")) {
                    BoxesTemplateDef::ArgDef arg;
                    arg.dest = json_string(arg_item, "dest");
                    arg.option = json_string(arg_item, "option");
                    arg.label = json_string(arg_item, "label", arg.dest);
                    arg.help = json_string(arg_item, "help");
                    arg.group = json_string(arg_item, "group", "Settings");
                    arg.default_string = json_string(arg_item, "default_string");
                    const auto kind = json_string(arg_item, "kind", "string");
                    if (kind == "float") {
                        arg.kind = BoxesTemplateDef::ArgKind::FLOAT;
                        arg.default_float = arg_item.value("default_float", 0.0);
                    }
                    else if (kind == "int") {
                        arg.kind = BoxesTemplateDef::ArgKind::INT;
                        arg.default_int = arg_item.value("default_int", 0);
                    }
                    else if (kind == "bool") {
                        arg.kind = BoxesTemplateDef::ArgKind::BOOL;
                        arg.default_bool = arg_item.value("default_bool", false);
                    }
                    else if (kind == "choice") {
                        arg.kind = BoxesTemplateDef::ArgKind::CHOICE;
                        arg.choices = arg_item.value("choices", std::vector<std::string>{});
                    }
                    else {
                        arg.kind = BoxesTemplateDef::ArgKind::STRING;
                    }
                    def.args.push_back(std::move(arg));
                }

                for (const auto &group_item : item.at("arg_groups")) {
                    BoxesTemplateDef::ArgGroupDef group;
                    group.title = json_string(group_item, "title", "Settings");
                    group.args = group_item.value("args", std::vector<std::string>{});
                    def.arg_groups.push_back(std::move(group));
                }

                g_boxes_templates.push_back(std::move(def));
            }

            loaded = !g_boxes_templates.empty();
        }
        catch (const std::exception &e) {
            error = std::string("Couldn't parse boxes catalog: ") + e.what();
            g_boxes_categories.clear();
            g_boxes_templates.clear();
            return false;
        }

        if (loaded)
            break;
    }

    if (!loaded) {
        if (last_exit_code == -1)
            error = "Bundled boxes runner couldn't start (missing Python runtime?)";
        else if (!last_stderr.empty())
            error = last_stderr;
        else if (!last_stdout.empty())
            error = last_stdout;
        else
            error = "Bundled boxes catalog loading failed";
        return false;
    }

    if (std::none_of(g_boxes_categories.begin(), g_boxes_categories.end(),
                     [](const auto &category) { return category.id == "Misc"; })) {
        g_boxes_categories.push_back({"Misc", "Misc", "", "", ""});
    }
    return true;
}

bool ensure_joint_families_loaded(std::string &error)
{
    if (!g_joint_families.empty())
        return true;

    std::filesystem::path runner_path;
    for (const auto &candidate : get_boxes_runner_candidates()) {
        if (std::filesystem::exists(candidate)) {
            runner_path = candidate;
            break;
        }
    }
    if (runner_path.empty()) {
        error = "Bundled boxes runner is missing";
        return false;
    }

    std::vector<std::vector<std::string>> commands;
    for (const auto &python_cmd : get_boxes_python_candidates(runner_path))
        commands.push_back({python_cmd, path_to_string(runner_path), "--joint-families-json"});

    std::string last_stdout;
    std::string last_stderr;
    int last_exit_code = -1;
    bool loaded = false;
    for (const auto &cmd : commands) {
        std::string out;
        std::string err;
        int exit_code = -1;
        const auto spawned = spawn_sync_argv(cmd, out, err, exit_code);
        last_stdout = std::move(out);
        last_stderr = std::move(err);
        last_exit_code = exit_code;
        if (!spawned || exit_code != 0 || last_stdout.empty())
            continue;

        try {
            const auto json = nlohmann::json::parse(last_stdout);
            const auto json_string = [](const nlohmann::json &obj, const char *key, std::string fallback = {}) {
                const auto it = obj.find(key);
                if (it == obj.end() || it->is_null())
                    return fallback;
                if (it->is_string())
                    return it->get<std::string>();
                return it->dump();
            };

            g_joint_families.clear();
            for (const auto &item : json.at("families")) {
                JointFamilyDef family;
                family.id = json_string(item, "id");
                family.label = json_string(item, "label", family.id);
                family.description = json_string(item, "description");

                for (const auto &arg_item : item.at("args")) {
                    SettingsArgDef arg;
                    arg.dest = json_string(arg_item, "dest");
                    arg.option = json_string(arg_item, "option");
                    arg.label = json_string(arg_item, "label", arg.dest);
                    arg.help = json_string(arg_item, "help");
                    arg.group = json_string(arg_item, "group", "Settings");
                    arg.default_string = json_string(arg_item, "default_string");
                    const auto kind = json_string(arg_item, "kind", "string");
                    if (kind == "float") {
                        arg.kind = SettingsArgKind::FLOAT;
                        arg.default_float = arg_item.value("default_float", 0.0);
                    }
                    else if (kind == "int") {
                        arg.kind = SettingsArgKind::INT;
                        arg.default_int = arg_item.value("default_int", 0);
                    }
                    else if (kind == "bool") {
                        arg.kind = SettingsArgKind::BOOL;
                        arg.default_bool = arg_item.value("default_bool", false);
                    }
                    else if (kind == "choice") {
                        arg.kind = SettingsArgKind::CHOICE;
                        arg.choices = arg_item.value("choices", std::vector<std::string>{});
                    }
                    else {
                        arg.kind = SettingsArgKind::STRING;
                    }
                    family.args.push_back(std::move(arg));
                }

                for (const auto &group_item : item.at("arg_groups")) {
                    SettingsArgGroupDef group;
                    group.title = json_string(group_item, "title", "Settings");
                    group.args = group_item.value("args", std::vector<std::string>{});
                    family.arg_groups.push_back(std::move(group));
                }

                for (const auto &edge_item : item.at("edges")) {
                    JointEdgeDef edge;
                    edge.id = json_string(edge_item, "id");
                    edge.label = json_string(edge_item, "label", edge.id);
                    edge.side_mode = json_string(edge_item, "side_mode", "feature");
                    for (const auto &arg_item : edge_item.value("extra_args", nlohmann::json::array())) {
                        SettingsArgDef arg;
                        arg.dest = json_string(arg_item, "dest");
                        arg.option = json_string(arg_item, "option");
                        arg.label = json_string(arg_item, "label", arg.dest);
                        arg.help = json_string(arg_item, "help");
                        arg.group = json_string(arg_item, "group", "Settings");
                        arg.default_string = json_string(arg_item, "default_string");
                        const auto kind = json_string(arg_item, "kind", "string");
                        if (kind == "float") {
                            arg.kind = SettingsArgKind::FLOAT;
                            arg.default_float = arg_item.value("default_float", 0.0);
                        }
                        else if (kind == "int") {
                            arg.kind = SettingsArgKind::INT;
                            arg.default_int = arg_item.value("default_int", 0);
                        }
                        else if (kind == "bool") {
                            arg.kind = SettingsArgKind::BOOL;
                            arg.default_bool = arg_item.value("default_bool", false);
                        }
                        else if (kind == "choice") {
                            arg.kind = SettingsArgKind::CHOICE;
                            arg.choices = arg_item.value("choices", std::vector<std::string>{});
                        }
                        else {
                            arg.kind = SettingsArgKind::STRING;
                        }
                        edge.extra_args.push_back(std::move(arg));
                    }
                    family.edges.push_back(std::move(edge));
                }

                for (const auto &role_item : item.at("roles")) {
                    JointRoleDef role;
                    role.id = json_string(role_item, "id");
                    role.label = json_string(role_item, "label", role.id);
                    role.pair = role_item.value("pair", false);
                    role.line0_edge = json_string(role_item, "line0_edge");
                    role.line1_edge = json_string(role_item, "line1_edge");
                    family.roles.push_back(std::move(role));
                }

                if (!family.id.empty() && !family.roles.empty())
                    g_joint_families.push_back(std::move(family));
            }

            loaded = !g_joint_families.empty();
        }
        catch (const std::exception &e) {
            error = std::string("Couldn't parse joints families: ") + e.what();
            g_joint_families.clear();
            return false;
        }

        if (loaded)
            break;
    }

    if (!loaded) {
        if (last_exit_code == -1)
            error = "Bundled joints helper couldn't start (missing Python runtime?)";
        else if (!last_stderr.empty())
            error = last_stderr;
        else if (!last_stdout.empty())
            error = last_stdout;
        else
            error = "Bundled joints families loading failed";
        return false;
    }

    return true;
}

std::vector<std::filesystem::path> get_boxes_sample_candidates(const std::string &sample_image)
{
    std::vector<std::filesystem::path> paths;
    if (sample_image.empty())
        return paths;

    const auto relative_path = std::filesystem::path("boxes") / "static" / "samples" / sample_image;

    const auto get_executable_dir = []() -> std::filesystem::path {
#ifdef _WIN32
        std::vector<wchar_t> buffer(MAX_PATH, L'\0');
        const auto len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0)
            return {};
        return std::filesystem::path(std::wstring(buffer.data(), buffer.data() + len)).parent_path();
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            return {};
        return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str())).parent_path();
#else
        std::vector<char> buffer(4096, '\0');
        const auto len = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (len <= 0)
            return {};
        buffer.at(static_cast<size_t>(len)) = '\0';
        return std::filesystem::weakly_canonical(std::filesystem::path(buffer.data())).parent_path();
#endif
    };

    const auto exe_dir = get_executable_dir();
    if (!exe_dir.empty()) {
        paths.emplace_back(exe_dir / "share" / "dxfsketcher" / "pyvendor" / relative_path);
        paths.emplace_back(exe_dir.parent_path() / "share" / "dxfsketcher" / "pyvendor" / relative_path);
        paths.emplace_back(exe_dir.parent_path() / "Resources" / "share" / "dxfsketcher" / "pyvendor" / relative_path);
    }
    paths.emplace_back(std::filesystem::path(DUNE_SOURCE_ROOT) / "3rd_party" / "pyvendor" / relative_path);
    paths.emplace_back(std::filesystem::path(DUNE_PKGDATADIR) / "pyvendor" / relative_path);
    return paths;
}

std::optional<std::filesystem::path> get_boxes_sample_path(const BoxesTemplateDef &template_def)
{
    return get_boxes_sample_path(template_def.sample_image);
}

std::optional<std::filesystem::path> get_boxes_sample_path(const std::string &sample_image)
{
    for (const auto &candidate : get_boxes_sample_candidates(sample_image)) {
        if (std::filesystem::exists(candidate))
            return candidate;
    }
    return std::nullopt;
}

const UUID &selection_transform_rotate_uuid()
{
    static const UUID uu{"00000000-0000-0000-0000-00000000a001"};
    return uu;
}

const UUID &selection_transform_scale_uuid()
{
    static const UUID uu{"00000000-0000-0000-0000-00000000a002"};
    return uu;
}

bool is_selection_transform_handle(const SelectableRef &sr)
{
    if (sr.type != SelectableRef::Type::SOLID_MODEL_EDGE)
        return false;
    return sr.item == selection_transform_rotate_uuid() || sr.item == selection_transform_scale_uuid();
}

struct SelectionTransformOverlayData {
    UUID group;
    UUID workplane;
    std::vector<UUID> entities;
    glm::dvec2 bbox_min;
    glm::dvec2 bbox_max;
    glm::dvec2 center;
    glm::dvec2 rotate_handle;
    std::array<glm::dvec2, 4> scale_handles;
};

void append_entity_points_for_transform_bbox(const Entity &en, std::vector<glm::dvec2> &points)
{
    if (const auto *line = dynamic_cast<const EntityLine2D *>(&en)) {
        points.push_back(line->m_p1);
        points.push_back(line->m_p2);
    }
    else if (const auto *circle = dynamic_cast<const EntityCircle2D *>(&en)) {
        points.push_back(circle->m_center + glm::dvec2(circle->m_radius, 0));
        points.push_back(circle->m_center + glm::dvec2(-circle->m_radius, 0));
        points.push_back(circle->m_center + glm::dvec2(0, circle->m_radius));
        points.push_back(circle->m_center + glm::dvec2(0, -circle->m_radius));
    }
    else if (const auto *arc = dynamic_cast<const EntityArc2D *>(&en)) {
        points.push_back(arc->m_center);
        points.push_back(arc->m_from);
        points.push_back(arc->m_to);
        const auto radius = glm::length(arc->m_from - arc->m_center);
        if (radius > 1e-9) {
            const auto c2pi_local = [](double x) {
                while (x < 0)
                    x += 2 * M_PI;
                while (x >= 2 * M_PI)
                    x -= 2 * M_PI;
                return x;
            };
            const auto a0 = c2pi_local(std::atan2(arc->m_from.y - arc->m_center.y, arc->m_from.x - arc->m_center.x));
            const auto a1 = c2pi_local(std::atan2(arc->m_to.y - arc->m_center.y, arc->m_to.x - arc->m_center.x));
            auto dphi = c2pi_local(a1 - a0);
            if (dphi < 1e-2)
                dphi = 2 * M_PI;
            constexpr unsigned int steps = 32;
            for (unsigned int i = 0; i <= steps; i++) {
                const auto a = a0 + dphi * static_cast<double>(i) / static_cast<double>(steps);
                points.push_back(arc->m_center + glm::dvec2(std::cos(a) * radius, std::sin(a) * radius));
            }
        }
    }
    else if (const auto *bezier = dynamic_cast<const EntityBezier2D *>(&en)) {
        constexpr unsigned int steps = 32;
        for (unsigned int i = 0; i <= steps; i++) {
            const auto t = static_cast<double>(i) / static_cast<double>(steps);
            points.push_back(bezier->get_interpolated(t));
        }
    }
    else if (const auto *point = dynamic_cast<const EntityPoint2D *>(&en)) {
        points.push_back(point->m_p);
    }
    else if (const auto *cluster = dynamic_cast<const EntityCluster *>(&en)) {
        const auto bb = cluster->get_bbox();
        points.push_back(bb.first);
        points.push_back({bb.first.x, bb.second.y});
        points.push_back(bb.second);
        points.push_back({bb.second.x, bb.first.y});
    }
}

bool collect_selection_transform_overlay_data(const Document &doc, const std::set<SelectableRef> &selection,
                                              double frame_angle, SelectionTransformOverlayData &out)
{
    std::set<UUID> selected_entities;
    std::set<UUID> selected_constraints;
    for (const auto &sr : selection) {
        if (sr.type == SelectableRef::Type::ENTITY)
            selected_entities.insert(sr.item);
        else if (sr.type == SelectableRef::Type::CONSTRAINT)
            selected_constraints.insert(sr.item);
    }

    if (selected_entities.empty() && !selected_constraints.empty()) {
        for (const auto &cuu : selected_constraints) {
            const auto *constr = doc.get_constraint_ptr(cuu);
            if (!constr)
                continue;
            for (const auto &euu : constr->get_referenced_entities())
                selected_entities.insert(euu);
        }
    }

    if (selected_entities.empty())
        return false;

    std::vector<UUID> entities;
    std::vector<glm::dvec2> points;
    UUID group;
    bool group_set = false;
    UUID workplane;
    bool workplane_set = false;
    for (const auto &uu : selected_entities) {
        const auto *en = doc.get_entity_ptr(uu);
        if (!en)
            continue;
        if (!en->of_type(Entity::Type::LINE_2D, Entity::Type::ARC_2D, Entity::Type::CIRCLE_2D, Entity::Type::BEZIER_2D,
                         Entity::Type::POINT_2D, Entity::Type::CLUSTER))
            continue;
        if (!group_set) {
            group = en->m_group;
            group_set = true;
        }
        else if (group != en->m_group) {
            continue;
        }
        const auto *en_wrkpl = dynamic_cast<const IEntityInWorkplane *>(en);
        if (!en_wrkpl)
            continue;
        if (!workplane_set) {
            workplane = en_wrkpl->get_workplane();
            workplane_set = true;
        }
        else if (workplane != en_wrkpl->get_workplane()) {
            continue;
        }
        entities.push_back(uu);
        append_entity_points_for_transform_bbox(*en, points);
    }

    if (entities.empty() || points.empty() || !workplane_set || !group_set)
        return false;

    std::vector<glm::dvec2> local_points;
    local_points.reserve(points.size());
    for (const auto &p : points)
        local_points.push_back(rotate_point_2d(p, {0, 0}, -frame_angle));

    auto bbox_min_local = local_points.front();
    auto bbox_max_local = local_points.front();
    for (const auto &p : local_points) {
        bbox_min_local = glm::min(bbox_min_local, p);
        bbox_max_local = glm::max(bbox_max_local, p);
    }

    auto size = bbox_max_local - bbox_min_local;
    if (size.x < 1e-6) {
        bbox_min_local.x -= 5;
        bbox_max_local.x += 5;
    }
    if (size.y < 1e-6) {
        bbox_min_local.y -= 5;
        bbox_max_local.y += 5;
    }
    size = bbox_max_local - bbox_min_local;
    const auto pad = std::max(2.0, std::max(size.x, size.y) * 0.08);
    bbox_min_local -= glm::dvec2(pad, pad);
    bbox_max_local += glm::dvec2(pad, pad);
    size = bbox_max_local - bbox_min_local;

    const auto center_local = (bbox_min_local + bbox_max_local) * 0.5;
    const auto center = rotate_point_2d(center_local, {0, 0}, frame_angle);
    const auto rotate_gap = std::max(10.0, std::max(size.x, size.y) * 0.22);
    std::array<glm::dvec2, 4> scale_handles = {
            glm::dvec2{bbox_min_local.x, bbox_min_local.y},
            glm::dvec2{bbox_min_local.x, bbox_max_local.y},
            glm::dvec2{bbox_max_local.x, bbox_max_local.y},
            glm::dvec2{bbox_max_local.x, bbox_min_local.y},
    };
    for (auto &p : scale_handles)
        p = rotate_point_2d(p, {0, 0}, frame_angle);

    const auto rotate_handle = rotate_point_2d({center_local.x, bbox_max_local.y + rotate_gap}, {0, 0}, frame_angle);

    auto bbox_min = scale_handles.front();
    auto bbox_max = scale_handles.front();
    for (const auto &p : scale_handles) {
        bbox_min = glm::min(bbox_min, p);
        bbox_max = glm::max(bbox_max, p);
    }
    bbox_min = glm::min(bbox_min, rotate_handle);
    bbox_max = glm::max(bbox_max, rotate_handle);

    out.group = group;
    out.workplane = workplane;
    out.entities = entities;
    out.bbox_min = bbox_min;
    out.bbox_max = bbox_max;
    out.center = center;
    out.rotate_handle = rotate_handle;
    out.scale_handles = scale_handles;
    return true;
}

void transform_2d_entity(Entity &dst, const Entity &src, const std::function<glm::dvec2(const glm::dvec2 &)> &transform,
                         double uniform_scale, double rotation_angle_rad)
{
    if (auto *line = dynamic_cast<EntityLine2D *>(&dst)) {
        if (const auto *src_line = dynamic_cast<const EntityLine2D *>(&src)) {
            line->m_p1 = transform(src_line->m_p1);
            line->m_p2 = transform(src_line->m_p2);
        }
    }
    else if (auto *arc = dynamic_cast<EntityArc2D *>(&dst)) {
        if (const auto *src_arc = dynamic_cast<const EntityArc2D *>(&src)) {
            arc->m_from = transform(src_arc->m_from);
            arc->m_to = transform(src_arc->m_to);
            arc->m_center = transform(src_arc->m_center);
        }
    }
    else if (auto *circle = dynamic_cast<EntityCircle2D *>(&dst)) {
        if (const auto *src_circle = dynamic_cast<const EntityCircle2D *>(&src)) {
            circle->m_center = transform(src_circle->m_center);
            circle->m_radius = std::max(1e-9, src_circle->m_radius * uniform_scale);
        }
    }
    else if (auto *bezier = dynamic_cast<EntityBezier2D *>(&dst)) {
        if (const auto *src_bezier = dynamic_cast<const EntityBezier2D *>(&src)) {
            bezier->m_p1 = transform(src_bezier->m_p1);
            bezier->m_p2 = transform(src_bezier->m_p2);
            bezier->m_c1 = transform(src_bezier->m_c1);
            bezier->m_c2 = transform(src_bezier->m_c2);
        }
    }
    else if (auto *point = dynamic_cast<EntityPoint2D *>(&dst)) {
        if (const auto *src_point = dynamic_cast<const EntityPoint2D *>(&src))
            point->m_p = transform(src_point->m_p);
    }
    else if (auto *cluster = dynamic_cast<EntityCluster *>(&dst)) {
        if (const auto *src_cluster = dynamic_cast<const EntityCluster *>(&src)) {
            cluster->m_origin = transform(src_cluster->m_origin);
            cluster->m_scale_x = std::max(1e-9, src_cluster->m_scale_x * uniform_scale);
            cluster->m_scale_y = std::max(1e-9, src_cluster->m_scale_y * uniform_scale);
            cluster->m_angle = src_cluster->m_angle + glm::degrees(rotation_angle_rad);
            for (auto &[idx, enp] : cluster->m_anchors) {
                if (!cluster->m_content || !cluster->m_content->m_entities.contains(enp.entity))
                    continue;
                cluster->m_anchors_transformed[idx] = cluster->transform(cluster->get_anchor_point(enp));
            }
        }
    }
}

bool remove_direction_constraints_for_entities(Document &doc, const UUID &group_uu, const std::set<UUID> &entities)
{
    (void)group_uu;
    std::vector<UUID> constraints_to_delete;
    for (const auto &[uu, constr] : doc.m_constraints) {
        if (!constr->of_type(ConstraintType::HORIZONTAL, ConstraintType::VERTICAL, ConstraintType::POINT_DISTANCE_HORIZONTAL,
                             ConstraintType::POINT_DISTANCE_VERTICAL))
            continue;
        bool has_non_workplane_ref = false;
        bool all_non_workplane_inside = true;
        for (const auto &enp : constr->get_referenced_entities_and_points()) {
            const auto *en = doc.get_entity_ptr(enp.entity);
            if (!en)
                continue;
            if (en->of_type(Entity::Type::WORKPLANE))
                continue;
            has_non_workplane_ref = true;
            if (!entities.contains(enp.entity)) {
                all_non_workplane_inside = false;
                break;
            }
        }
        if (!has_non_workplane_ref || !all_non_workplane_inside)
            continue;
        constraints_to_delete.push_back(uu);
    }
    for (const auto &uu : constraints_to_delete)
        doc.m_constraints.erase(uu);
    return !constraints_to_delete.empty();
}

struct HoverPopoverState {
    bool pointer_on_button = false;
    bool pointer_on_popover = false;
    sigc::connection close_timeout;
};

Gtk::Popover *g_active_hover_popover = nullptr;

bool widget_or_descendant_has_focus(Gtk::Widget &widget)
{
    if (widget.has_focus())
        return true;
    auto *focus = widget.get_root() ? widget.get_root()->get_focus() : nullptr;
    while (focus) {
        if (focus == &widget)
            return true;
        focus = focus->get_parent();
    }
    return false;
}

void install_hover_popover(Gtk::Widget &button, Gtk::Popover &popover, std::function<bool()> can_open = {},
                           std::function<bool()> right_click_only = {}, bool keep_open_on_focus = true)
{
    auto state = std::make_shared<HoverPopoverState>();

    auto maybe_close = [state, &button, &popover, keep_open_on_focus] {
        // Keep popover while focused widget is inside opener button or inside the popover.
        if (keep_open_on_focus && (widget_or_descendant_has_focus(button) || widget_or_descendant_has_focus(popover)))
            return;
        if (!state->pointer_on_button && !state->pointer_on_popover && popover.get_visible()) {
            if (g_active_hover_popover == &popover)
                g_active_hover_popover = nullptr;
            popover.popdown();
        }
    };

    auto schedule_close = [state, maybe_close] {
        state->close_timeout.disconnect();
        state->close_timeout = Glib::signal_timeout().connect(
                [maybe_close] {
                    maybe_close();
                    return false;
                },
                120);
    };

    auto button_motion = Gtk::EventControllerMotion::create();
    button_motion->signal_enter().connect([state, &popover, can_open, right_click_only](double, double) {
        state->pointer_on_button = true;
        state->close_timeout.disconnect();
        if (right_click_only && right_click_only())
            return;
        if (can_open && !can_open()) {
            if (g_active_hover_popover && g_active_hover_popover->get_visible()) {
                g_active_hover_popover->popdown();
                g_active_hover_popover = nullptr;
            }
            return;
        }
        if (g_active_hover_popover && g_active_hover_popover != &popover && g_active_hover_popover->get_visible())
            g_active_hover_popover->popdown();
        if (!popover.get_visible())
            popover.popup();
        g_active_hover_popover = &popover;
    });
    button_motion->signal_leave().connect([state, schedule_close] {
        state->pointer_on_button = false;
        schedule_close();
    });
    button.add_controller(button_motion);

    auto button_click = Gtk::GestureClick::create();
    button_click->set_button(3);
    button_click->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    button_click->signal_pressed().connect([state, &popover, can_open, right_click_only, button_click](int, double, double) {
        button_click->set_state(Gtk::EventSequenceState::CLAIMED);
        if (right_click_only && !right_click_only())
            return;
        state->pointer_on_button = true;
        state->close_timeout.disconnect();
        if (can_open && !can_open())
            return;
        if (g_active_hover_popover && g_active_hover_popover != &popover && g_active_hover_popover->get_visible())
            g_active_hover_popover->popdown();
        if (popover.get_visible()) {
            popover.popdown();
            if (g_active_hover_popover == &popover)
                g_active_hover_popover = nullptr;
        }
        else {
            popover.popup();
            g_active_hover_popover = &popover;
        }
    });
    button.add_controller(button_click);

    auto popover_motion = Gtk::EventControllerMotion::create();
    popover_motion->signal_enter().connect([state](double, double) {
        state->pointer_on_popover = true;
        state->close_timeout.disconnect();
    });
    popover_motion->signal_leave().connect([state, schedule_close] {
        state->pointer_on_popover = false;
        schedule_close();
    });
    popover.add_controller(popover_motion);

    popover.signal_hide().connect([state, &popover] {
        if (g_active_hover_popover == &popover)
            g_active_hover_popover = nullptr;
        state->pointer_on_button = false;
        state->pointer_on_popover = false;
        state->close_timeout.disconnect();
    });
}
} // namespace

Editor::CanvasUpdater::CanvasUpdater(Editor &editor) : m_editor(editor)
{
    m_editor.m_canvas_update_pending++;
}

Editor::CanvasUpdater::~CanvasUpdater()
{
    if (m_editor.m_canvas_update_pending == 0)
        return; // should not happen
    m_editor.m_canvas_update_pending--;
    if (m_editor.m_canvas_update_pending == 0)
        m_editor.canvas_update_keep_selection();
}

Editor::Editor(Dune3DAppWindow &win, Preferences &prefs)
    : m_preferences(prefs), m_dialogs(win, *this), m_win(win), m_core(*this), m_selection_menu_creator(m_core)
{
    m_drag_tool = ToolID::NONE;
}

Editor::~Editor() = default;

void Editor::init()
{
    init_workspace_browser();
    init_properties_notebook();
    init_header_bar();
    init_actions();
    init_tool_popover();
    init_canvas();
#ifdef DUNE_SKETCHER_ONLY
    init_radial_menu();
#endif

    m_core.signal_needs_save().connect([this] {
        update_action_sensitivity();
        m_workspace_browser->update_needs_save();
        update_workspace_view_names();
    });
    get_canvas().signal_selection_changed().connect([this] {
        if (sanitize_canvas_selection_if_needed())
            return;
        if (expand_selection_to_closed_loops_if_needed())
            return;
        update_action_sensitivity();
        sync_symmetry_popover_context();
        if (!m_core.tool_is_active())
            apply_symmetry_live_from_popover(false);
        sync_draw_text_popover_from_selection(true);
#ifdef DUNE_SKETCHER_ONLY
        update_gears_quick_popover();
        update_joints_summary();
        update_joints_quick_popover();
        if (m_selection_transform_enabled && !m_selection_transform_drag_active && !m_core.tool_is_active())
            canvas_update_keep_selection();
#endif
    });

    m_win.signal_close_request().connect(
            [this] {
                if (!m_core.get_needs_save_any())
                    return false;

                auto cb = [this] {
                    // here, the close dialog is still there and closing the main window causes a near-segfault
                    // so break out of the current event
                    Glib::signal_idle().connect_once([this] { m_win.close(); });
                };
                close_document(m_core.get_current_idocument_info().get_uuid(), cb, cb);

                return true; // keep window open
            },
            true);


    update_workplane_label();


    m_preferences.signal_changed().connect(sigc::mem_fun(*this, &Editor::apply_preferences));

    m_core.signal_tool_changed().connect(sigc::mem_fun(*this, &Editor::handle_tool_change));


    m_core.signal_documents_changed().connect([this] {
        for (auto doc : m_core.get_documents()) {
            for (auto &[uu, wsv] : m_workspace_views) {
                wsv.m_documents[doc->get_uuid()];
            }
        }
#ifdef DUNE_SKETCHER_ONLY
        m_win.get_workspace_notebook().set_visible(false);
#else
        m_win.get_workspace_notebook().set_visible(m_core.has_documents());
#endif
        CanvasUpdater canvas_updater{*this};
        m_workspace_browser->update_documents(get_current_document_views());
        update_group_editor();
        update_workplane_label();
        update_action_sensitivity();
        m_workspace_browser->set_sensitive(m_core.has_documents());
        m_win.set_welcome_box_visible(!m_core.has_documents());
        update_version_info();
        update_action_bar_buttons_sensitivity();
        update_action_bar_visibility();
        update_selection_editor();
        update_title();
#ifdef DUNE_SKETCHER_ONLY
        if (!m_core.has_documents()) {
            m_sticky_draw_tool = ToolID::NONE;
            set_symmetry_enabled(false, false);
            m_layers_mode_enabled = false;
            m_joints_mode_enabled = false;
            m_cup_template_enabled = false;
            m_active_layer_by_group.clear();
            if (m_joints_quick_popover && m_joints_quick_popover->get_visible())
                m_joints_quick_popover->popdown();
        }
#endif
        rebuild_layers_popover();
        update_sketcher_toolbar_button_states();
    });

    attach_action_button(m_win.get_welcome_open_button(), ActionID::OPEN_DOCUMENT);
    attach_action_button(m_win.get_welcome_new_button(), ActionID::NEW_DOCUMENT);
#ifdef DUNE_SKETCHER_ONLY
    m_win.get_welcome_open_folder_button().signal_clicked().connect(sigc::mem_fun(*this, &Editor::on_open_folder));
#endif

#ifdef DUNE_SKETCHER_ONLY
    m_selection_mode_button = Gtk::make_managed<Gtk::Button>();
    m_selection_mode_button->set_icon_name("edit-select-all-symbolic");
    m_selection_mode_button->set_has_frame(true);
    m_selection_mode_button->add_css_class("sketch-toolbar-button");
    m_selection_mode_button->signal_clicked().connect(sigc::mem_fun(*this, &Editor::activate_selection_mode));
    {
        auto popover = Gtk::make_managed<Gtk::Popover>();
        popover->set_has_arrow(true);
        popover->set_autohide(false);
        popover->add_css_class("sketch-grid-popover");
        popover->set_parent(*m_selection_mode_button);
        popover->set_size_request(sketch_popover_total_width, -1);
        install_hover_popover(*m_selection_mode_button, *popover, [this] { return !m_primary_button_pressed; },
                              [this] { return m_right_click_popovers_only; });
        m_selection_mode_popover = popover;

        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        box->set_margin_start(12);
        box->set_margin_end(12);
        box->set_margin_top(12);
        box->set_margin_bottom(12);
        box->set_size_request(sketch_popover_content_width, -1);
        popover->set_child(*box);

        auto transform_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto transform_label = Gtk::make_managed<Gtk::Label>("Transform");
        transform_label->set_hexpand(true);
        transform_label->set_xalign(0);
        m_selection_transform_switch = Gtk::make_managed<Gtk::Switch>();
        transform_row->append(*transform_label);
        transform_row->append(*m_selection_transform_switch);
        box->append(*transform_row);

        auto markers_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto markers_label = Gtk::make_managed<Gtk::Label>("Show markers");
        markers_label->set_hexpand(true);
        markers_label->set_xalign(0);
        m_selection_markers_switch = Gtk::make_managed<Gtk::Switch>();
        markers_row->append(*markers_label);
        markers_row->append(*m_selection_markers_switch);
        box->append(*markers_row);

        auto snap_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto snap_label = Gtk::make_managed<Gtk::Label>("Snap");
        snap_label->set_hexpand(true);
        snap_label->set_xalign(0);
        m_selection_snap_switch = Gtk::make_managed<Gtk::Switch>();
        snap_row->append(*snap_label);
        snap_row->append(*m_selection_snap_switch);
        box->append(*snap_row);

        auto closed_loop_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto closed_loop_label = Gtk::make_managed<Gtk::Label>("Closed loop");
        closed_loop_label->set_hexpand(true);
        closed_loop_label->set_xalign(0);
        m_selection_closed_loop_switch = Gtk::make_managed<Gtk::Switch>();
        closed_loop_row->append(*closed_loop_label);
        closed_loop_row->append(*m_selection_closed_loop_switch);
        box->append(*closed_loop_row);

        m_selection_transform_switch->property_active().signal_changed().connect([this] {
            if (m_updating_selection_mode_popover)
                return;
            m_selection_transform_enabled = m_selection_transform_switch->get_active();
            if (!m_selection_transform_enabled) {
                end_selection_transform_drag();
            }
            canvas_update_keep_selection();
            update_sketcher_toolbar_button_states();
        });
        m_selection_markers_switch->property_active().signal_changed().connect([this] {
            if (m_updating_selection_mode_popover)
                return;
            m_show_technical_markers = m_selection_markers_switch->get_active();
            if (!m_show_technical_markers)
                end_selection_transform_drag();
            canvas_update_keep_selection();
        });
        m_selection_snap_switch->property_active().signal_changed().connect([this] {
            if (m_updating_selection_mode_popover)
                return;
            m_selection_snap_enabled = m_selection_snap_switch->get_active();
        });
        m_selection_closed_loop_switch->property_active().signal_changed().connect([this] {
            if (m_updating_selection_mode_popover)
                return;
            m_selection_closed_loop_enabled = m_selection_closed_loop_switch->get_active();
            m_closed_loop_previous_selection = get_canvas().get_selection();
        });
    }
    sync_selection_mode_popover();
    m_win.add_action_button(*m_selection_mode_button);
    init_layers_popover();
    init_cup_template_popover();
    init_gears_popover();
    init_joints_popover();
    init_boxes_popover();
#endif

    auto add_sketch_toolbar_divider = [this]() {
        const bool in_header_bar = m_win.get_header_action_box() != nullptr;
        auto divider = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        divider->add_css_class("sketch-toolbar-divider");
        if (in_header_bar) {
            divider->add_css_class("sketch-toolbar-divider-vertical");
            divider->set_size_request(1, 22);
            divider->set_valign(Gtk::Align::CENTER);
            divider->set_margin_start(4);
            divider->set_margin_end(4);
        }
        else {
            divider->add_css_class("sketch-toolbar-divider-horizontal");
            divider->set_size_request(22, 1);
            divider->set_halign(Gtk::Align::CENTER);
            divider->set_margin_top(4);
            divider->set_margin_bottom(4);
        }
        m_win.add_action_button(*divider);
    };

    create_action_bar_button(ToolID::DRAW_CONTOUR);
    create_action_bar_button(ToolID::DRAW_RECTANGLE);
    create_action_bar_button(ToolID::DRAW_CIRCLE_2D);
    create_action_bar_button(ToolID::DRAW_REGULAR_POLYGON);
    add_sketch_toolbar_divider();
    create_action_bar_button(ToolID::DRAW_TEXT);
#ifdef DUNE_SKETCHER_ONLY
    {
        auto button = Gtk::make_managed<Gtk::Button>();
        button->set_icon_name("image-x-generic-symbolic");
        button->set_tooltip_text("Import Picture");
        button->set_has_frame(true);
        button->add_css_class("sketch-toolbar-button");
        button->signal_clicked().connect([this] {
            m_sticky_draw_tool = ToolID::NONE;
            trigger_action(ToolID::IMPORT_PICTURE);
            update_sketcher_toolbar_button_states();
        });
        m_win.add_action_button(*button);
    }
    {
        auto button = Gtk::make_managed<Gtk::Button>();
        button->set_icon_name("applications-graphics-symbolic");
        button->set_tooltip_text("Trace Image");
        button->set_has_frame(true);
        button->add_css_class("sketch-toolbar-button");
        button->signal_clicked().connect(sigc::mem_fun(*this, &Editor::on_trace_image_button));
        m_win.add_action_button(*button);
    }
    if (m_gears_button || m_joints_button || m_boxes_button || m_cup_template_button || m_layers_mode_button)
        add_sketch_toolbar_divider();
    if (m_gears_button)
        m_win.add_action_button(*m_gears_button);
    if (m_joints_button)
        m_win.add_action_button(*m_joints_button);
    if (m_boxes_button)
        m_win.add_action_button(*m_boxes_button);
    if (m_cup_template_button)
        m_win.add_action_button(*m_cup_template_button);
    if (m_layers_mode_button) {
        m_win.add_action_button(*m_layers_mode_button);
    }
#endif

    init_view_options();

    m_selection_filter_window = std::make_unique<SelectionFilterWindow>(m_core);
    m_selection_filter_window->set_transient_for(m_win);
    m_selection_filter_window->set_hide_on_close(true);
#ifdef DUNE_SKETCHER_ONLY
    m_selection_filter_window->set_current_group_only_locked(true);
#endif
    connect_action(ActionID::SELECTION_FILTER, [this](const auto &a) { m_selection_filter_window->present(); });
    get_canvas().set_selection_filter(*m_selection_filter_window);
    m_selection_filter_window->signal_changed().connect(sigc::mem_fun(*this, &Editor::update_view_hints));

    connect_action(ActionID::SELECT_UNDERCONSTRAINED, [this](const auto &a) {
        auto &doc = m_core.get_current_document();
        System sys{doc, m_core.get_current_group()};
        std::set<EntityAndPoint> free_points;
        sys.solve(&free_points);
        std::set<SelectableRef> sel;
        for (const auto &enp : free_points) {
            sel.emplace(SelectableRef::Type::ENTITY, enp.entity, enp.point);
        }
        get_canvas().set_selection(sel, true);
        get_canvas().set_selection_mode(SelectionMode::NORMAL);
    });

    m_win.signal_undo().connect([this] { trigger_action(ActionID::UNDO); });

    update_action_sensitivity();
    reset_key_hint_label();

    m_win.get_workspace_add_button().signal_clicked().connect([this] {
        auto new_wv_uu = create_workspace_view_from_current();
        set_current_workspace_view(new_wv_uu);
    });

    m_win.get_canvas().signal_view_changed().connect([this] {
        if (!m_current_workspace_view)
            return;
        if (m_workspace_view_loading)
            return;
        auto &wv = m_workspace_views.at(m_current_workspace_view);
        auto &ca = m_win.get_canvas();
        wv.m_cam_distance = ca.get_cam_distance();
        wv.m_cam_quat = ca.get_cam_quat();
        wv.m_center = ca.get_center();
        wv.m_projection = ca.get_projection();
    });

    m_win.get_workspace_notebook().signal_switch_page().connect([this](Gtk::Widget *page, guint index) {
        auto &pg = dynamic_cast<WorkspaceViewPage &>(*page);
        set_current_workspace_view(pg.m_uuid);
    });

#ifdef DUNE_SKETCHER_ONLY
    {
        auto controller = Gtk::EventControllerKey::create();
        controller->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
        controller->signal_key_pressed().connect(
                [this, controller](guint keyval, guint keycode, Gdk::ModifierType state) -> bool {
                    auto *focus = m_win.get_focus();
                    const auto mods = state & (Gdk::ModifierType::SHIFT_MASK | Gdk::ModifierType::CONTROL_MASK
                                               | Gdk::ModifierType::ALT_MASK);
                    if (focus && (GTK_IS_EDITABLE(focus->gobj()) || GTK_IS_TEXT_VIEW(focus->gobj()))
                        && (mods & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::ALT_MASK))
                                   == static_cast<Gdk::ModifierType>(0)) {
                        return false;
                    }
                    return handle_action_key(controller, keyval, keycode, state);
                },
                true);
        m_win.add_controller(controller);
    }
#endif

    apply_preferences();
#ifdef DUNE_SKETCHER_ONLY
    update_sketcher_toolbar_button_states();
    m_win.set_welcome_box_visible(!m_core.has_documents());
#endif
}

void Editor::add_tool_action(ActionToolID id, const std::string &action)
{
    m_win.add_action(action, [this, id] { trigger_action(id); });
}

void Editor::init_view_options()
{
    auto view_options_popover = Gtk::make_managed<Gtk::PopoverMenu>();
    m_win.get_view_options_button().set_popover(*view_options_popover);
    {
        Gdk::Rectangle rect;
        rect.set_width(32);
        m_win.get_view_options_button().get_popover()->set_pointing_to(rect);
    }

    m_view_options_menu = Gio::Menu::create();
    m_perspective_action = m_win.add_action_bool("perspective", false);
    m_perspective_action->signal_change_state().connect([this](const Glib::VariantBase &v) {
        auto b = Glib::VariantBase::cast_dynamic<Glib::Variant<bool>>(v).get();
        set_perspective_projection(b);
    });
    m_previous_construction_entities_action = m_win.add_action_bool("previous_construction", false);
    m_previous_construction_entities_action->signal_change_state().connect([this](const Glib::VariantBase &v) {
        auto b = Glib::VariantBase::cast_dynamic<Glib::Variant<bool>>(v).get();
        set_show_previous_construction_entities(b);
    });
    m_hide_irrelevant_workplanes_action = m_win.add_action_bool("irrelevant_workplanes", false);
    m_hide_irrelevant_workplanes_action->signal_change_state().connect([this](const Glib::VariantBase &v) {
        auto b = Glib::VariantBase::cast_dynamic<Glib::Variant<bool>>(v).get();
        set_hide_irrelevant_workplanes(b);
    });

    add_tool_action(ActionID::SELECTION_FILTER, "selection_filter");

    m_view_options_menu->append("Selection filter", "win.selection_filter");
#ifndef DUNE_SKETCHER_ONLY
    m_view_options_menu->append("Previous construction entities", "win.previous_construction");
    m_view_options_menu->append("Hide irrelevant workplanes", "win.irrelevant_workplanes");
    m_view_options_menu->append("Perspective projection", "win.perspective");
#endif
    {
        auto it = Gio::MenuItem::create("scale", "scale");
        it->set_attribute_value("custom", Glib::Variant<Glib::ustring>::create("scale"));

        m_view_options_menu->append_item(it);
    }

    view_options_popover->set_menu_model(m_view_options_menu);

    {
        auto adj = Gtk::Adjustment::create(-1, -1, 5, .01, .1);
        m_curvature_comb_scale = Gtk::make_managed<Gtk::Scale>(adj);
        m_curvature_comb_scale->add_mark(adj->get_lower(), Gtk::PositionType::BOTTOM, "Off");

        adj->signal_value_changed().connect([this, adj] {
            if (!m_core.has_documents())
                return;
            float scale = 0;
            auto val = adj->get_value();
            if (val > adj->get_lower())
                scale = powf(10, val);
            auto &wv = m_workspace_views.at(m_current_workspace_view);
            wv.m_curvature_comb_scale = scale;
            CanvasUpdater canvas_updater{*this};
            update_view_hints();
        });

        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
        box->set_margin_start(32);
        box->set_margin_top(6);
        auto label = Gtk::make_managed<Gtk::Label>("Curvature comb scale");
        label->set_xalign(0);
        box->append(*label);
        box->append(*m_curvature_comb_scale);

        view_options_popover->add_child(*box, "scale");
    }
}

Gtk::Button &Editor::create_action_bar_button(ActionToolID action)
{
    static const std::map<ActionToolID, std::string> action_icons = {
            {ToolID::DRAW_CONTOUR, "action-draw-contour-symbolic"},
            {ToolID::DRAW_CIRCLE_2D, "action-draw-line-circle-symbolic"},
            {ToolID::DRAW_RECTANGLE, "action-draw-line-rectangle-symbolic"},
            {ToolID::DRAW_REGULAR_POLYGON, "action-draw-line-regular-polygon-symbolic"},
            {ToolID::DRAW_TEXT, "action-draw-text-symbolic"},
            {ActionID::EXPORT_PATHS, "document-save-as-symbolic"},
            {ActionID::EXPORT_DXF_CURRENT_GROUP, "document-save-symbolic"},
    };
    auto bu = Gtk::make_managed<Gtk::Button>();
    auto img = Gtk::make_managed<Gtk::Image>();
    if (action_icons.count(action))
        img->set_from_icon_name(action_icons.at(action));
    else
        img->set_from_icon_name("face-worried-symbolic");
    img->set_icon_size(Gtk::IconSize::NORMAL);
    bu->set_child(*img);
    Gtk::Popover *tool_popover = nullptr;
#ifdef DUNE_SKETCHER_ONLY
    bu->set_has_frame(true);
    bu->add_css_class("sketch-toolbar-button");
    if (std::holds_alternative<ToolID>(action) && std::get<ToolID>(action) == ToolID::DRAW_REGULAR_POLYGON) {
        tool_popover = Gtk::make_managed<Gtk::Popover>();
        tool_popover->set_has_arrow(true);
        tool_popover->set_autohide(false);
        tool_popover->add_css_class("sketch-grid-popover");
        tool_popover->set_parent(*bu);
        tool_popover->set_size_request(sketch_popover_total_width, -1);

        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        box->set_margin_start(12);
        box->set_margin_end(12);
        box->set_margin_top(12);
        box->set_margin_bottom(12);
        box->set_size_request(sketch_popover_content_width, -1);
        tool_popover->set_child(*box);

        auto sides_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto sides_label = Gtk::make_managed<Gtk::Label>("Sides");
        sides_label->set_hexpand(true);
        sides_label->set_xalign(0);
        auto sides_spin = Gtk::make_managed<Gtk::SpinButton>();
        sides_spin->set_range(3, 64);
        sides_spin->set_increments(1, 1);
        sides_spin->set_digits(0);
        sides_spin->set_numeric(true);
        sides_spin->set_width_chars(2);
        sides_spin->set_value(ToolDrawRegularPolygon::get_default_sides());
        sides_row->append(*sides_label);
        sides_row->append(*sides_spin);
        box->append(*sides_row);

        auto rounded_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto rounded_label = Gtk::make_managed<Gtk::Label>("Rounded");
        rounded_label->set_hexpand(true);
        rounded_label->set_xalign(0);
        auto rounded_switch = Gtk::make_managed<Gtk::Switch>();
        rounded_switch->set_active(ToolDrawRegularPolygon::get_default_rounded());
        rounded_row->append(*rounded_label);
        rounded_row->append(*rounded_switch);
        box->append(*rounded_row);

        auto radius_revealer = Gtk::make_managed<Gtk::Revealer>();
        radius_revealer->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
        radius_revealer->set_transition_duration(120);
        auto radius_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto radius_label = Gtk::make_managed<Gtk::Label>("Radius");
        radius_label->set_hexpand(true);
        radius_label->set_xalign(0);
        auto radius_spin = Gtk::make_managed<Gtk::SpinButton>();
        radius_spin->set_range(0.0, 9999.99);
        radius_spin->set_increments(0.1, 1.0);
        radius_spin->set_digits(2);
        radius_spin->set_numeric(true);
        radius_spin->set_width_chars(5);
        radius_spin->set_value(ToolDrawRegularPolygon::get_default_round_radius());
        auto mm_label = Gtk::make_managed<Gtk::Label>("mm");
        mm_label->add_css_class("dim-label");
        radius_row->append(*radius_label);
        radius_row->append(*radius_spin);
        radius_row->append(*mm_label);
        radius_revealer->set_child(*radius_row);
        radius_revealer->set_reveal_child(rounded_switch->get_active());
        box->append(*radius_revealer);

        sides_spin->signal_value_changed().connect([sides_spin] {
            ToolDrawRegularPolygon::set_default_sides(static_cast<unsigned int>(sides_spin->get_value()));
        });
        rounded_switch->property_active().signal_changed().connect([rounded_switch, radius_revealer] {
            const bool active = rounded_switch->get_active();
            ToolDrawRegularPolygon::set_default_rounded(active);
            radius_revealer->set_reveal_child(active);
        });
        radius_spin->signal_value_changed().connect(
                [radius_spin] { ToolDrawRegularPolygon::set_default_round_radius(radius_spin->get_value()); });
    }
    else if (std::holds_alternative<ToolID>(action) && std::get<ToolID>(action) == ToolID::DRAW_CIRCLE_2D) {
        tool_popover = Gtk::make_managed<Gtk::Popover>();
        tool_popover->set_has_arrow(true);
        tool_popover->set_autohide(false);
        tool_popover->add_css_class("sketch-grid-popover");
        tool_popover->set_parent(*bu);
        tool_popover->set_size_request(sketch_popover_total_width, -1);

        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        box->set_margin_start(12);
        box->set_margin_end(12);
        box->set_margin_top(12);
        box->set_margin_bottom(12);
        box->set_size_request(sketch_popover_content_width, -1);
        tool_popover->set_child(*box);

        auto oval_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto oval_label = Gtk::make_managed<Gtk::Label>("Oval");
        oval_label->set_hexpand(true);
        oval_label->set_xalign(0);
        auto oval_switch = Gtk::make_managed<Gtk::Switch>();
        oval_switch->set_active(ToolDrawCircle2D::get_default_oval_mode());
        oval_row->append(*oval_label);
        oval_row->append(*oval_switch);
        box->append(*oval_row);

        oval_switch->property_active().signal_changed().connect(
                [oval_switch] { ToolDrawCircle2D::set_default_oval_mode(oval_switch->get_active()); });

        auto slice_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto slice_label = Gtk::make_managed<Gtk::Label>("Slice");
        slice_label->set_hexpand(true);
        slice_label->set_xalign(0);
        auto slice_switch = Gtk::make_managed<Gtk::Switch>();
        const auto default_span = ToolDrawCircle2D::get_default_span_degrees();
        slice_switch->set_active(default_span < 359.999);
        slice_row->append(*slice_label);
        slice_row->append(*slice_switch);
        box->append(*slice_row);

        auto angle_revealer = Gtk::make_managed<Gtk::Revealer>();
        angle_revealer->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
        angle_revealer->set_transition_duration(120);

        auto angle_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto angle_label = Gtk::make_managed<Gtk::Label>("Angle");
        angle_label->set_hexpand(true);
        angle_label->set_xalign(0);
        auto angle_spin = Gtk::make_managed<Gtk::SpinButton>();
        angle_spin->set_range(1.0, 359.0);
        angle_spin->set_increments(1.0, 15.0);
        angle_spin->set_digits(0);
        angle_spin->set_numeric(true);
        angle_spin->set_width_chars(3);
        angle_spin->set_value(std::clamp(default_span, 1.0, 359.0));
        auto deg_label = Gtk::make_managed<Gtk::Label>("deg");
        deg_label->add_css_class("dim-label");
        angle_row->append(*angle_label);
        angle_row->append(*angle_spin);
        angle_row->append(*deg_label);
        angle_revealer->set_child(*angle_row);
        angle_revealer->set_reveal_child(slice_switch->get_active());
        box->append(*angle_revealer);

        slice_switch->property_active().signal_changed().connect([slice_switch, angle_spin, angle_revealer] {
            if (slice_switch->get_active()) {
                auto span = ToolDrawCircle2D::get_default_span_degrees();
                if (span >= 359.999)
                    span = 180.0;
                ToolDrawCircle2D::set_default_span_degrees(span);
                angle_spin->set_value(std::clamp(span, 1.0, 359.0));
                angle_revealer->set_reveal_child(true);
            }
            else {
                ToolDrawCircle2D::set_default_span_degrees(360.0);
                angle_revealer->set_reveal_child(false);
            }
        });
        angle_spin->signal_value_changed().connect([angle_spin, slice_switch] {
            if (!slice_switch->get_active())
                return;
            ToolDrawCircle2D::set_default_span_degrees(angle_spin->get_value());
        });
    }
    else if (std::holds_alternative<ToolID>(action) && std::get<ToolID>(action) == ToolID::DRAW_TEXT) {
        tool_popover = Gtk::make_managed<Gtk::Popover>();
        tool_popover->set_has_arrow(true);
        tool_popover->set_autohide(false);
        tool_popover->add_css_class("sketch-grid-popover");
        tool_popover->set_parent(*bu);
        tool_popover->set_size_request(sketch_popover_total_width, -1);

        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        box->set_margin_start(12);
        box->set_margin_end(12);
        box->set_margin_top(12);
        box->set_margin_bottom(12);
        box->set_size_request(sketch_popover_content_width, -1);
        tool_popover->set_child(*box);
        m_draw_text_popover = tool_popover;

        m_draw_text_font_dialog = Gtk::FontDialog::create();
        m_draw_text_font_dialog->set_modal(true);
        m_draw_text_font_desc = Pango::FontDescription(ToolDrawText::get_default_font());
        m_draw_text_font_features = ToolDrawText::get_default_font_features();

        auto font_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto font_label = Gtk::make_managed<Gtk::Label>("Font");
        font_label->set_hexpand(true);
        font_label->set_xalign(0);
        m_draw_text_font_button = Gtk::make_managed<Gtk::Button>();
        m_draw_text_font_button->set_has_frame(true);
        m_draw_text_font_button->set_hexpand(true);
        m_draw_text_font_button->set_halign(Gtk::Align::END);
        font_row->append(*font_label);
        font_row->append(*m_draw_text_font_button);
        box->append(*font_row);

        auto bold_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto bold_label = Gtk::make_managed<Gtk::Label>("Bold");
        bold_label->set_hexpand(true);
        bold_label->set_xalign(0);
        m_draw_text_bold_switch = Gtk::make_managed<Gtk::Switch>();
        bold_row->append(*bold_label);
        bold_row->append(*m_draw_text_bold_switch);
        box->append(*bold_row);

        auto italic_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto italic_label = Gtk::make_managed<Gtk::Label>("Italic");
        italic_label->set_hexpand(true);
        italic_label->set_xalign(0);
        m_draw_text_italic_switch = Gtk::make_managed<Gtk::Switch>();
        italic_row->append(*italic_label);
        italic_row->append(*m_draw_text_italic_switch);
        box->append(*italic_row);

        sync_draw_text_popover_from_font_desc();
        apply_draw_text_popover_change(false);

        m_draw_text_font_button->signal_clicked().connect([this] {
            if (!m_draw_text_font_dialog || !m_draw_text_popover)
                return;
            m_draw_text_popover->popdown();
            m_draw_text_font_dialog->choose_font_and_features(
                    m_win,
                    [this](const Glib::RefPtr<Gio::AsyncResult> &result) {
                        try {
                            auto [desc, features, language] = m_draw_text_font_dialog->choose_font_and_features_finish(result);
                            (void)language;
                            m_draw_text_font_desc = desc;
                            m_draw_text_font_features = features;
                            sync_draw_text_popover_from_font_desc();
                            apply_draw_text_popover_change(true);
                        }
                        catch (const Glib::Error &) {
                        }
                        if (m_draw_text_popover) {
                            if (g_active_hover_popover && g_active_hover_popover != m_draw_text_popover
                                && g_active_hover_popover->get_visible()) {
                                g_active_hover_popover->popdown();
                            }
                            m_draw_text_popover->popup();
                            g_active_hover_popover = m_draw_text_popover;
                        }
                    },
                    m_draw_text_font_desc);
        });

        m_draw_text_bold_switch->property_active().signal_changed().connect([this] {
            if (m_updating_draw_text_popover)
                return;
            auto desc = m_draw_text_font_desc;
            desc.set_weight(m_draw_text_bold_switch->get_active() ? Pango::Weight::BOLD : Pango::Weight::NORMAL);
            m_draw_text_font_desc = desc;
            sync_draw_text_popover_from_font_desc();
            apply_draw_text_popover_change(true);
        });
        m_draw_text_italic_switch->property_active().signal_changed().connect([this] {
            if (m_updating_draw_text_popover)
                return;
            auto desc = m_draw_text_font_desc;
            desc.set_style(m_draw_text_italic_switch->get_active() ? Pango::Style::ITALIC : Pango::Style::NORMAL);
            m_draw_text_font_desc = desc;
            sync_draw_text_popover_from_font_desc();
            apply_draw_text_popover_change(true);
        });
    }
    else if (std::holds_alternative<ToolID>(action) && std::get<ToolID>(action) == ToolID::DRAW_RECTANGLE) {
        tool_popover = Gtk::make_managed<Gtk::Popover>();
        tool_popover->set_has_arrow(true);
        tool_popover->set_autohide(false);
        tool_popover->add_css_class("sketch-grid-popover");
        tool_popover->set_parent(*bu);
        tool_popover->set_size_request(sketch_popover_total_width, -1);

        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        box->set_margin_start(12);
        box->set_margin_end(12);
        box->set_margin_top(12);
        box->set_margin_bottom(12);
        box->set_size_request(sketch_popover_content_width, -1);
        tool_popover->set_child(*box);

        auto square_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto square_label = Gtk::make_managed<Gtk::Label>("Square");
        square_label->set_hexpand(true);
        square_label->set_xalign(0);
        auto square_switch = Gtk::make_managed<Gtk::Switch>();
        square_switch->set_active(ToolDrawRectangle::get_default_square());
        square_row->append(*square_label);
        square_row->append(*square_switch);
        box->append(*square_row);

        auto rounded_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto rounded_label = Gtk::make_managed<Gtk::Label>("Rounded");
        rounded_label->set_hexpand(true);
        rounded_label->set_xalign(0);
        auto rounded_switch = Gtk::make_managed<Gtk::Switch>();
        rounded_switch->set_active(ToolDrawRectangle::get_default_rounded());
        rounded_row->append(*rounded_label);
        rounded_row->append(*rounded_switch);
        box->append(*rounded_row);

        auto radius_revealer = Gtk::make_managed<Gtk::Revealer>();
        radius_revealer->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
        radius_revealer->set_transition_duration(120);
        auto radius_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto radius_label = Gtk::make_managed<Gtk::Label>("Radius");
        radius_label->set_hexpand(true);
        radius_label->set_xalign(0);
        auto radius_spin = Gtk::make_managed<Gtk::SpinButton>();
        radius_spin->set_range(0.0, 9999.99);
        radius_spin->set_increments(0.1, 1.0);
        radius_spin->set_digits(2);
        radius_spin->set_numeric(true);
        radius_spin->set_width_chars(5);
        radius_spin->set_value(ToolDrawRectangle::get_default_round_radius());
        auto mm_label = Gtk::make_managed<Gtk::Label>("mm");
        mm_label->add_css_class("dim-label");
        radius_row->append(*radius_label);
        radius_row->append(*radius_spin);
        radius_row->append(*mm_label);
        radius_revealer->set_child(*radius_row);
        radius_revealer->set_reveal_child(rounded_switch->get_active());
        box->append(*radius_revealer);

        square_switch->property_active().signal_changed().connect(
                [square_switch] { ToolDrawRectangle::set_default_square(square_switch->get_active()); });
        rounded_switch->property_active().signal_changed().connect([rounded_switch, radius_revealer] {
            const bool active = rounded_switch->get_active();
            ToolDrawRectangle::set_default_rounded(active);
            radius_revealer->set_reveal_child(active);
        });
        radius_spin->signal_value_changed().connect(
                [radius_spin] { ToolDrawRectangle::set_default_round_radius(radius_spin->get_value()); });
    }
    if (tool_popover) {
        install_hover_popover(*bu, *tool_popover, [this] { return !m_primary_button_pressed; },
                              [this] { return m_right_click_popovers_only; });
    }
#else
    bu->add_css_class("osd");
    bu->add_css_class("action-button");
#endif
    bu->signal_clicked().connect([this, action, tool_popover] {
#ifdef DUNE_SKETCHER_ONLY
        const auto as_tool = std::get_if<ToolID>(&action);
        const bool should_keep_sticky = as_tool && is_sticky_draw_tool(*as_tool);
        if (!force_end_tool())
            return;
        if (!trigger_action(action))
            return;
        if (should_keep_sticky) {
            m_sticky_draw_tool = *as_tool;
        }
        else if (as_tool) {
            m_sticky_draw_tool = ToolID::NONE;
        }
        if (tool_popover && !m_right_click_popovers_only)
            tool_popover->popup();
        update_sketcher_toolbar_button_states();
#else
        if (force_end_tool())
            trigger_action(action);
        if (tool_popover)
            tool_popover->popup();
#endif
    });
    m_win.add_action_button(*bu);
    m_action_bar_buttons.emplace(action, bu);
    return *bu;
}

bool Editor::is_sticky_draw_tool(ToolID id) const
{
#ifdef DUNE_SKETCHER_ONLY
    return id == ToolID::DRAW_CONTOUR || id == ToolID::DRAW_CIRCLE_2D || id == ToolID::DRAW_RECTANGLE
           || id == ToolID::DRAW_REGULAR_POLYGON;
#else
    (void)id;
    return false;
#endif
}

bool Editor::is_middle_toggle_draw_tool(ToolID id) const
{
#ifdef DUNE_SKETCHER_ONLY
    switch (id) {
    case ToolID::DRAW_CONTOUR:
    case ToolID::DRAW_CONTOUR_FROM_POINT:
    case ToolID::DRAW_LINE_2D:
    case ToolID::DRAW_ARC_2D:
    case ToolID::DRAW_BEZIER_2D:
    case ToolID::DRAW_POINT_2D:
    case ToolID::DRAW_CIRCLE_2D:
    case ToolID::DRAW_REGULAR_POLYGON:
    case ToolID::DRAW_RECTANGLE:
    case ToolID::DRAW_TEXT:
        return true;
    default:
        return false;
    }
#else
    (void)id;
    return false;
#endif
}

void Editor::remember_last_draw_tool(ToolID id)
{
#ifdef DUNE_SKETCHER_ONLY
    if (is_middle_toggle_draw_tool(id))
        m_last_middle_toggle_draw_tool = id;
#else
    (void)id;
#endif
}

void Editor::toggle_selection_mode_or_last_draw_tool()
{
#ifdef DUNE_SKETCHER_ONLY
    const auto active_tool = m_core.get_tool_id();
    if (m_core.tool_is_active() || m_sticky_draw_tool != ToolID::NONE) {
        if (is_middle_toggle_draw_tool(active_tool))
            remember_last_draw_tool(active_tool);
        else if (is_middle_toggle_draw_tool(m_sticky_draw_tool))
            remember_last_draw_tool(m_sticky_draw_tool);
        activate_selection_mode();
        return;
    }

    if (m_last_middle_toggle_draw_tool != ToolID::NONE && m_core.has_documents()) {
        const auto restore_tool = m_last_middle_toggle_draw_tool;
        if (!force_end_tool())
            return;
        if (!trigger_action(restore_tool))
            return;
        m_sticky_draw_tool = is_sticky_draw_tool(restore_tool) ? restore_tool : ToolID::NONE;
        update_sketcher_toolbar_button_states();
        return;
    }

    activate_selection_mode();
#endif
}

std::optional<UUID> Editor::get_single_selected_text_entity()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_core.has_documents())
        return {};
    std::optional<UUID> text_uu;
    const auto &doc = m_core.get_current_document();
    for (const auto &sr : get_canvas().get_selection()) {
        if (sr.type != SelectableRef::Type::ENTITY)
            return {};
        const auto *en = doc.get_entity_ptr(sr.item);
        if (!en || !en->of_type(Entity::Type::TEXT))
            return {};
        if (text_uu && *text_uu != sr.item)
            return {};
        text_uu = sr.item;
    }
    return text_uu;
#else
    return {};
#endif
}

void Editor::sync_draw_text_popover_from_font_desc()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_draw_text_font_button || !m_draw_text_bold_switch || !m_draw_text_italic_switch)
        return;
    m_updating_draw_text_popover = true;
    m_draw_text_font_button->set_label(format_text_font_label(m_draw_text_font_desc));
    m_draw_text_font_button->set_tooltip_text(m_draw_text_font_desc.to_string());
    m_draw_text_bold_switch->set_active(m_draw_text_font_desc.get_weight() >= Pango::Weight::BOLD);
    const auto style = m_draw_text_font_desc.get_style();
    m_draw_text_italic_switch->set_active(style == Pango::Style::ITALIC || style == Pango::Style::OBLIQUE);
    m_updating_draw_text_popover = false;
#endif
}

void Editor::sync_draw_text_popover_from_selection(bool show_popover)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_draw_text_popover || !m_draw_text_font_button)
        return;
    auto text_uu = get_single_selected_text_entity();
    if (!text_uu)
        return;
    auto *en = m_core.get_current_document().get_entity_ptr(*text_uu);
    if (!en || !en->of_type(Entity::Type::TEXT))
        return;
    auto &text = static_cast<EntityText &>(*en);
    m_draw_text_font_desc = Pango::FontDescription(text.m_font);
    m_draw_text_font_features = text.m_font_features;
    sync_draw_text_popover_from_font_desc();
    apply_draw_text_popover_change(false);
    if (!show_popover || m_primary_button_pressed)
        return;
    if (g_active_hover_popover && g_active_hover_popover != m_draw_text_popover && g_active_hover_popover->get_visible())
        g_active_hover_popover->popdown();
    m_draw_text_popover->popup();
    g_active_hover_popover = m_draw_text_popover;
#else
    (void)show_popover;
#endif
}

void Editor::apply_draw_text_popover_change(bool apply_to_selected_text)
{
#ifdef DUNE_SKETCHER_ONLY
    ToolDrawText::set_default_font(m_draw_text_font_desc.to_string());
    ToolDrawText::set_default_font_features(m_draw_text_font_features);
    if (!apply_to_selected_text || !m_core.has_documents())
        return;
    auto text_uu = get_single_selected_text_entity();
    if (!text_uu)
        return;
    auto *en = m_core.get_current_document().get_entity_ptr(*text_uu);
    if (!en || !en->of_type(Entity::Type::TEXT))
        return;
    auto &text = static_cast<EntityText &>(*en);
    text.m_font = m_draw_text_font_desc.to_string();
    text.m_font_features = m_draw_text_font_features;
    render_text(text, get_pango_context(), m_core.get_current_document());
    m_core.set_needs_save();
    m_core.rebuild("text style updated");
    canvas_update_keep_selection();
#else
    (void)apply_to_selected_text;
#endif
}

void Editor::sync_selection_mode_popover()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_selection_mode_button)
        return;
    m_selection_mode_button->set_tooltip_text("Selection tool (Middle mouse)");
    if (!m_selection_transform_switch || !m_selection_markers_switch || !m_selection_snap_switch
        || !m_selection_closed_loop_switch)
        return;
    m_updating_selection_mode_popover = true;
    m_selection_transform_switch->set_active(m_selection_transform_enabled);
    m_selection_markers_switch->set_active(m_show_technical_markers);
    m_selection_snap_switch->set_active(m_selection_snap_enabled);
    m_selection_closed_loop_switch->set_active(m_selection_closed_loop_enabled);
    m_updating_selection_mode_popover = false;
#endif
}

bool Editor::expand_selection_to_closed_loops_if_needed()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_core.has_documents())
        return false;
    const auto selection = get_canvas().get_selection();
    if (m_applying_closed_loop_selection) {
        m_closed_loop_previous_selection = selection;
        return false;
    }
    if (!m_selection_closed_loop_enabled) {
        m_closed_loop_previous_selection = selection;
        return false;
    }
    if (m_core.tool_is_active()) {
        m_closed_loop_previous_selection = selection;
        return false;
    }
    if (selection.empty()) {
        m_closed_loop_previous_selection.clear();
        return false;
    }

    const auto &doc = m_core.get_current_document();
    const auto group_uu = m_core.get_current_group();
    std::set<SelectableRef> added_selection;
    for (const auto &sr : selection) {
        if (!m_closed_loop_previous_selection.contains(sr))
            added_selection.insert(sr);
    }
    m_closed_loop_previous_selection = selection;
    if (added_selection.empty())
        return false;

    std::set<SelectableRef> expanded = selection;
    std::set<UUID> processed_entities;
    bool changed = false;
    for (const auto &sr : entities_from_selection(added_selection)) {
        if (sr.type != SelectableRef::Type::ENTITY || sr.point != 0)
            continue;
        if (processed_entities.contains(sr.item))
            continue;
        const auto *en = doc.get_entity_ptr(sr.item);
        if (!en || en->m_group != group_uu)
            continue;

        if (en->get_type() == Entity::Type::CIRCLE_2D) {
            processed_entities.insert(sr.item);
            changed |= expanded.emplace(SelectableRef::Type::ENTITY, sr.item, 0).second;
            continue;
        }

        UUID wrkpl_uu;
        glm::dvec2 p1, p2;
        if (!get_loop_edge_endpoints(*en, wrkpl_uu, p1, p2))
            continue;

        std::set<UUID> loop_entities;
        if (!collect_closed_loop_component(doc, group_uu, wrkpl_uu, sr.item, loop_entities)) {
            processed_entities.insert(sr.item);
            continue;
        }

        for (const auto &uu : loop_entities) {
            processed_entities.insert(uu);
            changed |= expanded.emplace(SelectableRef::Type::ENTITY, uu, 0).second;
        }
    }

    if (!changed)
        return false;

    m_closed_loop_previous_selection = expanded;
    m_applying_closed_loop_selection = true;
    get_canvas().set_selection(expanded, true);
    m_applying_closed_loop_selection = false;
    return true;
#else
    return false;
#endif
}

bool Editor::sanitize_canvas_selection_if_needed()
{
#ifdef DUNE_SKETCHER_ONLY
    if (m_sanitizing_selection || !m_core.has_documents())
        return false;

    const auto selection = get_canvas().get_selection();
    if (selection.empty())
        return false;

    const auto &doc = m_core.get_current_document();
    std::set<SelectableRef> sanitized;
    bool changed = false;
    for (const auto &sr : selection) {
        bool keep = true;
        switch (sr.type) {
        case SelectableRef::Type::ENTITY:
            {
                const auto *en = doc.get_entity_ptr(sr.item);
                keep = en && (sr.point == 0 || en->is_valid_point(sr.point));
            }
            break;
        case SelectableRef::Type::CONSTRAINT:
            keep = doc.get_constraint_ptr(sr.item) != nullptr;
            break;
        case SelectableRef::Type::SOLID_MODEL_EDGE:
        case SelectableRef::Type::DOCUMENT:
        default:
            keep = true;
            break;
        }
        if (keep)
            sanitized.insert(sr);
        else
            changed = true;
    }

    if (!changed)
        return false;

    m_sanitizing_selection = true;
    get_canvas().set_selection(sanitized, true);
    m_sanitizing_selection = false;
    return true;
#else
    return false;
#endif
}

void Editor::init_layers_popover()
{
#ifdef DUNE_SKETCHER_ONLY
    m_layers_mode_button = Gtk::make_managed<Gtk::Button>();
    m_layers_mode_button->set_icon_name("view-list-bullet-symbolic");
    m_layers_mode_button->set_tooltip_text("Layers");
    m_layers_mode_button->set_has_frame(true);
    m_layers_mode_button->add_css_class("sketch-toolbar-button");

    m_layers_popover = Gtk::make_managed<Gtk::Popover>();
    m_layers_popover->set_has_arrow(true);
    m_layers_popover->set_autohide(false);
    m_layers_popover->add_css_class("sketch-grid-popover");
    m_layers_popover->set_parent(*m_layers_mode_button);
    m_layers_popover->set_size_request(sketch_popover_total_width, -1);
    install_hover_popover(*m_layers_mode_button, *m_layers_popover, [this] { return !m_primary_button_pressed; },
                          [this] { return m_right_click_popovers_only; });
    m_layers_popover->signal_show().connect([this] { rebuild_layers_popover(); });

    auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    root->set_margin_start(12);
    root->set_margin_end(12);
    root->set_margin_top(12);
    root->set_margin_bottom(12);
    root->set_size_request(sketch_popover_content_width, -1);
    m_layers_popover->set_child(*root);

    m_layers_list_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    root->append(*m_layers_list_box);

    auto controls_root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    controls_root->set_margin_top(4);
    root->append(*controls_root);

    auto add_remove_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    add_remove_row->add_css_class("linked");
    add_remove_row->set_hexpand(true);
    controls_root->append(*add_remove_row);

    auto add_button = Gtk::make_managed<Gtk::Button>();
    add_button->set_icon_name("list-add-symbolic");
    add_button->set_tooltip_text("Add layer");
    add_button->set_has_frame(true);
    add_button->set_focusable(false);
    add_button->set_hexpand(true);
    add_button->set_halign(Gtk::Align::FILL);
    add_remove_row->append(*add_button);

    auto remove_button = Gtk::make_managed<Gtk::Button>();
    remove_button->set_icon_name("list-remove-symbolic");
    remove_button->set_tooltip_text("Remove layer");
    remove_button->set_has_frame(true);
    remove_button->set_focusable(false);
    remove_button->set_hexpand(true);
    remove_button->set_halign(Gtk::Align::FILL);
    add_remove_row->append(*remove_button);

    auto reorder_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    reorder_row->add_css_class("linked");
    reorder_row->set_hexpand(true);
    controls_root->append(*reorder_row);

    auto move_up_button = Gtk::make_managed<Gtk::Button>();
    move_up_button->set_icon_name("go-up-symbolic");
    move_up_button->set_tooltip_text("Move layer up");
    move_up_button->set_has_frame(true);
    move_up_button->set_focusable(false);
    move_up_button->set_hexpand(true);
    move_up_button->set_halign(Gtk::Align::FILL);
    reorder_row->append(*move_up_button);

    auto move_down_button = Gtk::make_managed<Gtk::Button>();
    move_down_button->set_icon_name("go-down-symbolic");
    move_down_button->set_tooltip_text("Move layer down");
    move_down_button->set_has_frame(true);
    move_down_button->set_focusable(false);
    move_down_button->set_hexpand(true);
    move_down_button->set_halign(Gtk::Align::FILL);
    reorder_row->append(*move_down_button);

    auto commit_layer_change = [this](const std::string &message) {
        m_core.set_needs_save();
        m_core.rebuild(message);
        canvas_update_keep_selection();
        update_action_sensitivity();
        rebuild_layers_popover();
        refresh_layer_edit_popover();
    };

    add_button->signal_clicked().connect([this, commit_layer_change] {
        if (!m_core.has_documents())
            return;
        if (m_core.tool_is_active() && !force_end_tool())
            return;
        auto &group = m_core.get_current_document().get_group(m_core.get_current_group());
        auto *sketch = dynamic_cast<GroupSketch *>(&group);
        if (!sketch)
            return;
        const auto new_layer = sketch->add_layer();
        select_active_layer_for_current_group(new_layer);
        commit_layer_change("add sketch layer");
    });

    remove_button->signal_clicked().connect([this, commit_layer_change] {
        if (!m_core.has_documents())
            return;
        if (m_core.tool_is_active() && !force_end_tool())
            return;
        auto &doc = m_core.get_current_document();
        auto &group = doc.get_group(m_core.get_current_group());
        auto *sketch = dynamic_cast<GroupSketch *>(&group);
        if (!sketch)
            return;
        ensure_current_group_layers_initialized();
        const auto active_layer = get_active_layer_for_current_group();
        UUID fallback_layer;
        if (!sketch->remove_layer(active_layer, fallback_layer))
            return;
        for (auto &[uu, en] : doc.m_entities) {
            if (en->m_group != group.m_uuid)
                continue;
            if (en->m_layer == active_layer)
                en->m_layer = fallback_layer;
        }
        select_active_layer_for_current_group(fallback_layer);
        commit_layer_change("remove sketch layer");
    });

    move_up_button->signal_clicked().connect([this, commit_layer_change] {
        if (!m_core.has_documents())
            return;
        if (m_core.tool_is_active() && !force_end_tool())
            return;
        auto &group = m_core.get_current_document().get_group(m_core.get_current_group());
        auto *sketch = dynamic_cast<GroupSketch *>(&group);
        if (!sketch)
            return;
        const auto active_layer = get_active_layer_for_current_group();
        if (!sketch->move_layer(active_layer, -1))
            return;
        commit_layer_change("reorder sketch layer");
    });

    move_down_button->signal_clicked().connect([this, commit_layer_change] {
        if (!m_core.has_documents())
            return;
        if (m_core.tool_is_active() && !force_end_tool())
            return;
        auto &group = m_core.get_current_document().get_group(m_core.get_current_group());
        auto *sketch = dynamic_cast<GroupSketch *>(&group);
        if (!sketch)
            return;
        const auto active_layer = get_active_layer_for_current_group();
        if (!sketch->move_layer(active_layer, 1))
            return;
        commit_layer_change("reorder sketch layer");
    });

    m_layer_edit_popover = Gtk::make_managed<Gtk::Popover>();
    m_layer_edit_popover->set_has_arrow(true);
    m_layer_edit_popover->set_autohide(true);
    m_layer_edit_popover->add_css_class("sketch-grid-popover");
    m_layer_edit_popover->set_parent(*m_layers_mode_button);
    m_layer_edit_popover->set_size_request(sketch_popover_total_width, -1);

    auto edit_root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    edit_root->set_margin_start(12);
    edit_root->set_margin_end(12);
    edit_root->set_margin_top(12);
    edit_root->set_margin_bottom(12);
    edit_root->set_size_request(sketch_popover_content_width, -1);
    m_layer_edit_popover->set_child(*edit_root);

    auto name_label = Gtk::make_managed<Gtk::Label>("Layer name");
    name_label->set_xalign(0);
    edit_root->append(*name_label);

    m_layer_edit_name_entry = Gtk::make_managed<Gtk::Entry>();
    edit_root->append(*m_layer_edit_name_entry);

    auto icon_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto icon_label = Gtk::make_managed<Gtk::Label>("Show process icon");
    icon_label->set_xalign(0);
    icon_label->set_hexpand(true);
    m_layer_edit_icon_switch = Gtk::make_managed<Gtk::Switch>();
    icon_row->append(*icon_label);
    icon_row->append(*m_layer_edit_icon_switch);
    edit_root->append(*icon_row);

    m_layer_edit_process_label = Gtk::make_managed<Gtk::Label>("Process");
    m_layer_edit_process_label->set_xalign(0);
    edit_root->append(*m_layer_edit_process_label);

    m_layer_edit_process_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    m_layer_edit_process_box->add_css_class("linked");
    edit_root->append(*m_layer_edit_process_box);

    const std::array<GroupSketch::SketchLayerProcess, 4> processes = {
            GroupSketch::SketchLayerProcess::LINE_ENGRAVING,
            GroupSketch::SketchLayerProcess::LINE_CUTTING,
            GroupSketch::SketchLayerProcess::FILL_ENGRAVING,
            GroupSketch::SketchLayerProcess::IMAGE_ENGRAVING,
    };

    for (const auto process : processes) {
        auto process_button = Gtk::make_managed<Gtk::Button>();
        process_button->set_icon_name(process_icon_name(process));
        process_button->set_has_frame(true);
        process_button->set_tooltip_text(process_label(process));
        process_button->set_size_request(44, 40);
        process_button->set_hexpand(true);
        process_button->set_halign(Gtk::Align::FILL);
        m_layer_edit_process_buttons[process] = process_button;
        process_button->signal_clicked().connect([this, process, commit_layer_change] {
            if (!m_core.has_documents() || !m_layer_editing_uuid)
                return;
            auto &group_now = m_core.get_current_document().get_group(m_core.get_current_group());
            auto *sketch_now = dynamic_cast<GroupSketch *>(&group_now);
            if (!sketch_now)
                return;
            auto *layer_now = sketch_now->get_layer_ptr(m_layer_editing_uuid);
            if (!layer_now || layer_now->m_process == process)
                return;
            layer_now->m_process = process;
            commit_layer_change("set sketch layer process");
        });
        m_layer_edit_process_box->append(*process_button);
    }

    auto color_title = Gtk::make_managed<Gtk::Label>("Color");
    color_title->set_xalign(0);
    edit_root->append(*color_title);

    auto color_grid = Gtk::make_managed<Gtk::Grid>();
    color_grid->set_row_spacing(4);
    color_grid->set_column_spacing(4);
    edit_root->append(*color_grid);

    int palette_idx = 0;
    for (const auto &[aci, hex] : kSketchLayerColorPalette) {
        auto color_button = Gtk::make_managed<Gtk::Button>();
        color_button->set_has_frame(true);
        color_button->set_tooltip_text("ACI " + std::to_string(aci));
        auto color_swatch = Gtk::make_managed<Gtk::Label>();
        color_swatch->set_markup("<span foreground=\"" + std::string(hex) + "\">■</span>");
        color_button->set_child(*color_swatch);
        m_layer_edit_color_buttons[aci] = color_button;
        color_button->signal_clicked().connect([this, aci, commit_layer_change] {
            if (!m_core.has_documents() || !m_layer_editing_uuid)
                return;
            auto &group_now = m_core.get_current_document().get_group(m_core.get_current_group());
            auto *sketch_now = dynamic_cast<GroupSketch *>(&group_now);
            if (!sketch_now)
                return;
            auto *layer_now = sketch_now->get_layer_ptr(m_layer_editing_uuid);
            if (!layer_now || layer_now->m_color == aci)
                return;
            layer_now->m_color = aci;
            commit_layer_change("set sketch layer color");
        });
        const int column = palette_idx % 6;
        const int row_idx = palette_idx / 6;
        color_grid->attach(*color_button, column, row_idx);
        palette_idx++;
    }

    auto apply_name = [this, commit_layer_change] {
        if (m_updating_layer_edit_popover)
            return;
        if (!m_core.has_documents() || !m_layer_editing_uuid || !m_layer_edit_name_entry)
            return;
        auto &group_now = m_core.get_current_document().get_group(m_core.get_current_group());
        auto *sketch_now = dynamic_cast<GroupSketch *>(&group_now);
        if (!sketch_now)
            return;
        auto *layer_now = sketch_now->get_layer_ptr(m_layer_editing_uuid);
        if (!layer_now)
            return;
        const auto new_name = m_layer_edit_name_entry->get_text().raw();
        if (new_name.empty() || new_name == layer_now->m_name)
            return;
        layer_now->m_name = new_name;
        commit_layer_change("rename sketch layer");
    };

    m_layer_edit_name_entry->signal_activate().connect(apply_name);
    m_layer_edit_name_entry->property_has_focus().signal_changed().connect([this, apply_name] {
        if (!m_layer_edit_name_entry->has_focus())
            apply_name();
    });

    m_layer_edit_icon_switch->property_active().signal_changed().connect([this, commit_layer_change] {
        if (m_updating_layer_edit_popover)
            return;
        if (!m_core.has_documents() || !m_layer_editing_uuid || !m_layer_edit_icon_switch)
            return;
        auto &group_now = m_core.get_current_document().get_group(m_core.get_current_group());
        auto *sketch_now = dynamic_cast<GroupSketch *>(&group_now);
        if (!sketch_now)
            return;
        auto *layer_now = sketch_now->get_layer_ptr(m_layer_editing_uuid);
        if (!layer_now)
            return;
        const bool show_icon = m_layer_edit_icon_switch->get_active();
        if (layer_now->m_show_process_icon == show_icon)
            return;
        layer_now->m_show_process_icon = show_icon;
        commit_layer_change("toggle sketch layer process icon");
    });

    m_layers_mode_button->signal_clicked().connect([this] {
        m_layers_mode_enabled = !m_layers_mode_enabled;
        if (m_layers_mode_enabled)
            ensure_current_group_layers_initialized();
        rebuild_layers_popover();
        update_sketcher_toolbar_button_states();
        if (!m_layers_popover)
            return;
        if (m_layers_popover->get_visible()) {
            m_layers_popover->popdown();
            if (m_layer_edit_popover && m_layer_edit_popover->get_visible())
                m_layer_edit_popover->popdown();
            if (g_active_hover_popover == m_layers_popover)
                g_active_hover_popover = nullptr;
        }
        else {
            m_layers_popover->popup();
            g_active_hover_popover = m_layers_popover;
        }
        canvas_update_keep_selection();
    });
#endif
}

void Editor::init_cup_template_popover()
{
#ifdef DUNE_SKETCHER_ONLY
    m_cup_template_button = Gtk::make_managed<Gtk::Button>();
    m_cup_template_button->set_icon_name("view-column-symbolic");
    m_cup_template_button->set_tooltip_text("Cup template");
    m_cup_template_button->set_has_frame(true);
    m_cup_template_button->set_focusable(false);
    m_cup_template_button->add_css_class("sketch-toolbar-button");

    m_cup_template_popover = Gtk::make_managed<Gtk::Popover>();
    m_cup_template_popover->set_has_arrow(true);
    m_cup_template_popover->set_autohide(false);
    m_cup_template_popover->add_css_class("sketch-grid-popover");
    m_cup_template_popover->set_parent(*m_cup_template_button);
    m_cup_template_popover->set_size_request(sketch_popover_total_width, -1);
    install_hover_popover(*m_cup_template_button, *m_cup_template_popover, [this] { return !m_primary_button_pressed; },
                          [this] { return m_right_click_popovers_only; });

    auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    root->set_margin_start(12);
    root->set_margin_end(12);
    root->set_margin_top(12);
    root->set_margin_bottom(12);
    root->set_size_request(sketch_popover_content_width, -1);
    m_cup_template_popover->set_child(*root);

    auto make_dimension_row = [root](const char *title, Gtk::SpinButton *&spin, int digits = 2) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto label = Gtk::make_managed<Gtk::Label>(title);
        label->set_hexpand(true);
        label->set_xalign(0);
        spin = Gtk::make_managed<Gtk::SpinButton>();
        spin->set_digits(digits);
        spin->set_numeric(true);
        spin->set_width_chars(7);
        spin->set_halign(Gtk::Align::END);
        auto unit = Gtk::make_managed<Gtk::Label>("mm");
        unit->add_css_class("dim-label");
        row->append(*label);
        row->append(*spin);
        row->append(*unit);
        root->append(*row);
    };

    make_dimension_row("Height", m_cup_template_height_spin, 2);
    make_dimension_row("Circumference", m_cup_template_circumference_spin, 2);
    make_dimension_row("Diameter", m_cup_template_diameter_spin, 2);

    auto segments_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto segments_label = Gtk::make_managed<Gtk::Label>("Segments");
    segments_label->set_hexpand(true);
    segments_label->set_xalign(0);
    m_cup_template_segments_spin = Gtk::make_managed<Gtk::SpinButton>();
    m_cup_template_segments_spin->set_digits(0);
    m_cup_template_segments_spin->set_numeric(true);
    m_cup_template_segments_spin->set_width_chars(4);
    m_cup_template_segments_spin->set_halign(Gtk::Align::END);
    segments_row->append(*segments_label);
    segments_row->append(*m_cup_template_segments_spin);
    root->append(*segments_row);

    m_cup_template_height_spin->set_range(1.0, 2000.0);
    m_cup_template_height_spin->set_increments(1.0, 10.0);
    m_cup_template_circumference_spin->set_range(1.0, 4000.0);
    m_cup_template_circumference_spin->set_increments(1.0, 10.0);
    m_cup_template_diameter_spin->set_range(0.1, 1200.0);
    m_cup_template_diameter_spin->set_increments(0.5, 10.0);
    m_cup_template_segments_spin->set_range(1, 12);
    m_cup_template_segments_spin->set_increments(1, 1);

    m_updating_cup_template_popover = true;
    m_cup_template_height_spin->set_value(m_cup_template_height_mm);
    m_cup_template_circumference_spin->set_value(m_cup_template_circumference_mm);
    m_cup_template_diameter_spin->set_value(m_cup_template_circumference_mm / M_PI);
    m_cup_template_segments_spin->set_value(m_cup_template_segments);
    m_updating_cup_template_popover = false;

    m_cup_template_height_spin->signal_value_changed().connect([this] {
        if (m_updating_cup_template_popover)
            return;
        m_cup_template_height_mm = std::max(1.0, m_cup_template_height_spin->get_value());
        canvas_update_keep_selection();
    });

    m_cup_template_circumference_spin->signal_value_changed().connect([this] {
        if (m_updating_cup_template_popover)
            return;
        m_cup_template_circumference_mm = std::max(1.0, m_cup_template_circumference_spin->get_value());
        m_updating_cup_template_popover = true;
        m_cup_template_diameter_spin->set_value(m_cup_template_circumference_mm / M_PI);
        m_updating_cup_template_popover = false;
        canvas_update_keep_selection();
    });

    m_cup_template_diameter_spin->signal_value_changed().connect([this] {
        if (m_updating_cup_template_popover)
            return;
        const auto diameter = std::max(0.1, m_cup_template_diameter_spin->get_value());
        m_cup_template_circumference_mm = diameter * M_PI;
        m_updating_cup_template_popover = true;
        m_cup_template_circumference_spin->set_value(m_cup_template_circumference_mm);
        m_updating_cup_template_popover = false;
        canvas_update_keep_selection();
    });

    m_cup_template_segments_spin->signal_value_changed().connect([this] {
        if (m_updating_cup_template_popover)
            return;
        m_cup_template_segments = std::clamp(static_cast<int>(std::lround(m_cup_template_segments_spin->get_value())), 1, 12);
        canvas_update_keep_selection();
    });

    m_cup_template_button->signal_clicked().connect([this] {
        m_cup_template_enabled = !m_cup_template_enabled;
        update_sketcher_toolbar_button_states();
        canvas_update_keep_selection();
        if (!m_cup_template_popover)
            return;
        if (m_cup_template_popover->get_visible()) {
            m_cup_template_popover->popdown();
            if (g_active_hover_popover == m_cup_template_popover)
                g_active_hover_popover = nullptr;
        }
        else {
            m_cup_template_popover->popup();
            g_active_hover_popover = m_cup_template_popover;
        }
    });
#endif
}

void Editor::init_gears_popover()
{
#ifdef DUNE_SKETCHER_ONLY
    m_gears_button = Gtk::make_managed<Gtk::Button>();
    m_gears_button->set_icon_name("applications-engineering-symbolic");
    m_gears_button->set_tooltip_text("Gears");
    m_gears_button->set_has_frame(true);
    m_gears_button->set_focusable(false);
    m_gears_button->add_css_class("sketch-toolbar-button");

    m_gears_popover = Gtk::make_managed<Gtk::Popover>();
    m_gears_popover->set_has_arrow(true);
    m_gears_popover->set_autohide(false);
    m_gears_popover->add_css_class("sketch-grid-popover");
    m_gears_popover->set_parent(*m_gears_button);
    m_gears_popover->set_size_request(sketch_popover_total_width, -1);
    install_hover_popover(*m_gears_button, *m_gears_popover, [this] { return !m_primary_button_pressed; },
                          [this] { return m_right_click_popovers_only; }, false);
    m_gears_popover->signal_show().connect([this] {
        update_gears_quick_popover();
        update_sketcher_toolbar_button_states();
    });
    m_gears_popover->signal_hide().connect([this] {
        update_gears_quick_popover();
        update_sketcher_toolbar_button_states();
    });

    auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    root->set_margin_start(12);
    root->set_margin_end(12);
    root->set_margin_top(12);
    root->set_margin_bottom(12);
    root->set_size_request(sketch_popover_content_width, -1);
    m_gears_popover->set_child(*root);

    auto add_spin_row = [root](const char *label_text, Gtk::SpinButton *&spin, int digits, double min, double max, double step,
                               const char *unit = nullptr) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto label = Gtk::make_managed<Gtk::Label>(label_text);
        label->set_hexpand(true);
        label->set_xalign(0);
        spin = Gtk::make_managed<Gtk::SpinButton>();
        spin->set_digits(digits);
        spin->set_numeric(true);
        spin->set_range(min, max);
        spin->set_increments(step, step * 10.0);
        spin->set_width_chars(6);
        row->append(*label);
        row->append(*spin);
        if (unit) {
            auto unit_label = Gtk::make_managed<Gtk::Label>(unit);
            unit_label->add_css_class("dim-label");
            row->append(*unit_label);
        }
        root->append(*row);
    };

    add_spin_row("Module", m_gears_module_spin, 2, 0.1, 50.0, 0.1, "mm");
    add_spin_row("Teeth", m_gears_teeth_spin, 0, 3, 360, 1);
    add_spin_row("Pressure", m_gears_pressure_angle_spin, 1, 5, 45, 0.5, "deg");
    add_spin_row("Backlash", m_gears_backlash_spin, 3, 0.0, 0.5, 0.01, "mm");
    add_spin_row("Segments", m_gears_segments_spin, 0, 4, 128, 1);
    add_spin_row("Bore", m_gears_bore_spin, 2, 0.0, 400.0, 0.1, "mm");
    add_spin_row("Material", m_gears_material_thickness_spin, 2, 0.1, 100.0, 0.1, "mm");

    auto hole_mode_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto hole_mode_title = Gtk::make_managed<Gtk::Label>("Hole");
    hole_mode_title->set_hexpand(true);
    hole_mode_title->set_xalign(0);
    m_gears_hole_mode_prev_button = Gtk::make_managed<Gtk::Button>();
    m_gears_hole_mode_prev_button->set_icon_name("go-previous-symbolic");
    m_gears_hole_mode_prev_button->set_has_frame(true);
    m_gears_hole_mode_label = Gtk::make_managed<Gtk::Label>("Circle");
    m_gears_hole_mode_label->set_width_chars(6);
    m_gears_hole_mode_label->set_xalign(0.5f);
    m_gears_hole_mode_next_button = Gtk::make_managed<Gtk::Button>();
    m_gears_hole_mode_next_button->set_icon_name("go-next-symbolic");
    m_gears_hole_mode_next_button->set_has_frame(true);
    hole_mode_row->append(*hole_mode_title);
    hole_mode_row->append(*m_gears_hole_mode_prev_button);
    hole_mode_row->append(*m_gears_hole_mode_label);
    hole_mode_row->append(*m_gears_hole_mode_next_button);
    root->append(*hole_mode_row);

    auto inward_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto inward_label = Gtk::make_managed<Gtk::Label>("Inward");
    inward_label->set_hexpand(true);
    inward_label->set_xalign(0);
    m_gears_inward_switch = Gtk::make_managed<Gtk::Switch>();
    inward_row->append(*inward_label);
    inward_row->append(*m_gears_inward_switch);
    root->append(*inward_row);

    m_gears_module_spin->set_value(2.0);
    m_gears_teeth_spin->set_value(24);
    m_gears_pressure_angle_spin->set_value(20.0);
    m_gears_backlash_spin->set_value(0.0);
    m_gears_segments_spin->set_value(12);
    m_gears_bore_spin->set_value(5.0);
    m_gears_material_thickness_spin->set_value(3.0);
    m_gears_inward_switch->set_active(false);
    m_gears_teeth_spin->set_tooltip_text("Standalone gear only. Selection-mode gears derive tooth count from Module.");
    m_gears_segments_spin->set_tooltip_text("Curve quality for standalone involute gear generation.");
    m_gears_backlash_spin->set_tooltip_text(
            "Tooth side clearance at pitch circle. Use small values (typically 0.02-0.10 mm).");

    const auto update_hole_mode_label = [this] {
        if (m_gears_hole_mode_label)
            m_gears_hole_mode_label->set_text(gear_hole_mode_name_by_index(static_cast<int>(m_gears_hole_mode)));
        if (m_gears_generator_hole_mode_label)
            m_gears_generator_hole_mode_label->set_text(gear_hole_mode_name_by_index(static_cast<int>(m_gears_hole_mode)));
    };
    m_gears_hole_mode_prev_button->signal_clicked().connect([this, update_hole_mode_label] {
        auto idx = static_cast<int>(m_gears_hole_mode);
        idx = normalize_gear_hole_mode_index(idx - 1);
        m_gears_hole_mode = static_cast<GearHoleMode>(idx);
        update_hole_mode_label();
        update_gears_generator_preview();
    });
    m_gears_hole_mode_next_button->signal_clicked().connect([this, update_hole_mode_label] {
        auto idx = static_cast<int>(m_gears_hole_mode);
        idx = normalize_gear_hole_mode_index(idx + 1);
        m_gears_hole_mode = static_cast<GearHoleMode>(idx);
        update_hole_mode_label();
        update_gears_generator_preview();
    });
    update_hole_mode_label();

    m_gears_quick_popover = Gtk::make_managed<Gtk::Popover>();
    m_gears_quick_popover->set_has_arrow(true);
    m_gears_quick_popover->set_autohide(false);
    m_gears_quick_popover->add_css_class("sketch-grid-popover");
    m_gears_quick_popover->set_parent(get_canvas());
    m_gears_quick_popover->set_position(Gtk::PositionType::RIGHT);
    m_gears_quick_popover->set_offset(10, 10);

    auto quick_root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    quick_root->set_margin_start(10);
    quick_root->set_margin_end(10);
    quick_root->set_margin_top(10);
    quick_root->set_margin_bottom(10);
    quick_root->set_size_request(160, -1);
    m_gears_quick_popover->set_child(*quick_root);

    auto quick_inward_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto quick_inward_label = Gtk::make_managed<Gtk::Label>("Inward");
    quick_inward_label->set_hexpand(true);
    quick_inward_label->set_xalign(0);
    m_gears_quick_inward_switch = Gtk::make_managed<Gtk::Switch>();
    quick_inward_row->append(*quick_inward_label);
    quick_inward_row->append(*m_gears_quick_inward_switch);
    quick_root->append(*quick_inward_row);

    m_gears_quick_apply_button = Gtk::make_managed<Gtk::Button>("Apply");
    m_gears_quick_apply_button->set_has_frame(true);
    m_gears_quick_apply_button->add_css_class("suggested-action");
    quick_root->append(*m_gears_quick_apply_button);
    m_gears_quick_apply_button->signal_clicked().connect([this] {
        if (!generate_gears_from_selected_profile()) {
            if (m_workspace_browser)
                m_workspace_browser->show_toast("Select a circle, arc, or oval (Bezier chain) for Gears");
            return;
        }
        update_gears_quick_popover();
    });

    auto sync_gears_inward = [this](bool inward) {
        if (m_updating_gears_inward_switches)
            return;
        m_updating_gears_inward_switches = true;
        if (m_gears_inward_switch && m_gears_inward_switch->get_active() != inward)
            m_gears_inward_switch->set_active(inward);
        if (m_gears_quick_inward_switch && m_gears_quick_inward_switch->get_active() != inward)
            m_gears_quick_inward_switch->set_active(inward);
        m_updating_gears_inward_switches = false;
    };
    m_gears_inward_switch->property_active().signal_changed().connect(
            [this, sync_gears_inward] { sync_gears_inward(m_gears_inward_switch->get_active()); });
    m_gears_quick_inward_switch->property_active().signal_changed().connect(
            [this, sync_gears_inward] { sync_gears_inward(m_gears_quick_inward_switch->get_active()); });
    sync_gears_inward(false);

    const auto sync_generator_from_main = [this] {
        update_gears_generator_ui();
        update_gears_generator_preview();
    };
    m_gears_module_spin->signal_value_changed().connect(sync_generator_from_main);
    m_gears_teeth_spin->signal_value_changed().connect(sync_generator_from_main);
    m_gears_pressure_angle_spin->signal_value_changed().connect(sync_generator_from_main);
    m_gears_backlash_spin->signal_value_changed().connect(sync_generator_from_main);
    m_gears_segments_spin->signal_value_changed().connect(sync_generator_from_main);
    m_gears_bore_spin->signal_value_changed().connect(sync_generator_from_main);
    m_gears_material_thickness_spin->signal_value_changed().connect(sync_generator_from_main);
    m_gears_inward_switch->property_active().signal_changed().connect(sync_generator_from_main);

    auto generator_button = Gtk::make_managed<Gtk::Button>("Generator");
    generator_button->set_has_frame(true);
    generator_button->add_css_class("suggested-action");
    root->append(*generator_button);
    generator_button->signal_clicked().connect([this] {
        if (m_gears_popover && m_gears_popover->get_visible()) {
            m_gears_popover->popdown();
            if (g_active_hover_popover == m_gears_popover)
                g_active_hover_popover = nullptr;
        }
        open_gears_generator_window();
    });

    m_gears_button->signal_clicked().connect([this] {
        m_gears_mode_enabled = !m_gears_mode_enabled;
        update_gears_quick_popover();
        update_sketcher_toolbar_button_states();
    });
#endif
}

void Editor::update_gears_quick_popover()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_gears_quick_popover)
        return;

    const auto hide_quick = [this] {
        if (m_gears_quick_popover && m_gears_quick_popover->get_visible())
            m_gears_quick_popover->popdown();
    };

    if (!m_gears_mode_enabled || !m_core.has_documents() || m_core.tool_is_active()) {
        hide_quick();
        return;
    }
    if (m_gears_popover && m_gears_popover->get_visible()) {
        hide_quick();
        return;
    }

    auto &doc = m_core.get_current_document();
    GearProfileSource source;
    std::string error;
    if (!collect_selected_gear_profile_source(doc, get_canvas().get_selection(), m_core.get_current_group(), source, &error)) {
        hide_quick();
        return;
    }

    if (!m_gears_quick_popover->get_visible()) {
        Gdk::Rectangle rect(static_cast<int>(std::lround(m_last_x)) + 1, static_cast<int>(std::lround(m_last_y)) + 1, 1, 1);
        m_gears_quick_popover->set_pointing_to(rect);
        m_gears_quick_popover->popup();
    }
#endif
}

void Editor::open_gears_generator_window()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_gears_generator_window) {
        m_gears_generator_window = Gtk::make_managed<Gtk::Window>();
        m_gears_generator_window->set_title("Gear Generator");
        m_gears_generator_window->set_default_size(1020, 640);
        m_gears_generator_window->set_transient_for(m_win);
        sync_sketch_theme_classes(m_win, *m_gears_generator_window);
        m_gears_generator_window->set_modal(false);
        m_gears_generator_window->set_hide_on_close(true);
        m_gears_generator_window->signal_close_request().connect(
                [this] {
                    if (m_gears_generator_window)
                        m_gears_generator_window->hide();
                    return true;
                },
                false);

        auto header = Gtk::make_managed<Gtk::HeaderBar>();
        header->set_show_title_buttons(true);
        auto title = Gtk::make_managed<Gtk::Label>("Generator");
        header->set_title_widget(*title);
        m_gears_generator_import_button = Gtk::make_managed<Gtk::Button>("Import");
        m_gears_generator_import_button->add_css_class("suggested-action");
        header->pack_end(*m_gears_generator_import_button);
        m_gears_generator_window->set_titlebar(*header);

        auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        root->set_margin_start(12);
        root->set_margin_end(12);
        root->set_margin_top(12);
        root->set_margin_bottom(12);
        m_gears_generator_window->set_child(*root);

        auto settings_frame = Gtk::make_managed<Gtk::Frame>("Settings");
        settings_frame->set_size_request(270, -1);
        settings_frame->set_vexpand(true);
        root->append(*settings_frame);

        auto settings_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        settings_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        settings_scroll->set_min_content_height(100);
        settings_frame->set_child(*settings_scroll);

        auto settings = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        settings->set_margin_start(10);
        settings->set_margin_end(10);
        settings->set_margin_top(10);
        settings->set_margin_bottom(10);
        settings_scroll->set_child(*settings);

        auto add_spin_row = [settings](const char *label_text, Gtk::SpinButton *&spin, int digits, double min, double max,
                                       double step, const char *unit = nullptr) {
            auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
            auto label = Gtk::make_managed<Gtk::Label>(label_text);
            label->set_hexpand(true);
            label->set_xalign(0);
            spin = Gtk::make_managed<Gtk::SpinButton>();
            spin->set_digits(digits);
            spin->set_numeric(true);
            spin->set_range(min, max);
            spin->set_increments(step, step * 10.0);
            spin->set_width_chars(6);
            row->append(*label);
            row->append(*spin);
            if (unit) {
                auto unit_label = Gtk::make_managed<Gtk::Label>(unit);
                unit_label->add_css_class("dim-label");
                row->append(*unit_label);
            }
            settings->append(*row);
        };

        add_spin_row("Module", m_gears_generator_module_spin, 2, 0.1, 50.0, 0.1, "mm");
        add_spin_row("Teeth", m_gears_generator_teeth_spin, 0, 3, 720, 1);
        add_spin_row("Pressure", m_gears_generator_pressure_spin, 1, 5, 45, 0.5, "deg");
        add_spin_row("Backlash", m_gears_generator_backlash_spin, 3, 0.0, 0.5, 0.01, "mm");
        add_spin_row("Segments", m_gears_generator_segments_spin, 0, 4, 128, 1);
        add_spin_row("Bore", m_gears_generator_bore_spin, 2, 0.0, 400.0, 0.1, "mm");
        add_spin_row("Material", m_gears_generator_material_thickness_spin, 2, 0.1, 100.0, 0.1, "mm");

        auto hole_mode_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto hole_mode_title = Gtk::make_managed<Gtk::Label>("Hole");
        hole_mode_title->set_hexpand(true);
        hole_mode_title->set_xalign(0);
        m_gears_generator_hole_mode_prev_button = Gtk::make_managed<Gtk::Button>();
        m_gears_generator_hole_mode_prev_button->set_icon_name("go-previous-symbolic");
        m_gears_generator_hole_mode_prev_button->set_has_frame(true);
        m_gears_generator_hole_mode_label = Gtk::make_managed<Gtk::Label>("Circle");
        m_gears_generator_hole_mode_label->set_width_chars(6);
        m_gears_generator_hole_mode_label->set_xalign(0.5f);
        m_gears_generator_hole_mode_next_button = Gtk::make_managed<Gtk::Button>();
        m_gears_generator_hole_mode_next_button->set_icon_name("go-next-symbolic");
        m_gears_generator_hole_mode_next_button->set_has_frame(true);
        hole_mode_row->append(*hole_mode_title);
        hole_mode_row->append(*m_gears_generator_hole_mode_prev_button);
        hole_mode_row->append(*m_gears_generator_hole_mode_label);
        hole_mode_row->append(*m_gears_generator_hole_mode_next_button);
        settings->append(*hole_mode_row);

        {
            auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
            auto label = Gtk::make_managed<Gtk::Label>("Diameter 1");
            label->set_hexpand(true);
            label->set_xalign(0);
            m_gears_generator_diameter1_spin = Gtk::make_managed<Gtk::SpinButton>();
            m_gears_generator_diameter1_spin->set_digits(2);
            m_gears_generator_diameter1_spin->set_numeric(true);
            m_gears_generator_diameter1_spin->set_range(1.0, 2000.0);
            m_gears_generator_diameter1_spin->set_increments(0.1, 1.0);
            auto unit = Gtk::make_managed<Gtk::Label>("mm");
            unit->add_css_class("dim-label");
            row->append(*label);
            row->append(*m_gears_generator_diameter1_spin);
            row->append(*unit);
            settings->append(*row);
        }

        auto pair_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto pair_label = Gtk::make_managed<Gtk::Label>("Two gears");
        pair_label->set_hexpand(true);
        pair_label->set_xalign(0);
        m_gears_generator_pair_switch = Gtk::make_managed<Gtk::Switch>();
        pair_row->append(*pair_label);
        pair_row->append(*m_gears_generator_pair_switch);
        settings->append(*pair_row);

        m_gears_generator_pair_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        settings->append(*m_gears_generator_pair_box);

        auto ratio_mode_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto ratio_mode_label = Gtk::make_managed<Gtk::Label>("Use degrees");
        ratio_mode_label->set_hexpand(true);
        ratio_mode_label->set_xalign(0);
        m_gears_generator_ratio_degrees_switch = Gtk::make_managed<Gtk::Switch>();
        ratio_mode_row->append(*ratio_mode_label);
        ratio_mode_row->append(*m_gears_generator_ratio_degrees_switch);
        m_gears_generator_pair_box->append(*ratio_mode_row);

        auto ratio_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        m_gears_generator_ratio_label = Gtk::make_managed<Gtk::Label>("Ratio");
        m_gears_generator_ratio_label->set_hexpand(true);
        m_gears_generator_ratio_label->set_xalign(0);
        m_gears_generator_ratio_spin = Gtk::make_managed<Gtk::SpinButton>();
        m_gears_generator_ratio_spin->set_digits(3);
        m_gears_generator_ratio_spin->set_numeric(true);
        m_gears_generator_ratio_spin->set_range(0.05, 20.0);
        m_gears_generator_ratio_spin->set_increments(0.01, 0.1);
        ratio_row->append(*m_gears_generator_ratio_label);
        ratio_row->append(*m_gears_generator_ratio_spin);
        m_gears_generator_pair_box->append(*ratio_row);

        m_gears_generator_summary_label = Gtk::make_managed<Gtk::Label>();
        m_gears_generator_summary_label->set_xalign(0);
        m_gears_generator_summary_label->set_wrap(true);
        m_gears_generator_summary_label->add_css_class("dim-label");
        settings->append(*m_gears_generator_summary_label);

        auto preview_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        preview_box->set_hexpand(true);
        preview_box->set_vexpand(true);
        root->append(*preview_box);

        auto preview_frame = Gtk::make_managed<Gtk::Frame>("Preview");
        preview_frame->set_hexpand(true);
        preview_frame->set_vexpand(true);
        preview_box->append(*preview_frame);

        m_gears_generator_preview_area = Gtk::make_managed<Gtk::DrawingArea>();
        m_gears_generator_preview_area->set_hexpand(true);
        m_gears_generator_preview_area->set_vexpand(true);
        m_gears_generator_preview_area->set_content_width(1);
        m_gears_generator_preview_area->set_content_height(1);
        m_gears_generator_preview_area->set_draw_func(sigc::mem_fun(*this, &Editor::draw_gears_generator_preview));
        preview_frame->set_child(*m_gears_generator_preview_area);

        m_gears_generator_rotation_slider = Gtk::make_managed<Gtk::Scale>(Gtk::Orientation::VERTICAL);
        m_gears_generator_rotation_slider->set_range(-360.0, 360.0);
        m_gears_generator_rotation_slider->set_increments(1.0, 15.0);
        m_gears_generator_rotation_slider->set_digits(1);
        m_gears_generator_rotation_slider->set_draw_value(false);
        m_gears_generator_rotation_slider->set_value(0.0);
        m_gears_generator_rotation_slider->set_vexpand(true);
        preview_box->append(*m_gears_generator_rotation_slider);

        const auto on_value_changed = [this] {
            if (m_updating_gears_generator_ui)
                return;
            update_gears_generator_ui();
            update_gears_generator_preview();
        };

        m_gears_generator_module_spin->signal_value_changed().connect(on_value_changed);
        m_gears_generator_teeth_spin->signal_value_changed().connect(on_value_changed);
        m_gears_generator_pressure_spin->signal_value_changed().connect(on_value_changed);
        m_gears_generator_backlash_spin->signal_value_changed().connect(on_value_changed);
        m_gears_generator_segments_spin->signal_value_changed().connect(on_value_changed);
        m_gears_generator_bore_spin->signal_value_changed().connect(on_value_changed);
        m_gears_generator_material_thickness_spin->signal_value_changed().connect(on_value_changed);
        m_gears_generator_pair_switch->property_active().signal_changed().connect(on_value_changed);
        m_gears_generator_ratio_degrees_switch->property_active().signal_changed().connect([this, on_value_changed] {
            if (!m_gears_generator_ratio_spin)
                return;
            m_updating_gears_generator_ui = true;
            if (m_gears_generator_ratio_degrees_switch && m_gears_generator_ratio_degrees_switch->get_active()) {
                m_gears_generator_ratio_spin->set_range(1.0, 3600.0);
                m_gears_generator_ratio_spin->set_digits(1);
                m_gears_generator_ratio_spin->set_increments(1.0, 10.0);
                m_gears_generator_ratio_spin->set_value(m_gears_generator_ratio_spin->get_value() * 360.0);
            }
            else {
                m_gears_generator_ratio_spin->set_range(0.05, 20.0);
                m_gears_generator_ratio_spin->set_digits(3);
                m_gears_generator_ratio_spin->set_increments(0.01, 0.1);
                m_gears_generator_ratio_spin->set_value(m_gears_generator_ratio_spin->get_value() / 360.0);
            }
            m_updating_gears_generator_ui = false;
            on_value_changed();
        });
        m_gears_generator_diameter1_spin->signal_value_changed().connect(on_value_changed);
        m_gears_generator_ratio_spin->signal_value_changed().connect(on_value_changed);
        m_gears_generator_rotation_slider->signal_value_changed().connect(on_value_changed);
        m_gears_generator_hole_mode_prev_button->signal_clicked().connect([this] {
            auto idx = normalize_gear_hole_mode_index(static_cast<int>(m_gears_hole_mode) - 1);
            m_gears_hole_mode = static_cast<GearHoleMode>(idx);
            if (m_gears_hole_mode_label)
                m_gears_hole_mode_label->set_text(gear_hole_mode_name_by_index(idx));
            update_gears_generator_ui();
            update_gears_generator_preview();
        });
        m_gears_generator_hole_mode_next_button->signal_clicked().connect([this] {
            auto idx = normalize_gear_hole_mode_index(static_cast<int>(m_gears_hole_mode) + 1);
            m_gears_hole_mode = static_cast<GearHoleMode>(idx);
            if (m_gears_hole_mode_label)
                m_gears_hole_mode_label->set_text(gear_hole_mode_name_by_index(idx));
            update_gears_generator_ui();
            update_gears_generator_preview();
        });
        m_gears_generator_import_button->signal_clicked().connect([this] {
            if (!import_gears_generator_to_document())
                return;
            if (m_gears_generator_window)
                m_gears_generator_window->hide();
        });

        m_updating_gears_generator_ui = true;
        m_gears_generator_pair_switch->set_active(false);
        m_gears_generator_ratio_degrees_switch->set_active(false);
        m_gears_generator_diameter1_spin->set_value(48.0);
        m_gears_generator_ratio_spin->set_value(1.0);
        m_updating_gears_generator_ui = false;
    }

    m_updating_gears_generator_ui = true;
    if (m_gears_generator_module_spin && m_gears_module_spin)
        m_gears_generator_module_spin->set_value(m_gears_module_spin->get_value());
    if (m_gears_generator_teeth_spin && m_gears_teeth_spin)
        m_gears_generator_teeth_spin->set_value(m_gears_teeth_spin->get_value());
    if (m_gears_generator_pressure_spin && m_gears_pressure_angle_spin)
        m_gears_generator_pressure_spin->set_value(m_gears_pressure_angle_spin->get_value());
    if (m_gears_generator_backlash_spin && m_gears_backlash_spin)
        m_gears_generator_backlash_spin->set_value(m_gears_backlash_spin->get_value());
    if (m_gears_generator_segments_spin && m_gears_segments_spin)
        m_gears_generator_segments_spin->set_value(m_gears_segments_spin->get_value());
    if (m_gears_generator_bore_spin && m_gears_bore_spin)
        m_gears_generator_bore_spin->set_value(m_gears_bore_spin->get_value());
    if (m_gears_generator_material_thickness_spin && m_gears_material_thickness_spin)
        m_gears_generator_material_thickness_spin->set_value(m_gears_material_thickness_spin->get_value());
    if (m_gears_generator_diameter1_spin && m_gears_generator_module_spin && m_gears_generator_teeth_spin)
        m_gears_generator_diameter1_spin->set_value(m_gears_generator_module_spin->get_value() * m_gears_generator_teeth_spin->get_value());
    m_updating_gears_generator_ui = false;

    update_gears_generator_ui();
    update_gears_generator_preview();
    m_gears_generator_window->present();
#endif
}

void Editor::update_gears_generator_ui()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_gears_generator_window)
        return;
    if (m_gears_generator_hole_mode_label)
        m_gears_generator_hole_mode_label->set_text(gear_hole_mode_name_by_index(static_cast<int>(m_gears_hole_mode)));
    if (m_gears_hole_mode_label)
        m_gears_hole_mode_label->set_text(gear_hole_mode_name_by_index(static_cast<int>(m_gears_hole_mode)));

    const auto pair_mode = m_gears_generator_pair_switch && m_gears_generator_pair_switch->get_active();
    if (m_gears_generator_pair_box)
        m_gears_generator_pair_box->set_visible(pair_mode);
    if (m_gears_generator_rotation_slider)
        m_gears_generator_rotation_slider->set_visible(pair_mode);
    if (m_gears_generator_ratio_label) {
        if (m_gears_generator_ratio_degrees_switch && m_gears_generator_ratio_degrees_switch->get_active())
            m_gears_generator_ratio_label->set_text("Angle2/360");
        else
            m_gears_generator_ratio_label->set_text("Ratio");
    }

    if (!m_gears_generator_summary_label || !m_gears_generator_module_spin || !m_gears_generator_teeth_spin)
        return;

    const auto module = std::max(0.05, m_gears_generator_module_spin->get_value());
    const auto diameter1 =
            m_gears_generator_diameter1_spin ? std::max(1.0, m_gears_generator_diameter1_spin->get_value()) : module * 24.0;
    const auto z1_from_diameter = std::max(3, static_cast<int>(std::lround(diameter1 / module)));
    if (m_gears_generator_teeth_spin) {
        const auto old = static_cast<int>(std::lround(m_gears_generator_teeth_spin->get_value()));
        if (old != z1_from_diameter) {
            m_updating_gears_generator_ui = true;
            m_gears_generator_teeth_spin->set_value(z1_from_diameter);
            m_updating_gears_generator_ui = false;
        }
    }
    if (!pair_mode) {
        const auto pitch_d = module * static_cast<double>(z1_from_diameter);
        std::ostringstream ss;
        ss << "z=" << z1_from_diameter << "  pitch diameter=" << std::fixed << std::setprecision(2) << pitch_d << " mm";
        m_gears_generator_summary_label->set_text(ss.str());
        return;
    }

    if (!m_gears_generator_diameter1_spin || !m_gears_generator_ratio_spin) {
        m_gears_generator_summary_label->set_text({});
        return;
    }
    auto ratio_value = std::max(0.001, m_gears_generator_ratio_spin->get_value());
    if (m_gears_generator_ratio_degrees_switch && m_gears_generator_ratio_degrees_switch->get_active())
        ratio_value /= 360.0;
    ratio_value = std::clamp(ratio_value, 0.01, 100.0);

    const auto z1 = z1_from_diameter;
    const auto z2 = std::max(3, static_cast<int>(std::lround(static_cast<double>(z1) / ratio_value)));
    const auto d1 = module * static_cast<double>(z1);
    const auto d2 = module * static_cast<double>(z2);
    const auto actual_ratio = static_cast<double>(z1) / static_cast<double>(z2);
    const auto actual_deg = 360.0 * actual_ratio;
    std::ostringstream ss;
    ss << "z1=" << z1 << "  z2=" << z2 << "  d1=" << std::fixed << std::setprecision(2) << d1 << " mm  d2=" << d2
       << " mm  ratio=" << std::setprecision(3) << actual_ratio << " (" << std::setprecision(1) << actual_deg
       << " deg/360)";
    m_gears_generator_summary_label->set_text(ss.str());
#endif
}

bool Editor::build_gears_generator_polylines(std::vector<std::vector<glm::dvec2>> &polylines) const
{
#ifdef DUNE_SKETCHER_ONLY
    polylines.clear();
    if (!m_gears_generator_module_spin || !m_gears_generator_teeth_spin || !m_gears_generator_pressure_spin
        || !m_gears_generator_backlash_spin || !m_gears_generator_segments_spin || !m_gears_generator_bore_spin
        || !m_gears_generator_material_thickness_spin)
        return false;

    const auto module = std::max(0.05, m_gears_generator_module_spin->get_value());
    const auto pressure = m_gears_generator_pressure_spin->get_value();
    const auto backlash = std::max(0.0, m_gears_generator_backlash_spin->get_value());
    const auto segments = std::max(4, static_cast<int>(std::lround(m_gears_generator_segments_spin->get_value())));
    const auto bore = std::max(0.0, m_gears_generator_bore_spin->get_value());
    const auto material = std::max(0.1, m_gears_generator_material_thickness_spin->get_value());
    const auto pair_mode = m_gears_generator_pair_switch && m_gears_generator_pair_switch->get_active();
    const auto slider_deg =
            (pair_mode && m_gears_generator_rotation_slider) ? m_gears_generator_rotation_slider->get_value() : 0.0;
    const auto angle1 = slider_deg * M_PI / 180.0;

    auto build_outline = [&](int teeth, double angle, const glm::dvec2 &center) -> std::vector<glm::dvec2> {
        GearGeneratorParams p;
        p.module = module;
        p.teeth = std::max(3, teeth);
        p.pressure_angle_deg = pressure;
        p.backlash_mm = backlash;
        p.involute_segments = segments;
        p.bore_diameter_mm = 0.0;
        std::vector<glm::dvec2> outline;
        if (!build_involute_gear_outline(p, outline))
            return {};
        for (auto &pt : outline)
            pt = rotate_point_2d(pt, {0, 0}, angle) + center;
        return outline;
    };

    if (!pair_mode) {
        const auto diameter1 =
                m_gears_generator_diameter1_spin ? std::max(1.0, m_gears_generator_diameter1_spin->get_value()) : module * 24.0;
        const auto teeth = std::max(3, static_cast<int>(std::lround(diameter1 / module)));
        auto outline = build_outline(teeth, 0.0, {0, 0});
        if (outline.empty())
            return false;
        append_closed_polyline(polylines, std::move(outline));
        append_gear_hole_polylines(polylines, static_cast<int>(m_gears_hole_mode), bore, material, {0, 0}, 0.0, segments * 4);
        return !polylines.empty();
    }

    if (!m_gears_generator_diameter1_spin || !m_gears_generator_ratio_spin)
        return false;

    const auto diameter1 = std::max(1.0, m_gears_generator_diameter1_spin->get_value());
    auto ratio_value = std::max(0.001, m_gears_generator_ratio_spin->get_value());
    if (m_gears_generator_ratio_degrees_switch && m_gears_generator_ratio_degrees_switch->get_active())
        ratio_value /= 360.0;
    ratio_value = std::clamp(ratio_value, 0.01, 100.0);

    const auto z1 = std::max(3, static_cast<int>(std::lround(diameter1 / module)));
    const auto z2 = std::max(3, static_cast<int>(std::lround(static_cast<double>(z1) / ratio_value)));
    const auto r1 = module * static_cast<double>(z1) * 0.5;
    const auto r2 = module * static_cast<double>(z2) * 0.5;
    const auto c1 = glm::dvec2(-(r1 + r2) * 0.5, 0.0);
    const auto c2 = glm::dvec2((r1 + r2) * 0.5, 0.0);
    // Mesh phase: our generated outline starts with a tooth centered at +X.
    // For odd z2, the opposite side is already a gap; for even z2 we shift by half tooth.
    const auto mesh_phase = (z2 % 2 == 0) ? (M_PI / static_cast<double>(z2)) : 0.0;
    const auto angle2 = -angle1 * static_cast<double>(z1) / static_cast<double>(z2) + mesh_phase;

    auto outline1 = build_outline(z1, angle1, c1);
    auto outline2 = build_outline(z2, angle2, c2);
    if (outline1.empty() || outline2.empty())
        return false;
    append_closed_polyline(polylines, std::move(outline1));
    append_closed_polyline(polylines, std::move(outline2));
    append_gear_hole_polylines(polylines, static_cast<int>(m_gears_hole_mode), bore, material, c1, angle1, segments * 4);
    append_gear_hole_polylines(polylines, static_cast<int>(m_gears_hole_mode), bore, material, c2, angle2, segments * 4);
    return !polylines.empty();
#else
    (void)polylines;
    return false;
#endif
}

void Editor::update_gears_generator_preview()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_gears_generator_preview_area)
        return;
    build_gears_generator_polylines(m_gears_generator_preview_polylines);
    m_gears_generator_preview_area->queue_draw();
#endif
}

void Editor::draw_gears_generator_preview(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h)
{
#ifdef DUNE_SKETCHER_ONLY
    cr->save();
    cr->set_source_rgb(0.96, 0.96, 0.96);
    cr->paint();

    if (m_gears_generator_preview_polylines.empty()) {
        cr->set_source_rgb(0.35, 0.35, 0.35);
        cr->set_line_width(1.0);
        cr->move_to(18, 28);
        cr->show_text("No preview");
        cr->restore();
        return;
    }

    glm::dvec2 bb_min{std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    glm::dvec2 bb_max{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
    for (const auto &poly : m_gears_generator_preview_polylines) {
        for (const auto &p : poly) {
            bb_min = glm::min(bb_min, p);
            bb_max = glm::max(bb_max, p);
        }
    }
    const auto size = glm::max(bb_max - bb_min, glm::dvec2(1e-6, 1e-6));
    const auto margin = 18.0;
    const auto sx = (static_cast<double>(w) - margin * 2.0) / size.x;
    const auto sy = (static_cast<double>(h) - margin * 2.0) / size.y;
    const auto scale = std::max(1e-6, std::min(sx, sy));
    const auto center = (bb_min + bb_max) * 0.5;

    cr->translate(w * 0.5, h * 0.5);
    cr->scale(scale, -scale);
    cr->translate(-center.x, -center.y);

    cr->set_source_rgb(0.1, 0.1, 0.1);
    cr->set_line_width(1.2 / scale);
    for (const auto &poly : m_gears_generator_preview_polylines) {
        if (poly.size() < 2)
            continue;
        cr->move_to(poly.front().x, poly.front().y);
        for (size_t i = 1; i < poly.size(); i++)
            cr->line_to(poly.at(i).x, poly.at(i).y);
        cr->close_path();
    }
    cr->stroke();
    cr->restore();
#else
    (void)cr;
    (void)w;
    (void)h;
#endif
}

bool Editor::import_gears_generator_to_document()
{
#ifdef DUNE_SKETCHER_ONLY
    std::vector<std::vector<glm::dvec2>> polylines;
    if (!build_gears_generator_polylines(polylines)) {
        if (m_workspace_browser)
            m_workspace_browser->show_toast("Generator preview is empty");
        return false;
    }
    const auto pair_mode = m_gears_generator_pair_switch && m_gears_generator_pair_switch->get_active();
    bool imported = false;
    if (pair_mode && polylines.size() >= 2) {
        std::vector<std::vector<std::vector<glm::dvec2>>> groups(2);
        groups.at(0).push_back(polylines.at(0));
        groups.at(1).push_back(polylines.at(1));

        const auto centroid = [](const std::vector<glm::dvec2> &poly) {
            glm::dvec2 c{0, 0};
            if (poly.empty())
                return c;
            for (const auto &p : poly)
                c += p;
            return c / static_cast<double>(poly.size());
        };
        const auto c0 = centroid(polylines.at(0));
        const auto c1 = centroid(polylines.at(1));
        for (size_t i = 2; i < polylines.size(); i++) {
            const auto c = centroid(polylines.at(i));
            const auto d0 = glm::length(c - c0);
            const auto d1 = glm::length(c - c1);
            groups.at((d0 <= d1) ? 0 : 1).push_back(polylines.at(i));
        }

        imported = commit_generator_polyline_groups(groups, true, "gear generator import", true);
    }
    else {
        imported = commit_generator_polylines(polylines, true, "gear generator import", true);
    }

    if (!imported) {
        if (m_workspace_browser)
            m_workspace_browser->show_toast("Couldn't import generator output");
        return false;
    }
    return true;
#else
    return false;
#endif
}

void Editor::rebuild_joints_family_dropdown()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_joints_category_dropdown || !m_joints_family_dropdown || g_joint_families.empty())
        return;

    const auto &categories = joint_ui_categories();
    const auto category_index =
            static_cast<size_t>(std::min<unsigned int>(m_joints_category_dropdown->get_selected(), categories.size() - 1));
    const auto &category = categories.at(category_index);
    if (m_joints_category_label)
        m_joints_category_label->set_text(category.label);
    if (m_joints_category_prev_button)
        m_joints_category_prev_button->set_sensitive(categories.size() > 1);
    if (m_joints_category_next_button)
        m_joints_category_next_button->set_sensitive(categories.size() > 1);

    auto family_model = Gtk::StringList::create();
    m_joints_visible_family_indices.clear();
    for (size_t i = 0; i < g_joint_families.size(); i++) {
        const auto &family = g_joint_families.at(i);
        if (joint_ui_category_for_family_id(family.id).id != category.id)
            continue;
        family_model->append(family.label);
        m_joints_visible_family_indices.push_back(static_cast<unsigned int>(i));
    }
    if (m_joints_visible_family_indices.empty()) {
        for (size_t i = 0; i < g_joint_families.size(); i++) {
            family_model->append(g_joint_families.at(i).label);
            m_joints_visible_family_indices.push_back(static_cast<unsigned int>(i));
        }
    }

    const auto last_family_id = m_joints_setting_values["__joint_family__"];
    size_t selected_index = 0;
    if (!last_family_id.empty()) {
        for (size_t i = 0; i < m_joints_visible_family_indices.size(); i++) {
            if (g_joint_families.at(m_joints_visible_family_indices.at(i)).id == last_family_id) {
                selected_index = i;
                break;
            }
        }
    }

    m_joints_family_dropdown->set_model(family_model);
    m_joints_family_dropdown->set_selected(static_cast<unsigned int>(std::min(selected_index, m_joints_visible_family_indices.size() - 1)));
    if (const auto *family = get_selected_joint_family(m_joints_family_dropdown, m_joints_visible_family_indices)) {
        m_joints_setting_values["__joint_family__"] = family->id;
        if (m_joints_family_label)
            m_joints_family_label->set_text(family->label);
        if (m_joints_family_description_label)
            m_joints_family_description_label->set_text(family->description);
    }
    if (m_joints_family_prev_button)
        m_joints_family_prev_button->set_sensitive(m_joints_visible_family_indices.size() > 1);
    if (m_joints_family_next_button)
        m_joints_family_next_button->set_sensitive(m_joints_visible_family_indices.size() > 1);
#endif
}

void Editor::rebuild_joints_role_dropdowns()
{
#ifdef DUNE_SKETCHER_ONLY
    const auto *family = get_selected_joint_family(m_joints_family_dropdown, m_joints_visible_family_indices);
    if (!family || !m_joints_role_dropdown)
        return;

    m_updating_joints_role_controls = true;
    auto role_model = Gtk::StringList::create();
    for (const auto &role : family->roles)
        role_model->append(role.label);
    m_joints_role_dropdown->set_model(role_model);

    const auto last_key = joints_value_key(family->id, "__role__");
    size_t selected_index = 0;
    if (const auto it = m_joints_setting_values.find(last_key); it != m_joints_setting_values.end()) {
        const auto role_it = std::find_if(family->roles.begin(), family->roles.end(),
                                          [&id = it->second](const auto &role) { return role.id == id; });
        if (role_it != family->roles.end())
            selected_index = static_cast<size_t>(std::distance(family->roles.begin(), role_it));
    }
    selected_index = std::min(selected_index, family->roles.size() - 1);
    m_joints_role_dropdown->set_selected(static_cast<unsigned int>(selected_index));
    m_joints_setting_values[last_key] = family->roles.at(selected_index).id;
    if (m_joints_quick_role_label)
        m_joints_quick_role_label->set_text(family->roles.at(selected_index).label);
    if (m_joints_role_label)
        m_joints_role_label->set_text(family->roles.at(selected_index).label);
    if (m_joints_quick_role_prev_button)
        m_joints_quick_role_prev_button->set_sensitive(family->roles.size() > 1);
    if (m_joints_quick_role_next_button)
        m_joints_quick_role_next_button->set_sensitive(family->roles.size() > 1);
    if (m_joints_role_prev_button)
        m_joints_role_prev_button->set_sensitive(family->roles.size() > 1);
    if (m_joints_role_next_button)
        m_joints_role_next_button->set_sensitive(family->roles.size() > 1);
    m_updating_joints_role_controls = false;
#endif
}

void Editor::rebuild_joints_settings_ui()
{
#ifdef DUNE_SKETCHER_ONLY
    const auto *family = get_selected_joint_family(m_joints_family_dropdown, m_joints_visible_family_indices);
    if (!m_joints_settings_box || !family)
        return;

    m_joints_rebuilding_settings = true;
    while (auto *child = m_joints_settings_box->get_first_child())
        m_joints_settings_box->remove(*child);

    m_joints_setting_rows.clear();
    m_joints_spin_settings.clear();
    m_joints_entry_settings.clear();
    m_joints_dropdown_settings.clear();
    m_joints_switch_settings.clear();

    const auto *role = get_selected_joint_role(*family, m_joints_role_dropdown);
    if (!role) {
        m_joints_rebuilding_settings = false;
        return;
    }
    m_joints_setting_values[joints_value_key(family->id, "__role__")] = role->id;

    const auto visible_args = collect_joint_visible_args(*family, *role);
    const auto get_value = [this, family](const SettingsArgDef &arg) {
        if (const auto it = m_joints_setting_values.find(joints_value_key(family->id, arg.dest)); it != m_joints_setting_values.end())
            return it->second;
        return arg.default_string;
    };
    const auto save_value = [this, family](const std::string &dest, std::string value) {
        m_joints_setting_values[joints_value_key(family->id, dest)] = std::move(value);
    };

    for (const auto &group : family->arg_groups) {
        auto frame = Gtk::make_managed<Gtk::Frame>(group.title);
        auto group_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        group_box->set_margin_start(8);
        group_box->set_margin_end(8);
        group_box->set_margin_top(8);
        group_box->set_margin_bottom(8);
        frame->set_child(*group_box);

        bool has_rows = false;
        for (const auto &dest : group.args) {
            const auto it = std::find_if(visible_args.begin(), visible_args.end(),
                                         [&dest](const auto &arg) { return arg.dest == dest; });
            if (it == visible_args.end())
                continue;
            const auto &arg = *it;

            auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
            auto label = Gtk::make_managed<Gtk::Label>(arg.label);
            label->set_hexpand(true);
            label->set_xalign(0.0f);
            label->set_tooltip_text(arg.help);
            row->append(*label);

            if (arg.kind == SettingsArgKind::FLOAT || arg.kind == SettingsArgKind::INT) {
                const auto digits = arg.kind == SettingsArgKind::INT ? 0 : 3;
                const auto step = arg.kind == SettingsArgKind::INT ? 1.0 : 0.1;
                auto spin = Gtk::make_managed<Gtk::SpinButton>();
                spin->set_digits(digits);
                spin->set_numeric(true);
                spin->set_range(-1000000.0, 1000000.0);
                spin->set_increments(step, step * 10.0);
                spin->set_width_chars(8);
                try {
                    spin->set_value(std::stod(get_value(arg)));
                }
                catch (...) {
                    spin->set_value(arg.kind == SettingsArgKind::INT ? static_cast<double>(arg.default_int) : arg.default_float);
                }
                spin->signal_value_changed().connect([this, save_value, dest = arg.dest, digits, spin] {
                    if (m_joints_rebuilding_settings)
                        return;
                    std::ostringstream ss;
                    ss << std::fixed << std::setprecision(digits) << spin->get_value();
                    save_value(dest, ss.str());
                    update_joints_summary();
                });
                row->append(*spin);
                m_joints_spin_settings[arg.dest] = spin;
            }
            else if (arg.kind == SettingsArgKind::BOOL) {
                auto sw = Gtk::make_managed<Gtk::Switch>();
                const auto value = get_value(arg);
                sw->set_active(value == "1" || value == "true" || (value.empty() && arg.default_bool));
                sw->property_active().signal_changed().connect([this, save_value, dest = arg.dest, sw] {
                    if (m_joints_rebuilding_settings)
                        return;
                    save_value(dest, sw->get_active() ? "1" : "0");
                    update_joints_summary();
                });
                row->append(*sw);
                m_joints_switch_settings[arg.dest] = sw;
            }
            else if (arg.kind == SettingsArgKind::CHOICE) {
                auto model = Gtk::StringList::create();
                for (const auto &choice : arg.choices)
                    model->append(choice);
                auto dropdown = Gtk::make_managed<Gtk::DropDown>(model, nullptr);
                auto value = get_value(arg);
                auto pos = std::find(arg.choices.begin(), arg.choices.end(), value);
                if (pos == arg.choices.end())
                    pos = arg.choices.begin();
                if (pos != arg.choices.end())
                    dropdown->set_selected(static_cast<unsigned int>(std::distance(arg.choices.begin(), pos)));
                dropdown->property_selected().signal_changed().connect([this, save_value, arg, dropdown] {
                    if (m_joints_rebuilding_settings)
                        return;
                    if (!arg.choices.empty()) {
                        const auto idx = std::min<size_t>(dropdown->get_selected(), arg.choices.size() - 1);
                        save_value(arg.dest, arg.choices.at(idx));
                    }
                    update_joints_summary();
                });
                row->append(*dropdown);
                m_joints_dropdown_settings[arg.dest] = dropdown;
            }
            else {
                auto entry = Gtk::make_managed<Gtk::Entry>();
                entry->set_hexpand(true);
                entry->set_width_chars(12);
                entry->set_text(get_value(arg));
                entry->signal_changed().connect([this, save_value, dest = arg.dest, entry] {
                    if (m_joints_rebuilding_settings)
                        return;
                    save_value(dest, entry->get_text());
                    update_joints_summary();
                });
                row->append(*entry);
                m_joints_entry_settings[arg.dest] = entry;
            }

            m_joints_setting_rows[arg.dest] = row;
            group_box->append(*row);
            has_rows = true;
        }

        if (has_rows)
            m_joints_settings_box->append(*frame);
    }

    const auto append_edge_extra_group = [this, &family, &visible_args, &get_value, &save_value](const JointEdgeDef *edge) {
        if (!edge || edge->extra_args.empty())
            return;
        auto frame = Gtk::make_managed<Gtk::Frame>(edge->label);
        auto group_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        group_box->set_margin_start(8);
        group_box->set_margin_end(8);
        group_box->set_margin_top(8);
        group_box->set_margin_bottom(8);
        frame->set_child(*group_box);

        for (const auto &arg : edge->extra_args) {
            if (std::none_of(visible_args.begin(), visible_args.end(), [&arg](const auto &existing) { return existing.dest == arg.dest; }))
                continue;
            auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
            auto label = Gtk::make_managed<Gtk::Label>(arg.label);
            label->set_hexpand(true);
            label->set_xalign(0.0f);
            label->set_tooltip_text(arg.help);
            row->append(*label);
            if (arg.kind == SettingsArgKind::FLOAT || arg.kind == SettingsArgKind::INT) {
                const auto digits = arg.kind == SettingsArgKind::INT ? 0 : 3;
                const auto step = arg.kind == SettingsArgKind::INT ? 1.0 : 0.1;
                auto spin = Gtk::make_managed<Gtk::SpinButton>();
                spin->set_digits(digits);
                spin->set_numeric(true);
                spin->set_range(-1000000.0, 1000000.0);
                spin->set_increments(step, step * 10.0);
                spin->set_width_chars(8);
                try {
                    spin->set_value(std::stod(get_value(arg)));
                }
                catch (...) {
                    spin->set_value(arg.kind == SettingsArgKind::INT ? static_cast<double>(arg.default_int) : arg.default_float);
                }
                spin->signal_value_changed().connect([this, save_value, dest = arg.dest, digits, spin] {
                    if (m_joints_rebuilding_settings)
                        return;
                    std::ostringstream ss;
                    ss << std::fixed << std::setprecision(digits) << spin->get_value();
                    save_value(dest, ss.str());
                    update_joints_summary();
                });
                row->append(*spin);
                m_joints_spin_settings[arg.dest] = spin;
            }
            group_box->append(*row);
        }
        m_joints_settings_box->append(*frame);
    };
    append_edge_extra_group(find_joint_edge(*family, role->line0_edge));
    if (role->pair)
        append_edge_extra_group(find_joint_edge(*family, role->line1_edge));

    if (!family->description.empty()) {
        auto description = Gtk::make_managed<Gtk::Label>(family->description);
        description->set_wrap(true);
        description->set_xalign(0.0f);
        description->add_css_class("dim-label");
        m_joints_settings_box->append(*description);
    }

    m_joints_rebuilding_settings = false;
#endif
}

void Editor::sync_joints_popover_controls()
{
#ifdef DUNE_SKETCHER_ONLY
    if (m_updating_joints_ui || m_joints_rebuilding_settings)
        return;
    m_updating_joints_ui = true;
    std::string error;
    if (ensure_joint_families_loaded(error)) {
        rebuild_joints_family_dropdown();
        rebuild_joints_role_dropdowns();
        rebuild_joints_settings_ui();
    }
    m_updating_joints_ui = false;
    update_joints_summary();
#endif
}

void Editor::update_joints_summary()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_joints_summary_label)
        return;

    std::string error;
    if (!ensure_joint_families_loaded(error)) {
        m_joints_summary_label->set_text(error);
        return;
    }
    if (g_joint_families.empty()) {
        m_joints_summary_label->set_text("No joint families");
        return;
    }

    const auto *family = get_selected_joint_family(m_joints_family_dropdown, m_joints_visible_family_indices);
    if (!family) {
        m_joints_summary_label->set_text("No edge feature family");
        return;
    }
    const auto *role = get_selected_joint_role(*family, m_joints_role_dropdown);
    if (!role) {
        m_joints_summary_label->set_text("No operation");
        return;
    }

    std::ostringstream ss;
    ss << family->label << "  " << role->label << "  thickness "
       << std::fixed << std::setprecision(2) << (m_joints_thickness_spin ? m_joints_thickness_spin->get_value() : 3.0)
       << " mm  burn " << (m_joints_burn_spin ? m_joints_burn_spin->get_value() : 0.1) << " mm";
    if (!family->description.empty())
        ss << "  " << family->description;
    m_joints_summary_label->set_text(ss.str());
    if (m_joints_family_description_label)
        m_joints_family_description_label->set_text(family->description);
    JointSelectionState selection_state;
    if (m_core.has_documents()) {
        auto &doc = m_core.get_current_document();
        selection_state =
                evaluate_joint_selection_state(doc, m_core.get_current_group(), get_canvas().get_selection(), *family, *role);
    }
    else {
        selection_state = {false, role->pair ? "Select first edge." : "Select one or more lines."};
    }
    if (m_joints_selection_hint_label) {
        auto hint = joint_selection_hint(*family, *role);
        if (!selection_state.text.empty())
            hint += "\n" + selection_state.text;
        m_joints_selection_hint_label->set_text(hint);
    }
    if (m_joints_quick_hint_label)
        m_joints_quick_hint_label->set_text(selection_state.text);
    if (m_joints_apply_button)
        m_joints_apply_button->set_sensitive(selection_state.ready);
    if (m_joints_quick_apply_button)
        m_joints_quick_apply_button->set_sensitive(selection_state.ready);
    const auto can_swap = find_swapped_joint_role(*family, *role) != nullptr;
    if (m_joints_swap_roles_button)
        m_joints_swap_roles_button->set_sensitive(can_swap);
    if (m_joints_quick_swap_roles_button)
        m_joints_quick_swap_roles_button->set_sensitive(can_swap);
    const auto side_mode = joint_side_mode_from_index(m_joints_side_dropdown ? m_joints_side_dropdown->get_selected() : 0);
    if (m_joints_quick_side_label)
        m_joints_quick_side_label->set_text(joint_side_mode_label(side_mode));
    if (m_joints_side_label)
        m_joints_side_label->set_text(joint_side_mode_label(side_mode));
#endif
}

void Editor::init_joints_popover()
{
#ifdef DUNE_SKETCHER_ONLY
    m_joints_button = Gtk::make_managed<Gtk::Button>();
    m_joints_button->set_icon_name("insert-link-symbolic");
    m_joints_button->set_tooltip_text("Edge Tools: joints, hinges, lids, grooves, and utility edge cuts");
    m_joints_button->set_has_frame(true);
    m_joints_button->set_focusable(false);
    m_joints_button->add_css_class("sketch-toolbar-button");

    m_joints_popover = Gtk::make_managed<Gtk::Popover>();
    m_joints_popover->set_has_arrow(true);
    m_joints_popover->set_autohide(false);
    m_joints_popover->add_css_class("sketch-grid-popover");
    m_joints_popover->set_parent(*m_joints_button);
    m_joints_popover->set_size_request(edge_features_popover_total_width, -1);
    install_hover_popover(*m_joints_button, *m_joints_popover, [this] { return !m_primary_button_pressed; },
                          [this] { return m_right_click_popovers_only; });
    m_joints_popover->signal_show().connect([this] {
        sync_joints_popover_controls();
        update_sketcher_toolbar_button_states();
    });
    m_joints_popover->signal_hide().connect([this] { update_sketcher_toolbar_button_states(); });

    auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    root->set_margin_start(12);
    root->set_margin_end(12);
    root->set_margin_top(12);
    root->set_margin_bottom(12);
    root->set_size_request(edge_features_popover_content_width, -1);
    m_joints_popover->set_child(*root);

    auto add_spin_row = [](Gtk::Box &parent, const char *label_text, Gtk::SpinButton *&spin, int digits, double min, double max,
                           double step, const char *unit = nullptr) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto label = Gtk::make_managed<Gtk::Label>(label_text);
        label->set_hexpand(true);
        label->set_xalign(0);
        spin = Gtk::make_managed<Gtk::SpinButton>();
        spin->set_digits(digits);
        spin->set_numeric(true);
        spin->set_range(min, max);
        spin->set_increments(step, step * 10.0);
        spin->set_width_chars(6);
        row->append(*label);
        row->append(*spin);
        if (unit) {
            auto unit_label = Gtk::make_managed<Gtk::Label>(unit);
            unit_label->add_css_class("dim-label");
            row->append(*unit_label);
        }
        parent.append(*row);
    };
    auto add_dropdown_row = [](Gtk::Box &parent, const char *label_text, Gtk::DropDown *&dropdown,
                               std::initializer_list<const char *> items) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto label = Gtk::make_managed<Gtk::Label>(label_text);
        label->set_hexpand(true);
        label->set_xalign(0);
        auto model = Gtk::StringList::create();
        for (const auto *item : items)
            model->append(item);
        dropdown = Gtk::make_managed<Gtk::DropDown>(model, nullptr);
        row->append(*label);
        row->append(*dropdown);
        parent.append(*row);
    };
    auto add_dropdown_row_model = [](Gtk::Box &parent, const char *label_text, Gtk::DropDown *&dropdown,
                                     const Glib::RefPtr<Gtk::StringList> &model) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto label = Gtk::make_managed<Gtk::Label>(label_text);
        label->set_hexpand(true);
        label->set_xalign(0);
        dropdown = Gtk::make_managed<Gtk::DropDown>(model, nullptr);
        row->append(*label);
        row->append(*dropdown);
        parent.append(*row);
    };
    auto add_switcher_row = [](Gtk::Box &parent, const char *label_text, Gtk::Button *&prev_button, Gtk::Label *&value_label,
                               Gtk::Button *&next_button) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto label = Gtk::make_managed<Gtk::Label>(label_text);
        label->set_hexpand(true);
        label->set_xalign(0);
        prev_button = Gtk::make_managed<Gtk::Button>();
        prev_button->set_icon_name("go-previous-symbolic");
        prev_button->set_has_frame(false);
        prev_button->set_focusable(false);
        value_label = Gtk::make_managed<Gtk::Label>("-");
        value_label->set_width_chars(8);
        value_label->set_xalign(0.5f);
        value_label->set_ellipsize(Pango::EllipsizeMode::END);
        next_button = Gtk::make_managed<Gtk::Button>();
        next_button->set_icon_name("go-next-symbolic");
        next_button->set_has_frame(false);
        next_button->set_focusable(false);
        row->append(*label);
        row->append(*prev_button);
        row->append(*value_label);
        row->append(*next_button);
        parent.append(*row);
    };
    auto add_switch_row = [](Gtk::Box &parent, const char *label_text, Gtk::Switch *&sw) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto label = Gtk::make_managed<Gtk::Label>(label_text);
        label->set_hexpand(true);
        label->set_xalign(0);
        sw = Gtk::make_managed<Gtk::Switch>();
        row->append(*label);
        row->append(*sw);
        parent.append(*row);
    };

    std::string joints_error;
    ensure_joint_families_loaded(joints_error);
    auto category_model = Gtk::StringList::create();
    for (const auto &category : joint_ui_categories())
        category_model->append(category.label);
    auto family_model = Gtk::StringList::create();
    if (g_joint_families.empty()) {
        family_model->append("Edge Features");
    }
    else {
        for (const auto &family : g_joint_families)
            family_model->append(family.label);
    }

    m_joints_category_dropdown = Gtk::make_managed<Gtk::DropDown>(category_model, nullptr);
    m_joints_family_dropdown = Gtk::make_managed<Gtk::DropDown>(family_model, nullptr);
    auto role_model = Gtk::StringList::create();
    role_model->append("Edge");
    m_joints_role_dropdown = Gtk::make_managed<Gtk::DropDown>(role_model, nullptr);
    add_switcher_row(*root, "Category", m_joints_category_prev_button, m_joints_category_label, m_joints_category_next_button);
    add_switcher_row(*root, "Family", m_joints_family_prev_button, m_joints_family_label, m_joints_family_next_button);
    add_switcher_row(*root, "Operation", m_joints_role_prev_button, m_joints_role_label, m_joints_role_next_button);
    auto side_model = Gtk::StringList::create();
    side_model->append("Auto");
    side_model->append("Inside");
    side_model->append("Outside");
    m_joints_side_dropdown = Gtk::make_managed<Gtk::DropDown>(side_model, nullptr);
    add_switcher_row(*root, "Side", m_joints_side_prev_button, m_joints_side_label, m_joints_side_next_button);
    add_switch_row(*root, "Flip direction", m_joints_flip_direction_switch);
    add_spin_row(*root, "Thickness", m_joints_thickness_spin, 2, 0.1, 100.0, 0.1, "mm");
    add_spin_row(*root, "Burn", m_joints_burn_spin, 3, 0.0, 3.0, 0.01, "mm");
    m_joints_swap_roles_button = Gtk::make_managed<Gtk::Button>("Swap roles");
    m_joints_swap_roles_button->set_has_frame(true);
    m_joints_swap_roles_button->set_tooltip_text("Swap the first and second edge roles for pair operations");
    root->append(*m_joints_swap_roles_button);

    m_joints_family_description_label = Gtk::make_managed<Gtk::Label>();
    m_joints_family_description_label->set_xalign(0);
    m_joints_family_description_label->set_wrap(true);
    m_joints_family_description_label->set_max_width_chars(24);
    m_joints_family_description_label->add_css_class("dim-label");
    root->append(*m_joints_family_description_label);

    m_joints_selection_hint_label = Gtk::make_managed<Gtk::Label>();
    m_joints_selection_hint_label->set_xalign(0);
    m_joints_selection_hint_label->set_wrap(true);
    m_joints_selection_hint_label->set_max_width_chars(24);
    m_joints_selection_hint_label->add_css_class("dim-label");
    root->append(*m_joints_selection_hint_label);

    m_joints_summary_label = Gtk::make_managed<Gtk::Label>();
    m_joints_summary_label->set_xalign(0);
    m_joints_summary_label->set_wrap(true);
    m_joints_summary_label->set_max_width_chars(24);
    m_joints_summary_label->add_css_class("dim-label");
    root->append(*m_joints_summary_label);

    m_joints_advanced_expander = Gtk::make_managed<Gtk::Expander>("Advanced");
    m_joints_advanced_expander->set_expanded(false);
    auto advanced_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    advanced_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    advanced_scroll->set_min_content_height(160);
    m_joints_settings_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    m_joints_settings_box->set_margin_top(8);
    advanced_scroll->set_child(*m_joints_settings_box);
    m_joints_advanced_expander->set_child(*advanced_scroll);
    root->append(*m_joints_advanced_expander);

    if (m_joints_category_dropdown)
        m_joints_category_dropdown->set_selected(0);
    m_joints_family_dropdown->set_selected(0);
    if (m_joints_side_dropdown)
        m_joints_side_dropdown->set_selected(0);
    m_joints_thickness_spin->set_value(3.0);
    m_joints_burn_spin->set_value(0.1);
    if (m_joints_flip_direction_switch)
        m_joints_flip_direction_switch->set_active(false);

    m_joints_quick_popover = Gtk::make_managed<Gtk::Popover>();
    m_joints_quick_popover->set_has_arrow(true);
    m_joints_quick_popover->set_autohide(false);
    m_joints_quick_popover->add_css_class("sketch-grid-popover");
    m_joints_quick_popover->set_parent(get_canvas());
    m_joints_quick_popover->set_position(Gtk::PositionType::RIGHT);
    m_joints_quick_popover->set_offset(10, 10);

    auto quick_root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    quick_root->set_margin_start(10);
    quick_root->set_margin_end(10);
    quick_root->set_margin_top(10);
    quick_root->set_margin_bottom(10);
    quick_root->set_size_request(160, -1);
    m_joints_quick_popover->set_child(*quick_root);

    auto quick_role_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto quick_role_title = Gtk::make_managed<Gtk::Label>("Operation");
    quick_role_title->set_hexpand(true);
    quick_role_title->set_xalign(0);
    m_joints_quick_role_prev_button = Gtk::make_managed<Gtk::Button>();
    m_joints_quick_role_prev_button->set_icon_name("go-previous-symbolic");
    m_joints_quick_role_prev_button->set_has_frame(false);
    m_joints_quick_role_prev_button->set_focusable(false);
    m_joints_quick_role_label = Gtk::make_managed<Gtk::Label>("Edge");
    m_joints_quick_role_label->set_hexpand(true);
    m_joints_quick_role_label->set_xalign(0.5f);
    m_joints_quick_role_label->set_ellipsize(Pango::EllipsizeMode::END);
    m_joints_quick_role_next_button = Gtk::make_managed<Gtk::Button>();
    m_joints_quick_role_next_button->set_icon_name("go-next-symbolic");
    m_joints_quick_role_next_button->set_has_frame(false);
    m_joints_quick_role_next_button->set_focusable(false);
    quick_role_row->append(*quick_role_title);
    quick_role_row->append(*m_joints_quick_role_prev_button);
    quick_role_row->append(*m_joints_quick_role_label);
    quick_role_row->append(*m_joints_quick_role_next_button);
    quick_root->append(*quick_role_row);
    auto quick_side_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto quick_side_title = Gtk::make_managed<Gtk::Label>("Side");
    quick_side_title->set_hexpand(true);
    quick_side_title->set_xalign(0);
    m_joints_quick_side_prev_button = Gtk::make_managed<Gtk::Button>();
    m_joints_quick_side_prev_button->set_icon_name("go-previous-symbolic");
    m_joints_quick_side_prev_button->set_has_frame(false);
    m_joints_quick_side_prev_button->set_focusable(false);
    m_joints_quick_side_label = Gtk::make_managed<Gtk::Label>("Auto");
    m_joints_quick_side_label->set_hexpand(true);
    m_joints_quick_side_label->set_xalign(0.5f);
    m_joints_quick_side_next_button = Gtk::make_managed<Gtk::Button>();
    m_joints_quick_side_next_button->set_icon_name("go-next-symbolic");
    m_joints_quick_side_next_button->set_has_frame(false);
    m_joints_quick_side_next_button->set_focusable(false);
    quick_side_row->append(*quick_side_title);
    quick_side_row->append(*m_joints_quick_side_prev_button);
    quick_side_row->append(*m_joints_quick_side_label);
    quick_side_row->append(*m_joints_quick_side_next_button);
    quick_root->append(*quick_side_row);

    auto quick_flip_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto quick_flip_label = Gtk::make_managed<Gtk::Label>("Flip direction");
    quick_flip_label->set_hexpand(true);
    quick_flip_label->set_xalign(0);
    m_joints_quick_flip_direction_switch = Gtk::make_managed<Gtk::Switch>();
    quick_flip_row->append(*quick_flip_label);
    quick_flip_row->append(*m_joints_quick_flip_direction_switch);
    quick_root->append(*quick_flip_row);

    m_joints_quick_swap_roles_button = Gtk::make_managed<Gtk::Button>("Swap roles");
    m_joints_quick_swap_roles_button->set_has_frame(true);
    m_joints_quick_swap_roles_button->set_tooltip_text("Swap the first and second edge roles");
    quick_root->append(*m_joints_quick_swap_roles_button);

    m_joints_quick_apply_button = Gtk::make_managed<Gtk::Button>("Apply");
    m_joints_quick_apply_button->set_has_frame(true);
    m_joints_quick_apply_button->add_css_class("suggested-action");
    m_joints_quick_apply_button->set_tooltip_text("Apply the current Edge Feature to the selected edges");
    quick_root->append(*m_joints_quick_apply_button);
    m_joints_quick_hint_label = Gtk::make_managed<Gtk::Label>();
    m_joints_quick_hint_label->set_xalign(0);
    m_joints_quick_hint_label->set_wrap(true);
    m_joints_quick_hint_label->add_css_class("dim-label");
    quick_root->append(*m_joints_quick_hint_label);
    m_joints_quick_apply_button->signal_clicked().connect([this] {
        if (!generate_joints_from_selected_lines()) {
            if (m_workspace_browser)
                m_workspace_browser->show_toast("Select a valid edge selection for Edge Features");
            return;
        }
        update_joints_quick_popover();
    });

    auto sync_joints_controls = [this](unsigned int side_idx, bool flip_direction) {
        if (m_updating_joints_side_controls)
            return;
        m_updating_joints_side_controls = true;
        if (m_joints_side_dropdown && m_joints_side_dropdown->get_selected() != side_idx)
            m_joints_side_dropdown->set_selected(side_idx);
        if (m_joints_quick_side_label)
            m_joints_quick_side_label->set_text(joint_side_mode_label(joint_side_mode_from_index(side_idx)));
        if (m_joints_side_label)
            m_joints_side_label->set_text(joint_side_mode_label(joint_side_mode_from_index(side_idx)));
        if (m_joints_flip_direction_switch && m_joints_flip_direction_switch->get_active() != flip_direction)
            m_joints_flip_direction_switch->set_active(flip_direction);
        if (m_joints_quick_flip_direction_switch && m_joints_quick_flip_direction_switch->get_active() != flip_direction)
            m_joints_quick_flip_direction_switch->set_active(flip_direction);
        m_updating_joints_side_controls = false;
        update_joints_summary();
    };
    if (m_joints_side_dropdown) {
        m_joints_side_dropdown->property_selected().signal_changed().connect([this, sync_joints_controls] {
            sync_joints_controls(m_joints_side_dropdown->get_selected(),
                                 m_joints_flip_direction_switch && m_joints_flip_direction_switch->get_active());
        });
    }
    if (m_joints_flip_direction_switch) {
        m_joints_flip_direction_switch->property_active().signal_changed().connect([this, sync_joints_controls] {
            sync_joints_controls(m_joints_side_dropdown ? m_joints_side_dropdown->get_selected() : 0,
                                 m_joints_flip_direction_switch->get_active());
        });
    }
    if (m_joints_quick_flip_direction_switch) {
        m_joints_quick_flip_direction_switch->property_active().signal_changed().connect([this, sync_joints_controls] {
            sync_joints_controls(m_joints_side_dropdown ? m_joints_side_dropdown->get_selected() : 0,
                                 m_joints_quick_flip_direction_switch->get_active());
        });
    }
    auto cycle_quick_side = [this, sync_joints_controls](int delta) {
        const auto current = static_cast<int>(m_joints_side_dropdown ? m_joints_side_dropdown->get_selected() : 0);
        const auto next = (current + delta + 3) % 3;
        sync_joints_controls(static_cast<unsigned int>(next),
                             m_joints_flip_direction_switch && m_joints_flip_direction_switch->get_active());
    };
    if (m_joints_side_prev_button)
        m_joints_side_prev_button->signal_clicked().connect([cycle_quick_side] { cycle_quick_side(-1); });
    if (m_joints_side_next_button)
        m_joints_side_next_button->signal_clicked().connect([cycle_quick_side] { cycle_quick_side(1); });
    if (m_joints_quick_side_prev_button)
        m_joints_quick_side_prev_button->signal_clicked().connect([cycle_quick_side] { cycle_quick_side(-1); });
    if (m_joints_quick_side_next_button)
        m_joints_quick_side_next_button->signal_clicked().connect([cycle_quick_side] { cycle_quick_side(1); });
    sync_joints_controls(0, false);

    auto sync_joints_role = [this](unsigned int role_idx) {
        if (m_updating_joints_role_controls)
            return;
        m_updating_joints_role_controls = true;
        if (m_joints_role_dropdown && m_joints_role_dropdown->get_selected() != role_idx)
            m_joints_role_dropdown->set_selected(role_idx);
        if (const auto *family = get_selected_joint_family(m_joints_family_dropdown, m_joints_visible_family_indices)) {
            if (m_joints_quick_role_label && role_idx < family->roles.size()) {
                m_joints_quick_role_label->set_text(family->roles.at(role_idx).label);
                m_joints_setting_values[joints_value_key(family->id, "__role__")] = family->roles.at(role_idx).id;
            }
        }
        m_updating_joints_role_controls = false;
        rebuild_joints_settings_ui();
        update_joints_summary();
    };
    m_joints_role_dropdown->property_selected().signal_changed().connect(
            [this, sync_joints_role] { sync_joints_role(m_joints_role_dropdown->get_selected()); });
    auto cycle_quick_role = [this, sync_joints_role](int delta) {
        std::string error;
        if (!ensure_joint_families_loaded(error) || !m_joints_family_dropdown || !m_joints_role_dropdown || g_joint_families.empty())
            return;
        const auto *family = get_selected_joint_family(m_joints_family_dropdown, m_joints_visible_family_indices);
        if (!family || family->roles.empty())
            return;
        const auto count = static_cast<int>(family->roles.size());
        const auto current = static_cast<int>(std::min<size_t>(m_joints_role_dropdown->get_selected(), family->roles.size() - 1));
        const auto next = (current + delta + count) % count;
        sync_joints_role(static_cast<unsigned int>(next));
    };
    if (m_joints_quick_role_prev_button) {
        m_joints_quick_role_prev_button->signal_clicked().connect([cycle_quick_role] { cycle_quick_role(-1); });
    }
    if (m_joints_quick_role_next_button) {
        m_joints_quick_role_next_button->signal_clicked().connect([cycle_quick_role] { cycle_quick_role(1); });
    }
    if (m_joints_role_prev_button) {
        m_joints_role_prev_button->signal_clicked().connect([cycle_quick_role] { cycle_quick_role(-1); });
    }
    if (m_joints_role_next_button) {
        m_joints_role_next_button->signal_clicked().connect([cycle_quick_role] { cycle_quick_role(1); });
    }
    auto swap_roles = [this, sync_joints_role] {
        const auto *family = get_selected_joint_family(m_joints_family_dropdown, m_joints_visible_family_indices);
        const auto *role = family ? get_selected_joint_role(*family, m_joints_role_dropdown) : nullptr;
        const auto *swapped = (family && role) ? find_swapped_joint_role(*family, *role) : nullptr;
        if (!family || !role || !swapped)
            return;
        const auto idx = static_cast<unsigned int>(std::distance(family->roles.begin(),
                                                                 std::find_if(family->roles.begin(), family->roles.end(),
                                                                              [swapped](const auto &candidate) {
                                                                                  return candidate.id == swapped->id;
                                                                              })));
        sync_joints_role(idx);
    };
    if (m_joints_swap_roles_button)
        m_joints_swap_roles_button->signal_clicked().connect(swap_roles);
    if (m_joints_quick_swap_roles_button)
        m_joints_quick_swap_roles_button->signal_clicked().connect(swap_roles);
    sync_joints_role(0);

    auto cycle_category = [this](int delta) {
        if (!m_joints_category_dropdown)
            return;
        const auto count = static_cast<int>(joint_ui_categories().size());
        if (count <= 0)
            return;
        const auto current = static_cast<int>(m_joints_category_dropdown->get_selected());
        const auto next = (current + delta + count) % count;
        m_joints_category_dropdown->set_selected(static_cast<unsigned int>(next));
    };
    auto cycle_family = [this](int delta) {
        if (!m_joints_family_dropdown || m_joints_visible_family_indices.empty())
            return;
        const auto count = static_cast<int>(m_joints_visible_family_indices.size());
        const auto current = static_cast<int>(m_joints_family_dropdown->get_selected());
        const auto next = (current + delta + count) % count;
        m_joints_family_dropdown->set_selected(static_cast<unsigned int>(next));
    };
    if (m_joints_category_prev_button)
        m_joints_category_prev_button->signal_clicked().connect([cycle_category] { cycle_category(-1); });
    if (m_joints_category_next_button)
        m_joints_category_next_button->signal_clicked().connect([cycle_category] { cycle_category(1); });
    if (m_joints_family_prev_button)
        m_joints_family_prev_button->signal_clicked().connect([cycle_family] { cycle_family(-1); });
    if (m_joints_family_next_button)
        m_joints_family_next_button->signal_clicked().connect([cycle_family] { cycle_family(1); });
    if (m_joints_category_dropdown)
        m_joints_category_dropdown->property_selected().signal_changed().connect([this] { sync_joints_popover_controls(); });
    m_joints_family_dropdown->property_selected().signal_changed().connect([this] {
        if (m_updating_joints_ui || m_joints_rebuilding_settings)
            return;

        m_updating_joints_ui = true;
        if (const auto *family = get_selected_joint_family(m_joints_family_dropdown, m_joints_visible_family_indices))
            m_joints_setting_values["__joint_family__"] = family->id;
        if (const auto *family = get_selected_joint_family(m_joints_family_dropdown, m_joints_visible_family_indices)) {
            if (m_joints_family_label)
                m_joints_family_label->set_text(family->label);
            if (m_joints_family_description_label)
                m_joints_family_description_label->set_text(family->description);
        }
        rebuild_joints_role_dropdowns();
        rebuild_joints_settings_ui();
        m_updating_joints_ui = false;
        update_joints_summary();
    });
    m_joints_thickness_spin->signal_value_changed().connect([this] { update_joints_summary(); });
    m_joints_burn_spin->signal_value_changed().connect([this] { update_joints_summary(); });

    m_joints_apply_button = Gtk::make_managed<Gtk::Button>("Apply");
    m_joints_apply_button->set_has_frame(true);
    m_joints_apply_button->add_css_class("suggested-action");
    root->append(*m_joints_apply_button);
    m_joints_apply_button->signal_clicked().connect([this] {
        if (!generate_joints_from_selected_lines()) {
            if (m_workspace_browser)
                m_workspace_browser->show_toast("Select a valid edge selection for Edge Features");
            return;
        }
        if (m_joints_popover)
            m_joints_popover->popdown();
    });

    m_joints_button->signal_clicked().connect([this] {
        m_joints_mode_enabled = !m_joints_mode_enabled;
        update_joints_quick_popover(false);
        update_sketcher_toolbar_button_states();
    });

    if (!joints_error.empty() && m_workspace_browser)
        m_workspace_browser->show_toast(joints_error);
    sync_joints_popover_controls();
#endif
}

void Editor::update_joints_quick_popover(bool request_popup)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_joints_quick_popover)
        return;

    if (m_boxes_generator_window && m_boxes_generator_window->get_visible()) {
        if (m_joints_quick_popover->get_visible())
            m_joints_quick_popover->popdown();
        return;
    }

    const auto hide_quick = [this] {
        if (m_joints_quick_popover && m_joints_quick_popover->get_visible())
            m_joints_quick_popover->popdown();
    };

    if (!m_joints_mode_enabled || !m_core.has_documents() || m_core.tool_is_active()) {
        hide_quick();
        return;
    }
    if (m_joints_popover && m_joints_popover->get_visible()) {
        hide_quick();
        return;
    }

    std::string error;
    if (!ensure_joint_families_loaded(error) || g_joint_families.empty()) {
        hide_quick();
        return;
    }
    const auto *family = get_selected_joint_family(m_joints_family_dropdown, m_joints_visible_family_indices);
    if (!family) {
        hide_quick();
        return;
    }
    const auto *role = get_selected_joint_role(*family, m_joints_role_dropdown);
    if (!role) {
        hide_quick();
        return;
    }
    update_joints_summary();
        if (!joint_family_quick_popover_supported(*family)) {
        hide_quick();
        if (request_popup && m_workspace_browser)
            m_workspace_browser->show_toast("This family needs the main Edge Features panel");
        return;
    }

    auto &doc = m_core.get_current_document();
    auto &group = doc.get_group(m_core.get_current_group());
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    if (!sketch) {
        hide_quick();
        return;
    }

    const auto selection = get_canvas().get_selection();
    const auto lines = selected_line_entities_in_group(doc, selection, group.m_uuid);
    if (lines.empty()) {
        hide_quick();
        if (request_popup && m_workspace_browser)
            m_workspace_browser->show_toast("Select one or more lines first, then press Shift+LMB");
        return;
    }

    if (!m_joints_quick_popover->get_visible() && request_popup) {
        Gdk::Rectangle rect(static_cast<int>(std::lround(m_last_x)) + 1, static_cast<int>(std::lround(m_last_y)) + 1, 1, 1);
        m_joints_quick_popover->set_pointing_to(rect);
        m_joints_quick_popover->popup();
    }
    else if (!m_joints_quick_popover->get_visible()) {
        return;
    }
#endif
}

void Editor::init_boxes_popover()
{
#ifdef DUNE_SKETCHER_ONLY
    m_boxes_button = Gtk::make_managed<Gtk::Button>();
    m_boxes_button->set_icon_name("package-x-generic-symbolic");
    m_boxes_button->set_tooltip_text("Boxes");
    m_boxes_button->set_has_frame(true);
    m_boxes_button->set_focusable(false);
    m_boxes_button->add_css_class("sketch-toolbar-button");
    m_boxes_button->signal_clicked().connect([this] {
        open_boxes_generator_window();
    });
#endif
}

void Editor::open_boxes_generator_window()
{
#ifdef DUNE_SKETCHER_ONLY
    if (g_boxes_templates.empty()) {
        if (!m_boxes_loading_window) {
            m_boxes_loading_window = Gtk::make_managed<Gtk::Window>();
            m_boxes_loading_window->set_title("Loading Boxes");
            m_boxes_loading_window->set_default_size(320, 110);
            m_boxes_loading_window->set_transient_for(m_win);
            sync_sketch_theme_classes(m_win, *m_boxes_loading_window);
            m_boxes_loading_window->set_modal(true);
            m_boxes_loading_window->set_hide_on_close(true);
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
            box->set_margin_start(16);
            box->set_margin_end(16);
            box->set_margin_top(16);
            box->set_margin_bottom(16);
            auto spinner = Gtk::make_managed<Gtk::Spinner>();
            spinner->start();
            auto label = Gtk::make_managed<Gtk::Label>("Please wait while the boxes catalog is loaded...");
            label->set_wrap(true);
            label->set_justify(Gtk::Justification::CENTER);
            box->append(*spinner);
            box->append(*label);
            m_boxes_loading_window->set_child(*box);
        }
        m_boxes_loading_window->present();

        if (!m_boxes_catalog_loading) {
            m_boxes_catalog_loading = true;
            m_boxes_catalog_future = std::async(std::launch::async, [] {
                std::string error;
                return std::make_pair(ensure_boxes_catalog_loaded(error), error);
            });
            if (m_boxes_catalog_poll_connection.connected())
                m_boxes_catalog_poll_connection.disconnect();
            m_boxes_catalog_poll_connection = Glib::signal_timeout().connect(
                    [this] {
                        if (!m_boxes_catalog_loading)
                            return false;
                        if (m_boxes_catalog_future.valid()
                            && m_boxes_catalog_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                            const auto [ok, error] = m_boxes_catalog_future.get();
                            m_boxes_catalog_loading = false;
                            if (m_boxes_loading_window)
                                m_boxes_loading_window->hide();
                            if (!ok) {
                                m_workspace_browser->show_toast(error.empty() ? "Failed to load bundled boxes catalog" : error);
                                return false;
                            }
                            open_boxes_generator_window();
                            return false;
                        }
                        return true;
                    },
                    30);
        }
        return;
    }

    if (g_boxes_templates.empty()) {
        m_workspace_browser->show_toast("Bundled boxes catalog is empty");
        return;
    }
    m_boxes_template_index = std::clamp(m_boxes_template_index, 0, static_cast<int>(g_boxes_templates.size()) - 1);

    if (!m_boxes_generator_window) {
        m_boxes_generator_window = Gtk::make_managed<Gtk::Window>();
        m_boxes_generator_window->set_title("Boxes");
        m_boxes_generator_window->set_default_size(1320, 820);
        m_boxes_generator_window->set_transient_for(m_win);
        sync_sketch_theme_classes(m_win, *m_boxes_generator_window);
        m_boxes_generator_window->set_modal(true);
        m_boxes_generator_window->set_hide_on_close(true);
        m_boxes_generator_window->signal_close_request().connect(
                [this] {
                    if (m_boxes_generator_window)
                        m_boxes_generator_window->hide();
                    return true;
                },
                false);
        m_boxes_generator_window->signal_show().connect([this] {
            if (m_joints_mode_enabled) {
                m_boxes_suspend_joints_active = true;
                m_joints_mode_enabled = false;
                update_joints_quick_popover();
            }
            update_sketcher_toolbar_button_states();
        });
        m_boxes_generator_window->signal_hide().connect([this] {
            if (m_boxes_suspend_joints_active) {
                m_boxes_suspend_joints_active = false;
                m_joints_mode_enabled = true;
                update_joints_quick_popover();
            }
            update_sketcher_toolbar_button_states();
        });

        auto header = Gtk::make_managed<Gtk::HeaderBar>();
        header->set_show_title_buttons(true);
        auto title = Gtk::make_managed<Gtk::Label>("Boxes");
        header->set_title_widget(*title);
        m_boxes_import_button = Gtk::make_managed<Gtk::Button>("Import");
        m_boxes_import_button->add_css_class("suggested-action");
        m_boxes_import_button->signal_clicked().connect(sigc::mem_fun(*this, &Editor::generate_boxes_geometry));
        header->pack_end(*m_boxes_import_button);
        m_boxes_generator_window->set_titlebar(*header);

        auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        root->set_margin_start(12);
        root->set_margin_end(12);
        root->set_margin_top(12);
        root->set_margin_bottom(12);
        m_boxes_generator_window->set_child(*root);

        auto left = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
        left->set_size_request(260, -1);
        root->append(*left);

        auto catalog_frame = Gtk::make_managed<Gtk::Frame>("Catalog");
        catalog_frame->set_vexpand(false);
        left->append(*catalog_frame);

        auto catalog_root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        catalog_root->set_margin_start(8);
        catalog_root->set_margin_end(8);
        catalog_root->set_margin_top(8);
        catalog_root->set_margin_bottom(8);
        catalog_frame->set_child(*catalog_root);

        auto catalog_header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto menu_button = Gtk::make_managed<Gtk::Button>();
        menu_button->set_icon_name("open-menu-symbolic");
        menu_button->set_has_frame(false);
        menu_button->set_focusable(false);
        menu_button->set_tooltip_text("Show categories");
        catalog_header->append(*menu_button);
        m_boxes_gallery_button = Gtk::make_managed<Gtk::Button>();
        m_boxes_gallery_button->set_icon_name("view-grid-symbolic");
        m_boxes_gallery_button->set_has_frame(false);
        m_boxes_gallery_button->set_focusable(false);
        m_boxes_gallery_button->set_tooltip_text("Open gallery");
        m_boxes_gallery_button->signal_clicked().connect(sigc::mem_fun(*this, &Editor::open_boxes_gallery_window));
        catalog_header->append(*m_boxes_gallery_button);
        auto category_label = Gtk::make_managed<Gtk::Label>("Templates");
        category_label->set_xalign(0.0f);
        category_label->set_hexpand(true);
        catalog_header->append(*category_label);
        catalog_root->append(*catalog_header);

        auto catalog_body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        catalog_root->append(*catalog_body);

        m_boxes_category_revealer = Gtk::make_managed<Gtk::Revealer>();
        m_boxes_category_revealer->set_transition_type(Gtk::RevealerTransitionType::SLIDE_RIGHT);
        m_boxes_category_revealer->set_reveal_child(true);
        auto category_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        category_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        category_scroll->set_min_content_width(170);
        category_scroll->set_min_content_height(220);
        m_boxes_category_list = Gtk::make_managed<Gtk::ListBox>();
        m_boxes_category_list->set_selection_mode(Gtk::SelectionMode::SINGLE);
        m_boxes_category_list->add_css_class("boxed-list");
        category_scroll->set_child(*m_boxes_category_list);
        m_boxes_category_revealer->set_child(*category_scroll);
        catalog_body->append(*m_boxes_category_revealer);

        auto template_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        template_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        template_scroll->set_min_content_height(220);
        template_scroll->set_hexpand(true);
        m_boxes_template_list = Gtk::make_managed<Gtk::ListBox>();
        m_boxes_template_list->set_selection_mode(Gtk::SelectionMode::SINGLE);
        m_boxes_template_list->add_css_class("boxed-list");
        template_scroll->set_child(*m_boxes_template_list);
        catalog_body->append(*template_scroll);

        menu_button->signal_clicked().connect([this] {
            if (m_boxes_category_revealer)
                m_boxes_category_revealer->set_reveal_child(!m_boxes_category_revealer->get_reveal_child());
        });

        m_boxes_category_list->signal_row_selected().connect([this](Gtk::ListBoxRow *row) {
            if (!row || m_boxes_syncing_catalog)
                return;
            const auto row_idx = row->get_index();
            if (row_idx == 0)
                m_boxes_current_category_id = "__favorites__";
            else {
                const auto category_idx = row_idx - 1;
                if (category_idx >= 0 && category_idx < static_cast<int>(g_boxes_categories.size()))
                    m_boxes_current_category_id = g_boxes_categories.at(static_cast<size_t>(category_idx)).id;
            }
            Glib::signal_idle().connect_once([this] {
                rebuild_boxes_template_list();
                sync_boxes_catalog_selection(false);
                if (m_boxes_category_revealer)
                    m_boxes_category_revealer->set_reveal_child(false);
            });
        });

        auto settings_frame = Gtk::make_managed<Gtk::Frame>("Settings");
        settings_frame->set_hexpand(true);
        settings_frame->set_vexpand(true);
        left->append(*settings_frame);

        auto settings_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        settings_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        settings_frame->set_child(*settings_scroll);

        m_boxes_settings_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        m_boxes_settings_box->set_margin_start(10);
        m_boxes_settings_box->set_margin_end(10);
        m_boxes_settings_box->set_margin_top(10);
        m_boxes_settings_box->set_margin_bottom(10);
        settings_scroll->set_child(*m_boxes_settings_box);

        m_boxes_preview_status_label = Gtk::make_managed<Gtk::Label>();
        m_boxes_preview_status_label->set_xalign(0.0f);
        m_boxes_preview_status_label->set_wrap(true);
        m_boxes_preview_status_label->add_css_class("dim-label");

        auto preview_column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        preview_column->set_hexpand(true);
        preview_column->set_vexpand(true);
        root->append(*preview_column);

        auto preview_frame = Gtk::make_managed<Gtk::Frame>("Preview");
        preview_frame->set_hexpand(true);
        preview_frame->set_vexpand(true);
        preview_column->append(*preview_frame);

        auto preview_aspect = Gtk::make_managed<Gtk::AspectFrame>();
        preview_aspect->set_obey_child(false);
        preview_aspect->set_ratio(4.0f / 3.0f);
        preview_aspect->set_xalign(0.5f);
        preview_aspect->set_yalign(0.5f);
        preview_aspect->set_hexpand(true);
        preview_aspect->set_vexpand(true);
        m_boxes_preview_area = Gtk::make_managed<Gtk::DrawingArea>();
        m_boxes_preview_area->set_hexpand(true);
        m_boxes_preview_area->set_vexpand(true);
        m_boxes_preview_area->set_content_width(1);
        m_boxes_preview_area->set_content_height(1);
        m_boxes_preview_area->set_draw_func(sigc::mem_fun(*this, &Editor::draw_boxes_preview));
        preview_aspect->set_child(*m_boxes_preview_area);
        preview_frame->set_child(*preview_aspect);

        m_boxes_template_list->signal_row_selected().connect([this](Gtk::ListBoxRow *row) {
            if (!row || m_boxes_syncing_catalog)
                return;
            const auto it = m_boxes_template_indices_by_list.find(m_boxes_template_list);
            if (it == m_boxes_template_indices_by_list.end())
                return;
            const auto row_idx = row->get_index();
            if (row_idx < 0 || row_idx >= static_cast<int>(it->second.size()))
                return;
            m_boxes_template_index = it->second.at(static_cast<size_t>(row_idx));
            std::fprintf(stderr, "[boxes] select template %s\n", get_boxes_template(m_boxes_template_index).label.c_str());
            std::fflush(stderr);
            m_boxes_preview_zoom = 1.0;
            m_boxes_preview_pan_x = 0.0;
            m_boxes_preview_pan_y = 0.0;
            sync_boxes_catalog_selection(true);
            Glib::signal_idle().connect_once([this] {
                update_boxes_settings_visibility();
                queue_boxes_preview_refresh(true);
            });
        });

        auto motion = Gtk::EventControllerMotion::create();
        motion->signal_motion().connect([this](double x, double y) {
            m_boxes_preview_cursor_x = x;
            m_boxes_preview_cursor_y = y;
        });
        m_boxes_preview_area->add_controller(motion);

        auto scroll = Gtk::EventControllerScroll::create();
        scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
        scroll->signal_scroll().connect([this](double, double dy) {
            if (!m_boxes_preview_area || m_boxes_preview_polylines.empty())
                return false;
            const auto width = m_boxes_preview_area->get_width();
            const auto height = m_boxes_preview_area->get_height();
            if (width <= 0 || height <= 0)
                return false;

            const auto before = get_boxes_preview_transform(m_boxes_preview_bbox_min, m_boxes_preview_bbox_max, width, height,
                                                            m_boxes_preview_zoom, m_boxes_preview_pan_x, m_boxes_preview_pan_y);
            if (before.scale <= 1e-9)
                return false;

            const auto world_x = (m_boxes_preview_cursor_x - before.ox) / before.scale;
            const auto world_y = (before.oy - m_boxes_preview_cursor_y) / before.scale;
            const auto factor = (dy < 0.0) ? 1.15 : (1.0 / 1.15);
            m_boxes_preview_zoom = std::clamp(m_boxes_preview_zoom * factor, 0.1, 64.0);

            const auto after = get_boxes_preview_transform(m_boxes_preview_bbox_min, m_boxes_preview_bbox_max, width, height,
                                                           m_boxes_preview_zoom, m_boxes_preview_pan_x, m_boxes_preview_pan_y);
            m_boxes_preview_pan_x = m_boxes_preview_cursor_x - ((world_x * after.scale)
                                                                + (after.ox - m_boxes_preview_pan_x));
            m_boxes_preview_pan_y = m_boxes_preview_cursor_y - (after.oy - m_boxes_preview_pan_y - world_y * after.scale);
            m_boxes_preview_area->queue_draw();
            return true;
        },
                                                  false);
        m_boxes_preview_area->add_controller(scroll);

        auto drag = Gtk::GestureDrag::create();
        drag->set_button(1);
        drag->signal_drag_begin().connect([this](double, double) {
            m_boxes_preview_drag_pan_x = m_boxes_preview_pan_x;
            m_boxes_preview_drag_pan_y = m_boxes_preview_pan_y;
        });
        drag->signal_drag_update().connect([this](double offset_x, double offset_y) {
            m_boxes_preview_pan_x = m_boxes_preview_drag_pan_x + offset_x;
            m_boxes_preview_pan_y = m_boxes_preview_drag_pan_y + offset_y;
            if (m_boxes_preview_area)
                m_boxes_preview_area->queue_draw();
        });
        m_boxes_preview_area->add_controller(drag);

        auto click = Gtk::GestureClick::create();
        click->set_button(1);
        click->signal_pressed().connect([this](int n_press, double, double) {
            if (n_press != 2)
                return;
            m_boxes_preview_zoom = 1.0;
            m_boxes_preview_pan_x = 0.0;
            m_boxes_preview_pan_y = 0.0;
            if (m_boxes_preview_area)
                m_boxes_preview_area->queue_draw();
        });
        m_boxes_preview_area->add_controller(click);

        Glib::signal_idle().connect_once([this] {
            sync_boxes_catalog_selection(true);
            rebuild_boxes_catalog_lists();
            sync_boxes_catalog_selection(true);
            update_boxes_settings_visibility();
            queue_boxes_preview_refresh(true);
        });
    }

    if (!m_boxes_generator_window)
        return;

    m_boxes_generator_window->present();
#endif
}

void Editor::rebuild_boxes_catalog_lists()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_boxes_category_list)
        return;
    while (auto *child = m_boxes_category_list->get_first_child())
        m_boxes_category_list->remove(*child);

    const auto append_category_row = [this](const std::string &title, const std::string &sample_image) {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        box->set_margin_start(10);
        box->set_margin_end(10);
        box->set_margin_top(8);
        box->set_margin_bottom(8);
        if (get_boxes_sample_path(sample_image)) {
            auto image = Gtk::make_managed<Gtk::Image>();
            image->set_from_icon_name("image-x-generic-symbolic");
            box->append(*image);
        }
        auto label = Gtk::make_managed<Gtk::Label>(title);
        label->set_xalign(0.0f);
        label->set_hexpand(true);
        box->append(*label);
        row->set_child(*box);
        m_boxes_category_list->append(*row);
    };

    append_category_row("Favorites", "");
    for (const auto &category : g_boxes_categories)
        append_category_row(category.title, category.sample_image);

    if (!g_boxes_templates.empty()) {
        const auto &def = get_boxes_template(m_boxes_template_index);
        const bool in_favorites = m_boxes_favorite_template_indices.contains(m_boxes_template_index);
        m_boxes_current_category_id = in_favorites ? "__favorites__" : def.category;
    }

    rebuild_boxes_template_list();
    rebuild_boxes_gallery();
#endif
}

void Editor::rebuild_boxes_template_list()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_boxes_template_list)
        return;
    while (auto *child = m_boxes_template_list->get_first_child())
        m_boxes_template_list->remove(*child);

    auto &indices = m_boxes_template_indices_by_list[m_boxes_template_list];
    indices.clear();
    for (size_t i = 0; i < g_boxes_templates.size(); i++) {
        const auto idx = static_cast<int>(i);
        const auto &def = g_boxes_templates.at(i);
        const bool matches = m_boxes_current_category_id == "__favorites__" ? m_boxes_favorite_template_indices.contains(idx)
                                                                            : def.category == m_boxes_current_category_id;
        if (!matches)
            continue;

        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        box->set_margin_start(10);
        box->set_margin_end(10);
        box->set_margin_top(8);
        box->set_margin_bottom(8);
        auto row_content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto label = Gtk::make_managed<Gtk::Label>(def.label);
        label->set_hexpand(true);
        label->set_xalign(0.0f);
        const auto sample_path = get_boxes_sample_path(def);
        if (sample_path) {
            auto sample_button = Gtk::make_managed<Gtk::Button>();
            sample_button->set_has_frame(false);
            sample_button->set_focusable(false);
            sample_button->set_valign(Gtk::Align::CENTER);
            sample_button->set_icon_name("image-x-generic-symbolic");
            sample_button->set_tooltip_text("Open sample photo");
            sample_button->signal_clicked().connect([this, idx] { show_boxes_sample_preview(idx); });
            row_content->append(*sample_button);
        }
        auto star_button = Gtk::make_managed<Gtk::Button>();
        star_button->set_has_frame(false);
        star_button->set_focusable(false);
        star_button->set_valign(Gtk::Align::CENTER);
        auto star_label = Gtk::make_managed<Gtk::Label>();
        const bool favorite = m_boxes_favorite_template_indices.contains(idx);
        star_label->set_use_markup(true);
        star_label->set_markup(favorite ? "<span foreground='#f5c542' size='large'>★</span>"
                                        : "<span foreground='#8a8a8a' size='large'>☆</span>");
        star_button->set_child(*star_label);
        star_button->signal_clicked().connect([this, idx] {
            if (m_boxes_favorite_template_indices.contains(idx))
                m_boxes_favorite_template_indices.erase(idx);
            else
                m_boxes_favorite_template_indices.insert(idx);
            rebuild_boxes_template_list();
            rebuild_boxes_gallery();
            sync_boxes_catalog_selection(false);
        });
        row_content->append(*label);
        row_content->append(*star_button);
        box->append(*row_content);
        row->set_child(*box);
        m_boxes_template_list->append(*row);
        indices.push_back(idx);
    }
#endif
}

void Editor::rebuild_boxes_gallery()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_boxes_gallery_flowbox)
        return;
    while (auto *child = m_boxes_gallery_flowbox->get_first_child())
        m_boxes_gallery_flowbox->remove(*child);

    for (size_t i = 0; i < g_boxes_templates.size(); i++) {
        const auto idx = static_cast<int>(i);
        const auto &def = g_boxes_templates.at(i);
        const bool matches = m_boxes_current_category_id == "__favorites__" ? m_boxes_favorite_template_indices.contains(idx)
                                                                            : def.category == m_boxes_current_category_id;
        if (!matches)
            continue;

        auto button = Gtk::make_managed<Gtk::Button>();
        button->set_has_frame(true);
        button->set_focusable(false);
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        box->set_margin_start(8);
        box->set_margin_end(8);
        box->set_margin_top(8);
        box->set_margin_bottom(8);
        auto sample_path = get_boxes_sample_path(!def.sample_thumbnail.empty() ? def.sample_thumbnail : def.sample_image);
        if (sample_path) {
            auto picture = Gtk::make_managed<Gtk::Picture>(path_to_string(*sample_path));
            picture->set_size_request(160, 120);
            picture->set_can_shrink(true);
            picture->set_content_fit(Gtk::ContentFit::COVER);
            box->append(*picture);
        }
        else {
            auto image = Gtk::make_managed<Gtk::Image>();
            image->set_from_icon_name("image-missing");
            image->set_pixel_size(64);
            box->append(*image);
        }
        auto label = Gtk::make_managed<Gtk::Label>(def.label);
        label->set_wrap(true);
        label->set_justify(Gtk::Justification::CENTER);
        label->set_xalign(0.5f);
        box->append(*label);
        button->set_child(*box);
        button->signal_clicked().connect([this, idx, category = def.category] {
            m_boxes_template_index = idx;
            m_boxes_current_category_id = category;
            rebuild_boxes_template_list();
            update_boxes_settings_visibility();
            sync_boxes_catalog_selection(true);
            queue_boxes_preview_refresh(true);
            if (m_boxes_gallery_window)
                m_boxes_gallery_window->hide();
            if (m_boxes_generator_window)
                m_boxes_generator_window->present();
        });
        m_boxes_gallery_flowbox->insert(*button, -1);
    }
#endif
}

void Editor::ensure_boxes_importing_window()
{
#ifdef DUNE_SKETCHER_ONLY
    if (m_boxes_importing_window)
        return;

    m_boxes_importing_window = Gtk::make_managed<Gtk::Window>();
    m_boxes_importing_window->set_title("Importing");
    m_boxes_importing_window->set_default_size(320, 120);
    m_boxes_importing_window->set_transient_for(m_win);
    sync_sketch_theme_classes(m_win, *m_boxes_importing_window);
    m_boxes_importing_window->set_modal(true);
    m_boxes_importing_window->set_resizable(false);
    m_boxes_importing_window->set_hide_on_close(true);
    m_boxes_importing_window->signal_close_request().connect(
            [] {
                // Keep this visible while the async import is running.
                return true;
            },
            false);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    box->set_margin_start(16);
    box->set_margin_end(16);
    box->set_margin_top(16);
    box->set_margin_bottom(16);

    auto spinner = Gtk::make_managed<Gtk::Spinner>();
    spinner->start();
    spinner->set_halign(Gtk::Align::CENTER);
    box->append(*spinner);

    m_boxes_importing_label = Gtk::make_managed<Gtk::Label>("Importing...");
    m_boxes_importing_label->set_wrap(true);
    m_boxes_importing_label->set_justify(Gtk::Justification::CENTER);
    box->append(*m_boxes_importing_label);

    m_boxes_importing_progress = Gtk::make_managed<Gtk::ProgressBar>();
    m_boxes_importing_progress->set_pulse_step(0.08);
    box->append(*m_boxes_importing_progress);

    m_boxes_importing_window->set_child(*box);
#endif
}

void Editor::finish_boxes_geometry_import()
{
#ifdef DUNE_SKETCHER_ONLY
    if (m_boxes_import_poll_connection.connected())
        m_boxes_import_poll_connection.disconnect();
    m_boxes_import_generation_running = false;

    if (m_boxes_import_button)
        m_boxes_import_button->set_sensitive(m_core.has_documents());
    if (m_boxes_importing_window)
        m_boxes_importing_window->hide();

    if (!m_boxes_ready_import_result.error.empty()) {
        Logger::get().log_warning(m_boxes_ready_import_result.error, Logger::Domain::EDITOR);
        m_workspace_browser->show_toast(m_boxes_ready_import_result.error);
        if (m_boxes_generator_window)
            m_boxes_generator_window->present();
        return;
    }

    auto &doc = m_core.get_current_document();
    auto &group = doc.get_group(m_boxes_import_group);
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    auto *wrkpl = doc.get_entity_ptr<EntityWorkplane>(m_boxes_import_workplane);
    if (!sketch || !wrkpl) {
        m_workspace_browser->show_toast("Boxes import target is no longer available");
        if (m_boxes_generator_window)
            m_boxes_generator_window->present();
        return;
    }

    const auto bbox_center = (m_boxes_ready_import_result.bbox_min + m_boxes_ready_import_result.bbox_max) * 0.5;
    const auto offset = m_boxes_import_target_center - bbox_center;

    std::set<SelectableRef> imported_selection;
    for (const auto &seg : m_boxes_ready_import_result.segments) {
        if (!seg.bezier) {
            auto line = std::make_unique<EntityLine2D>(UUID::random());
            line->m_group = group.m_uuid;
            line->m_layer = m_boxes_import_layer;
            line->m_wrkpl = m_boxes_import_workplane;
            line->m_p1 = seg.p1 + offset;
            line->m_p2 = seg.p2 + offset;
            imported_selection.emplace(SelectableRef::Type::ENTITY, line->m_uuid, 0);
            doc.m_entities.emplace(line->m_uuid, std::move(line));
        }
        else {
            auto bezier = std::make_unique<EntityBezier2D>(UUID::random());
            bezier->m_group = group.m_uuid;
            bezier->m_layer = m_boxes_import_layer;
            bezier->m_wrkpl = m_boxes_import_workplane;
            bezier->m_p1 = seg.p1 + offset;
            bezier->m_c1 = seg.c1 + offset;
            bezier->m_c2 = seg.c2 + offset;
            bezier->m_p2 = seg.p2 + offset;
            imported_selection.emplace(SelectableRef::Type::ENTITY, bezier->m_uuid, 0);
            doc.m_entities.emplace(bezier->m_uuid, std::move(bezier));
        }
    }

    if (imported_selection.empty()) {
        m_workspace_browser->show_toast("Bundled boxes ran, but no geometry was imported");
        if (m_boxes_generator_window)
            m_boxes_generator_window->present();
        return;
    }

    doc.set_group_generate_pending(m_boxes_import_group);
    m_core.set_needs_save();
    m_core.rebuild("import boxes svg");
    get_canvas().set_selection(imported_selection, false);
    canvas_update_keep_selection();
    update_action_sensitivity();
    rebuild_layers_popover();
    m_workspace_browser->show_toast(std::string("Imported ") + m_boxes_import_template_label + " from bundled boxes");
#endif
}

void Editor::show_boxes_sample_preview(int template_index)
{
#ifdef DUNE_SKETCHER_ONLY
    const auto &template_def = get_boxes_template(template_index);
    const auto sample_path = get_boxes_sample_path(template_def);
    if (!sample_path)
        return;

    if (!m_boxes_sample_window) {
        m_boxes_sample_window = Gtk::make_managed<Gtk::Window>();
        m_boxes_sample_window->set_default_size(960, 720);
        if (m_boxes_generator_window)
            m_boxes_sample_window->set_transient_for(*m_boxes_generator_window);
        else
            m_boxes_sample_window->set_transient_for(m_win);
        sync_sketch_theme_classes(m_win, *m_boxes_sample_window);
        m_boxes_sample_window->set_modal(false);
        m_boxes_sample_window->set_hide_on_close(true);
        m_boxes_sample_window->signal_close_request().connect(
                [this] {
                    if (m_boxes_sample_window)
                        m_boxes_sample_window->hide();
                    return true;
                },
                false);

        auto root = Gtk::make_managed<Gtk::ScrolledWindow>();
        root->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
        root->set_min_content_width(640);
        root->set_min_content_height(480);

        m_boxes_sample_picture = Gtk::make_managed<Gtk::Picture>();
        m_boxes_sample_picture->set_can_shrink(true);
        m_boxes_sample_picture->set_content_fit(Gtk::ContentFit::CONTAIN);
        root->set_child(*m_boxes_sample_picture);
        m_boxes_sample_window->set_child(*root);
    }

    m_boxes_sample_window->set_title(std::string(template_def.label) + " sample");
    if (m_boxes_sample_picture)
        m_boxes_sample_picture->set_filename(path_to_string(*sample_path));
    m_boxes_sample_window->present();
#else
    (void)template_index;
#endif
}

void Editor::open_boxes_gallery_window()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_boxes_gallery_window) {
        m_boxes_gallery_window = Gtk::make_managed<Gtk::Window>();
        m_boxes_gallery_window->set_title("Boxes Gallery");
        m_boxes_gallery_window->set_default_size(1100, 760);
        if (m_boxes_generator_window)
            m_boxes_gallery_window->set_transient_for(*m_boxes_generator_window);
        else
            m_boxes_gallery_window->set_transient_for(m_win);
        sync_sketch_theme_classes(m_win, *m_boxes_gallery_window);
        m_boxes_gallery_window->set_hide_on_close(true);
        m_boxes_gallery_window->signal_close_request().connect(
                [this] {
                    if (m_boxes_gallery_window)
                        m_boxes_gallery_window->hide();
                    return true;
                },
                false);

        auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        root->set_margin_start(12);
        root->set_margin_end(12);
        root->set_margin_top(12);
        root->set_margin_bottom(12);

        auto category_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        category_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        category_scroll->set_min_content_width(220);
        m_boxes_gallery_category_list = Gtk::make_managed<Gtk::ListBox>();
        m_boxes_gallery_category_list->set_selection_mode(Gtk::SelectionMode::SINGLE);
        category_scroll->set_child(*m_boxes_gallery_category_list);
        root->append(*category_scroll);

        auto gallery_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        gallery_scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
        gallery_scroll->set_hexpand(true);
        gallery_scroll->set_vexpand(true);
        m_boxes_gallery_flowbox = Gtk::make_managed<Gtk::FlowBox>();
        m_boxes_gallery_flowbox->set_selection_mode(Gtk::SelectionMode::NONE);
        m_boxes_gallery_flowbox->set_row_spacing(10);
        m_boxes_gallery_flowbox->set_column_spacing(10);
        m_boxes_gallery_flowbox->set_max_children_per_line(4);
        gallery_scroll->set_child(*m_boxes_gallery_flowbox);
        root->append(*gallery_scroll);
        m_boxes_gallery_window->set_child(*root);

        m_boxes_gallery_category_list->signal_row_selected().connect([this](Gtk::ListBoxRow *row) {
            if (!row || m_boxes_syncing_catalog)
                return;
            const auto idx = row->get_index();
            if (idx == 0)
                m_boxes_current_category_id = "__favorites__";
            else if (idx - 1 >= 0 && idx - 1 < static_cast<int>(g_boxes_categories.size()))
                m_boxes_current_category_id = g_boxes_categories.at(static_cast<size_t>(idx - 1)).id;
            rebuild_boxes_gallery();
            sync_boxes_catalog_selection(false);
        });
    }

    if (m_boxes_gallery_category_list) {
        while (auto *child = m_boxes_gallery_category_list->get_first_child())
            m_boxes_gallery_category_list->remove(*child);
        const auto add_row = [this](const std::string &title) {
            auto row = Gtk::make_managed<Gtk::ListBoxRow>();
            auto label = Gtk::make_managed<Gtk::Label>(title);
            label->set_margin_start(10);
            label->set_margin_end(10);
            label->set_margin_top(8);
            label->set_margin_bottom(8);
            label->set_xalign(0.0f);
            row->set_child(*label);
            m_boxes_gallery_category_list->append(*row);
        };
        add_row("Favorites");
        for (const auto &category : g_boxes_categories)
            add_row(category.title);
    }
    rebuild_boxes_gallery();
    sync_boxes_catalog_selection(false);
    m_boxes_gallery_window->present();
#endif
}

void Editor::sync_boxes_catalog_selection(bool switch_to_category_page)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_boxes_template_list || !m_boxes_category_list)
        return;

    m_boxes_syncing_catalog = true;
    const auto &def = get_boxes_template(m_boxes_template_index);
    const bool in_favorites = m_boxes_favorite_template_indices.contains(m_boxes_template_index);
    if (switch_to_category_page || (m_boxes_current_category_id == "__favorites__" && !in_favorites))
        m_boxes_current_category_id = in_favorites ? "__favorites__" : def.category;

    m_boxes_category_list->unselect_all();
    if (m_boxes_current_category_id == "__favorites__") {
        if (auto *row = m_boxes_category_list->get_row_at_index(0))
            m_boxes_category_list->select_row(*row);
    }
    else {
        for (size_t i = 0; i < g_boxes_categories.size(); i++) {
            if (g_boxes_categories.at(i).id == m_boxes_current_category_id) {
                if (auto *row = m_boxes_category_list->get_row_at_index(static_cast<int>(i) + 1))
                    m_boxes_category_list->select_row(*row);
                break;
            }
        }
    }

    m_boxes_template_list->unselect_all();
    if (const auto it = m_boxes_template_indices_by_list.find(m_boxes_template_list);
        it != m_boxes_template_indices_by_list.end()) {
        const auto pos = std::find(it->second.begin(), it->second.end(), m_boxes_template_index);
        if (pos != it->second.end()) {
            if (auto *row = m_boxes_template_list->get_row_at_index(static_cast<int>(std::distance(it->second.begin(), pos))))
                m_boxes_template_list->select_row(*row);
        }
    }

    if (m_boxes_gallery_category_list) {
        m_boxes_gallery_category_list->unselect_all();
        if (m_boxes_current_category_id == "__favorites__") {
            if (auto *row = m_boxes_gallery_category_list->get_row_at_index(0))
                m_boxes_gallery_category_list->select_row(*row);
        }
        else {
            for (size_t i = 0; i < g_boxes_categories.size(); i++) {
                if (g_boxes_categories.at(i).id == m_boxes_current_category_id) {
                    if (auto *row = m_boxes_gallery_category_list->get_row_at_index(static_cast<int>(i) + 1))
                        m_boxes_gallery_category_list->select_row(*row);
                    break;
                }
            }
        }
    }
    m_boxes_syncing_catalog = false;
#else
    (void)switch_to_category_page;
#endif
}

void Editor::update_boxes_settings_visibility()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_boxes_settings_box || !m_boxes_preview_status_label || m_boxes_rebuilding_settings)
        return;
    m_boxes_rebuilding_settings = true;

    const auto &def = get_boxes_template(m_boxes_template_index);
    while (auto *child = m_boxes_settings_box->get_first_child())
        m_boxes_settings_box->remove(*child);

    m_boxes_preview_status_label = Gtk::make_managed<Gtk::Label>();
    m_boxes_preview_status_label->set_xalign(0.0f);
    m_boxes_preview_status_label->set_wrap(true);
    m_boxes_preview_status_label->add_css_class("dim-label");

    m_boxes_setting_rows.clear();
    m_boxes_spin_settings.clear();
    m_boxes_entry_settings.clear();
    m_boxes_dropdown_settings.clear();
    m_boxes_switch_settings.clear();

    const auto template_id = def.id;
    const auto value_key = [template_id](const std::string &dest) {
        return template_id + ":" + dest;
    };
    const auto get_value = [this, value_key](const BoxesTemplateDef::ArgDef &arg) {
        if (const auto it = m_boxes_setting_values.find(value_key(arg.dest)); it != m_boxes_setting_values.end())
            return it->second;
        return arg.default_string;
    };
    const auto save_value = [this, value_key](const std::string &dest, std::string value) {
        m_boxes_setting_values[value_key(dest)] = std::move(value);
    };

    for (const auto &group : def.arg_groups) {
        auto frame = Gtk::make_managed<Gtk::Frame>(group.title);
        auto group_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        group_box->set_margin_start(8);
        group_box->set_margin_end(8);
        group_box->set_margin_top(8);
        group_box->set_margin_bottom(8);
        frame->set_child(*group_box);

        bool has_rows = false;
        for (const auto &dest : group.args) {
            const auto it = std::find_if(def.args.begin(), def.args.end(),
                                         [&dest](const auto &arg) { return arg.dest == dest; });
            if (it == def.args.end())
                continue;
            const auto &arg = *it;
            auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
            auto label = Gtk::make_managed<Gtk::Label>(arg.label);
            label->set_hexpand(true);
            label->set_xalign(0.0f);
            label->set_tooltip_text(arg.help);
            row->append(*label);

            if (arg.kind == BoxesTemplateDef::ArgKind::FLOAT || arg.kind == BoxesTemplateDef::ArgKind::INT) {
                const auto digits = arg.kind == BoxesTemplateDef::ArgKind::INT ? 0 : 3;
                const auto step = arg.kind == BoxesTemplateDef::ArgKind::INT ? 1.0 : 0.1;
                auto spin = Gtk::make_managed<Gtk::SpinButton>();
                spin->set_digits(digits);
                spin->set_numeric(true);
                spin->set_range(-1000000.0, 1000000.0);
                spin->set_increments(step, step * 10.0);
                spin->set_width_chars(8);
                const auto value = get_value(arg);
                try {
                    spin->set_value(std::stod(value.empty() ? arg.default_string : value));
                }
                catch (...) {
                    spin->set_value(arg.kind == BoxesTemplateDef::ArgKind::INT ? static_cast<double>(arg.default_int)
                                                                               : arg.default_float);
                }
                spin->signal_value_changed().connect([this, save_value, dest = arg.dest, digits, spin] {
                    if (m_boxes_rebuilding_settings)
                        return;
                    std::ostringstream ss;
                    ss << std::fixed << std::setprecision(digits) << spin->get_value();
                    save_value(dest, ss.str());
                    queue_boxes_preview_refresh(false);
                });
                row->append(*spin);
                m_boxes_spin_settings[arg.dest] = spin;
            }
            else if (arg.kind == BoxesTemplateDef::ArgKind::BOOL) {
                auto sw = Gtk::make_managed<Gtk::Switch>();
                const auto value = get_value(arg);
                sw->set_active(value == "1" || value == "true" || (value.empty() && arg.default_bool));
                sw->property_active().signal_changed().connect([this, save_value, dest = arg.dest, sw] {
                    if (m_boxes_rebuilding_settings)
                        return;
                    save_value(dest, sw->get_active() ? "1" : "0");
                    queue_boxes_preview_refresh(false);
                });
                row->append(*sw);
                m_boxes_switch_settings[arg.dest] = sw;
            }
            else if (arg.kind == BoxesTemplateDef::ArgKind::CHOICE) {
                auto model = Gtk::StringList::create();
                for (const auto &choice : arg.choices)
                    model->append(choice);
                auto dropdown = Gtk::make_managed<Gtk::DropDown>(model, nullptr);
                auto value = get_value(arg);
                auto pos = std::find(arg.choices.begin(), arg.choices.end(), value);
                if (pos == arg.choices.end())
                    pos = arg.choices.begin();
                if (pos != arg.choices.end())
                    dropdown->set_selected(static_cast<unsigned int>(std::distance(arg.choices.begin(), pos)));
                dropdown->property_selected().signal_changed().connect([this, save_value, arg, dropdown] {
                    if (m_boxes_rebuilding_settings)
                        return;
                    if (!arg.choices.empty()) {
                        const auto idx = std::min<size_t>(dropdown->get_selected(), arg.choices.size() - 1);
                        save_value(arg.dest, arg.choices.at(idx));
                    }
                    queue_boxes_preview_refresh(false);
                });
                row->append(*dropdown);
                m_boxes_dropdown_settings[arg.dest] = dropdown;
            }
            else {
                auto entry = Gtk::make_managed<Gtk::Entry>();
                entry->set_hexpand(true);
                entry->set_width_chars(14);
                entry->set_text(get_value(arg));
                entry->set_tooltip_text(arg.help);
                entry->signal_changed().connect([this, save_value, dest = arg.dest, entry] {
                    if (m_boxes_rebuilding_settings)
                        return;
                    save_value(dest, entry->get_text());
                    queue_boxes_preview_refresh(false);
                });
                row->append(*entry);
                m_boxes_entry_settings[arg.dest] = entry;
            }

            m_boxes_setting_rows[arg.dest] = row;
            group_box->append(*row);
            has_rows = true;
        }

        if (has_rows)
            m_boxes_settings_box->append(*frame);
    }

    if (!def.short_description.empty()) {
        auto description = Gtk::make_managed<Gtk::Label>(def.short_description);
        description->set_wrap(true);
        description->set_xalign(0.0f);
        description->add_css_class("dim-label");
        m_boxes_settings_box->append(*description);
    }

    m_boxes_preview_status_label->set_text("Loading preview...");
    m_boxes_settings_box->append(*m_boxes_preview_status_label);
    m_boxes_rebuilding_settings = false;
#endif
}

void Editor::queue_boxes_preview_refresh(bool reset_view)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_boxes_generator_window || !m_boxes_generator_window->get_visible() || m_boxes_rebuilding_settings)
        return;
    if (m_boxes_preview_debounce_connection.connected())
        m_boxes_preview_debounce_connection.disconnect();
    m_boxes_preview_debounce_connection = Glib::signal_timeout().connect(
            [this, reset_view] {
                update_boxes_preview(reset_view);
                return false;
            },
            80);
#else
    (void)reset_view;
#endif
}

void Editor::update_boxes_preview(bool reset_view)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_boxes_preview_status_label || !m_boxes_preview_area)
        return;

    const auto &template_def = get_boxes_template(m_boxes_template_index);
    m_boxes_preview_status_label->set_text(std::string("Loading ") + template_def.label + " preview...");
    const auto request_id = ++m_boxes_preview_request_serial;
    const auto snapshot =
            capture_boxes_values_snapshot(template_def, m_boxes_spin_settings, m_boxes_entry_settings, m_boxes_dropdown_settings,
                                          m_boxes_switch_settings);

    const auto start_job = [this, template_def, snapshot](int request_id_for_job, bool reset_view_for_job) {
        m_boxes_preview_generation_running = true;
        m_boxes_preview_future = std::async(std::launch::async, [template_def, snapshot, request_id_for_job] {
            BoxesPreviewAsyncResult result;
            result.request_id = request_id_for_job;
            std::vector<BoxesSvgSegment> segments;
            if (!generate_boxes_svg_segments(template_def, snapshot, segments, result.bbox_min, result.bbox_max, result.error))
                return result;
            result.polylines.reserve(segments.size());
            for (const auto &seg : segments)
                append_boxes_preview_polyline(result.polylines, seg);
            return result;
        });

        if (m_boxes_preview_poll_connection.connected())
            m_boxes_preview_poll_connection.disconnect();
        m_boxes_preview_poll_connection = Glib::signal_timeout().connect(
                [this, reset_view_for_job, label = template_def.label] {
                    if (!m_boxes_preview_generation_running)
                        return false;
                    if (m_boxes_preview_future.valid()
                        && m_boxes_preview_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                        m_boxes_ready_preview_result = m_boxes_preview_future.get();
                        m_boxes_preview_generation_running = false;
                        if (m_boxes_ready_preview_result.request_id == m_boxes_preview_request_serial) {
                            if (!m_boxes_ready_preview_result.error.empty()) {
                                m_boxes_preview_polylines.clear();
                                m_boxes_preview_bbox_min = {0, 0};
                                m_boxes_preview_bbox_max = {0, 0};
                                m_boxes_preview_status_label->set_text(m_boxes_ready_preview_result.error);
                            }
                            else {
                                m_boxes_preview_polylines = m_boxes_ready_preview_result.polylines;
                                m_boxes_preview_bbox_min = m_boxes_ready_preview_result.bbox_min;
                                m_boxes_preview_bbox_max = m_boxes_ready_preview_result.bbox_max;
                                if (reset_view_for_job) {
                                    m_boxes_preview_zoom = 1.0;
                                    m_boxes_preview_pan_x = 0.0;
                                    m_boxes_preview_pan_y = 0.0;
                                }
                                m_boxes_preview_status_label->set_text(label + " preview: "
                                                                       + std::to_string(m_boxes_preview_polylines.size())
                                                                       + " segments");
                            }
                            m_boxes_preview_area->queue_draw();
                        }
                        if (m_boxes_pending_preview_request_id) {
                            const auto next_reset = m_boxes_pending_preview_reset_view;
                            m_boxes_pending_preview_request_id.reset();
                            update_boxes_preview(next_reset);
                            return false;
                        }
                        return false;
                    }
                    return true;
                },
                30);
    };

    if (m_boxes_preview_generation_running) {
        m_boxes_pending_preview_request_id = request_id;
        m_boxes_pending_preview_reset_view = reset_view;
    }
    else {
        start_job(request_id, reset_view);
    }
#else
    (void)reset_view;
#endif
}

void Editor::draw_boxes_preview(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h)
{
#ifdef DUNE_SKETCHER_ONLY
    cr->save();
    cr->set_source_rgb(0.965, 0.965, 0.965);
    cr->paint();

    cr->set_source_rgb(0.90, 0.90, 0.90);
    cr->rectangle(0.5, 0.5, std::max(0, w - 1), std::max(0, h - 1));
    cr->stroke();

    if (m_boxes_preview_polylines.empty()) {
        cr->restore();
        return;
    }

    const auto transform = get_boxes_preview_transform(m_boxes_preview_bbox_min, m_boxes_preview_bbox_max, w, h,
                                                       m_boxes_preview_zoom, m_boxes_preview_pan_x, m_boxes_preview_pan_y);
    cr->translate(transform.ox, transform.oy);
    cr->scale(transform.scale, -transform.scale);
    cr->set_line_width(1.4 / transform.scale);
    cr->set_line_join(Cairo::Context::LineJoin::ROUND);
    cr->set_line_cap(Cairo::Context::LineCap::ROUND);

    for (const auto &polyline : m_boxes_preview_polylines) {
        if (polyline.points.size() < 2)
            continue;
        switch (polyline.layer) {
        case 0:
            cr->set_source_rgb(0.08, 0.08, 0.08);
            break;
        case 1:
            cr->set_source_rgb(0.28, 0.42, 0.95);
            break;
        case 2:
            cr->set_source_rgb(0.10, 0.62, 0.18);
            break;
        case 3:
            cr->set_source_rgb(0.00, 0.62, 0.62);
            break;
        case 4:
            cr->set_source_rgb(0.86, 0.15, 0.15);
            break;
        default:
            cr->set_source_rgb(0.12, 0.12, 0.12);
            break;
        }
        cr->move_to(polyline.points.front().x, polyline.points.front().y);
        for (size_t i = 1; i < polyline.points.size(); i++)
            cr->line_to(polyline.points.at(i).x, polyline.points.at(i).y);
        cr->stroke();
    }

    cr->restore();
#else
    (void)cr;
    (void)w;
    (void)h;
#endif
}

bool Editor::commit_generator_polyline_groups(const std::vector<std::vector<std::vector<glm::dvec2>>> &polyline_groups, bool closed,
                                              const std::string &rebuild_reason, bool clusterize_each_group)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_core.has_documents())
        return false;
    if (m_core.tool_is_active() && !force_end_tool())
        return false;
    if (polyline_groups.empty())
        return false;

    auto &doc = m_core.get_current_document();
    auto &group = doc.get_group(m_core.get_current_group());
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    if (!sketch) {
        m_workspace_browser->show_toast("Generator tools work in sketch groups");
        return false;
    }

    const auto wrkpl_uu = m_core.get_current_workplane();
    auto *wrkpl = doc.get_entity_ptr<EntityWorkplane>(wrkpl_uu);
    if (!wrkpl) {
        m_workspace_browser->show_toast("Current group needs an active workplane");
        return false;
    }

    ensure_current_group_layers_initialized();
    auto layer_uu = get_active_layer_for_current_group();
    if (!layer_uu)
        layer_uu = sketch->get_default_layer_uuid();

    const auto cursor_world = get_canvas().get_cursor_pos_for_plane(wrkpl->m_origin, wrkpl->get_normal_vector());
    const auto offset = wrkpl->project(cursor_world);

    std::set<SelectableRef> selection;
    bool changed = false;
    for (const auto &group_polylines : polyline_groups) {
        std::vector<UUID> created_entities;
        for (const auto &polyline : group_polylines) {
            if (polyline.size() < 2)
                continue;
            const auto segment_count = polyline.size() - 1 + (closed ? 1 : 0);
            for (size_t i = 0; i < segment_count; i++) {
                const auto p1 = polyline.at(i) + offset;
                const auto p2 = polyline.at((i + 1) % polyline.size()) + offset;
                if (glm::length(p2 - p1) < 1e-6)
                    continue;
                auto line = std::make_unique<EntityLine2D>(UUID::random());
                line->m_group = group.m_uuid;
                line->m_layer = layer_uu;
                line->m_wrkpl = wrkpl_uu;
                line->m_p1 = p1;
                line->m_p2 = p2;
                created_entities.push_back(line->m_uuid);
                doc.m_entities.emplace(line->m_uuid, std::move(line));
                changed = true;
            }
        }

        if (clusterize_each_group && created_entities.size() >= 2) {
            auto cluster = std::make_unique<EntityCluster>(UUID::random());
            cluster->m_group = group.m_uuid;
            cluster->m_layer = layer_uu;
            cluster->m_wrkpl = wrkpl_uu;

            auto content = ClusterContent::create();
            const auto cloned_wrkpl_uu = doc.get_reference_group().get_workplane_xy_uuid();
            for (const auto &uu : created_entities) {
                auto *entity = doc.get_entity_ptr(uu);
                if (!entity)
                    continue;
                auto clone = entity->clone();
                if (auto *cluster_en = dynamic_cast<IEntityInWorkplaneSet *>(clone.get()))
                    cluster_en->set_workplane(cloned_wrkpl_uu);
                content->m_entities.emplace(uu, std::move(clone));
            }
            cluster->m_content = content;
            const auto cluster_uu = cluster->m_uuid;
            doc.m_entities.emplace(cluster_uu, std::move(cluster));

            ItemsToDelete items_to_delete;
            for (const auto &uu : created_entities)
                items_to_delete.entities.insert(uu);
            const auto extra_items = doc.get_additional_items_to_delete(items_to_delete);
            items_to_delete.append(extra_items);
            doc.delete_items(items_to_delete);

            selection.emplace(SelectableRef::Type::ENTITY, cluster_uu, 0);
        }
        else {
            for (const auto &uu : created_entities)
                selection.emplace(SelectableRef::Type::ENTITY, uu, 0);
        }
    }

    if (!changed)
        return false;

    m_core.set_needs_save();
    m_core.rebuild(rebuild_reason);
    get_canvas().set_selection(selection, false);
    canvas_update_keep_selection();
    update_action_sensitivity();
    rebuild_layers_popover();
    return true;
#else
    (void)polyline_groups;
    (void)closed;
    (void)rebuild_reason;
    return false;
#endif
}

bool Editor::commit_generator_polylines(const std::vector<std::vector<glm::dvec2>> &polylines, bool closed,
                                        const std::string &rebuild_reason, bool clusterize)
{
#ifdef DUNE_SKETCHER_ONLY
    if (polylines.empty())
        return false;
    std::vector<std::vector<std::vector<glm::dvec2>>> groups;
    groups.push_back(polylines);
    return commit_generator_polyline_groups(groups, closed, rebuild_reason, clusterize);
#else
    (void)polylines;
    (void)closed;
    (void)rebuild_reason;
    (void)clusterize;
    return false;
#endif
}

bool Editor::generate_gears_from_selected_profile()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_core.has_documents())
        return false;
    if (m_core.tool_is_active() && !force_end_tool())
        return true;
    if (!m_gears_module_spin)
        return false;

    auto &doc = m_core.get_current_document();
    auto &group = doc.get_group(m_core.get_current_group());
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    if (!sketch)
        return false;

    GearProfileSource source;
    std::string error;
    if (!collect_selected_gear_profile_source(doc, get_canvas().get_selection(), group.m_uuid, source, &error))
        return false;

    const auto module_mm = std::max(0.05, m_gears_module_spin->get_value());
    const auto inward = m_gears_inward_switch && m_gears_inward_switch->get_active();

    std::vector<glm::dvec2> profile;
    bool closed = false;
    switch (source.kind) {
    case GearProfileSource::Kind::CIRCLE:
        {
            GearGeneratorParams params;
            params.module = module_mm;
            params.teeth = std::clamp(std::max(3, static_cast<int>(std::lround((2.0 * source.radius) / module_mm))), 3, 720);
            params.pressure_angle_deg = m_gears_pressure_angle_spin ? m_gears_pressure_angle_spin->get_value() : 20.0;
            params.backlash_mm = std::max(0.0, m_gears_backlash_spin ? m_gears_backlash_spin->get_value() : 0.0);
            params.involute_segments =
                    std::max(4, static_cast<int>(std::lround(m_gears_segments_spin ? m_gears_segments_spin->get_value() : 12.0)));
            params.bore_diameter_mm = 0.0;

            std::vector<glm::dvec2> outline;
            if (!build_involute_gear_outline(params, outline))
                outline = build_radial_teeth_profile({0, 0}, source.radius, 0.0, 2.0 * M_PI, true, module_mm, false);

            const auto pitch_radius = params.module * static_cast<double>(params.teeth) * 0.5;
            for (auto &p : outline) {
                auto q = p;
                if (inward) {
                    const auto r = glm::length(q);
                    if (r > 1e-9) {
                        const auto dir = q / r;
                        const auto mirrored_r = std::max(0.05, 2.0 * pitch_radius - r);
                        q = dir * mirrored_r;
                    }
                }
                q += source.center;
                profile.push_back(q);
            }
        }
        closed = true;
        break;
    case GearProfileSource::Kind::ARC:
        profile = build_radial_teeth_profile(source.center, source.radius, source.start_angle, source.sweep_angle, false, module_mm,
                                             inward);
        closed = false;
        break;
    case GearProfileSource::Kind::BEZIER_CHAIN:
        profile = build_polyline_teeth_profile(source.polyline, source.closed, module_mm, inward);
        closed = source.closed;
        break;
    }
    if (profile.size() < 2) {
        if (m_workspace_browser)
            m_workspace_browser->show_toast("Couldn't generate gears from selected profile");
        return true;
    }

    std::vector<std::pair<std::vector<glm::dvec2>, bool>> output_paths;
    output_paths.emplace_back(profile, closed);

    if (m_gears_bore_spin && m_gears_material_thickness_spin) {
        bool can_add_hole = false;
        glm::dvec2 hole_center{0, 0};
        if (source.kind == GearProfileSource::Kind::CIRCLE || source.kind == GearProfileSource::Kind::ARC) {
            hole_center = source.center;
            can_add_hole = true;
        }
        else if (source.kind == GearProfileSource::Kind::BEZIER_CHAIN && source.closed && !source.polyline.empty()) {
            glm::dvec2 sum{0, 0};
            for (const auto &p : source.polyline)
                sum += p;
            hole_center = sum / static_cast<double>(source.polyline.size());
            can_add_hole = true;
        }

        if (can_add_hole) {
        std::vector<std::vector<glm::dvec2>> holes;
        const auto segments = std::max(24, m_gears_segments_spin ? static_cast<int>(std::lround(m_gears_segments_spin->get_value())) * 4 : 48);
        append_gear_hole_polylines(holes, static_cast<int>(m_gears_hole_mode), std::max(0.0, m_gears_bore_spin->get_value()),
                                   std::max(0.1, m_gears_material_thickness_spin->get_value()), hole_center, 0.0, segments);
        for (auto &h : holes)
            output_paths.emplace_back(std::move(h), true);
        }
    }

    std::set<SelectableRef> selection;
    std::vector<std::unique_ptr<EntityLine2D>> new_entities;
    for (const auto &[path, path_closed] : output_paths) {
        const auto seg_count = path.size() - 1 + (path_closed ? 1 : 0);
        for (size_t i = 0; i < seg_count; i++) {
            const auto &p1 = path.at(i);
            const auto &p2 = path.at((i + 1) % path.size());
            if (glm::length(p2 - p1) < 1e-6)
                continue;
            auto en = std::make_unique<EntityLine2D>(UUID::random());
            en->m_group = group.m_uuid;
            en->m_layer = source.layer;
            en->m_wrkpl = source.wrkpl;
            en->m_p1 = p1;
            en->m_p2 = p2;
            new_entities.push_back(std::move(en));
        }
    }
    if (new_entities.empty()) {
        if (m_workspace_browser)
            m_workspace_browser->show_toast("Couldn't generate gears from selected profile");
        return true;
    }

    get_canvas().set_selection({}, false);

    ItemsToDelete items_to_delete;
    for (const auto &uu : source.source_entities)
        items_to_delete.entities.insert(uu);
    const auto deleted_entities = items_to_delete.entities;
    auto extra_items = doc.get_additional_items_to_delete(items_to_delete);
    items_to_delete.append(extra_items);
    doc.delete_items(items_to_delete);

    for (auto &[uu, entity] : doc.m_entities) {
        (void)uu;
        if (entity->m_group != group.m_uuid)
            continue;
        for (auto it = entity->m_move_instead.begin(); it != entity->m_move_instead.end();) {
            const auto &enp = it->second;
            if (deleted_entities.contains(enp.entity) || !doc.get_entity_ptr(enp.entity))
                it = entity->m_move_instead.erase(it);
            else
                ++it;
        }
    }

    std::vector<UUID> created_entities;
    created_entities.reserve(new_entities.size());
    for (auto &en : new_entities) {
        created_entities.push_back(en->m_uuid);
        selection.emplace(SelectableRef::Type::ENTITY, en->m_uuid, 0);
        doc.m_entities.emplace(en->m_uuid, std::move(en));
    }

    if (created_entities.size() >= 2) {
        auto cluster = std::make_unique<EntityCluster>(UUID::random());
        cluster->m_group = group.m_uuid;
        cluster->m_layer = source.layer;
        cluster->m_wrkpl = source.wrkpl;

        auto content = ClusterContent::create();
        const auto cloned_wrkpl_uu = doc.get_reference_group().get_workplane_xy_uuid();
        for (const auto &uu : created_entities) {
            auto *entity = doc.get_entity_ptr(uu);
            if (!entity)
                continue;
            auto clone = entity->clone();
            if (auto *cluster_en = dynamic_cast<IEntityInWorkplaneSet *>(clone.get()))
                cluster_en->set_workplane(cloned_wrkpl_uu);
            content->m_entities.emplace(uu, std::move(clone));
        }
        cluster->m_content = content;
        const auto cluster_uu = cluster->m_uuid;
        doc.m_entities.emplace(cluster_uu, std::move(cluster));

        ItemsToDelete created_items;
        for (const auto &uu : created_entities)
            created_items.entities.insert(uu);
        const auto created_extra = doc.get_additional_items_to_delete(created_items);
        created_items.append(created_extra);
        doc.delete_items(created_items);

        selection.clear();
        selection.emplace(SelectableRef::Type::ENTITY, cluster_uu, 0);
    }

    get_canvas().set_selection(selection, false);
    m_core.set_needs_save();
    m_core.rebuild("generate gears from selected profile");
    canvas_update_keep_selection();
    update_action_sensitivity();
    rebuild_layers_popover();
    if (m_workspace_browser)
        m_workspace_browser->show_toast("Gears generated from selected profile");
    return true;
#else
    return false;
#endif
}

void Editor::generate_gears_geometry()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_gears_module_spin || !m_gears_teeth_spin || !m_gears_pressure_angle_spin || !m_gears_backlash_spin
        || !m_gears_segments_spin || !m_gears_bore_spin)
        return;

    GearGeneratorParams params;
    params.module = std::max(0.01, m_gears_module_spin->get_value());
    params.teeth = std::max(3, static_cast<int>(std::lround(m_gears_teeth_spin->get_value())));
    params.pressure_angle_deg = m_gears_pressure_angle_spin->get_value();
    params.backlash_mm = std::max(0.0, m_gears_backlash_spin->get_value());
    params.involute_segments = std::max(4, static_cast<int>(std::lround(m_gears_segments_spin->get_value())));
    params.bore_diameter_mm = std::max(0.0, m_gears_bore_spin->get_value());

    std::vector<glm::dvec2> outline;
    if (!build_involute_gear_outline(params, outline)) {
        m_workspace_browser->show_toast("Couldn't generate gear with current parameters");
        return;
    }

    std::vector<std::vector<glm::dvec2>> polylines;
    polylines.push_back(outline);

    append_gear_hole_polylines(polylines, static_cast<int>(m_gears_hole_mode), params.bore_diameter_mm,
                               std::max(0.1, m_gears_material_thickness_spin ? m_gears_material_thickness_spin->get_value() : 3.0),
                               {0, 0}, 0.0, std::max(24, params.involute_segments * 4));

    if (!commit_generator_polylines(polylines, true, "generate gear", true))
        m_workspace_browser->show_toast("Gear generation failed");
#endif
}

void Editor::generate_joints_geometry()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!generate_joints_from_selected_lines() && m_workspace_browser)
        m_workspace_browser->show_toast("Select a valid edge selection for Edge Features");
#endif
}

bool Editor::generate_joints_from_selected_lines()
{
#ifdef DUNE_SKETCHER_ONLY
    try {
    if (!m_core.has_documents())
        return false;
    if (m_core.tool_is_active() && !force_end_tool())
        return true;

    auto &doc = m_core.get_current_document();
    auto &group = doc.get_group(m_core.get_current_group());
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    if (!sketch)
        return false;

    const auto selected = get_canvas().get_selection();
    auto lines = selected_line_entities_in_group(doc, selected, group.m_uuid);
    if (lines.empty())
        return false;

    struct LineData {
        UUID uu;
        UUID layer;
        UUID wrkpl;
        glm::dvec2 p1;
        glm::dvec2 p2;
    };

    std::vector<LineData> line_data;
    line_data.reserve(lines.size());
    for (const auto &line_uu : lines) {
        auto *line = doc.get_entity_ptr<EntityLine2D>(line_uu);
        if (!line)
            return true;
        line_data.push_back({line->m_uuid, line->m_layer, line->m_wrkpl, line->m_p1, line->m_p2});
    }

    std::string metadata_error;
    if (!ensure_joint_families_loaded(metadata_error)) {
        if (m_workspace_browser)
            m_workspace_browser->show_toast(metadata_error.empty() ? "Couldn't load edge feature families" : metadata_error);
        return true;
    }
    if (g_joint_families.empty()) {
        if (m_workspace_browser)
            m_workspace_browser->show_toast("Edge Features catalog is empty");
        return true;
    }

    const auto *family = get_selected_joint_family(m_joints_family_dropdown, m_joints_visible_family_indices);
    if (!family) {
        if (m_workspace_browser)
            m_workspace_browser->show_toast("No edge feature family selected");
        return true;
    }
    const auto *role = get_selected_joint_role(*family, m_joints_role_dropdown);
    if (!role) {
        if (m_workspace_browser)
            m_workspace_browser->show_toast("No edge feature operation selected");
        return true;
    }
    const auto snapshot = capture_joint_values_snapshot(*family, *role, m_joints_spin_settings, m_joints_entry_settings,
                                                        m_joints_dropdown_settings, m_joints_switch_settings);
    const auto thickness_mm = m_joints_thickness_spin ? std::max(0.1, m_joints_thickness_spin->get_value()) : 3.0;
    const auto burn_mm = m_joints_burn_spin ? std::max(0.0, m_joints_burn_spin->get_value()) : 0.1;
    const auto side_mode = joint_side_mode_from_index(m_joints_side_dropdown ? m_joints_side_dropdown->get_selected() : 0);
    const auto flip_direction = m_joints_flip_direction_switch && m_joints_flip_direction_switch->get_active();
    if (flip_direction) {
        for (auto &line : line_data)
            std::swap(line.p1, line.p2);
    }

    const auto pair_mode_candidate = line_data.size() == 2 && line_data[0].wrkpl == line_data[1].wrkpl;
    const auto dir0 = line_data[0].p2 - line_data[0].p1;
    const auto dir1 = pair_mode_candidate ? (line_data[1].p2 - line_data[1].p1) : glm::dvec2{0, 0};
    const auto len0 = glm::length(dir0);
    const auto len1 = pair_mode_candidate ? glm::length(dir1) : 0.0;
    if (len0 < 1e-6 || (pair_mode_candidate && len1 < 1e-6)) {
        if (m_workspace_browser)
            m_workspace_browser->show_toast("Selected edges are too short for Edge Features");
        return true;
    }

    auto dir0n = dir0 / len0;
    auto dir1n = pair_mode_candidate ? (dir1 / len1) : glm::dvec2{0, 0};
    if (pair_mode_candidate && glm::dot(dir0n, dir1n) < 0) {
        std::swap(line_data[1].p1, line_data[1].p2);
        dir1n = -dir1n;
    }
    const auto pair_mode = pair_mode_candidate;
    const auto map_joint_segment = [](const BoxesSvgSegment &local_seg, const LineData &line, double side_sign) {
        const auto delta = line.p2 - line.p1;
        const auto length = glm::length(delta);
        const auto tangent = delta / std::max(length, 1e-9);
        const auto normal = normalize_dir(glm::dvec2{-tangent.y, tangent.x}) * side_sign;
        const auto map_point = [&](const glm::dvec2 &p) { return line.p1 + tangent * p.x + normal * (-p.y); };
        auto seg = local_seg;
        seg.p1 = map_point(local_seg.p1);
        seg.p2 = map_point(local_seg.p2);
        if (seg.kind == BoxesSvgSegment::Kind::BEZIER) {
            seg.c1 = map_point(local_seg.c1);
            seg.c2 = map_point(local_seg.c2);
        }
        return seg;
    };

    struct GeneratedJointSegment {
        BoxesSvgSegment seg;
        LineData line;
    };
    std::vector<GeneratedJointSegment> generated_segments;
    auto emit_edge = [&](const LineData &line, const JointEdgeDef *edge, double side_sign) {
        if (!edge)
            return false;
        std::vector<BoxesSvgSegment> local_segments;
        glm::dvec2 bbox_min{0, 0};
        glm::dvec2 bbox_max{0, 0};
        std::string error;
        if (!generate_joint_edge_segments(*family, *edge, glm::length(line.p2 - line.p1), snapshot, thickness_mm, burn_mm,
                                          local_segments, bbox_min, bbox_max, error)) {
            if (m_workspace_browser)
                m_workspace_browser->show_toast(error.empty() ? "Edge Features helper failed" : error);
            return false;
        }
        for (const auto &seg : local_segments)
            generated_segments.push_back({map_joint_segment(seg, line, side_sign), line});
        return true;
    };

    if (pair_mode && role->pair) {
        const auto c0 = (line_data[0].p1 + line_data[0].p2) * 0.5;
        const auto c1 = (line_data[1].p1 + line_data[1].p2) * 0.5;
        auto normal0 = normalize_dir(glm::dvec2{-dir0n.y, dir0n.x});
        auto normal1 = normalize_dir(glm::dvec2{-dir1n.y, dir1n.x});
        const auto side0_toward_pair = glm::dot(normal0, c1 - c0) >= 0 ? 1.0 : -1.0;
        const auto side1_toward_pair = glm::dot(normal1, c0 - c1) >= 0 ? 1.0 : -1.0;
        const auto side0_feature = side_mode == JointSideMode::OUTSIDE ? -side0_toward_pair : side0_toward_pair;
        const auto side1_feature = side_mode == JointSideMode::OUTSIDE ? -side1_toward_pair : side1_toward_pair;
        const auto side0_recess = -side0_feature;
        const auto side1_recess = -side1_feature;

        const auto *edge0 = find_joint_edge(*family, role->line0_edge);
        const auto *edge1 = find_joint_edge(*family, role->line1_edge);
        const auto line0_side = edge0 && edge0->side_mode == "recess" ? side0_recess : side0_feature;
        const auto line1_side = edge1 && edge1->side_mode == "recess" ? side1_recess : side1_feature;
        if (!emit_edge(line_data[0], edge0, line0_side) || !emit_edge(line_data[1], edge1, line1_side))
            return true;
    }
    else {
        if (role->pair) {
            if (m_workspace_browser)
                m_workspace_browser->show_toast(joint_selection_hint(*family, *role));
            return true;
        }
        const auto *edge = find_joint_edge(*family, role->line0_edge);
        if (!edge) {
            if (m_workspace_browser)
                m_workspace_browser->show_toast("Selected joint edge is missing");
            return true;
        }

        for (const auto &line : line_data) {
            const auto side_toward_center =
                    infer_line_side_toward_closed_loop_center(doc, group.m_uuid, line.wrkpl, line.uu, line.p1, line.p2).value_or(1.0);
            const auto outward_side = -side_toward_center;
            const auto feature_side = side_mode == JointSideMode::OUTSIDE ? outward_side : side_toward_center;
            const auto recess_side = side_toward_center;
            const auto side_sign = edge->side_mode == "recess" ? recess_side : feature_side;
            if (!emit_edge(line, edge, side_sign))
                return true;
        }
    }

    if (generated_segments.empty()) {
        if (m_workspace_browser)
            m_workspace_browser->show_toast("Couldn't generate joints from selected lines");
        return true;
    }

    std::set<SelectableRef> selection;
    std::vector<std::unique_ptr<Entity>> new_entities;
    struct CoincidentCandidate {
        EntityAndPoint ref;
        UUID wrkpl;
    };
    std::map<LoopEndpointKey, std::vector<CoincidentCandidate>> coincident_candidates;
    auto same_point = [](const glm::dvec2 &a, const glm::dvec2 &b) { return glm::length(a - b) < 1e-6; };
    auto add_segment = [&](const BoxesSvgSegment &seg, const LineData &line) {
        if (glm::length(seg.p2 - seg.p1) < 1e-6)
            return;
        if (seg.kind == BoxesSvgSegment::Kind::LINE) {
            auto en = std::make_unique<EntityLine2D>(UUID::random());
            en->m_group = group.m_uuid;
            en->m_layer = line.layer;
            en->m_wrkpl = line.wrkpl;
            en->m_p1 = seg.p1;
            en->m_p2 = seg.p2;
            coincident_candidates[make_loop_endpoint_key(seg.p1)].push_back({{en->m_uuid, 1}, line.wrkpl});
            coincident_candidates[make_loop_endpoint_key(seg.p2)].push_back({{en->m_uuid, 2}, line.wrkpl});
            selection.emplace(SelectableRef::Type::ENTITY, en->m_uuid, 0);
            new_entities.push_back(std::move(en));
        }
        else {
            auto en = std::make_unique<EntityBezier2D>(UUID::random());
            en->m_group = group.m_uuid;
            en->m_layer = line.layer;
            en->m_wrkpl = line.wrkpl;
            en->m_p1 = seg.p1;
            en->m_c1 = seg.c1;
            en->m_c2 = seg.c2;
            en->m_p2 = seg.p2;
            coincident_candidates[make_loop_endpoint_key(seg.p1)].push_back({{en->m_uuid, 1}, line.wrkpl});
            coincident_candidates[make_loop_endpoint_key(seg.p2)].push_back({{en->m_uuid, 2}, line.wrkpl});
            selection.emplace(SelectableRef::Type::ENTITY, en->m_uuid, 0);
            new_entities.push_back(std::move(en));
        }
    };
    for (const auto &item : generated_segments)
        add_segment(item.seg, item.line);

    if (new_entities.empty()) {
        if (m_workspace_browser)
            m_workspace_browser->show_toast("Couldn't generate joints from selected lines");
        return true;
    }

    get_canvas().set_selection({}, false);

    ItemsToDelete items_to_delete;
    for (const auto &line : line_data)
        items_to_delete.entities.insert(line.uu);
    const auto deleted_entities = items_to_delete.entities;
    auto extra_items = doc.get_additional_items_to_delete(items_to_delete);
    items_to_delete.append(extra_items);
    doc.delete_items(items_to_delete);

    for (auto &[uu, entity] : doc.m_entities) {
        (void)uu;
        if (entity->m_group != group.m_uuid)
            continue;
        for (auto it = entity->m_move_instead.begin(); it != entity->m_move_instead.end();) {
            const auto &enp = it->second;
            if (deleted_entities.contains(enp.entity) || !doc.get_entity_ptr(enp.entity))
                it = entity->m_move_instead.erase(it);
            else
                ++it;
        }
    }

    for (auto &en : new_entities)
        doc.m_entities.emplace(en->m_uuid, std::move(en));

    for (const auto &[key, refs] : coincident_candidates) {
        (void)key;
        if (refs.size() < 2)
            continue;
        const auto &anchor = refs.front();
        for (size_t i = 1; i < refs.size(); i++) {
            const auto &other = refs.at(i);
            if (anchor.ref.entity == other.ref.entity && anchor.ref.point == other.ref.point)
                continue;
            if (anchor.wrkpl != other.wrkpl)
                continue;
            auto co = std::make_unique<ConstraintPointsCoincident>(UUID::random());
            co->m_group = group.m_uuid;
            co->m_wrkpl = anchor.wrkpl;
            co->m_entity1 = anchor.ref;
            co->m_entity2 = other.ref;
            doc.m_constraints.emplace(co->m_uuid, std::move(co));
        }
    }

    get_canvas().set_selection(selection, false);
    m_core.set_needs_save();
    m_core.rebuild("generate joints from selected lines");
    canvas_update_keep_selection();
    update_action_sensitivity();
    rebuild_layers_popover();
    if (m_workspace_browser)
        m_workspace_browser->show_toast("Edge Features generated from selected lines");
    return true;
    }
    catch (const std::exception &e) {
        if (m_workspace_browser)
            m_workspace_browser->show_toast(std::string("Edge Features failed: ") + e.what());
        Logger::log_warning(std::string("Edge Features quick mode exception: ") + e.what());
        return true;
    }
    catch (...) {
        if (m_workspace_browser)
            m_workspace_browser->show_toast("Edge Features failed with unknown error");
        Logger::log_warning("Edge Features quick mode exception: unknown");
        return true;
    }
#else
    return false;
#endif
}

void Editor::generate_boxes_geometry()
{
#ifdef DUNE_SKETCHER_ONLY
    if (m_boxes_import_generation_running)
        return;
    if (m_boxes_spin_settings.empty() && m_boxes_entry_settings.empty() && m_boxes_dropdown_settings.empty()
        && m_boxes_switch_settings.empty())
        return;
    if (!m_core.has_documents())
        return;
    if (m_core.tool_is_active() && !force_end_tool())
        return;
    const auto wrkpl_uu = m_core.get_current_workplane();
    if (!wrkpl_uu) {
        m_workspace_browser->show_toast("Current group needs an active workplane");
        return;
    }
    auto &doc = m_core.get_current_document();
    auto &group = doc.get_group(m_core.get_current_group());
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    if (!sketch) {
        m_workspace_browser->show_toast("Generator tools work in sketch groups");
        return;
    }
    auto *wrkpl = doc.get_entity_ptr<EntityWorkplane>(wrkpl_uu);
    if (!wrkpl) {
        m_workspace_browser->show_toast("Current group needs an active workplane");
        return;
    }

    const auto &template_def = get_boxes_template(m_boxes_template_index);
    const auto snapshot =
            capture_boxes_values_snapshot(template_def, m_boxes_spin_settings, m_boxes_entry_settings, m_boxes_dropdown_settings,
                                          m_boxes_switch_settings);

    ensure_current_group_layers_initialized();
    auto layer_uu = get_active_layer_for_current_group();
    if (!layer_uu)
        layer_uu = sketch->get_default_layer_uuid();

    m_boxes_import_group = group.m_uuid;
    m_boxes_import_workplane = wrkpl_uu;
    m_boxes_import_layer = layer_uu;
    m_boxes_import_target_center = wrkpl->project(glm::dvec3(get_canvas().get_center()));
    m_boxes_import_template_label = template_def.label;

    ensure_boxes_importing_window();
    if (m_boxes_importing_label)
        m_boxes_importing_label->set_text(std::string("Importing ") + template_def.label + "...");
    if (m_boxes_importing_progress)
        m_boxes_importing_progress->set_fraction(0.0);
    if (m_boxes_importing_window)
        m_boxes_importing_window->present();
    if (m_boxes_generator_window && m_boxes_generator_window->get_visible())
        m_boxes_generator_window->hide();
    if (m_boxes_import_button)
        m_boxes_import_button->set_sensitive(false);

    m_boxes_import_generation_running = true;
    m_boxes_import_future = std::async(std::launch::async, [template_def, snapshot] {
        BoxesImportAsyncResult result;
        std::vector<BoxesSvgSegment> segments;
        if (!generate_boxes_svg_segments(template_def, snapshot, segments, result.bbox_min, result.bbox_max, result.error))
            return result;
        result.segments.reserve(segments.size());
        for (const auto &seg : segments) {
            BoxesImportSegment converted;
            converted.bezier = seg.kind == BoxesSvgSegment::Kind::BEZIER;
            converted.p1 = seg.p1;
            converted.c1 = seg.c1;
            converted.c2 = seg.c2;
            converted.p2 = seg.p2;
            result.segments.push_back(std::move(converted));
        }
        return result;
    });

    if (m_boxes_import_poll_connection.connected())
        m_boxes_import_poll_connection.disconnect();
    m_boxes_import_poll_connection = Glib::signal_timeout().connect(
            [this] {
                if (!m_boxes_import_generation_running)
                    return false;
                if (m_boxes_importing_progress)
                    m_boxes_importing_progress->pulse();
                if (m_boxes_import_future.valid()
                    && m_boxes_import_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    m_boxes_ready_import_result = m_boxes_import_future.get();
                    finish_boxes_geometry_import();
                    return false;
                }
                return true;
            },
            40);
#endif
}

void Editor::ensure_current_group_layers_initialized()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_core.has_documents())
        return;
    auto &group = m_core.get_current_document().get_group(m_core.get_current_group());
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    if (!sketch)
        return;
    sketch->ensure_default_layers();
    auto active_layer = get_active_layer_for_current_group();
    if (!active_layer || !sketch->get_layer_ptr(active_layer)) {
        active_layer = sketch->get_default_layer_uuid();
        if (active_layer)
            m_active_layer_by_group[group.m_uuid] = active_layer;
    }
#endif
}

void Editor::open_layer_edit_popover(const UUID &layer_uu)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_layer_edit_popover)
        return;
    const bool same_layer = (m_layer_editing_uuid == layer_uu);
    const bool was_visible = m_layer_edit_popover->get_visible();
    m_layer_editing_uuid = layer_uu;
    refresh_layer_edit_popover();
    if (was_visible && same_layer)
        m_layer_edit_popover->popdown();
    else
        m_layer_edit_popover->popup();
#else
    (void)layer_uu;
#endif
}

void Editor::refresh_layer_edit_popover()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_layer_edit_popover || !m_layer_edit_name_entry || !m_layer_edit_icon_switch || !m_layer_edit_process_label
        || !m_layer_edit_process_box)
        return;
    if (!m_core.has_documents() || !m_layer_editing_uuid) {
        if (m_layer_edit_popover->get_visible())
            m_layer_edit_popover->popdown();
        return;
    }

    auto &group = m_core.get_current_document().get_group(m_core.get_current_group());
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    if (!sketch) {
        if (m_layer_edit_popover->get_visible())
            m_layer_edit_popover->popdown();
        return;
    }
    auto *layer = sketch->get_layer_ptr(m_layer_editing_uuid);
    if (!layer) {
        if (m_layer_edit_popover->get_visible())
            m_layer_edit_popover->popdown();
        return;
    }

    m_updating_layer_edit_popover = true;
    m_layer_edit_name_entry->set_text(layer->m_name);
    m_layer_edit_icon_switch->set_active(layer->m_show_process_icon);
    m_layer_edit_process_label->set_visible(layer->m_show_process_icon);
    m_layer_edit_process_box->set_visible(layer->m_show_process_icon);

    for (const auto &[process, button] : m_layer_edit_process_buttons) {
        if (layer->m_process == process)
            button->add_css_class("suggested-action");
        else
            button->remove_css_class("suggested-action");
    }

    for (const auto &[aci, button] : m_layer_edit_color_buttons) {
        if (layer->m_color == aci)
            button->add_css_class("suggested-action");
        else
            button->remove_css_class("suggested-action");
    }
    m_updating_layer_edit_popover = false;
#endif
}

void Editor::select_active_layer_for_current_group(const UUID &layer_uu)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_core.has_documents())
        return;
    auto &group = m_core.get_current_document().get_group(m_core.get_current_group());
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    if (!sketch)
        return;
    if (!sketch->get_layer_ptr(layer_uu))
        return;
    m_active_layer_by_group[group.m_uuid] = layer_uu;
#else
    (void)layer_uu;
#endif
}

UUID Editor::get_active_layer_for_current_group() const
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_core.has_documents())
        return {};
    const auto group_uu = m_core.get_current_group();
    if (const auto it = m_active_layer_by_group.find(group_uu); it != m_active_layer_by_group.end())
        return it->second;
    return {};
#else
    return {};
#endif
}

void Editor::move_selection_to_layer(const UUID &layer_uu)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_core.has_documents())
        return;
    if (m_core.tool_is_active())
        return;
    auto &doc = m_core.get_current_document();
    auto &group = doc.get_group(m_core.get_current_group());
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    if (!sketch)
        return;
    if (!sketch->get_layer_ptr(layer_uu))
        return;

    bool changed = false;
    for (const auto &sr : entities_from_selection(get_canvas().get_selection())) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        auto *en = doc.get_entity_ptr(sr.item);
        if (!en)
            continue;
        if (en->m_group != group.m_uuid)
            continue;
        if (en->m_layer == layer_uu)
            continue;
        en->m_layer = layer_uu;
        changed = true;
    }

    if (!changed)
        return;
    m_core.set_needs_save();
    m_core.rebuild("move entities to sketch layer");
    canvas_update_keep_selection();
    update_action_sensitivity();
#else
    (void)layer_uu;
#endif
}

void Editor::rebuild_layers_popover()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_layers_list_box)
        return;

    while (auto *child = m_layers_list_box->get_first_child())
        m_layers_list_box->remove(*child);

    if (!m_core.has_documents()) {
        auto label = Gtk::make_managed<Gtk::Label>("No active document");
        label->add_css_class("dim-label");
        label->set_xalign(0);
        m_layers_list_box->append(*label);
        refresh_layer_edit_popover();
        return;
    }

    auto &group = m_core.get_current_document().get_group(m_core.get_current_group());
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    if (!sketch) {
        auto label = Gtk::make_managed<Gtk::Label>("Layers are available in sketch groups");
        label->add_css_class("dim-label");
        label->set_xalign(0);
        m_layers_list_box->append(*label);
        refresh_layer_edit_popover();
        return;
    }

    ensure_current_group_layers_initialized();
    const auto active_layer = get_active_layer_for_current_group();

    for (const auto &layer : sketch->m_layers) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->set_hexpand(true);
        m_layers_list_box->append(*row);

        auto select_button = Gtk::make_managed<Gtk::Button>();
        select_button->set_has_frame(true);
        select_button->set_focusable(false);
        select_button->set_hexpand(true);
        select_button->set_halign(Gtk::Align::FILL);
        select_button->set_tooltip_text(layer.m_show_process_icon ? process_label(layer.m_process) : "Layer");

        auto select_content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        if (layer.m_show_process_icon) {
            auto process_image = Gtk::make_managed<Gtk::Image>();
            process_image->set_from_icon_name(process_icon_name(layer.m_process));
            process_image->set_icon_size(Gtk::IconSize::NORMAL);
            select_content->append(*process_image);
        }

        auto layer_name = Gtk::make_managed<Gtk::Label>(layer.m_name);
        layer_name->set_xalign(0);
        layer_name->set_hexpand(true);
        select_content->append(*layer_name);

        auto color_label = Gtk::make_managed<Gtk::Label>();
        color_label->set_markup("<span foreground=\"" + aci_color_hex(layer.m_color) + "\">■</span>");
        color_label->set_tooltip_text("Color ACI " + std::to_string(layer.m_color));
        select_content->append(*color_label);

        select_button->set_child(*select_content);
        if (m_layers_mode_enabled && layer.m_uuid == active_layer)
            select_button->add_css_class("suggested-action");
        row->append(*select_button);

        auto edit_button = Gtk::make_managed<Gtk::Button>();
        edit_button->set_icon_name("document-edit-symbolic");
        edit_button->set_has_frame(true);
        edit_button->set_focusable(false);
        edit_button->set_tooltip_text("Edit layer");
        row->append(*edit_button);

        const auto layer_uu = layer.m_uuid;
        select_button->signal_clicked().connect([this, layer_uu] {
            select_active_layer_for_current_group(layer_uu);
            move_selection_to_layer(layer_uu);
            rebuild_layers_popover();
        });

        edit_button->signal_clicked().connect([this, layer_uu] { open_layer_edit_popover(layer_uu); });
    }
    refresh_layer_edit_popover();
#endif
}

void Editor::capture_layer_entities_before_tool()
{
#ifdef DUNE_SKETCHER_ONLY
    m_layers_pre_tool_entities.clear();
    m_layers_pre_tool_entities_captured = false;
    m_layer_capture_group = UUID();
    if (!m_layers_mode_enabled || !m_core.has_documents())
        return;
    auto &group = m_core.get_current_document().get_group(m_core.get_current_group());
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    if (!sketch)
        return;
    ensure_current_group_layers_initialized();
    m_layer_capture_group = group.m_uuid;
    const auto &doc = m_core.get_current_document();
    for (const auto &[uu, en] : doc.m_entities) {
        if (en->m_group == group.m_uuid)
            m_layers_pre_tool_entities.insert(uu);
    }
    m_layers_pre_tool_entities_captured = true;
#endif
}

void Editor::apply_active_layer_to_new_entities_after_commit()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_layers_mode_enabled || !m_core.has_documents())
        return;
    if (!m_layers_pre_tool_entities_captured)
        return;
    auto &doc = m_core.get_current_document();
    auto &group = doc.get_group(m_core.get_current_group());
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    if (!sketch)
        return;
    if (group.m_uuid != m_layer_capture_group)
        return;
    const auto active_layer = get_active_layer_for_current_group();
    if (!active_layer || !sketch->get_layer_ptr(active_layer))
        return;

    bool changed = false;
    for (const auto &[uu, en] : doc.m_entities) {
        if (en->m_group != group.m_uuid)
            continue;
        if (m_layers_pre_tool_entities.contains(uu))
            continue;
        if (en->m_kind != ItemKind::USER)
            continue;
        if (en->m_layer != active_layer) {
            en->m_layer = active_layer;
            changed = true;
        }
        m_layers_pre_tool_entities.insert(uu);
    }
    if (!changed)
        return;

    m_core.set_needs_save();
    m_core.rebuild("assign sketch layer");
    canvas_update_keep_selection();
    update_action_sensitivity();
    rebuild_layers_popover();
#endif
}

void Editor::activate_selection_mode()
{
#ifdef DUNE_SKETCHER_ONLY
    m_sticky_draw_tool = ToolID::NONE;
    m_sticky_tool_restart_connection.disconnect();
    m_restarting_sticky_tool = false;
    if (m_core.tool_is_active())
        force_end_tool();
    get_canvas().set_selection_mode(SelectionMode::NORMAL);
    update_sketcher_toolbar_button_states();
#endif
}

void Editor::update_sketcher_toolbar_button_states()
{
#ifdef DUNE_SKETCHER_ONLY
    const auto active_tool = m_core.get_tool_id();
    for (const auto &[act, button] : m_action_bar_buttons) {
        bool active = false;
        if (const auto tool = std::get_if<ToolID>(&act)) {
            if (m_core.tool_is_active()) {
                active = (*tool == active_tool);
            }
            else {
                active = (*tool == m_sticky_draw_tool) && is_sticky_draw_tool(*tool);
            }
        }
        if (active)
            button->add_css_class("sketch-toolbar-active");
        else
            button->remove_css_class("sketch-toolbar-active");
    }

    if (m_selection_mode_button) {
        if (m_core.has_documents() && m_sticky_draw_tool == ToolID::NONE && !m_core.tool_is_active())
            m_selection_mode_button->add_css_class("sketch-toolbar-active");
        else
            m_selection_mode_button->remove_css_class("sketch-toolbar-active");
    }

    if (m_layers_mode_button) {
        if (m_layers_mode_enabled)
            m_layers_mode_button->add_css_class("sketch-toolbar-active");
        else
            m_layers_mode_button->remove_css_class("sketch-toolbar-active");
    }

    if (m_cup_template_button) {
        if (m_cup_template_enabled)
            m_cup_template_button->add_css_class("sketch-toolbar-active");
        else
            m_cup_template_button->remove_css_class("sketch-toolbar-active");
    }

    if (m_gears_button) {
        if (m_gears_mode_enabled || (m_gears_popover && m_gears_popover->get_visible()))
            m_gears_button->add_css_class("sketch-toolbar-active");
        else
            m_gears_button->remove_css_class("sketch-toolbar-active");
    }
    if (m_joints_button) {
        if (m_joints_mode_enabled || (m_joints_popover && m_joints_popover->get_visible()))
            m_joints_button->add_css_class("sketch-toolbar-active");
        else
            m_joints_button->remove_css_class("sketch-toolbar-active");
    }
    if (m_boxes_button) {
        if (m_boxes_generator_window && m_boxes_generator_window->get_visible())
            m_boxes_button->add_css_class("sketch-toolbar-active");
        else
            m_boxes_button->remove_css_class("sketch-toolbar-active");
    }

    if (m_grid_menu_button) {
        if (get_canvas().get_grid_enabled())
            m_grid_menu_button->add_css_class("sketch-toolbar-active");
        else
            m_grid_menu_button->remove_css_class("sketch-toolbar-active");
    }

    if (m_symmetry_menu_button) {
        if (m_symmetry_enabled)
            m_symmetry_menu_button->add_css_class("sketch-toolbar-active");
        else
            m_symmetry_menu_button->remove_css_class("sketch-toolbar-active");
    }
    update_radial_menu_button_states();
#endif
}

void Editor::update_action_bar_buttons_sensitivity()
{
    auto has_docs = m_core.has_documents();
    for (auto &[act, bu] : m_action_bar_buttons) {
        auto sensitive = false;
        if (has_docs) {
            if (std::holds_alternative<ActionID>(act)) {
                auto a = std::get<ActionID>(act);
                if (m_action_sensitivity.contains(a))
                    sensitive = m_action_sensitivity.at(a);
            }
            else {
                sensitive = m_core.tool_can_begin(std::get<ToolID>(act), {}).get_can_begin();
            }
        }
        bu->set_sensitive(sensitive);
    }
#ifdef DUNE_SKETCHER_ONLY
    if (m_selection_mode_button)
        m_selection_mode_button->set_sensitive(has_docs);
    if (m_layers_mode_button)
        m_layers_mode_button->set_sensitive(has_docs);
    if (m_cup_template_button)
        m_cup_template_button->set_sensitive(has_docs);
    if (m_gears_button)
        m_gears_button->set_sensitive(has_docs);
    if (m_joints_button)
        m_joints_button->set_sensitive(has_docs);
    if (m_boxes_button)
        m_boxes_button->set_sensitive(has_docs);
    if (m_boxes_import_button)
        m_boxes_import_button->set_sensitive(has_docs && !m_boxes_import_generation_running);
    if (m_grid_menu_button)
        m_grid_menu_button->set_sensitive(has_docs);
    if (m_symmetry_menu_button)
        m_symmetry_menu_button->set_sensitive(has_docs);
    update_sketcher_toolbar_button_states();
#endif
}

void Editor::update_action_bar_visibility()
{
    bool visible = false;
    if (m_preferences.action_bar.enable && m_core.has_documents()) {
        auto tool_is_active = m_core.tool_is_active();
        if (m_preferences.action_bar.show_in_tool)
            visible = true;
        else
            visible = !tool_is_active;
    }
    m_win.set_action_bar_visible(visible);
}

static std::string action_tool_id_to_string(ActionToolID id)
{
    if (auto tool = std::get_if<ToolID>(&id))
        return "T_" + tool_lut.lookup_reverse(*tool);
    else if (auto act = std::get_if<ActionID>(&id))
        return "A_" + action_lut.lookup_reverse(*act);
    throw std::runtime_error("invalid action");
}

void Editor::init_canvas()
{
    {
        auto controller = Gtk::EventControllerKey::create();
        controller->signal_key_pressed().connect(
                [this, controller](guint keyval, guint keycode, Gdk::ModifierType state) -> bool {
                    return handle_action_key(controller, keyval, keycode, state);
                },
                true);

        get_canvas().add_controller(controller);
    }
    {
        auto controller = Gtk::GestureClick::create();
        controller->set_button(0);
        controller->signal_pressed().connect([this, controller](int n_press, double x, double y) {
            auto button = controller->get_current_button();
            if (button == 1)
                m_primary_button_pressed = true;
#ifdef DUNE_SKETCHER_ONLY
            if (button == 2 && n_press == 1) {
                m_middle_click_toggle_candidate = true;
                m_middle_click_press_pos = {x, y};
                return;
            }
#else
            if (n_press == 2 && button == 2) {
                trigger_action(ActionID::VIEW_RESET_TILT);
                return;
            }
#endif
            if (button == 3) {
                m_rmb_last_x = x;
                m_rmb_last_y = y;
            }
#ifdef G_OS_WIN32
            static const int button_next = 4;
            static const int button_prev = 5;
#else
            static const int button_next = 8;
            static const int button_prev = 9;
#endif
            if (button == 1 /*|| button == 3*/)
                handle_click(button, n_press);
            else if (button == button_next)
                trigger_action(ActionID::NEXT_GROUP);
            else if (button == button_prev)
                trigger_action(ActionID::PREVIOUS_GROUP);
        });

        controller->signal_released().connect([this, controller](int n_press, double x, double y) {
            m_drag_tool = ToolID::NONE;
            const auto button = controller->get_current_button();
            const auto state = controller->get_current_event_state();
            if (button == 1)
                m_primary_button_pressed = false;
#ifdef DUNE_SKETCHER_ONLY
            if (button == 2) {
                const auto dist = glm::length(glm::dvec2{x, y} - m_middle_click_press_pos);
                const auto should_toggle = m_middle_click_toggle_candidate && n_press == 1 && dist <= 16.0;
                m_middle_click_toggle_candidate = false;
                if (should_toggle) {
                    toggle_selection_mode_or_last_draw_tool();
                    return;
                }
            }
#endif
#ifdef DUNE_SKETCHER_ONLY
            if (button == 1 && m_selection_transform_drag_active) {
                end_selection_transform_drag();
                return;
            }
#endif
            if (button == 1 && n_press == 1) {
                if (m_core.tool_is_active()) {
                    ToolArgs args;
                    args.type = ToolEventType::ACTION;
                    args.action = InToolActionID::LMB;
                    ToolResponse r = tool_update_with_symmetry(args);
                    tool_process(r);
                }
                else if (m_joints_mode_enabled && static_cast<bool>(state & Gdk::ModifierType::SHIFT_MASK)) {
                    update_joints_quick_popover(true);
                }
            }
            else if (button == 3 && n_press == 1) {
                const auto dist = glm::length(glm::vec2(x, y) - glm::vec2(m_rmb_last_x, m_rmb_last_y));
                if (dist > 16)
                    return;
                if (m_core.tool_is_active()) {
                    ToolArgs args;
                    args.type = ToolEventType::ACTION;
                    args.action = InToolActionID::RMB;
                    ToolResponse r = tool_update_with_symmetry(args);
                    tool_process(r);
                }
                else
                    open_context_menu();
            }
        });
        controller->signal_cancel().connect([this](Gdk::EventSequence *) { m_middle_click_toggle_candidate = false; });

        get_canvas().add_controller(controller);
    }
    {
        auto controller = Gtk::EventControllerMotion::create();
        controller->signal_motion().connect([this](double x, double y) {
            if (m_last_x != x || m_last_y != y) {
                m_last_x = x;
                m_last_y = y;
            }
        });
        get_canvas().add_controller(controller);
    }
    get_canvas().signal_cursor_moved().connect(sigc::mem_fun(*this, &Editor::handle_cursor_move));
    get_canvas().signal_view_changed().connect(sigc::mem_fun(*this, &Editor::handle_view_changed));
    get_canvas().signal_select_from_menu().connect([this](const auto &sel) {
        if (m_core.tool_is_active()) {
            ToolArgs args;
            args.type = ToolEventType::ACTION;
            args.action = InToolActionID::LMB;
            ToolResponse r = tool_update_with_symmetry(args);
            tool_process(r);
        }
    });

    m_context_menu = Gtk::make_managed<Gtk::PopoverMenu>();
#if GTK_CHECK_VERSION(4, 14, 0)
    gtk_popover_menu_set_flags(m_context_menu->gobj(), GTK_POPOVER_MENU_NESTED);
#endif
    m_context_menu->set_parent(get_canvas());
    m_context_menu->signal_closed().connect([this] {
        if (m_core.reset_preview()) {
            canvas_update_keep_selection();
            m_context_menu->set_opacity(1);
        }
    });

    auto actions = Gio::SimpleActionGroup::create();
    for (const auto &[id, act] : action_catalog) {
        std::string name;
        auto action_id = id;
        actions->add_action(action_tool_id_to_string(id), [this, action_id] {
            get_canvas().set_selection(m_context_menu_selection, false);
            trigger_action(action_id);
        });
    }

    actions->add_action_with_parameter(
            "remove_constraint", Glib::Variant<std::string>::variant_type(), [this](Glib::VariantBase const &value) {
                UUID uu = Glib::VariantBase::cast_dynamic<Glib::Variant<std::string>>(value).get();
                auto &doc = m_core.get_current_document();
                doc.m_constraints.erase(uu);
                doc.set_group_solve_pending(m_core.get_current_group());
                m_core.set_needs_save();
                m_core.rebuild("remove constraint");
                canvas_update_keep_selection();
            });
    m_context_menu->insert_action_group("menu", actions);


    get_canvas().signal_hover_selection_changed().connect([this] {
        auto hsel = get_canvas().get_hover_selection();
        if (!hsel) {
            get_canvas().set_highlight({});
            return;
        }
        if (hsel->type != SelectableRef::Type::CONSTRAINT) {
            get_canvas().set_highlight({});
            return;
        }
        auto &constraint = m_core.get_current_document().get_constraint(hsel->item);
        std::set<SelectableRef> sel;
        std::map<UUID, std::set<unsigned int>> enps;
        UUID constraint_wrkpl;
        if (auto co_wrkpl = dynamic_cast<const IConstraintWorkplane *>(&constraint))
            constraint_wrkpl = co_wrkpl->get_workplane(m_core.get_current_document());
        for (const auto &enp : constraint.get_referenced_entities_and_points()) {
            // ignore constraint workplanes
            if (enp.entity == constraint_wrkpl)
                continue;
            sel.emplace(SelectableRef::Type::ENTITY, enp.entity, enp.point);
            if (enp.point != 0)
                sel.emplace(SelectableRef::Type::ENTITY, enp.entity, 0);
            enps[enp.entity].insert(enp.point);
        }
        for (const auto &[uu, pts] : enps) {
            auto &entity = m_core.get_current_document().get_entity(uu);
            if (entity.of_type(Entity::Type::LINE_2D, Entity::Type::LINE_3D)) {
                if (pts.contains(1) && pts.contains(2)) {
                    sel.emplace(SelectableRef::Type::ENTITY, uu, 0);
                }
            }
        }
        get_canvas().set_highlight(sel);
    });

    get_canvas().signal_selection_mode_changed().connect(sigc::mem_fun(*this, &Editor::update_selection_mode_label));
    update_selection_mode_label();

    get_canvas().signal_query_tooltip().connect(
            [this](int x, int y, bool keyboard_tooltip, const Glib::RefPtr<Gtk::Tooltip> &tooltip) {
                if (keyboard_tooltip)
                    return false;
                auto sr = get_canvas().get_hover_selection();
                if (!sr.has_value())
                    return false;

                auto tip = get_selectable_ref_description(m_core, m_core.get_current_idocument_info().get_uuid(), *sr);
                tooltip->set_text(tip);

                return true;
            },
            true);
    get_canvas().set_has_tooltip(true);

    get_canvas().set_selection_menu_creator(m_selection_menu_creator);

    /*
     * we want the canvas click event controllers to run before the one in the editor,
     * so we need to attach them afterwards since event controllers attached last run first
     */
    get_canvas().setup_controllers();
#ifdef DUNE_SKETCHER_ONLY
    {
        auto controller = Gtk::GestureClick::create();
        controller->set_button(0);
        controller->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
        controller->signal_pressed().connect([this, controller](int n_press, double x, double y) {
            const auto button = controller->get_current_button();
            const auto state = controller->get_current_event_state();
            if (n_press != 1)
                return;
            const bool is_trigger = matches_radial_menu_trigger(button, state);
            if (!is_trigger) {
                if (m_radial_menu_popover && m_radial_menu_popover->get_visible())
                    close_radial_menu();
                return;
            }
            controller->set_state(Gtk::EventSequenceState::CLAIMED);
            if (m_context_menu && m_context_menu->get_visible())
                m_context_menu->popdown();
            open_radial_menu(x, y);
        });
        get_canvas().add_controller(controller);
    }
#endif
}

#ifdef DUNE_SKETCHER_ONLY
void Editor::init_radial_menu()
{
    if (m_radial_menu_popover)
        return;

    m_radial_menu_popover = Gtk::make_managed<Gtk::Popover>();
    m_radial_menu_popover->add_css_class("sketch-quick-menu");
    m_radial_menu_popover->set_has_arrow(true);
    m_radial_menu_popover->set_autohide(true);
    m_radial_menu_popover->set_position(Gtk::PositionType::LEFT);
    m_radial_menu_popover->set_offset(-8, 0);
    m_radial_menu_popover->set_size_request(64, -1);
    m_radial_menu_popover->set_parent(get_canvas());

    auto layout = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    layout->set_margin_start(6);
    layout->set_margin_end(6);
    layout->set_margin_top(6);
    layout->set_margin_bottom(6);
    m_radial_menu_popover->set_child(*layout);

    const auto add_strip_button = [layout](const char *icon_name, const char *tooltip) {
        auto button = Gtk::make_managed<Gtk::Button>();
        button->set_icon_name(icon_name);
        button->set_tooltip_text(tooltip);
        button->set_has_frame(true);
        button->set_size_request(44, 44);
        button->add_css_class("sketch-toolbar-button");
        button->add_css_class("sketch-quick-menu-button");
        layout->append(*button);
        return button;
    };
    const auto create_quick_options_popover = [](Gtk::Button &anchor) {
        auto popover = Gtk::make_managed<Gtk::Popover>();
        popover->add_css_class("sketch-grid-popover");
        popover->set_has_arrow(true);
        popover->set_autohide(true);
        popover->set_position(Gtk::PositionType::LEFT);
        popover->set_parent(anchor);
        popover->set_size_request(sketch_popover_total_width, -1);
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        box->set_margin_start(12);
        box->set_margin_end(12);
        box->set_margin_top(12);
        box->set_margin_bottom(12);
        box->set_size_request(sketch_popover_content_width, -1);
        popover->set_child(*box);
        return std::pair<Gtk::Popover *, Gtk::Box *>(popover, box);
    };

    auto contour_button = add_strip_button("action-draw-contour-symbolic", "Draw contour");
    contour_button->signal_clicked().connect([this] { trigger_radial_tool(ToolID::DRAW_CONTOUR); });
    m_radial_tool_buttons[ToolID::DRAW_CONTOUR] = contour_button;

    auto rectangle_button = add_strip_button("action-draw-line-rectangle-symbolic", "Draw rectangle");
    rectangle_button->signal_clicked().connect([this] { trigger_radial_tool(ToolID::DRAW_RECTANGLE); });
    m_radial_tool_buttons[ToolID::DRAW_RECTANGLE] = rectangle_button;
    {
        auto [popover, box] = create_quick_options_popover(*rectangle_button);
        m_quick_tool_option_popovers[ToolID::DRAW_RECTANGLE] = popover;
        auto square_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto square_label = Gtk::make_managed<Gtk::Label>("Square");
        square_label->set_hexpand(true);
        square_label->set_xalign(0);
        auto square_switch = Gtk::make_managed<Gtk::Switch>();
        square_switch->set_active(ToolDrawRectangle::get_default_square());
        square_row->append(*square_label);
        square_row->append(*square_switch);
        box->append(*square_row);

        auto rounded_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto rounded_label = Gtk::make_managed<Gtk::Label>("Rounded");
        rounded_label->set_hexpand(true);
        rounded_label->set_xalign(0);
        auto rounded_switch = Gtk::make_managed<Gtk::Switch>();
        rounded_switch->set_active(ToolDrawRectangle::get_default_rounded());
        rounded_row->append(*rounded_label);
        rounded_row->append(*rounded_switch);
        box->append(*rounded_row);

        auto radius_revealer = Gtk::make_managed<Gtk::Revealer>();
        radius_revealer->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
        radius_revealer->set_transition_duration(120);
        auto radius_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto radius_label = Gtk::make_managed<Gtk::Label>("Radius");
        radius_label->set_hexpand(true);
        radius_label->set_xalign(0);
        auto radius_spin = Gtk::make_managed<Gtk::SpinButton>();
        radius_spin->set_range(0.0, 9999.99);
        radius_spin->set_increments(0.1, 1.0);
        radius_spin->set_digits(2);
        radius_spin->set_numeric(true);
        radius_spin->set_width_chars(5);
        radius_spin->set_value(ToolDrawRectangle::get_default_round_radius());
        auto mm_label = Gtk::make_managed<Gtk::Label>("mm");
        mm_label->add_css_class("dim-label");
        radius_row->append(*radius_label);
        radius_row->append(*radius_spin);
        radius_row->append(*mm_label);
        radius_revealer->set_child(*radius_row);
        radius_revealer->set_reveal_child(rounded_switch->get_active());
        box->append(*radius_revealer);

        square_switch->property_active().signal_changed().connect(
                [square_switch] { ToolDrawRectangle::set_default_square(square_switch->get_active()); });
        rounded_switch->property_active().signal_changed().connect([rounded_switch, radius_revealer] {
            const bool active = rounded_switch->get_active();
            ToolDrawRectangle::set_default_rounded(active);
            radius_revealer->set_reveal_child(active);
        });
        radius_spin->signal_value_changed().connect(
                [radius_spin] { ToolDrawRectangle::set_default_round_radius(radius_spin->get_value()); });
    }

    auto circle_button = add_strip_button("action-draw-line-circle-symbolic", "Draw circle / oval");
    circle_button->signal_clicked().connect([this] { trigger_radial_tool(ToolID::DRAW_CIRCLE_2D); });
    m_radial_tool_buttons[ToolID::DRAW_CIRCLE_2D] = circle_button;
    {
        auto [popover, box] = create_quick_options_popover(*circle_button);
        m_quick_tool_option_popovers[ToolID::DRAW_CIRCLE_2D] = popover;
        auto oval_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto oval_label = Gtk::make_managed<Gtk::Label>("Oval");
        oval_label->set_hexpand(true);
        oval_label->set_xalign(0);
        auto oval_switch = Gtk::make_managed<Gtk::Switch>();
        oval_switch->set_active(ToolDrawCircle2D::get_default_oval_mode());
        oval_row->append(*oval_label);
        oval_row->append(*oval_switch);
        box->append(*oval_row);
        oval_switch->property_active().signal_changed().connect(
                [oval_switch] { ToolDrawCircle2D::set_default_oval_mode(oval_switch->get_active()); });

        auto slice_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto slice_label = Gtk::make_managed<Gtk::Label>("Slice");
        slice_label->set_hexpand(true);
        slice_label->set_xalign(0);
        auto slice_switch = Gtk::make_managed<Gtk::Switch>();
        const auto default_span = ToolDrawCircle2D::get_default_span_degrees();
        slice_switch->set_active(default_span < 359.999);
        slice_row->append(*slice_label);
        slice_row->append(*slice_switch);
        box->append(*slice_row);

        auto angle_revealer = Gtk::make_managed<Gtk::Revealer>();
        angle_revealer->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
        angle_revealer->set_transition_duration(120);
        auto angle_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto angle_label = Gtk::make_managed<Gtk::Label>("Angle");
        angle_label->set_hexpand(true);
        angle_label->set_xalign(0);
        auto angle_spin = Gtk::make_managed<Gtk::SpinButton>();
        angle_spin->set_range(1.0, 359.0);
        angle_spin->set_increments(1.0, 15.0);
        angle_spin->set_digits(0);
        angle_spin->set_numeric(true);
        angle_spin->set_width_chars(3);
        angle_spin->set_value(std::clamp(default_span, 1.0, 359.0));
        auto deg_label = Gtk::make_managed<Gtk::Label>("deg");
        deg_label->add_css_class("dim-label");
        angle_row->append(*angle_label);
        angle_row->append(*angle_spin);
        angle_row->append(*deg_label);
        angle_revealer->set_child(*angle_row);
        angle_revealer->set_reveal_child(slice_switch->get_active());
        box->append(*angle_revealer);

        slice_switch->property_active().signal_changed().connect([slice_switch, angle_spin, angle_revealer] {
            if (slice_switch->get_active()) {
                auto span = ToolDrawCircle2D::get_default_span_degrees();
                if (span >= 359.999)
                    span = 180.0;
                ToolDrawCircle2D::set_default_span_degrees(span);
                angle_spin->set_value(std::clamp(span, 1.0, 359.0));
                angle_revealer->set_reveal_child(true);
            }
            else {
                ToolDrawCircle2D::set_default_span_degrees(360.0);
                angle_revealer->set_reveal_child(false);
            }
        });
        angle_spin->signal_value_changed().connect([angle_spin, slice_switch] {
            if (!slice_switch->get_active())
                return;
            ToolDrawCircle2D::set_default_span_degrees(angle_spin->get_value());
        });
    }

    auto polygon_button = add_strip_button("action-draw-line-regular-polygon-symbolic", "Draw polygon");
    polygon_button->signal_clicked().connect([this] { trigger_radial_tool(ToolID::DRAW_REGULAR_POLYGON); });
    m_radial_tool_buttons[ToolID::DRAW_REGULAR_POLYGON] = polygon_button;
    {
        auto [popover, box] = create_quick_options_popover(*polygon_button);
        m_quick_tool_option_popovers[ToolID::DRAW_REGULAR_POLYGON] = popover;
        auto sides_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto sides_label = Gtk::make_managed<Gtk::Label>("Sides");
        sides_label->set_hexpand(true);
        sides_label->set_xalign(0);
        auto sides_spin = Gtk::make_managed<Gtk::SpinButton>();
        sides_spin->set_range(3, 64);
        sides_spin->set_increments(1, 1);
        sides_spin->set_digits(0);
        sides_spin->set_numeric(true);
        sides_spin->set_width_chars(2);
        sides_spin->set_value(ToolDrawRegularPolygon::get_default_sides());
        sides_row->append(*sides_label);
        sides_row->append(*sides_spin);
        box->append(*sides_row);

        auto rounded_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto rounded_label = Gtk::make_managed<Gtk::Label>("Rounded");
        rounded_label->set_hexpand(true);
        rounded_label->set_xalign(0);
        auto rounded_switch = Gtk::make_managed<Gtk::Switch>();
        rounded_switch->set_active(ToolDrawRegularPolygon::get_default_rounded());
        rounded_row->append(*rounded_label);
        rounded_row->append(*rounded_switch);
        box->append(*rounded_row);

        auto radius_revealer = Gtk::make_managed<Gtk::Revealer>();
        radius_revealer->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
        radius_revealer->set_transition_duration(120);
        auto radius_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto radius_label = Gtk::make_managed<Gtk::Label>("Radius");
        radius_label->set_hexpand(true);
        radius_label->set_xalign(0);
        auto radius_spin = Gtk::make_managed<Gtk::SpinButton>();
        radius_spin->set_range(0.0, 9999.99);
        radius_spin->set_increments(0.1, 1.0);
        radius_spin->set_digits(2);
        radius_spin->set_numeric(true);
        radius_spin->set_width_chars(5);
        radius_spin->set_value(ToolDrawRegularPolygon::get_default_round_radius());
        auto mm_label = Gtk::make_managed<Gtk::Label>("mm");
        mm_label->add_css_class("dim-label");
        radius_row->append(*radius_label);
        radius_row->append(*radius_spin);
        radius_row->append(*mm_label);
        radius_revealer->set_child(*radius_row);
        radius_revealer->set_reveal_child(rounded_switch->get_active());
        box->append(*radius_revealer);

        sides_spin->signal_value_changed().connect(
                [sides_spin] { ToolDrawRegularPolygon::set_default_sides(static_cast<unsigned int>(sides_spin->get_value())); });
        rounded_switch->property_active().signal_changed().connect([rounded_switch, radius_revealer] {
            const bool active = rounded_switch->get_active();
            ToolDrawRegularPolygon::set_default_rounded(active);
            radius_revealer->set_reveal_child(active);
        });
        radius_spin->signal_value_changed().connect(
                [radius_spin] { ToolDrawRegularPolygon::set_default_round_radius(radius_spin->get_value()); });
    }

    auto text_button = add_strip_button("action-draw-text-symbolic", "Draw text");
    text_button->signal_clicked().connect([this] { trigger_radial_tool(ToolID::DRAW_TEXT); });
    m_radial_tool_buttons[ToolID::DRAW_TEXT] = text_button;
    {
        auto [popover, box] = create_quick_options_popover(*text_button);
        m_quick_tool_option_popovers[ToolID::DRAW_TEXT] = popover;
        auto font_dialog = Gtk::FontDialog::create();
        font_dialog->set_modal(true);
        auto font_desc = std::make_shared<Pango::FontDescription>(ToolDrawText::get_default_font());
        auto font_features = std::make_shared<Glib::ustring>(ToolDrawText::get_default_font_features());
        auto font_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto font_label = Gtk::make_managed<Gtk::Label>("Font");
        font_label->set_hexpand(true);
        font_label->set_xalign(0);
        auto font_button = Gtk::make_managed<Gtk::Button>();
        font_button->set_has_frame(true);
        font_button->set_hexpand(true);
        font_button->set_halign(Gtk::Align::END);
        font_row->append(*font_label);
        font_row->append(*font_button);
        box->append(*font_row);

        auto bold_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto bold_label = Gtk::make_managed<Gtk::Label>("Bold");
        bold_label->set_hexpand(true);
        bold_label->set_xalign(0);
        auto bold_switch = Gtk::make_managed<Gtk::Switch>();
        bold_row->append(*bold_label);
        bold_row->append(*bold_switch);
        box->append(*bold_row);

        auto italic_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto italic_label = Gtk::make_managed<Gtk::Label>("Italic");
        italic_label->set_hexpand(true);
        italic_label->set_xalign(0);
        auto italic_switch = Gtk::make_managed<Gtk::Switch>();
        italic_row->append(*italic_label);
        italic_row->append(*italic_switch);
        box->append(*italic_row);

        const auto sync_font_ui = [font_button, bold_switch, italic_switch, font_desc] {
            if (!font_button || !bold_switch || !italic_switch || !font_desc)
                return;
            auto family = font_desc->get_family();
            if (family.empty())
                family = "Default";
            font_button->set_label(family);
            const auto weight = font_desc->get_weight();
            bold_switch->set_active(weight >= Pango::Weight::BOLD);
            italic_switch->set_active(font_desc->get_style() == Pango::Style::ITALIC);
        };
        sync_font_ui();

        font_button->signal_clicked().connect([this, popover, font_dialog, font_desc, font_features, sync_font_ui] {
            popover->popdown();
            font_dialog->choose_font_and_features(
                    m_win,
                    [popover, font_dialog, font_desc, font_features, sync_font_ui](const Glib::RefPtr<Gio::AsyncResult> &result) {
                        try {
                            auto [desc, features, language] = font_dialog->choose_font_and_features_finish(result);
                            (void)language;
                            *font_desc = desc;
                            *font_features = features;
                            ToolDrawText::set_default_font(font_desc->to_string());
                            ToolDrawText::set_default_font_features(*font_features);
                            sync_font_ui();
                        }
                        catch (const Glib::Error &) {
                        }
                        popover->popup();
                    },
                    *font_desc);
        });
        bold_switch->property_active().signal_changed().connect([bold_switch, font_desc, font_features, sync_font_ui] {
            auto desc = *font_desc;
            desc.set_weight(bold_switch->get_active() ? Pango::Weight::BOLD : Pango::Weight::NORMAL);
            *font_desc = desc;
            ToolDrawText::set_default_font(font_desc->to_string());
            ToolDrawText::set_default_font_features(*font_features);
            sync_font_ui();
        });
        italic_switch->property_active().signal_changed().connect([italic_switch, font_desc, font_features, sync_font_ui] {
            auto desc = *font_desc;
            desc.set_style(italic_switch->get_active() ? Pango::Style::ITALIC : Pango::Style::NORMAL);
            *font_desc = desc;
            ToolDrawText::set_default_font(font_desc->to_string());
            ToolDrawText::set_default_font_features(*font_features);
            sync_font_ui();
        });
    }

    auto sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    sep->add_css_class("sketch-toolbar-divider");
    layout->append(*sep);

    m_radial_grid_button = add_strip_button("view-grid-symbolic", "Toggle grid");
    m_radial_grid_button->signal_clicked().connect([this] { toggle_radial_grid(); });
    {
        auto [popover, box] = create_quick_options_popover(*m_radial_grid_button);
        m_quick_grid_popover = popover;
        auto snap_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto snap_label = Gtk::make_managed<Gtk::Label>("Snap to grid");
        snap_label->set_hexpand(true);
        snap_label->set_xalign(0);
        auto snap_switch = Gtk::make_managed<Gtk::Switch>();
        snap_row->append(*snap_label);
        snap_row->append(*snap_switch);
        box->append(*snap_row);
        auto spacing_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto spacing_title = Gtk::make_managed<Gtk::Label>("Size");
        spacing_title->set_hexpand(true);
        spacing_title->set_xalign(0);
        auto spacing_spin = Gtk::make_managed<Gtk::SpinButton>();
        spacing_spin->set_range(0.1, 100000.0);
        spacing_spin->set_increments(0.1, 1.0);
        spacing_spin->set_digits(2);
        spacing_spin->set_numeric(true);
        spacing_spin->set_width_chars(6);
        spacing_spin->set_halign(Gtk::Align::END);
        auto spacing_label = Gtk::make_managed<Gtk::Label>("mm");
        spacing_label->add_css_class("dim-label");
        spacing_box->append(*spacing_title);
        spacing_box->append(*spacing_spin);
        spacing_box->append(*spacing_label);
        box->append(*spacing_box);
        popover->signal_show().connect([this, snap_switch, spacing_spin] {
            snap_switch->set_active(get_canvas().get_grid_snap_enabled());
            spacing_spin->set_value(get_canvas().get_grid_spacing_mm());
        });
        snap_switch->property_active().signal_changed().connect(
                [this, snap_switch] { get_canvas().set_grid_snap_enabled(snap_switch->get_active()); });
        spacing_spin->signal_value_changed().connect(
                [this, spacing_spin] { get_canvas().set_grid_spacing_mm(spacing_spin->get_value()); });
    }

    m_radial_symmetry_button = add_strip_button("object-flip-horizontal-symbolic", "Toggle symmetry");
    m_radial_symmetry_button->signal_clicked().connect([this] { toggle_radial_symmetry(); });
    {
        auto [popover, box] = create_quick_options_popover(*m_radial_symmetry_button);
        m_quick_symmetry_popover = popover;
        auto updating = std::make_shared<bool>(false);
        auto radial_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto radial_label = Gtk::make_managed<Gtk::Label>("Radial");
        radial_label->set_hexpand(true);
        radial_label->set_xalign(0);
        auto radial_switch = Gtk::make_managed<Gtk::Switch>();
        radial_row->append(*radial_label);
        radial_row->append(*radial_switch);
        box->append(*radial_row);

        auto mode_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto mode_label = Gtk::make_managed<Gtk::Label>("Mode");
        mode_label->set_hexpand(true);
        mode_label->set_xalign(0);
        auto mode_prev_button = Gtk::make_managed<Gtk::Button>();
        mode_prev_button->set_icon_name("go-previous-symbolic");
        mode_prev_button->set_has_frame(true);
        mode_prev_button->set_tooltip_text("Previous mode");
        auto mode_value_label = Gtk::make_managed<Gtk::Label>("Horizontal");
        mode_value_label->set_hexpand(true);
        mode_value_label->set_halign(Gtk::Align::CENTER);
        mode_value_label->set_xalign(.5f);
        mode_value_label->set_max_width_chars(10);
        auto mode_next_button = Gtk::make_managed<Gtk::Button>();
        mode_next_button->set_icon_name("go-next-symbolic");
        mode_next_button->set_has_frame(true);
        mode_next_button->set_tooltip_text("Next mode");
        mode_row->append(*mode_label);
        mode_row->append(*mode_prev_button);
        mode_row->append(*mode_value_label);
        mode_row->append(*mode_next_button);
        box->append(*mode_row);

        auto axes_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto axes_label = Gtk::make_managed<Gtk::Label>("Segments");
        axes_label->set_hexpand(true);
        axes_label->set_xalign(0);
        auto axes_spin = Gtk::make_managed<Gtk::SpinButton>();
        axes_spin->set_range(3, 32);
        axes_spin->set_increments(1, 1);
        axes_spin->set_digits(0);
        axes_spin->set_numeric(true);
        axes_row->append(*axes_label);
        axes_row->append(*axes_spin);
        box->append(*axes_row);

        auto rot_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto rot_label = Gtk::make_managed<Gtk::Label>("Rotation");
        rot_label->set_hexpand(true);
        rot_label->set_xalign(0);
        auto rot_spin = Gtk::make_managed<Gtk::SpinButton>();
        rot_spin->set_range(-180, 180);
        rot_spin->set_increments(1, 10);
        rot_spin->set_digits(1);
        rot_spin->set_numeric(true);
        auto rot_unit = Gtk::make_managed<Gtk::Label>("deg");
        rot_row->append(*rot_label);
        rot_row->append(*rot_spin);
        rot_row->append(*rot_unit);
        box->append(*rot_row);

        const auto sync_quick =
                [this, updating, radial_switch, mode_value_label, mode_prev_button, mode_next_button, axes_spin, rot_spin,
                 mode_row, axes_row] {
            if (!m_symmetry_radial_switch || !m_symmetry_axes_spin || !m_symmetry_rotation_spin)
                return;
            *updating = true;
            radial_switch->set_active(m_symmetry_radial_switch->get_active());
            mode_value_label->set_text((m_symmetry_mode_selected % 2u) == 0u ? "Horizontal" : "Vertical");
            axes_spin->set_value(m_symmetry_axes_spin->get_value());
            rot_spin->set_value(m_symmetry_rotation_spin->get_value());
            const bool radial = radial_switch->get_active();
            mode_row->set_visible(!radial);
            axes_row->set_visible(radial);
            mode_prev_button->set_sensitive(!radial);
            mode_next_button->set_sensitive(!radial);
            *updating = false;
        };
        popover->signal_show().connect(sync_quick);
        radial_switch->property_active().signal_changed().connect(
                [this, updating, radial_switch, mode_row, axes_row] {
                    if (*updating || !m_symmetry_radial_switch)
                        return;
                    m_symmetry_radial_switch->set_active(radial_switch->get_active());
                    const bool radial = radial_switch->get_active();
                    mode_row->set_visible(!radial);
                    axes_row->set_visible(radial);
                    sync_symmetry_popover_context();
                    apply_symmetry_live_from_popover(false);
                });
        mode_prev_button->signal_clicked().connect([this, updating, mode_value_label] {
            if (*updating)
                return;
            m_symmetry_mode_selected = (m_symmetry_mode_selected + 1u) % 2u;
            mode_value_label->set_text((m_symmetry_mode_selected % 2u) == 0u ? "Horizontal" : "Vertical");
            sync_symmetry_popover_context();
            apply_symmetry_live_from_popover(false);
        });
        mode_next_button->signal_clicked().connect([this, updating, mode_value_label] {
            if (*updating)
                return;
            m_symmetry_mode_selected = (m_symmetry_mode_selected + 1u) % 2u;
            mode_value_label->set_text((m_symmetry_mode_selected % 2u) == 0u ? "Horizontal" : "Vertical");
            sync_symmetry_popover_context();
            apply_symmetry_live_from_popover(false);
        });
        axes_spin->signal_value_changed().connect([this, updating, axes_spin] {
            if (*updating || !m_symmetry_axes_spin)
                return;
            m_symmetry_axes_spin->set_value(axes_spin->get_value());
        });
        rot_spin->signal_value_changed().connect([this, updating, rot_spin] {
            if (*updating || !m_symmetry_rotation_spin)
                return;
            m_symmetry_rotation_spin->set_value(rot_spin->get_value());
        });
    }

    update_radial_menu_button_states();
}

void Editor::open_radial_menu(double x, double y)
{
    if (!m_radial_menu_popover)
        return;
    for (const auto &[tool_id, popover] : m_quick_tool_option_popovers) {
        (void)tool_id;
        if (popover && popover->get_visible())
            popover->popdown();
    }
    if (m_quick_grid_popover && m_quick_grid_popover->get_visible())
        m_quick_grid_popover->popdown();
    if (m_quick_symmetry_popover && m_quick_symmetry_popover->get_visible())
        m_quick_symmetry_popover->popdown();
    update_radial_menu_button_states();
    Gdk::Rectangle rect;
    rect.set_x(static_cast<int>(std::round(x)));
    rect.set_y(static_cast<int>(std::round(y)));
    rect.set_width(1);
    rect.set_height(1);
    m_radial_menu_popover->set_pointing_to(rect);
    m_radial_menu_popover->popup();
}

void Editor::close_radial_menu()
{
    for (const auto &[tool_id, popover] : m_quick_tool_option_popovers) {
        (void)tool_id;
        if (popover && popover->get_visible())
            popover->popdown();
    }
    if (m_quick_grid_popover && m_quick_grid_popover->get_visible())
        m_quick_grid_popover->popdown();
    if (m_quick_symmetry_popover && m_quick_symmetry_popover->get_visible())
        m_quick_symmetry_popover->popdown();
    if (m_radial_menu_popover && m_radial_menu_popover->get_visible())
        m_radial_menu_popover->popdown();
}

bool Editor::matches_radial_menu_trigger(unsigned int button, Gdk::ModifierType state) const
{
    const auto primary_mods = Gdk::ModifierType::SHIFT_MASK | Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::ALT_MASK;
    const auto mods = state & primary_mods;
#ifdef G_OS_WIN32
    static constexpr unsigned int button_back = 4;
    static constexpr unsigned int button_forward = 5;
#else
    static constexpr unsigned int button_back = 8;
    static constexpr unsigned int button_forward = 9;
#endif
    switch (m_preferences.editor.radial_menu_trigger) {
    case EditorPreferences::RadialMenuTrigger::SHIFT_MMB:
        return button == 2 && mods == Gdk::ModifierType::SHIFT_MASK;
    case EditorPreferences::RadialMenuTrigger::MOUSE_BACK:
        return button == button_back && mods == static_cast<Gdk::ModifierType>(0);
    case EditorPreferences::RadialMenuTrigger::MOUSE_FORWARD:
        return button == button_forward && mods == static_cast<Gdk::ModifierType>(0);
    case EditorPreferences::RadialMenuTrigger::SHIFT_RMB:
    default:
        return button == 3 && mods == Gdk::ModifierType::SHIFT_MASK;
    }
}

void Editor::trigger_radial_tool(ToolID tool_id)
{
    if (!m_core.has_documents())
        return;
    const bool should_keep_sticky = is_sticky_draw_tool(tool_id);
    if (!force_end_tool())
        return;
    if (!trigger_action(tool_id))
        return;
    m_sticky_draw_tool = should_keep_sticky ? tool_id : ToolID::NONE;
    update_sketcher_toolbar_button_states();
    update_radial_menu_button_states();
    if (m_quick_tool_option_popovers.count(tool_id) && m_quick_tool_option_popovers.at(tool_id)) {
        auto *popover = m_quick_tool_option_popovers.at(tool_id);
        if (g_active_hover_popover && g_active_hover_popover != popover && g_active_hover_popover->get_visible())
            g_active_hover_popover->popdown();
        popover->popup();
        g_active_hover_popover = popover;
    }
    else {
        close_radial_menu();
    }
}

void Editor::toggle_radial_grid()
{
    if (!m_core.has_documents())
        return;
    get_canvas().set_grid_enabled(!get_canvas().get_grid_enabled());
    update_sketcher_toolbar_button_states();
    sync_symmetry_popover_context();
    update_radial_menu_button_states();
    if (m_quick_grid_popover) {
        if (g_active_hover_popover && g_active_hover_popover != m_quick_grid_popover && g_active_hover_popover->get_visible())
            g_active_hover_popover->popdown();
        m_quick_grid_popover->popup();
        g_active_hover_popover = m_quick_grid_popover;
    }
}

void Editor::toggle_radial_symmetry()
{
    if (!m_core.has_documents())
        return;
    if (m_symmetry_enabled)
        set_symmetry_enabled(false, false);
    else
        set_symmetry_enabled(true, true);
    update_sketcher_toolbar_button_states();
    update_radial_menu_button_states();
    if (m_quick_symmetry_popover) {
        if (g_active_hover_popover && g_active_hover_popover != m_quick_symmetry_popover
            && g_active_hover_popover->get_visible())
            g_active_hover_popover->popdown();
        m_quick_symmetry_popover->popup();
        g_active_hover_popover = m_quick_symmetry_popover;
    }
}

void Editor::update_radial_menu_button_states()
{
    const bool has_docs = m_core.has_documents();
    const auto active_tool = m_core.get_tool_id();
    for (const auto &[tool_id, button] : m_radial_tool_buttons) {
        if (!button)
            continue;
        bool sensitive = false;
        if (has_docs) {
            sensitive = m_core.tool_can_begin(tool_id, {}).get_can_begin();
        }
        button->set_sensitive(sensitive);
        bool active = false;
        if (m_core.tool_is_active())
            active = (active_tool == tool_id);
        else
            active = (m_sticky_draw_tool == tool_id) && is_sticky_draw_tool(tool_id);
        if (active)
            button->add_css_class("sketch-toolbar-active");
        else
            button->remove_css_class("sketch-toolbar-active");
    }
    if (m_radial_grid_button) {
        m_radial_grid_button->set_sensitive(has_docs);
        if (get_canvas().get_grid_enabled())
            m_radial_grid_button->add_css_class("sketch-toolbar-active");
        else
            m_radial_grid_button->remove_css_class("sketch-toolbar-active");
    }
    if (m_radial_symmetry_button) {
        m_radial_symmetry_button->set_sensitive(has_docs);
        if (m_symmetry_enabled)
            m_radial_symmetry_button->add_css_class("sketch-toolbar-active");
        else
            m_radial_symmetry_button->remove_css_class("sketch-toolbar-active");
    }
}
#endif

void Editor::update_selection_mode_label()
{
    const auto mode = get_canvas().get_selection_mode();
    std::string label;
    switch (mode) {
    case SelectionMode::HOVER:
        m_win.set_selection_mode_label_text("Hover select");
        break;
    case SelectionMode::NORMAL:
        m_win.set_selection_mode_label_text("Click select");
        break;
    default:;
    }
}

void Editor::install_hover(Gtk::Button &button, ToolID id)
{
    auto ctrl = Gtk::EventControllerMotion::create();

    ctrl->signal_leave().connect([this] {
        m_context_menu_hover_timeout.disconnect();
        m_context_menu_hover_timeout = Glib::signal_timeout().connect(
                [this] {
                    if (m_core.reset_preview()) {
                        canvas_update_keep_selection();
                        m_context_menu->set_opacity(1);
                    }
                    return false;
                },
                200);
    });
    ctrl->signal_motion().connect([this, id](double, double) {
        m_context_menu_hover_timeout.disconnect();
        if (m_core.get_current_preview_tool() == ToolID::NONE) {
            m_context_menu_hover_timeout = Glib::signal_timeout().connect(
                    [this, id] {
                        if (m_core.apply_preview(id, m_context_menu_selection)) {
                            m_context_menu->set_opacity(.5);
                            canvas_update_keep_selection();
                        }
                        return false;
                    },
                    200);
        }
        else {
            if (m_core.apply_preview(id, m_context_menu_selection))
                canvas_update_keep_selection();
        }
    });
    button.add_controller(ctrl);
}

void Editor::open_context_menu(ContextMenuMode mode)
{
    Gdk::Rectangle rect;
    rect.set_x(m_last_x);
    rect.set_y(m_last_y);
    m_context_menu->set_pointing_to(rect);
    get_canvas().end_pan();
    auto menu = Gio::Menu::create();
    auto sel = get_canvas().get_selection();
    if (mode != ContextMenuMode::CONSTRAIN) {
        auto hover_sel = get_canvas().get_hover_selection();
        if (!hover_sel)
            return;
        if (!sel.contains(*hover_sel))
            sel = {*hover_sel};
    }
    m_context_menu_selection = sel;
    update_action_sensitivity(sel);
    struct ActionInfo {
        ActionToolID id;
        bool can_preview = false;
        ToolID force_unset_workplane_tool = ToolID::NONE;
        bool constraint_is_in_workplane = false;
    };
    std::vector<ActionInfo> ids;

    std::list<Glib::RefPtr<Gio::MenuItem>> meas_items;
    for (const auto &[action_group, action_group_name] : action_group_catalog) {
        if (mode == ContextMenuMode::CONSTRAIN && action_group != ActionGroup::CONSTRAIN)
            continue;
        for (const auto &[id, it_cat] : action_catalog) {
            if (it_cat.group == action_group && !(it_cat.flags & ActionCatalogItem::FLAGS_NO_MENU)) {
                if (auto tool = std::get_if<ToolID>(&id)) {
                    auto r = m_core.tool_can_begin(*tool, sel);
                    if (r.can_begin == ToolBase::CanBegin::YES && r.is_specific) {
                        ids.emplace_back(id, r.can_preview, r.force_unset_workplane_tool, r.constraint_is_in_workplane);
                        auto item = Gio::MenuItem::create(it_cat.name.menu, "menu." + action_tool_id_to_string(id));
                        if (it_cat.group == ActionGroup::MEASURE) {
                            meas_items.push_back(item);
                        }
                        else {
                            item->set_attribute_value(
                                    "custom", Glib::Variant<Glib::ustring>::create(action_tool_id_to_string(id)));
                            menu->append_item(item);
                        }
                    }
                }
                else if (auto act = std::get_if<ActionID>(&id)) {
                    if (get_action_sensitive(*act) && (it_cat.flags & ActionCatalogItem::FLAGS_SPECIFIC)) {
                        ids.emplace_back(id);
                        auto item = Gio::MenuItem::create(it_cat.name.menu, "menu." + action_tool_id_to_string(id));
                        item->set_attribute_value("custom",
                                                  Glib::Variant<Glib::ustring>::create(action_tool_id_to_string(id)));
                        menu->append_item(item);
                    }
                }
            }
        }
    }
    if (mode == ContextMenuMode::CONSTRAIN && ids.size() == 1) {
        get_canvas().set_selection(m_context_menu_selection, false);
        trigger_action(ids.front().id);
        return;
    }
    else if (mode == ContextMenuMode::CONSTRAIN && ids.size() == 0) {
        auto item = Gio::MenuItem::create("No applicable constraints", "menu.invalid");
        menu->append_item(item);
    }
    if (meas_items.size() > 1) {
        auto measurement_submenu = Gio::Menu::create();
        for (auto it : meas_items) {
            measurement_submenu->append_item(it);
        }
        auto item = Gio::MenuItem::create("Measure", measurement_submenu);
        menu->append_item(item);
    }
    else if (meas_items.size() == 1) {
        menu->append_item(meas_items.front());
    }

    if (m_core.has_documents() && mode != ContextMenuMode::CONSTRAIN) {
        auto &doc = m_core.get_current_document();
        if (auto enp = point_from_selection(doc, m_context_menu_selection)) {
            auto &en = doc.get_entity(enp->entity);
            std::vector<const Constraint *> constraints;
            for (auto constraint : en.get_constraints(doc)) {
                if (enp->point == 0 || constraint->get_referenced_entities_and_points().contains(*enp))
                    constraints.push_back(constraint);
            }
            std::ranges::sort(constraints, [](auto a, auto b) { return a->get_type() < b->get_type(); });

            if (constraints.size()) {
                auto submenu = Gio::Menu::create();
                for (auto constraint : constraints) {
                    auto item = Gio::MenuItem::create(constraint->get_type_name(), "menu.remove_constraint");
                    item->set_action_and_target("menu.remove_constraint",
                                                Glib::Variant<std::string>::create(constraint->m_uuid));
                    submenu->append_item(item);
                }
                auto item = Gio::MenuItem::create("Remove constraint", submenu);
                menu->append_item(item);
            }
        }
    }

    const bool has_any_can_force_unset_workplane =
            std::ranges::any_of(ids, [](const auto &x) { return x.force_unset_workplane_tool != ToolID::NONE; });
    auto sg = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);
    if (menu->get_n_items() != 0) {
        m_context_menu->set_menu_model(menu);
        for (const auto [id, can_preview, force_unset_workplane_tool, constraint_is_in_workplane] : ids) {
            auto button = Gtk::make_managed<Gtk::Button>();
            button->signal_clicked().connect([this, id] {
                m_context_menu->popdown();
                get_canvas().set_selection(m_context_menu_selection, false);
                trigger_action(id);
            });
            if (m_preferences.editor.preview_constraints && can_preview) {
                install_hover(*button, std::get<ToolID>(id));
            }
            button->add_css_class("context-menu-button");
            auto label = Gtk::make_managed<Gtk::Label>(action_catalog.at(id).name.menu);
            label->set_xalign(0);
            auto label2 =
                    Gtk::make_managed<Gtk::Label>(key_sequences_to_string(m_action_connections.at(id).key_sequences));
            label2->add_css_class("dim-label");
            label2->set_margin_start(8);
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
            box->append(*label);
            if (constraint_is_in_workplane) {
                auto icon = Gtk::make_managed<Gtk::Image>();
                icon->set_tooltip_text("Constraint applies in workplane");
                icon->set_from_icon_name("action-constrain-in-workplane-symbolic");
                box->append(*icon);
                icon->set_hexpand(true);
                icon->set_halign(Gtk::Align::START);
            }
            else {
                label->set_hexpand(true);
            }
            box->append(*label2);
            button->set_child(*box);

            button->set_has_frame(false);
            auto box2 = Gtk::make_managed<Gtk::Box>();
            box2->append(*button);
            if (force_unset_workplane_tool != ToolID::NONE) {
                auto button2 = Gtk::make_managed<Gtk::Button>("in 3D");
                button2->add_css_class("context-menu-button");
                button2->add_css_class("context-menu-button-3d");
                button2->set_has_frame(false);
                sg->add_widget(*button2);
                button2->signal_clicked().connect([this, force_unset_workplane_tool] {
                    m_context_menu->popdown();
                    get_canvas().set_selection(m_context_menu_selection, false);
                    tool_begin(force_unset_workplane_tool);
                });
                if (m_preferences.editor.preview_constraints && can_preview) {
                    install_hover(*button2, force_unset_workplane_tool);
                }


                box2->append(*button2);
            }
            else if (has_any_can_force_unset_workplane) {
                auto placeholder = Gtk::make_managed<Gtk::Label>();
                sg->add_widget(*placeholder);
                box2->append(*placeholder);
            }
            m_context_menu->add_child(*box2, action_tool_id_to_string(id));
        }
        m_context_menu->popup();
    }
    else if (mode == ContextMenuMode::CONSTRAIN) {
        m_context_menu->popup();
    }
}

void Editor::init_properties_notebook()
{
#ifdef DUNE_SKETCHER_ONLY
    return;
#endif
    m_properties_notebook = Gtk::make_managed<Gtk::Notebook>();
    m_properties_notebook->set_show_border(false);
    m_properties_notebook->set_tab_pos(Gtk::PositionType::BOTTOM);
    m_win.get_left_bar().set_end_child(*m_properties_notebook);
    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
        m_group_editor_box = Gtk::make_managed<Gtk::Box>();
        m_group_editor_box->set_vexpand(true);
        box->append(*m_group_editor_box);
        auto label = Gtk::make_managed<Gtk::Label>("Commit pending");
        label->set_margin(3);
        m_group_commit_pending_revealer = Gtk::make_managed<Gtk::Revealer>();
        m_group_commit_pending_revealer->set_transition_type(Gtk::RevealerTransitionType::CROSSFADE);
        m_group_commit_pending_revealer->set_child(*label);
        box->append(*m_group_commit_pending_revealer);
        m_properties_notebook->append_page(*box, "Group");
    }
    m_core.signal_rebuilt().connect([this] {
        if (m_group_editor) {
            if (m_core.get_current_group() != m_group_editor->get_group())
                update_group_editor();
            else
                m_group_editor->reload();
        }
    });


    m_constraints_box = Gtk::make_managed<ConstraintsBox>(m_core);
    m_properties_notebook->append_page(*m_constraints_box, "Constraints");
    m_core.signal_rebuilt().connect(
            [this] { Glib::signal_idle().connect_once([this] { m_constraints_box->update(); }); });
    m_core.signal_documents_changed().connect([this] { m_constraints_box->update(); });
    m_constraints_box->signal_constraint_selected().connect([this](const UUID &uu) {
        SelectableRef sr{.type = SelectableRef::Type::CONSTRAINT, .item = uu};
        get_canvas().set_selection({sr}, true);
        get_canvas().set_selection_mode(SelectionMode::NORMAL);
    });
    m_constraints_box->signal_changed().connect([this] { CanvasUpdater canvas_updater{*this}; });

    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);

        m_selection_editor = Gtk::make_managed<SelectionEditor>(m_core, static_cast<IDocumentViewProvider &>(*this));
        m_selection_editor->signal_changed().connect(sigc::mem_fun(*this, &Editor::handle_commit_from_editor));
        m_selection_editor->signal_view_changed().connect([this] { CanvasUpdater canvas_updater{*this}; });
        m_selection_editor->set_vexpand(true);
        box->append(*m_selection_editor);
        auto label = Gtk::make_managed<Gtk::Label>("Commit pending");
        label->set_margin(3);
        m_selection_commit_pending_revealer = Gtk::make_managed<Gtk::Revealer>();
        m_selection_commit_pending_revealer->set_transition_type(Gtk::RevealerTransitionType::CROSSFADE);
        m_selection_commit_pending_revealer->set_child(*label);
        box->append(*m_selection_commit_pending_revealer);
        m_properties_notebook->append_page(*box, "Selection");
    }
    get_canvas().signal_selection_changed().connect(sigc::mem_fun(*this, &Editor::update_selection_editor));
}

void Editor::update_selection_editor()
{
#ifdef DUNE_SKETCHER_ONLY
    if (sanitize_canvas_selection_if_needed())
        return;
#endif
    if (!m_selection_editor)
        return;
    if (get_canvas().get_selection_mode() == SelectionMode::HOVER)
        m_selection_editor->set_selection({});
    else
        m_selection_editor->set_selection(get_canvas().get_selection());
}

void Editor::init_header_bar()
{
    attach_action_button(m_win.get_open_button(), ActionID::OPEN_DOCUMENT);
    attach_action_sensitive(m_win.get_open_menu_button(), ActionID::OPEN_DOCUMENT);
    attach_action_button(m_win.get_new_button(), ActionID::NEW_DOCUMENT);
    attach_action_button(m_win.get_save_button(), ActionID::SAVE);
    attach_action_button(m_win.get_save_as_button(), ActionID::SAVE_AS);

    {
#ifdef DUNE_SKETCHER_ONLY
        if (auto sidebar_button = m_win.get_sidebar_floating_button()) {
            m_win.get_header_bar().pack_start(*sidebar_button);
            auto sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
            m_win.get_header_bar().pack_start(*sep);
        }
#endif
        auto undo_redo_box = Gtk::manage(new Gtk::Box(Gtk::Orientation::HORIZONTAL, 0));
        undo_redo_box->add_css_class("linked");

        auto undo_button = create_action_button(ActionID::UNDO);
        undo_button->set_tooltip_text("Undo");
        undo_button->set_image_from_icon_name("edit-undo-symbolic");
        undo_redo_box->append(*undo_button);

        auto redo_button = create_action_button(ActionID::REDO);
        redo_button->set_tooltip_text("Redo");
        redo_button->set_image_from_icon_name("edit-redo-symbolic");
        undo_redo_box->append(*redo_button);

        m_win.get_header_bar().pack_start(*undo_redo_box);
#ifdef DUNE_SKETCHER_ONLY
        auto sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
        m_win.get_header_bar().pack_start(*sep);
        if (auto header_action_box = m_win.get_header_action_box()) {
            m_win.get_header_bar().pack_start(*header_action_box);
            auto action_grid_sep = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
            action_grid_sep->add_css_class("sketch-toolbar-divider");
            action_grid_sep->add_css_class("sketch-toolbar-divider-vertical");
            action_grid_sep->set_size_request(1, 22);
            action_grid_sep->set_valign(Gtk::Align::CENTER);
            action_grid_sep->set_margin_start(4);
            action_grid_sep->set_margin_end(4);
            m_win.get_header_bar().pack_start(*action_grid_sep);
        }

        m_grid_menu_button = Gtk::make_managed<Gtk::Button>();
        m_grid_menu_button->set_icon_name("view-grid-symbolic");
        m_grid_menu_button->set_has_frame(true);
        m_grid_menu_button->add_css_class("sketch-toolbar-button");
        m_grid_menu_button->set_tooltip_text("Grid settings");

        auto grid_popover = Gtk::make_managed<Gtk::Popover>();
        grid_popover->add_css_class("sketch-grid-popover");
        grid_popover->set_has_arrow(true);
        grid_popover->set_autohide(false);
        grid_popover->set_parent(*m_grid_menu_button);
        grid_popover->set_size_request(sketch_popover_total_width, -1);
        auto grid_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        grid_box->set_margin_start(12);
        grid_box->set_margin_end(12);
        grid_box->set_margin_top(12);
        grid_box->set_margin_bottom(12);
        grid_box->set_size_request(sketch_popover_content_width, -1);
        grid_popover->set_child(*grid_box);
        m_win.get_header_bar().pack_start(*m_grid_menu_button);
        install_hover_popover(*m_grid_menu_button, *grid_popover, [this] { return !m_primary_button_pressed; },
                              [this] { return m_right_click_popovers_only; });

        auto make_switch_row = [grid_box](const Glib::ustring &text, Gtk::Switch *&sw) {
            auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
            auto label = Gtk::make_managed<Gtk::Label>(text);
            label->set_hexpand(true);
            label->set_xalign(0);
            sw = Gtk::make_managed<Gtk::Switch>();
            sw->set_halign(Gtk::Align::END);
            row->append(*label);
            row->append(*sw);
            grid_box->append(*row);
        };
        make_switch_row("Snap to grid", m_grid_snap_button);

        auto spacing_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto spacing_title = Gtk::make_managed<Gtk::Label>("Size");
        spacing_title->set_hexpand(true);
        spacing_title->set_xalign(0);
        m_grid_spacing_spin = Gtk::make_managed<Gtk::SpinButton>();
        m_grid_spacing_spin->set_range(0.1, 100000.0);
        m_grid_spacing_spin->set_increments(0.1, 1.0);
        m_grid_spacing_spin->set_digits(2);
        m_grid_spacing_spin->set_numeric(true);
        m_grid_spacing_spin->set_width_chars(6);
        m_grid_spacing_spin->set_halign(Gtk::Align::END);
        auto spacing_label = Gtk::make_managed<Gtk::Label>("mm");
        spacing_label->add_css_class("dim-label");
        spacing_box->append(*spacing_title);
        spacing_box->append(*m_grid_spacing_spin);
        spacing_box->append(*spacing_label);
        grid_box->append(*spacing_box);

        m_grid_menu_button->signal_clicked().connect([this] {
            get_canvas().set_grid_enabled(!get_canvas().get_grid_enabled());
            update_sketcher_toolbar_button_states();
            sync_symmetry_popover_context();
        });
        m_grid_spacing_spin->signal_value_changed().connect([this] {
            if (m_grid_spacing_spin)
                get_canvas().set_grid_spacing_mm(m_grid_spacing_spin->get_value());
        });
        m_grid_snap_button->property_active().signal_changed().connect([this] {
            if (m_grid_snap_button)
                get_canvas().set_grid_snap_enabled(m_grid_snap_button->get_active());
        });

        m_grid_spacing_spin->set_value(get_canvas().get_grid_spacing_mm());
        m_grid_snap_button->set_active(get_canvas().get_grid_snap_enabled());

        init_symmetry_popover();
#endif
    }

    init_settings_popover();
}

void Editor::init_symmetry_popover()
{
#ifndef DUNE_SKETCHER_ONLY
    return;
#else
    m_symmetry_menu_button = Gtk::make_managed<Gtk::Button>();
    m_symmetry_menu_button->set_icon_name("object-flip-horizontal-symbolic");
    m_symmetry_menu_button->set_has_frame(true);
    m_symmetry_menu_button->add_css_class("sketch-toolbar-button");
    m_symmetry_menu_button->set_tooltip_text("Symmetry");
    m_win.get_header_bar().pack_start(*m_symmetry_menu_button);

    auto popover = Gtk::make_managed<Gtk::Popover>();
    popover->add_css_class("sketch-grid-popover");
    popover->set_has_arrow(true);
    popover->set_autohide(false);
    popover->set_parent(*m_symmetry_menu_button);
    popover->set_size_request(sketch_popover_total_width, -1);
    install_hover_popover(*m_symmetry_menu_button, *popover, [this] { return !m_primary_button_pressed; },
                          [this] { return m_right_click_popovers_only; });
    m_symmetry_popover = popover;

    auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    root->set_margin_start(12);
    root->set_margin_end(12);
    root->set_margin_top(12);
    root->set_margin_bottom(12);
    root->set_size_request(sketch_popover_content_width, -1);
    popover->set_child(*root);

    m_symmetry_context_label = Gtk::make_managed<Gtk::Label>();
    m_symmetry_context_label->set_wrap(true);
    m_symmetry_context_label->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
    m_symmetry_context_label->set_max_width_chars(16);
    m_symmetry_context_label->set_xalign(0);
    m_symmetry_context_label->add_css_class("dim-label");

    auto radial_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto radial_label = Gtk::make_managed<Gtk::Label>("Radial");
    radial_label->set_xalign(0);
    radial_label->set_hexpand(true);
    m_symmetry_radial_switch = Gtk::make_managed<Gtk::Switch>();
    radial_row->append(*radial_label);
    radial_row->append(*m_symmetry_radial_switch);
    root->append(*radial_row);

    m_symmetry_mode_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_symmetry_mode_prev_button = Gtk::make_managed<Gtk::Button>();
    m_symmetry_mode_prev_button->set_icon_name("go-previous-symbolic");
    m_symmetry_mode_prev_button->set_has_frame(true);
    m_symmetry_mode_prev_button->set_tooltip_text("Previous mode");

    m_symmetry_mode_value_label = Gtk::make_managed<Gtk::Label>("Horizontal");
    m_symmetry_mode_value_label->set_hexpand(true);
    m_symmetry_mode_value_label->set_halign(Gtk::Align::CENTER);
    m_symmetry_mode_value_label->set_xalign(.5f);
    m_symmetry_mode_value_label->set_ellipsize(Pango::EllipsizeMode::END);
    m_symmetry_mode_value_label->set_max_width_chars(12);

    m_symmetry_mode_next_button = Gtk::make_managed<Gtk::Button>();
    m_symmetry_mode_next_button->set_icon_name("go-next-symbolic");
    m_symmetry_mode_next_button->set_has_frame(true);
    m_symmetry_mode_next_button->set_tooltip_text("Next mode");

    m_symmetry_mode_row->append(*m_symmetry_mode_prev_button);
    m_symmetry_mode_row->append(*m_symmetry_mode_value_label);
    m_symmetry_mode_row->append(*m_symmetry_mode_next_button);
    root->append(*m_symmetry_mode_row);

    m_symmetry_axes_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto count_label = Gtk::make_managed<Gtk::Label>("Segments");
    count_label->set_xalign(0);
    count_label->set_hexpand(true);
    m_symmetry_axes_spin = Gtk::make_managed<Gtk::SpinButton>();
    m_symmetry_axes_spin->set_range(3, 32);
    m_symmetry_axes_spin->set_increments(1, 1);
    m_symmetry_axes_spin->set_digits(0);
    m_symmetry_axes_spin->set_numeric(true);
    m_symmetry_axes_spin->set_width_chars(4);
    m_symmetry_axes_spin->set_value(4);
    m_symmetry_axes_row->append(*count_label);
    m_symmetry_axes_row->append(*m_symmetry_axes_spin);
    root->append(*m_symmetry_axes_row);

    m_symmetry_rotation_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto rotation_label = Gtk::make_managed<Gtk::Label>("Rotation");
    rotation_label->set_xalign(0);
    rotation_label->set_hexpand(true);
    m_symmetry_rotation_spin = Gtk::make_managed<Gtk::SpinButton>();
    m_symmetry_rotation_spin->set_range(-180, 180);
    m_symmetry_rotation_spin->set_increments(1, 10);
    m_symmetry_rotation_spin->set_digits(1);
    m_symmetry_rotation_spin->set_numeric(true);
    m_symmetry_rotation_spin->set_width_chars(5);
    m_symmetry_rotation_spin->set_value(0);
    auto rotation_unit_label = Gtk::make_managed<Gtk::Label>("deg");
    m_symmetry_rotation_row->append(*rotation_label);
    m_symmetry_rotation_row->append(*m_symmetry_rotation_spin);
    m_symmetry_rotation_row->append(*rotation_unit_label);
    root->append(*m_symmetry_rotation_row);

    m_symmetry_apply_button = Gtk::make_managed<Gtk::Button>("Set Axis");
    m_symmetry_apply_button->set_hexpand(true);
    root->append(*m_symmetry_apply_button);

    // Keep context hint at the bottom of the popover.
    root->append(*m_symmetry_context_label);

    m_symmetry_menu_button->signal_clicked().connect([this] {
        if (m_symmetry_enabled)
            set_symmetry_enabled(false, false);
        else
            set_symmetry_enabled(true, true);
    });
    m_symmetry_radial_switch->property_active().signal_changed().connect([this] {
        sync_symmetry_popover_context();
        apply_symmetry_live_from_popover(false);
    });
    m_symmetry_mode_prev_button->signal_clicked().connect([this] {
        m_symmetry_mode_selected = (m_symmetry_mode_selected + 1u) % 2u;
        sync_symmetry_popover_context();
        apply_symmetry_live_from_popover(false);
    });
    m_symmetry_mode_next_button->signal_clicked().connect([this] {
        m_symmetry_mode_selected = (m_symmetry_mode_selected + 1u) % 2u;
        sync_symmetry_popover_context();
        apply_symmetry_live_from_popover(false);
    });
    m_symmetry_apply_button->signal_clicked().connect(sigc::mem_fun(*this, &Editor::apply_symmetry_from_popover));
    popover->signal_show().connect(sigc::mem_fun(*this, &Editor::sync_symmetry_popover_context));
    m_symmetry_axes_spin->signal_value_changed().connect([this] {
        sync_symmetry_popover_context();
        apply_symmetry_live_from_popover(false);
        if (m_symmetry_popover && !m_symmetry_popover->get_visible())
            m_symmetry_popover->popup();
    });
    m_symmetry_rotation_spin->signal_value_changed().connect([this] {
        sync_symmetry_popover_context();
        apply_symmetry_live_from_popover(false);
    });

    sync_symmetry_popover_context();
#endif
}

void Editor::sync_symmetry_popover_context()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_symmetry_context_label || !m_symmetry_mode_value_label || !m_symmetry_radial_switch || !m_symmetry_mode_row
        || !m_symmetry_mode_prev_button || !m_symmetry_mode_next_button || !m_symmetry_axes_row || !m_symmetry_axes_spin
        || !m_symmetry_rotation_row || !m_symmetry_rotation_spin || !m_symmetry_apply_button)
        return;
    update_sketcher_toolbar_button_states();

    const bool radial_selected = m_symmetry_radial_switch->get_active();
    m_symmetry_mode_row->set_visible(!radial_selected);
    m_symmetry_axes_row->set_visible(radial_selected);
    m_symmetry_rotation_row->set_visible(true);
    m_symmetry_apply_button->set_visible(radial_selected);

    if (!m_core.has_documents()) {
        m_symmetry_context_label->set_text("Open or create a sketch first.");
        m_symmetry_mode_value_label->set_text(m_symmetry_mode_selected == 1 ? "Vertical" : "Horizontal");
        m_symmetry_radial_switch->set_sensitive(false);
        m_symmetry_mode_prev_button->set_sensitive(false);
        m_symmetry_mode_next_button->set_sensitive(false);
        m_symmetry_axes_spin->set_sensitive(false);
        m_symmetry_rotation_spin->set_sensitive(false);
        m_symmetry_apply_button->set_sensitive(false);
        return;
    }
    m_symmetry_radial_switch->set_sensitive(true);
    m_symmetry_mode_prev_button->set_sensitive(!radial_selected);
    m_symmetry_mode_next_button->set_sensitive(!radial_selected);

    const auto &doc = m_core.get_current_document();
    const auto current_group_uu = m_core.get_current_group();
    const auto &current_group = doc.get_group(current_group_uu);
    if (!current_group.m_active_wrkpl) {
        m_symmetry_context_label->set_text("Current sketch has no active workplane.");
        m_symmetry_mode_value_label->set_text(m_symmetry_mode_selected == 1 ? "Vertical" : "Horizontal");
        m_symmetry_axes_spin->set_sensitive(radial_selected);
        m_symmetry_rotation_spin->set_sensitive(true);
        m_symmetry_apply_button->set_sensitive(radial_selected);
        return;
    }
    const auto source_wrkpl_uu = current_group.m_active_wrkpl;
    const auto &source_wrkpl = doc.get_entity<EntityWorkplane>(source_wrkpl_uu);

    const auto selection = get_canvas().get_selection();
    const auto selected_entities = entities_from_selection(selection);
    const auto lines = selected_line_entities_in_group(doc, selection, current_group_uu);
    const auto circle_uu = selected_circle_entity_in_group(doc, selection, current_group_uu);
    auto two_points = two_points_from_selection(doc, selection);
    if (two_points) {
        const auto &en1 = doc.get_entity(two_points->point1.entity);
        const auto &en2 = doc.get_entity(two_points->point2.entity);
        if (en1.m_group != current_group_uu && en2.m_group != current_group_uu)
            two_points.reset();
    }

    std::optional<std::pair<glm::dvec2, glm::dvec2>> two_points_in_plane;
    if (two_points) {
        const auto p1 = source_wrkpl.project(doc.get_point(two_points->point1));
        const auto p2 = source_wrkpl.project(doc.get_point(two_points->point2));
        two_points_in_plane = std::make_pair(p1, p2);
    }
    else if (selected_entities.size() == 2) {
        auto selectable_to_point_plane = [&](const SelectableRef &sr) -> std::optional<glm::dvec2> {
            if (sr.type != SelectableRef::Type::ENTITY)
                return {};
            const auto &en = doc.get_entity(sr.item);
            if (en.m_group != current_group_uu)
                return {};
            if (doc.is_valid_point(sr.get_entity_and_point()))
                return source_wrkpl.project(doc.get_point(sr.get_entity_and_point()));
            if (sr.point != 0)
                return {};
            if (const auto *line = doc.get_entity_ptr<EntityLine2D>(sr.item))
                return (line->m_p1 + line->m_p2) * .5;
            if (const auto *circle = doc.get_entity_ptr<EntityCircle2D>(sr.item))
                return (circle->m_wrkpl == source_wrkpl_uu)
                               ? circle->m_center
                               : source_wrkpl.project(doc.get_entity<EntityWorkplane>(circle->m_wrkpl).transform(circle->m_center));
            if (const auto *arc = doc.get_entity_ptr<EntityArc2D>(sr.item))
                return (arc->m_wrkpl == source_wrkpl_uu)
                               ? arc->m_center
                               : source_wrkpl.project(doc.get_entity<EntityWorkplane>(arc->m_wrkpl).transform(arc->m_center));
            return {};
        };
        auto it = selected_entities.begin();
        const auto p1 = selectable_to_point_plane(*it++);
        const auto p2 = selectable_to_point_plane(*it);
        if (p1 && p2)
            two_points_in_plane = std::make_pair(*p1, *p2);
    }

    if (radial_selected) {
        m_symmetry_mode_value_label->set_text("Radial");
        m_symmetry_axes_spin->set_sensitive(true);
        m_symmetry_rotation_spin->set_sensitive(true);
        m_symmetry_apply_button->set_sensitive(true);
        if (circle_uu && selected_entities.size() == 1) {
            m_symmetry_context_label->set_text("Circle selected: radial center uses the circle center.");
        }
        else if (two_points_in_plane) {
            m_symmetry_context_label->set_text("Two items selected: radial center is their midpoint.");
        }
        else if (selected_entities.empty()) {
            m_symmetry_context_label->set_text("No selection: radial center is at coordinate origin (X/Y).");
        }
        else {
            m_symmetry_context_label->set_text("Radial mode: select one circle or two items.");
        }
    }
    else {
        m_symmetry_axes_spin->set_sensitive(false);
        m_symmetry_rotation_spin->set_sensitive(true);
        m_symmetry_apply_button->set_sensitive(false);
        const bool dual_axes_selection = selected_entities.size() >= 3 && !lines.empty();
        if (dual_axes_selection) {
            m_symmetry_mode_prev_button->set_sensitive(false);
            m_symmetry_mode_next_button->set_sensitive(false);
            m_symmetry_mode_value_label->set_text("H + V");
            m_symmetry_context_label->set_text("Multi-selection: horizontal and vertical axes use selection center.");
        }
        else if (two_points_in_plane) {
            const auto d = two_points_in_plane->second - two_points_in_plane->first;
            // Left-right spread means vertical symmetry axis, top-bottom means horizontal.
            if (std::abs(d.x) >= std::abs(d.y))
                m_symmetry_mode_selected = 1;
            else
                m_symmetry_mode_selected = 0;
            m_symmetry_context_label->set_text("Two items selected: axis orientation is picked automatically.");
        }
        else if (selected_entities.empty()) {
            m_symmetry_context_label->set_text("No selection: choose Horizontal/Vertical mode for origin symmetry.");
        }
        else {
            m_symmetry_context_label->set_text("Selection detected: axis goes through selection center.");
        }
        if (!dual_axes_selection)
            m_symmetry_mode_value_label->set_text(m_symmetry_mode_selected == 1 ? "Vertical" : "Horizontal");
    }
#endif
}

bool Editor::configure_symmetry_from_current_context(bool show_toast_on_fail)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_core.has_documents())
        return false;

    const auto &doc = m_core.get_current_document();
    const auto source_group_uu = m_core.get_current_group();
    const auto &source_group = doc.get_group(source_group_uu);
    if (!source_group.m_active_wrkpl) {
        if (show_toast_on_fail)
            m_workspace_browser->show_toast("Current group needs an active workplane");
        return false;
    }
    const auto source_wrkpl_uu = source_group.m_active_wrkpl;
    const auto &source_wrkpl = doc.get_entity<EntityWorkplane>(source_wrkpl_uu);

    const auto selection = get_canvas().get_selection();
    const auto selected_entities = entities_from_selection(selection);
    const auto selected_circle_uu = selected_circle_entity_in_group(doc, selection, source_group_uu);
    auto two_points = two_points_from_selection(doc, selection);
    std::optional<glm::dvec3> midpoint_from_entities;
    std::optional<std::pair<glm::dvec2, glm::dvec2>> two_points_in_plane;
    std::optional<glm::dvec2> selection_center_in_plane;

    if (two_points) {
        const auto &en1 = doc.get_entity(two_points->point1.entity);
        const auto &en2 = doc.get_entity(two_points->point2.entity);
        if (en1.m_group != source_group_uu && en2.m_group != source_group_uu)
            two_points.reset();
    }

    auto selectable_to_point = [&](const SelectableRef &sr) -> std::optional<glm::dvec3> {
        if (sr.type != SelectableRef::Type::ENTITY)
            return {};
        const auto &en = doc.get_entity(sr.item);
        if (en.m_group != source_group_uu)
            return {};
        if (doc.is_valid_point(sr.get_entity_and_point()))
            return doc.get_point(sr.get_entity_and_point());
        if (sr.point != 0)
            return {};
        if (const auto *line = doc.get_entity_ptr<EntityLine2D>(sr.item))
            return source_wrkpl.transform((line->m_p1 + line->m_p2) * .5);
        if (const auto *circle = doc.get_entity_ptr<EntityCircle2D>(sr.item))
            return doc.get_entity<EntityWorkplane>(circle->m_wrkpl).transform(circle->m_center);
        if (const auto *arc = doc.get_entity_ptr<EntityArc2D>(sr.item))
            return doc.get_entity<EntityWorkplane>(arc->m_wrkpl).transform(arc->m_center);
        if (const auto *point = doc.get_entity_ptr<EntityPoint2D>(sr.item))
            return doc.get_entity<EntityWorkplane>(point->m_wrkpl).transform(point->m_p);
        return {};
    };

    if (!two_points && selected_entities.size() == 2) {

        auto it = selected_entities.begin();
        const auto p1 = selectable_to_point(*it++);
        const auto p2 = selectable_to_point(*it);
        if (p1 && p2) {
            midpoint_from_entities = (*p1 + *p2) * .5;
            two_points_in_plane = std::make_pair(source_wrkpl.project(*p1), source_wrkpl.project(*p2));
        }
    }
    else if (two_points) {
        const auto p1 = source_wrkpl.project(doc.get_point(two_points->point1));
        const auto p2 = source_wrkpl.project(doc.get_point(two_points->point2));
        two_points_in_plane = std::make_pair(p1, p2);
    }

    {
        glm::dvec2 sum(0, 0);
        size_t count = 0;
        for (const auto &sr : selected_entities) {
            const auto p = selectable_to_point(sr);
            if (!p)
                continue;
            sum += source_wrkpl.project(*p);
            count++;
        }
        if (count > 0)
            selection_center_in_plane = sum / static_cast<double>(count);
    }

    SymmetryMode mode = SymmetryMode::HORIZONTAL;
    std::vector<std::pair<glm::dvec2, glm::dvec2>> axes;
    unsigned int radial_axes =
            std::max(3u, static_cast<unsigned int>(m_symmetry_axes_spin ? m_symmetry_axes_spin->get_value_as_int() : 4));
    double radial_rotation_deg = m_symmetry_radial_rotation_deg;
    if (m_symmetry_rotation_spin)
        radial_rotation_deg = m_symmetry_rotation_spin->get_value();
    const bool radial_selected = m_symmetry_radial_switch && m_symmetry_radial_switch->get_active();

    if (radial_selected) {
        mode = SymmetryMode::RADIAL;
        std::optional<glm::dvec2> center;
        if (selected_circle_uu && selected_entities.size() == 1) {
            if (const auto *circle = doc.get_entity_ptr<EntityCircle2D>(*selected_circle_uu)) {
                center = (circle->m_wrkpl == source_wrkpl_uu)
                                 ? circle->m_center
                                 : source_wrkpl.project(doc.get_entity<EntityWorkplane>(circle->m_wrkpl).transform(circle->m_center));
            }
            else if (const auto *arc = doc.get_entity_ptr<EntityArc2D>(*selected_circle_uu)) {
                center = (arc->m_wrkpl == source_wrkpl_uu)
                                 ? arc->m_center
                                 : source_wrkpl.project(doc.get_entity<EntityWorkplane>(arc->m_wrkpl).transform(arc->m_center));
            }
        }
        else if (two_points || midpoint_from_entities) {
            const auto midpoint_world = two_points
                                                ? (doc.get_point(two_points->point1) + doc.get_point(two_points->point2)) * .5
                                                : *midpoint_from_entities;
            center = source_wrkpl.project(midpoint_world);
        }
        else if (selected_entities.empty()) {
            center = glm::dvec2(0, 0);
        }
        else if (selection_center_in_plane) {
            center = *selection_center_in_plane;
        }

        if (!center) {
            if (show_toast_on_fail)
                m_workspace_browser->show_toast("Radial mode needs a center: circle, two items, or any selection");
            return false;
        }
        const auto phase = radial_rotation_deg_to_rad(radial_rotation_deg);
        for (unsigned int i = 0; i < radial_axes; i++) {
            const auto angle = phase + 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(radial_axes);
            axes.emplace_back(*center, glm::dvec2(std::cos(angle), std::sin(angle)));
        }
    }
    else if (selection_center_in_plane && selected_entities.size() >= 3) {
        const auto phase = radial_rotation_deg_to_rad(radial_rotation_deg);
        const auto horizontal_dir = rotate_dir_2d(glm::dvec2(1, 0), phase);
        const auto vertical_dir = rotate_dir_2d(glm::dvec2(0, 1), phase);
        mode = SymmetryMode::HORIZONTAL;
        axes.emplace_back(*selection_center_in_plane, horizontal_dir);
        axes.emplace_back(*selection_center_in_plane, vertical_dir);
    }
    else if (two_points || midpoint_from_entities) {
        const auto midpoint_world = two_points
                                            ? (doc.get_point(two_points->point1) + doc.get_point(two_points->point2)) * .5
                                            : *midpoint_from_entities;
        const auto center = source_wrkpl.project(midpoint_world);
        const auto phase = radial_rotation_deg_to_rad(radial_rotation_deg);
        const auto horizontal_dir = rotate_dir_2d(glm::dvec2(1, 0), phase);
        const auto vertical_dir = rotate_dir_2d(glm::dvec2(0, 1), phase);
        if (two_points_in_plane) {
            const auto d = two_points_in_plane->second - two_points_in_plane->first;
            if (std::abs(d.x) >= std::abs(d.y)) {
                mode = SymmetryMode::VERTICAL;
                axes.emplace_back(center, vertical_dir);
            }
            else {
                mode = SymmetryMode::HORIZONTAL;
                axes.emplace_back(center, horizontal_dir);
            }
        }
        else {
            mode = SymmetryMode::HORIZONTAL;
            axes.emplace_back(center, horizontal_dir);
        }
    }
    else if (selection_center_in_plane) {
        const auto phase = radial_rotation_deg_to_rad(radial_rotation_deg);
        const auto horizontal_dir = rotate_dir_2d(glm::dvec2(1, 0), phase);
        const auto vertical_dir = rotate_dir_2d(glm::dvec2(0, 1), phase);
        if (m_symmetry_mode_selected == 1) {
            mode = SymmetryMode::VERTICAL;
            axes.emplace_back(*selection_center_in_plane, vertical_dir);
        }
        else {
            mode = SymmetryMode::HORIZONTAL;
            axes.emplace_back(*selection_center_in_plane, horizontal_dir);
        }
    }
    else if (selected_entities.empty()) {
        const auto phase = radial_rotation_deg_to_rad(radial_rotation_deg);
        const auto horizontal_dir = rotate_dir_2d(glm::dvec2(1, 0), phase);
        const auto vertical_dir = rotate_dir_2d(glm::dvec2(0, 1), phase);
        if (m_symmetry_mode_selected == 1) {
            mode = SymmetryMode::VERTICAL;
            axes.emplace_back(glm::dvec2(0, 0), vertical_dir);
        }
        else {
            mode = SymmetryMode::HORIZONTAL;
            axes.emplace_back(glm::dvec2(0, 0), horizontal_dir);
        }
    }
    else {
        if (show_toast_on_fail)
            m_workspace_browser->show_toast("Select entities to define center, or switch to radial");
        return false;
    }

    m_symmetry_group = source_group_uu;
    m_symmetry_workplane = source_wrkpl_uu;
    m_symmetry_mode = mode;
    m_symmetry_radial_axes = radial_axes;
    m_symmetry_radial_rotation_deg = radial_rotation_deg;
    m_symmetry_axes = std::move(axes);
    return true;
#else
    (void)show_toast_on_fail;
    return false;
#endif
}

void Editor::set_symmetry_enabled(bool enabled, bool reconfigure)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!enabled) {
        m_symmetry_enabled = false;
        m_symmetry_axes.clear();
        m_symmetry_capture_tool_id = ToolID::NONE;
        m_symmetry_pre_tool_entities.clear();
        m_symmetry_pre_tool_constraints.clear();
        m_symmetry_pre_tool_entities_captured = false;
        m_symmetry_move_roots_before.clear();
        clear_symmetry_live_preview_entities();
        if (m_core.has_documents())
            canvas_update_keep_selection();
        sync_symmetry_popover_context();
        return;
    }

    if (reconfigure || m_symmetry_axes.empty()) {
        if (!configure_symmetry_from_current_context(true)) {
            m_symmetry_enabled = false;
            sync_symmetry_popover_context();
            return;
        }
    }

    m_symmetry_enabled = true;
    if (m_core.tool_is_active())
        capture_symmetry_entities_before_tool(m_core.get_tool_id());
    update_symmetry_live_preview_entities();
    if (m_core.has_documents())
        canvas_update_keep_selection();
    sync_symmetry_popover_context();
#endif
}

void Editor::apply_symmetry_from_popover()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_symmetry_enabled) {
        set_symmetry_enabled(true, true);
        return;
    }
    apply_symmetry_live_from_popover(true);
    sync_symmetry_popover_context();
#endif
}

void Editor::apply_symmetry_live_from_popover(bool show_toast_on_fail)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_symmetry_enabled)
        return;
    if (configure_symmetry_from_current_context(show_toast_on_fail) && m_core.has_documents()) {
        if (m_core.tool_is_active())
            capture_symmetry_entities_before_tool(m_core.get_tool_id());
        update_symmetry_live_preview_entities();
        canvas_update_keep_selection();
    }
#else
    (void)show_toast_on_fail;
#endif
}

bool Editor::should_apply_symmetry_for_tool(ToolID id) const
{
#ifdef DUNE_SKETCHER_ONLY
    switch (id) {
    case ToolID::DRAW_CONTOUR:
    case ToolID::DRAW_CONTOUR_FROM_POINT:
    case ToolID::DRAW_LINE_2D:
    case ToolID::DRAW_ARC_2D:
    case ToolID::DRAW_BEZIER_2D:
    case ToolID::DRAW_POINT_2D:
    case ToolID::DRAW_CIRCLE_2D:
    case ToolID::DRAW_REGULAR_POLYGON:
    case ToolID::DRAW_RECTANGLE:
    case ToolID::DRAW_TEXT:
        return true;
    default:
        return false;
    }
#else
    (void)id;
    return false;
#endif
}

void Editor::capture_symmetry_entities_before_tool(ToolID id)
{
#ifdef DUNE_SKETCHER_ONLY
    clear_symmetry_live_preview_entities();
    m_symmetry_capture_tool_id = id;
    m_symmetry_pre_tool_entities.clear();
    m_symmetry_pre_tool_constraints.clear();
    m_symmetry_pre_tool_entities_captured = false;
    m_symmetry_move_roots_before.clear();
    if (!m_symmetry_enabled || !should_apply_symmetry_for_tool(id) || !m_core.has_documents())
        return;
    if (m_core.get_current_group() != m_symmetry_group)
        return;

    const auto &doc = m_core.get_current_document();
    for (const auto &[uu, en] : doc.m_entities) {
        if (en->m_group == m_symmetry_group)
            m_symmetry_pre_tool_entities.insert(uu);
    }
    for (const auto &[uu, constr] : doc.m_constraints) {
        if (constr->m_group == m_symmetry_group)
            m_symmetry_pre_tool_constraints.insert(uu);
    }
    m_symmetry_pre_tool_entities_captured = true;

    if (id == ToolID::MOVE) {
        const auto selection = get_canvas().get_selection();
        for (const auto &sr : entities_from_selection(selection)) {
            if (sr.type != SelectableRef::Type::ENTITY)
                continue;
            const auto *en = doc.get_entity_ptr(sr.item);
            if (!en)
                continue;
            if (en->m_group != m_symmetry_group)
                continue;
            if (en->m_generated_from)
                continue;
            const auto en_wrkpl = dynamic_cast<const IEntityInWorkplane *>(en);
            if (!en_wrkpl || en_wrkpl->get_workplane() != m_symmetry_workplane)
                continue;
            if (!en->of_type(Entity::Type::LINE_2D, Entity::Type::ARC_2D, Entity::Type::CIRCLE_2D, Entity::Type::BEZIER_2D,
                             Entity::Type::POINT_2D))
                continue;
            m_symmetry_move_roots_before.emplace(sr.item, en->clone());
        }
    }
#else
    (void)id;
#endif
}

void Editor::clear_symmetry_live_preview_entities()
{
#ifdef DUNE_SKETCHER_ONLY
    if (m_symmetry_live_preview_entities.empty() || !m_core.has_documents())
        return;
    auto &doc = m_core.get_current_document();
    for (const auto &uu : m_symmetry_live_preview_entities)
        doc.m_entities.erase(uu);
    m_symmetry_live_preview_entities.clear();
#endif
}

void Editor::update_symmetry_live_preview_entities()
{
#ifdef DUNE_SKETCHER_ONLY
    clear_symmetry_live_preview_entities();
    if (!m_symmetry_enabled || !m_core.tool_is_active() || !m_core.has_documents())
        return;
    if (!should_apply_symmetry_for_tool(m_core.get_tool_id()))
        return;
    if (m_core.get_current_group() != m_symmetry_group)
        return;
    if (m_symmetry_axes.empty())
        return;

    auto &doc = m_core.get_current_document();
    std::vector<UUID> source_entities;
    for (const auto &[uu, en] : doc.m_entities) {
        if (en->m_group != m_symmetry_group)
            continue;
        if (m_symmetry_pre_tool_entities.contains(uu))
            continue;
        if (en->m_kind != ItemKind::USER)
            continue;
        const auto en_wrkpl = dynamic_cast<const IEntityInWorkplane *>(en.get());
        if (!en_wrkpl || en_wrkpl->get_workplane() != m_symmetry_workplane)
            continue;
        if (!en->of_type(Entity::Type::LINE_2D, Entity::Type::ARC_2D, Entity::Type::CIRCLE_2D, Entity::Type::BEZIER_2D,
                         Entity::Type::POINT_2D))
            continue;
        source_entities.push_back(uu);
    }
    if (source_entities.empty())
        return;

    auto transform_entity = [&](Entity &dst, const Entity &src,
                                const std::function<glm::dvec2(const glm::dvec2 &)> &transform, bool mirrored) {
        (void)src;
        if (auto *line = dynamic_cast<EntityLine2D *>(&dst)) {
            line->m_p1 = transform(line->m_p1);
            line->m_p2 = transform(line->m_p2);
        }
        else if (auto *arc = dynamic_cast<EntityArc2D *>(&dst)) {
            const auto from = transform(arc->m_from);
            const auto to = transform(arc->m_to);
            arc->m_center = transform(arc->m_center);
            if (mirrored) {
                arc->m_from = to;
                arc->m_to = from;
            }
            else {
                arc->m_from = from;
                arc->m_to = to;
            }
        }
        else if (auto *circle = dynamic_cast<EntityCircle2D *>(&dst)) {
            circle->m_center = transform(circle->m_center);
        }
        else if (auto *bez = dynamic_cast<EntityBezier2D *>(&dst)) {
            bez->m_p1 = transform(bez->m_p1);
            bez->m_p2 = transform(bez->m_p2);
            bez->m_c1 = transform(bez->m_c1);
            bez->m_c2 = transform(bez->m_c2);
        }
        else if (auto *point = dynamic_cast<EntityPoint2D *>(&dst)) {
            point->m_p = transform(point->m_p);
        }
    };

    std::vector<std::unique_ptr<Entity>> clones;
    if (m_symmetry_mode == SymmetryMode::RADIAL) {
        const auto center = m_symmetry_axes.front().first;
        const auto count = std::max(3u, m_symmetry_radial_axes);
        const auto phase = radial_rotation_deg_to_rad(m_symmetry_radial_rotation_deg);
        for (const auto &uu : source_entities) {
            const auto &src = doc.get_entity(uu);
            for (unsigned int i = 1; i < count; i++) {
                const auto angle = phase + 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(count);
                auto clone = src.clone();
                transform_entity(*clone, src, [&](const glm::dvec2 &p) { return rotate_point_2d(p, center, angle); }, false);
                clone->m_uuid = UUID::random();
                clone->m_group = m_symmetry_group;
                clone->m_kind = ItemKind::GENRERATED;
                clone->m_generated_from = uu;
                // Keep preview geometry non-interactive: tools must not snap/constrain against ephemeral clones.
                clone->m_selection_invisible = true;
                clone->m_move_instead.clear();
                clones.push_back(std::move(clone));
            }
        }
    }
    else {
        for (const auto &uu : source_entities) {
            const auto &src = doc.get_entity(uu);
            for (const auto &[axis_point, axis_dir] : m_symmetry_axes) {
                auto clone = src.clone();
                transform_entity(*clone, src,
                                 [&](const glm::dvec2 &p) { return reflect_point_2d(p, axis_point, axis_dir); }, true);
                clone->m_uuid = UUID::random();
                clone->m_group = m_symmetry_group;
                clone->m_kind = ItemKind::GENRERATED;
                clone->m_generated_from = uu;
                // Keep preview geometry non-interactive: tools must not snap/constrain against ephemeral clones.
                clone->m_selection_invisible = true;
                clone->m_move_instead.clear();
                clones.push_back(std::move(clone));
            }
        }
    }

    for (auto &clone : clones) {
        m_symmetry_live_preview_entities.insert(clone->m_uuid);
        doc.m_entities.emplace(clone->m_uuid, std::move(clone));
    }
#endif
}

void Editor::sync_symmetry_for_move_selection()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_symmetry_enabled || !m_core.tool_is_active() || !m_core.has_documents())
        return;
    if (m_core.get_tool_id() != ToolID::MOVE)
        return;
    if (m_core.get_current_group() != m_symmetry_group)
        return;
    if (m_symmetry_axes.empty())
        return;

    auto &doc = m_core.get_current_document();
    std::vector<UUID> roots;
    for (const auto &sr : entities_from_selection(m_core.get_tool_selection())) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        const auto *en = doc.get_entity_ptr(sr.item);
        if (!en)
            continue;
        if (en->m_group != m_symmetry_group)
            continue;
        if (en->m_generated_from)
            continue;
        const auto en_wrkpl = dynamic_cast<const IEntityInWorkplane *>(en);
        if (!en_wrkpl || en_wrkpl->get_workplane() != m_symmetry_workplane)
            continue;
        if (!en->of_type(Entity::Type::LINE_2D, Entity::Type::ARC_2D, Entity::Type::CIRCLE_2D, Entity::Type::BEZIER_2D,
                         Entity::Type::POINT_2D))
            continue;
        if (std::find(roots.begin(), roots.end(), sr.item) == roots.end())
            roots.push_back(sr.item);
    }
    if (roots.empty())
        return;

    auto transform_entity = [&](Entity &dst, const Entity &src,
                                const std::function<glm::dvec2(const glm::dvec2 &)> &transform, bool mirrored) {
        (void)src;
        if (auto *line = dynamic_cast<EntityLine2D *>(&dst)) {
            line->m_p1 = transform(line->m_p1);
            line->m_p2 = transform(line->m_p2);
        }
        else if (auto *arc = dynamic_cast<EntityArc2D *>(&dst)) {
            const auto from = transform(arc->m_from);
            const auto to = transform(arc->m_to);
            arc->m_center = transform(arc->m_center);
            if (mirrored) {
                arc->m_from = to;
                arc->m_to = from;
            }
            else {
                arc->m_from = from;
                arc->m_to = to;
            }
        }
        else if (auto *circle = dynamic_cast<EntityCircle2D *>(&dst)) {
            circle->m_center = transform(circle->m_center);
        }
        else if (auto *bez = dynamic_cast<EntityBezier2D *>(&dst)) {
            bez->m_p1 = transform(bez->m_p1);
            bez->m_p2 = transform(bez->m_p2);
            bez->m_c1 = transform(bez->m_c1);
            bez->m_c2 = transform(bez->m_c2);
        }
        else if (auto *point = dynamic_cast<EntityPoint2D *>(&dst)) {
            point->m_p = transform(point->m_p);
        }
    };

    auto entity_match_score = [&](const Entity &a, const Entity &b) -> double {
        if (a.get_type() != b.get_type())
            return std::numeric_limits<double>::infinity();

        if (const auto *ca = dynamic_cast<const EntityCircle2D *>(&a)) {
            const auto *cb = dynamic_cast<const EntityCircle2D *>(&b);
            if (!cb)
                return std::numeric_limits<double>::infinity();
            return glm::length(ca->m_center - cb->m_center) + std::abs(ca->m_radius - cb->m_radius);
        }
        if (const auto *pa = dynamic_cast<const EntityPoint2D *>(&a)) {
            const auto *pb = dynamic_cast<const EntityPoint2D *>(&b);
            if (!pb)
                return std::numeric_limits<double>::infinity();
            return glm::length(pa->m_p - pb->m_p);
        }
        if (const auto *la = dynamic_cast<const EntityLine2D *>(&a)) {
            const auto *lb = dynamic_cast<const EntityLine2D *>(&b);
            if (!lb)
                return std::numeric_limits<double>::infinity();
            const auto d1 = glm::length(la->m_p1 - lb->m_p1) + glm::length(la->m_p2 - lb->m_p2);
            const auto d2 = glm::length(la->m_p1 - lb->m_p2) + glm::length(la->m_p2 - lb->m_p1);
            return std::min(d1, d2);
        }
        if (const auto *aa = dynamic_cast<const EntityArc2D *>(&a)) {
            const auto *ab = dynamic_cast<const EntityArc2D *>(&b);
            if (!ab)
                return std::numeric_limits<double>::infinity();
            const auto d1 = glm::length(aa->m_center - ab->m_center) + glm::length(aa->m_from - ab->m_from)
                    + glm::length(aa->m_to - ab->m_to);
            const auto d2 = glm::length(aa->m_center - ab->m_center) + glm::length(aa->m_from - ab->m_to)
                    + glm::length(aa->m_to - ab->m_from);
            return std::min(d1, d2);
        }
        if (const auto *ba = dynamic_cast<const EntityBezier2D *>(&a)) {
            const auto *bb = dynamic_cast<const EntityBezier2D *>(&b);
            if (!bb)
                return std::numeric_limits<double>::infinity();
            const auto d1 = glm::length(ba->m_p1 - bb->m_p1) + glm::length(ba->m_p2 - bb->m_p2)
                    + glm::length(ba->m_c1 - bb->m_c1) + glm::length(ba->m_c2 - bb->m_c2);
            const auto d2 = glm::length(ba->m_p1 - bb->m_p2) + glm::length(ba->m_p2 - bb->m_p1)
                    + glm::length(ba->m_c1 - bb->m_c2) + glm::length(ba->m_c2 - bb->m_c1);
            return std::min(d1, d2);
        }
        return std::numeric_limits<double>::infinity();
    };

    bool changed = false;
    std::set<UUID> protected_roots(roots.begin(), roots.end());
    for (const auto &root_uu : roots) {
        const auto *root = doc.get_entity_ptr(root_uu);
        if (!root)
            continue;
        if (!m_symmetry_move_roots_before.contains(root_uu)) {
            if (const auto *root_before = m_core.get_current_last_document().get_entity_ptr(root_uu))
                m_symmetry_move_roots_before.emplace(root_uu, root_before->clone());
            else
                m_symmetry_move_roots_before.emplace(root_uu, root->clone());
        }

        std::vector<UUID> to_erase;
        for (const auto &[uu, en] : doc.m_entities) {
            if (en->m_group == m_symmetry_group && en->m_generated_from == root_uu)
                to_erase.push_back(uu);
        }

        if (to_erase.empty() && m_symmetry_move_roots_before.contains(root_uu)) {
            const auto &root_before = *m_symmetry_move_roots_before.at(root_uu);
            std::vector<std::unique_ptr<Entity>> expected_legacy;
            if (m_symmetry_mode == SymmetryMode::RADIAL) {
                const auto center = m_symmetry_axes.front().first;
                const auto count = std::max(3u, m_symmetry_radial_axes);
                const auto phase = radial_rotation_deg_to_rad(m_symmetry_radial_rotation_deg);
                for (unsigned int i = 1; i < count; i++) {
                    const auto angle = phase + 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(count);
                    auto expected = root_before.clone();
                    transform_entity(*expected, root_before,
                                     [&](const glm::dvec2 &p) { return rotate_point_2d(p, center, angle); }, false);
                    expected_legacy.push_back(std::move(expected));
                }
            }
            else {
                for (const auto &[axis_point, axis_dir] : m_symmetry_axes) {
                    auto expected = root_before.clone();
                    transform_entity(*expected, root_before,
                                     [&](const glm::dvec2 &p) { return reflect_point_2d(p, axis_point, axis_dir); }, true);
                    expected_legacy.push_back(std::move(expected));
                }
            }

            std::set<UUID> matched;
            for (const auto &expected : expected_legacy) {
                UUID best_uu;
                double best_score = std::numeric_limits<double>::infinity();
                for (const auto &[uu, en] : doc.m_entities) {
                    if (en->m_group != m_symmetry_group)
                        continue;
                    if (uu == root_uu || protected_roots.contains(uu) || matched.contains(uu))
                        continue;
                    if (en->m_generated_from)
                        continue;
                    const auto en_wrkpl = dynamic_cast<const IEntityInWorkplane *>(en.get());
                    if (!en_wrkpl || en_wrkpl->get_workplane() != m_symmetry_workplane)
                        continue;
                    const auto score = entity_match_score(*expected, *en);
                    if (score < best_score) {
                        best_score = score;
                        best_uu = uu;
                    }
                }
                if (best_uu && std::isfinite(best_score) && best_score < 1e-2) {
                    matched.insert(best_uu);
                    to_erase.push_back(best_uu);
                }
            }
        }

        for (const auto &uu : to_erase)
            doc.m_entities.erase(uu);

        std::vector<std::unique_ptr<Entity>> clones;
        if (m_symmetry_mode == SymmetryMode::RADIAL) {
            const auto center = m_symmetry_axes.front().first;
            const auto count = std::max(3u, m_symmetry_radial_axes);
            const auto phase = radial_rotation_deg_to_rad(m_symmetry_radial_rotation_deg);
            for (unsigned int i = 1; i < count; i++) {
                const auto angle = phase + 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(count);
                auto clone = root->clone();
                transform_entity(*clone, *root, [&](const glm::dvec2 &p) { return rotate_point_2d(p, center, angle); }, false);
                clone->m_uuid = UUID::random();
                clone->m_group = m_symmetry_group;
                clone->m_kind = ItemKind::USER;
                clone->m_generated_from = root_uu;
                clone->m_selection_invisible = false;
                clone->m_move_instead.clear();
                clones.push_back(std::move(clone));
            }
        }
        else {
            for (const auto &[axis_point, axis_dir] : m_symmetry_axes) {
                auto clone = root->clone();
                transform_entity(*clone, *root,
                                 [&](const glm::dvec2 &p) { return reflect_point_2d(p, axis_point, axis_dir); }, true);
                clone->m_uuid = UUID::random();
                clone->m_group = m_symmetry_group;
                clone->m_kind = ItemKind::USER;
                clone->m_generated_from = root_uu;
                clone->m_selection_invisible = false;
                clone->m_move_instead.clear();
                clones.push_back(std::move(clone));
            }
        }

        for (auto &clone : clones)
            doc.m_entities.emplace(clone->m_uuid, std::move(clone));
        changed = true;
    }

    if (changed)
        m_core.set_needs_save();
#endif
}

void Editor::apply_symmetry_to_new_entities_after_commit()
{
#ifdef DUNE_SKETCHER_ONLY
    clear_symmetry_live_preview_entities();
    if (!m_symmetry_enabled || !m_core.has_documents())
        return;
    if (!should_apply_symmetry_for_tool(m_symmetry_capture_tool_id))
        return;
    if (m_core.get_current_group() != m_symmetry_group)
        return;
    if (m_symmetry_axes.empty())
        return;

    auto &doc = m_core.get_current_document();
    if (!m_symmetry_pre_tool_entities_captured) {
        for (const auto &[uu, en] : doc.m_entities) {
            if (en->m_group == m_symmetry_group)
                m_symmetry_pre_tool_entities.insert(uu);
        }
        for (const auto &[uu, constr] : doc.m_constraints) {
            if (constr->m_group == m_symmetry_group)
                m_symmetry_pre_tool_constraints.insert(uu);
        }
        m_symmetry_pre_tool_entities_captured = true;
        return;
    }

    std::vector<UUID> new_entities;
    for (const auto &[uu, en] : doc.m_entities) {
        if (en->m_group != m_symmetry_group)
            continue;
        if (m_symmetry_pre_tool_entities.contains(uu))
            continue;
        if (en->m_kind != ItemKind::USER)
            continue;
        if (en->m_selection_invisible)
            continue;
        const auto en_wrkpl = dynamic_cast<const IEntityInWorkplane *>(en.get());
        if (!en_wrkpl || en_wrkpl->get_workplane() != m_symmetry_workplane)
            continue;
        if (!en->of_type(Entity::Type::LINE_2D, Entity::Type::ARC_2D, Entity::Type::CIRCLE_2D, Entity::Type::BEZIER_2D,
                         Entity::Type::POINT_2D))
            continue;
        new_entities.push_back(uu);
    }

    std::set<UUID> new_entities_set(new_entities.begin(), new_entities.end());
    std::vector<UUID> new_constraints;
    for (const auto &[uu, constr] : doc.m_constraints) {
        if (constr->m_group != m_symmetry_group)
            continue;
        if (m_symmetry_pre_tool_constraints.contains(uu))
            continue;
        bool has_new_entity_ref = false;
        for (const auto &enp : constr->get_referenced_entities_and_points()) {
            if (new_entities_set.contains(enp.entity)) {
                has_new_entity_ref = true;
                break;
            }
        }
        if (has_new_entity_ref)
            new_constraints.push_back(uu);
    }

    if (new_entities.empty()) {
        for (const auto &uu : new_constraints)
            m_symmetry_pre_tool_constraints.insert(uu);
        return;
    }

    auto transform_entity = [&](Entity &dst, const Entity &src,
                                const std::function<glm::dvec2(const glm::dvec2 &)> &transform, bool mirrored) {
        (void)src;
        if (auto *line = dynamic_cast<EntityLine2D *>(&dst)) {
            line->m_p1 = transform(line->m_p1);
            line->m_p2 = transform(line->m_p2);
        }
        else if (auto *arc = dynamic_cast<EntityArc2D *>(&dst)) {
            const auto from = transform(arc->m_from);
            const auto to = transform(arc->m_to);
            arc->m_center = transform(arc->m_center);
            if (mirrored) {
                arc->m_from = to;
                arc->m_to = from;
            }
            else {
                arc->m_from = from;
                arc->m_to = to;
            }
        }
        else if (auto *circle = dynamic_cast<EntityCircle2D *>(&dst)) {
            circle->m_center = transform(circle->m_center);
        }
        else if (auto *bez = dynamic_cast<EntityBezier2D *>(&dst)) {
            bez->m_p1 = transform(bez->m_p1);
            bez->m_p2 = transform(bez->m_p2);
            bez->m_c1 = transform(bez->m_c1);
            bez->m_c2 = transform(bez->m_c2);
        }
        else if (auto *point = dynamic_cast<EntityPoint2D *>(&dst)) {
            point->m_p = transform(point->m_p);
        }
    };

    std::vector<std::unique_ptr<Entity>> clones;
    struct SymmetryCloneMap {
        std::map<UUID, UUID> entity_map;
        bool mirrored = false;
    };
    std::vector<SymmetryCloneMap> clone_maps;
    if (m_symmetry_mode == SymmetryMode::RADIAL) {
        const auto center = m_symmetry_axes.front().first;
        const auto count = std::max(3u, m_symmetry_radial_axes);
        const auto phase = radial_rotation_deg_to_rad(m_symmetry_radial_rotation_deg);
        for (unsigned int i = 1; i < count; i++) {
            std::map<UUID, UUID> id_map;
            for (const auto &uu : new_entities) {
                const auto &src = doc.get_entity(uu);
                const auto angle = phase + 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(count);
                auto clone = src.clone();
                transform_entity(*clone, src,
                                 [&](const glm::dvec2 &p) { return rotate_point_2d(p, center, angle); }, false);
                clone->m_uuid = UUID::random();
                clone->m_group = m_symmetry_group;
                clone->m_kind = ItemKind::USER;
                clone->m_generated_from = UUID();
                clone->m_selection_invisible = false;
                clone->m_move_instead.clear();
                id_map.emplace(uu, clone->m_uuid);
                clones.push_back(std::move(clone));
            }
            clone_maps.push_back({std::move(id_map), false});
        }
    }
    else {
        for (const auto &[axis_point, axis_dir] : m_symmetry_axes) {
            std::map<UUID, UUID> id_map;
            for (const auto &uu : new_entities) {
                const auto &src = doc.get_entity(uu);
                auto clone = src.clone();
                transform_entity(*clone, src,
                                 [&](const glm::dvec2 &p) { return reflect_point_2d(p, axis_point, axis_dir); }, true);
                clone->m_uuid = UUID::random();
                clone->m_group = m_symmetry_group;
                clone->m_kind = ItemKind::USER;
                clone->m_generated_from = UUID();
                clone->m_selection_invisible = false;
                clone->m_move_instead.clear();
                id_map.emplace(uu, clone->m_uuid);
                clones.push_back(std::move(clone));
            }
            clone_maps.push_back({std::move(id_map), true});
        }
    }

    if (clones.empty()) {
        for (const auto &uu : new_entities)
            m_symmetry_pre_tool_entities.insert(uu);
        for (const auto &uu : new_constraints)
            m_symmetry_pre_tool_constraints.insert(uu);
        return;
    }

    for (auto &clone : clones) {
        m_symmetry_pre_tool_entities.insert(clone->m_uuid);
        doc.m_entities.emplace(clone->m_uuid, std::move(clone));
    }
    for (const auto &uu : new_entities)
        m_symmetry_pre_tool_entities.insert(uu);
    for (const auto &uu : new_constraints)
        m_symmetry_pre_tool_constraints.insert(uu);

    for (const auto &clone_map : clone_maps) {
        for (const auto &constraint_uu : new_constraints) {
            const auto *src_constr = doc.get_constraint_ptr(constraint_uu);
            if (!src_constr)
                continue;

            auto constr_clone = src_constr->clone();
            bool has_mapped_ref = false;
            bool unsupported_ref = false;
            for (const auto &enp : src_constr->get_referenced_entities_and_points()) {
                if (auto it = clone_map.entity_map.find(enp.entity); it != clone_map.entity_map.end()) {
                    auto mapped_enp = EntityAndPoint{it->second, enp.point};
                    if (clone_map.mirrored) {
                        if (const auto *arc = doc.get_entity_ptr<EntityArc2D>(enp.entity)) {
                            (void)arc;
                            if (enp.point == 1)
                                mapped_enp.point = 2;
                            else if (enp.point == 2)
                                mapped_enp.point = 1;
                        }
                    }
                    if (!constr_clone->replace_point(enp, mapped_enp)) {
                        unsupported_ref = true;
                        break;
                    }
                    has_mapped_ref = true;
                    continue;
                }
                const auto *ref_en = doc.get_entity_ptr(enp.entity);
                if (!ref_en || !ref_en->of_type(Entity::Type::WORKPLANE)) {
                    unsupported_ref = true;
                    break;
                }
            }
            if (unsupported_ref || !has_mapped_ref)
                continue;

            constr_clone->m_uuid = UUID::random();
            constr_clone->m_group = m_symmetry_group;
            m_symmetry_pre_tool_constraints.insert(constr_clone->m_uuid);
            doc.m_constraints.emplace(constr_clone->m_uuid, std::move(constr_clone));
        }
    }

    m_core.set_needs_save();
    m_core.rebuild("symmetry clone");
    canvas_update_keep_selection();
    m_workspace_browser->update_documents(get_current_document_views());
    update_action_sensitivity();
#endif
}

std::vector<std::pair<glm::dvec3, glm::dvec3>> Editor::get_symmetry_overlay_lines_world()
{
    std::vector<std::pair<glm::dvec3, glm::dvec3>> lines;
#ifdef DUNE_SKETCHER_ONLY
    if (!m_symmetry_enabled || !m_core.has_documents())
        return lines;
    if (m_core.get_current_group() != m_symmetry_group)
        return lines;
    if (m_symmetry_axes.empty())
        return lines;

    const auto &doc = m_core.get_current_document();
    const auto *wrkpl = doc.get_entity_ptr<EntityWorkplane>(m_symmetry_workplane);
    if (!wrkpl)
        return lines;

    const auto len = std::max(1000.0, std::max(wrkpl->m_size.x, wrkpl->m_size.y) * 20.0);
    for (const auto &[axis_point, axis_dir] : m_symmetry_axes) {
        const auto dir = normalize_dir(axis_dir);
        const auto p1 = wrkpl->transform(axis_point);
        const auto p2 = wrkpl->transform(axis_point + dir * len);
        // Radial mode shows N rays (segments), not full infinite lines that look like 2N rays.
        if (m_symmetry_mode != SymmetryMode::RADIAL) {
            const auto p1_full = wrkpl->transform(axis_point - dir * len);
            lines.emplace_back(p1_full, p2);
            continue;
        }
        lines.emplace_back(p1, p2);
    }
#endif
    return lines;
}

void Editor::init_settings_popover()
{
    auto popover = Gtk::make_managed<Gtk::Popover>();
    popover->add_css_class("sketch-settings-popover");
    popover->add_css_class("menu");
    popover->set_has_arrow(true);
    popover->set_autohide(true);
    popover->set_size_request(sketch_popover_total_width, -1);

    auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    root->set_margin_start(12);
    root->set_margin_end(12);
    root->set_margin_top(12);
    root->set_margin_bottom(12);
    root->set_size_request(sketch_popover_content_width, -1);
    popover->set_child(*root);

    auto theme_title = Gtk::make_managed<Gtk::Label>("Theme");
    theme_title->set_halign(Gtk::Align::CENTER);
    theme_title->add_css_class("dim-label");
    root->append(*theme_title);

    auto theme_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    theme_row->add_css_class("linked");
    root->append(*theme_row);

    m_theme_prev_button = Gtk::make_managed<Gtk::Button>();
    m_theme_prev_button->set_icon_name("go-previous-symbolic");
    m_theme_prev_button->set_tooltip_text("Previous theme");
    m_theme_prev_button->set_has_frame(true);
    theme_row->append(*m_theme_prev_button);

    auto theme_value_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    theme_value_box->add_css_class("sketch-theme-value");
    theme_value_box->set_hexpand(true);
    m_theme_value_label = Gtk::make_managed<Gtk::Label>();
    m_theme_value_label->set_halign(Gtk::Align::CENTER);
    m_theme_value_label->set_hexpand(true);
    theme_value_box->append(*m_theme_value_label);
    theme_row->append(*theme_value_box);

    m_theme_next_button = Gtk::make_managed<Gtk::Button>();
    m_theme_next_button->set_icon_name("go-next-symbolic");
    m_theme_next_button->set_tooltip_text("Next theme");
    m_theme_next_button->set_has_frame(true);
    theme_row->append(*m_theme_next_button);

    const auto set_theme_variant = [this, popover](CanvasPreferences::ThemeVariant variant) {
        const bool keep_open = popover && popover->get_visible();
        const auto normalized = normalize_sketch_theme_variant(variant);
        m_preferences.canvas.theme_variant = normalized;
        m_preferences.canvas.dark_theme = (normalized == CanvasPreferences::ThemeVariant::DARK
                                           || normalized == CanvasPreferences::ThemeVariant::DARK_BLUE
                                           || normalized == CanvasPreferences::ThemeVariant::MIX);
        m_preferences.signal_changed().emit();
        if (keep_open) {
            Glib::signal_idle().connect_once([popover] {
                if (popover && popover->get_root())
                    popover->popup();
            });
        }
    };
    m_theme_prev_button->signal_clicked().connect(
            [this, set_theme_variant] { set_theme_variant(cycle_sketch_theme_variant(m_preferences.canvas.theme_variant, -1)); });
    m_theme_next_button->signal_clicked().connect(
            [this, set_theme_variant] { set_theme_variant(cycle_sketch_theme_variant(m_preferences.canvas.theme_variant, +1)); });

    m_theme_accent_section = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    root->append(*m_theme_accent_section);

    auto accent_title = Gtk::make_managed<Gtk::Label>("Accent");
    accent_title->set_halign(Gtk::Align::CENTER);
    accent_title->add_css_class("dim-label");
    m_theme_accent_section->append(*accent_title);

    m_theme_accent_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_theme_accent_row->set_halign(Gtk::Align::CENTER);
    m_theme_accent_section->append(*m_theme_accent_row);

    const auto set_accent_variant = [this](CanvasPreferences::AccentVariant accent) {
        m_preferences.canvas.accent_variant = accent;
        m_preferences.signal_changed().emit();
    };
    struct AccentButtonDef {
        CanvasPreferences::AccentVariant accent;
        const char *css_class;
        const char *tooltip;
    };
    constexpr std::array<AccentButtonDef, 5> accent_buttons = {{
            {CanvasPreferences::AccentVariant::BLUE, "sketch-accent-chip-blue", "Blue accent"},
            {CanvasPreferences::AccentVariant::ORANGE, "sketch-accent-chip-orange", "Orange accent"},
            {CanvasPreferences::AccentVariant::TEAL, "sketch-accent-chip-teal", "Teal accent"},
            {CanvasPreferences::AccentVariant::PINK, "sketch-accent-chip-pink", "Pink accent"},
            {CanvasPreferences::AccentVariant::LIME, "sketch-accent-chip-lime", "Lime accent"},
    }};
    for (const auto &def : accent_buttons) {
        auto button = Gtk::make_managed<Gtk::Button>();
        button->set_has_frame(false);
        button->set_focusable(false);
        button->set_tooltip_text(def.tooltip);
        button->add_css_class("sketch-accent-chip");
        button->add_css_class(def.css_class);
        button->signal_clicked().connect([set_accent_variant, accent = def.accent] { set_accent_variant(accent); });
        m_theme_accent_row->append(*button);
        m_theme_accent_buttons.emplace(def.accent, button);
    }

    auto width_title = Gtk::make_managed<Gtk::Label>("Line Thickness");
    width_title->set_halign(Gtk::Align::CENTER);
    width_title->add_css_class("dim-label");
    root->append(*width_title);

    m_line_width_scale = Gtk::make_managed<Gtk::Scale>(Gtk::Orientation::HORIZONTAL);
    m_line_width_scale->set_range(1.0, 5.0);
    m_line_width_scale->set_increments(0.1, 0.5);
    m_line_width_scale->set_draw_value(false);
    root->append(*m_line_width_scale);

    m_line_width_value_label = Gtk::make_managed<Gtk::Label>();
    m_line_width_value_label->set_halign(Gtk::Align::CENTER);
    m_line_width_value_label->add_css_class("dim-label");
    root->append(*m_line_width_value_label);

    m_line_width_scale->signal_value_changed().connect([this] {
        if (!m_line_width_scale)
            return;
        if (!m_line_width_value_label)
            return;
        const auto line_width = m_line_width_scale->get_value();
        m_line_width_value_label->set_text(format_line_width_multiplier(line_width));
        if (m_updating_settings_popover)
            return;
        m_preferences.canvas.appearance.line_width = line_width;
        m_preferences.signal_changed().emit();
    });

    auto right_click_popover_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto right_click_popover_label = Gtk::make_managed<Gtk::Label>("Options by right click");
    right_click_popover_label->set_hexpand(true);
    right_click_popover_label->set_xalign(0);
    m_right_click_popovers_switch = Gtk::make_managed<Gtk::Switch>();
    right_click_popover_row->append(*right_click_popover_label);
    right_click_popover_row->append(*m_right_click_popovers_switch);
    root->append(*right_click_popover_row);

    m_right_click_popovers_switch->property_active().signal_changed().connect([this] {
        if (m_updating_settings_popover || !m_right_click_popovers_switch)
            return;
        m_right_click_popovers_only = m_right_click_popovers_switch->get_active();
    });

    auto pref_button = Gtk::make_managed<Gtk::Button>("Preferences");
    pref_button->set_hexpand(true);
    root->append(*pref_button);

    auto actions_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    actions_row->add_css_class("linked");
    root->append(*actions_row);

    auto help_button = Gtk::make_managed<Gtk::Button>("Help");
    auto about_button = Gtk::make_managed<Gtk::Button>("About");
    help_button->set_hexpand(true);
    about_button->set_hexpand(true);
    actions_row->append(*help_button);
    actions_row->append(*about_button);

    pref_button->signal_clicked().connect([this, popover] {
        popover->popdown();
        if (auto app = m_win.get_application())
            app->activate_action("preferences");
    });

    help_button->signal_clicked().connect([this, popover] {
        popover->popdown();
        if (auto app = m_win.get_application())
            app->activate_action("help");
    });
    about_button->signal_clicked().connect([this, popover] {
        popover->popdown();
        if (auto app = m_win.get_application())
            app->activate_action("about");
    });

    popover->signal_show().connect(sigc::mem_fun(*this, &Editor::sync_settings_popover_from_preferences));
    m_win.get_hamburger_menu_button().set_popover(*popover);
    m_settings_popover = popover;
    sync_settings_popover_from_preferences();
}

void Editor::sync_settings_popover_from_preferences()
{
    if (!m_theme_prev_button || !m_theme_next_button || !m_theme_value_label || !m_theme_accent_section
        || !m_theme_accent_row || !m_line_width_scale
        || !m_line_width_value_label || !m_right_click_popovers_switch)
        return;
    m_updating_settings_popover = true;
    const auto variant = normalize_sketch_theme_variant(m_preferences.canvas.theme_variant);
    m_theme_value_label->set_text(sketch_theme_variant_name(variant));
    for (const auto &[accent, button] : m_theme_accent_buttons) {
        if (!button)
            continue;
        if (accent == m_preferences.canvas.accent_variant)
            button->add_css_class("sketch-accent-chip-active");
        else
            button->remove_css_class("sketch-accent-chip-active");
    }
    m_line_width_scale->set_value(m_preferences.canvas.appearance.line_width);
    m_line_width_value_label->set_text(format_line_width_multiplier(m_preferences.canvas.appearance.line_width));
    m_right_click_popovers_switch->set_active(m_right_click_popovers_only);
    m_updating_settings_popover = false;
}

void Editor::update_view_hints()
{
    std::vector<std::string> hints;
    if (get_canvas().get_projection() == Canvas::Projection::PERSP)
        hints.push_back("persp.");
    {
        const auto cl = get_canvas().get_clipping_planes();
        if (cl.x.enabled || cl.y.enabled || cl.z.enabled) {
            std::string s = "clipped:";
            if (cl.x.enabled)
                s += "x";
            if (cl.y.enabled)
                s += "y";
            if (cl.z.enabled)
                s += "z";
            hints.push_back(s);
        }
    }
    if (m_selection_filter_window->is_active())
        hints.push_back("selection filtered");
    if (m_core.has_documents()) {
        if (get_current_workspace_view().m_show_construction_entities_from_previous_groups)
            hints.push_back("prev. construction entities");
        if (get_current_workspace_view().m_hide_irrelevant_workplanes)
            hints.push_back("no irrelevant workplanes");
        auto &wv = m_workspace_views.at(m_current_workspace_view);
        if (wv.m_curvature_comb_scale > 0)
            hints.push_back("curv. combs");
    }
    m_win.set_view_hints_label(hints);
}

void Editor::on_open_document(const ActionConnection &conn)
{
#ifdef DUNE_SKETCHER_ONLY
    if (m_sidebar_popover)
        m_sidebar_popover->popdown();
    auto dialog = Gtk::FileDialog::create();

    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto filter_any = Gtk::FileFilter::create();
    filter_any->set_name("Sketch files");
    filter_any->add_pattern("*.dxf");
    filter_any->add_pattern("*.DXF");
    filter_any->add_pattern("*.svg");
    filter_any->add_pattern("*.SVG");
    filters->append(filter_any);
    dialog->set_filters(filters);

    dialog->open_multiple(m_win, [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto files = dialog->open_multiple_finish(result);
            for (const auto &file : files) {
                if (!file)
                    continue;
                open_file(path_from_string(file->get_path()));
            }
            m_workspace_browser->update_documents(get_current_document_views());
        }
        catch (const Gtk::DialogError &err) {
            std::cout << "No file selected. " << err.what() << std::endl;
        }
        catch (const Glib::Error &err) {
            std::cout << "Unexpected exception. " << err.what() << std::endl;
        }
    });
    return;
#else
    auto dialog = Gtk::FileDialog::create();
    if (m_core.has_documents()) {
        if (m_core.get_current_idocument_info().has_path()) {
            dialog->set_initial_file(
                    Gio::File::create_for_path(path_to_string(m_core.get_current_idocument_info().get_path())));
        }
    }

    // Add filters, so that only certain file types can be selected:
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();

    auto filter_any = Gtk::FileFilter::create();
#ifdef DUNE_SKETCHER_ONLY
    filter_any->set_name("Sketch files");
    filter_any->add_pattern("*.dxf");
    filter_any->add_pattern("*.DXF");
    filter_any->add_pattern("*.svg");
    filter_any->add_pattern("*.SVG");
#else
    filter_any->set_name("Dune 3D documents");
    filter_any->add_pattern("*.d3ddoc");
#endif
    filters->append(filter_any);

    dialog->set_filters(filters);

    // Show the dialog and wait for a user response:
    dialog->open(m_win, [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->open_finish(result);
            open_file(path_from_string(file->get_path()));
            // Notice that this is a std::string, not a Glib::ustring.
            auto filename = file->get_path();
            std::cout << "File selected: " << filename << std::endl;
        }
        catch (const Gtk::DialogError &err) {
            // Can be thrown by dialog->open_finish(result).
            std::cout << "No file selected. " << err.what() << std::endl;
        }
        catch (const Glib::Error &err) {
            std::cout << "Unexpected exception. " << err.what() << std::endl;
        }
    });
#endif
}

void Editor::on_open_folder()
{
#ifdef DUNE_SKETCHER_ONLY
    if (m_sidebar_popover)
        m_sidebar_popover->popdown();
    auto dialog = Gtk::FileDialog::create();
    dialog->select_folder(m_win, [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto folder = dialog->select_folder_finish(result);
            open_folder(path_from_string(folder->get_path()));
        }
        catch (const Gtk::DialogError &err) {
            std::cout << "No folder selected. " << err.what() << std::endl;
        }
        catch (const Glib::Error &err) {
            std::cout << "Unexpected exception. " << err.what() << std::endl;
        }
    });
#endif
}

void Editor::on_trace_image_button()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_core.has_documents()) {
        tool_bar_flash("Open or create a sketch first");
        return;
    }

    if (m_core.tool_is_active() && !force_end_tool())
        return;

    auto dialog = Gtk::FileDialog::create();
    {
        auto dir = m_core.get_current_document_directory();
        if (!dir.empty())
            dialog->set_initial_folder(Gio::File::create_for_path(path_to_string(dir)));
    }

    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto filter_any = Gtk::FileFilter::create();
    filter_any->add_pixbuf_formats();
    filter_any->set_name("Pictures");
    filters->append(filter_any);
    dialog->set_filters(filters);

    dialog->open(m_win, [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->open_finish(result);
            if (!file)
                return;

            if (!m_image_trace_dialog) {
                m_image_trace_dialog = std::make_unique<ImageTraceDialog>();
                m_image_trace_dialog->set_transient_for(m_win);
                sync_sketch_theme_classes(m_win, *m_image_trace_dialog);
                m_image_trace_dialog->signal_apply().connect(sigc::mem_fun(*this, &Editor::apply_traced_svg));
            }

            std::string error_message;
            if (!m_image_trace_dialog->load_image(path_from_string(file->get_path()), error_message)) {
                tool_bar_flash("Couldn't load image for tracing");
                if (!error_message.empty())
                    Logger::get().log_warning(error_message, Logger::Domain::EDITOR);
                return;
            }
            m_image_trace_dialog->present();
        }
        catch (const Gtk::DialogError &) {
        }
        catch (const Glib::Error &err) {
            Logger::get().log_warning(err.what(), Logger::Domain::EDITOR);
        }
    });
#endif
}

void Editor::open_trace_image_dialog(const std::shared_ptr<const PictureData> &picture)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!picture)
        return;
    if (!m_core.has_documents()) {
        tool_bar_flash("Open or create a sketch first");
        return;
    }
    if (!m_image_trace_dialog) {
        m_image_trace_dialog = std::make_unique<ImageTraceDialog>();
        m_image_trace_dialog->set_transient_for(m_win);
        sync_sketch_theme_classes(m_win, *m_image_trace_dialog);
        m_image_trace_dialog->signal_apply().connect(sigc::mem_fun(*this, &Editor::apply_traced_svg));
    }

    std::string error_message;
    if (!m_image_trace_dialog->load_picture(picture, error_message)) {
        tool_bar_flash("Couldn't load image for tracing");
        if (!error_message.empty())
            Logger::get().log_warning(error_message, Logger::Domain::EDITOR);
        return;
    }
    m_image_trace_dialog->present();
#else
    (void)picture;
#endif
}

void Editor::apply_traced_svg(const std::string &svg)
{
#ifdef DUNE_SKETCHER_ONLY
    if (svg.empty())
        return;
    if (!m_core.has_documents()) {
        tool_bar_flash("Open a sketch before importing trace");
        return;
    }

    if (m_core.tool_is_active() && !force_end_tool())
        return;

    const auto selection_before_trace_import = get_canvas().get_selection();

    std::filesystem::path temp_svg;
    try {
        temp_svg = std::filesystem::temp_directory_path() / ("dxfsketcher-trace-" + std::string(UUID::random()) + ".svg");
        std::ofstream ofs(path_to_string(temp_svg), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!ofs.good()) {
            tool_bar_flash("Couldn't create temporary trace file");
            return;
        }
        ofs.write(svg.data(), static_cast<std::streamsize>(svg.size()));
        ofs.close();
    }
    catch (const std::exception &err) {
        Logger::get().log_warning(err.what(), Logger::Domain::EDITOR);
        tool_bar_flash("Couldn't prepare trace import");
        return;
    }

    tool_begin(ToolID::IMPORT_PICTURE_SILENT);
    if (!m_core.tool_is_active()) {
        std::error_code ec;
        std::filesystem::remove(temp_svg, ec);
        tool_bar_flash("Couldn't import traced image in current context");
        return;
    }
    tool_update_data(std::make_unique<ToolDataPath>(temp_svg));
    if (!m_core.tool_is_active()) {
        const auto selection_after_trace_import = get_canvas().get_selection();
        std::size_t imported_entities = 0;
        for (const auto &sr : selection_after_trace_import) {
            if (sr.type == SelectableRef::Type::ENTITY)
                imported_entities++;
        }
        if (imported_entities >= 2 && selection_after_trace_import != selection_before_trace_import)
            trigger_action(ToolID::CREATE_CLUSTER);
    }

    std::error_code ec;
    std::filesystem::remove(temp_svg, ec);

    if (m_image_trace_dialog)
        m_image_trace_dialog->hide();
#endif
}

void Editor::open_folder(const std::filesystem::path &folder_path)
{
#ifdef DUNE_SKETCHER_ONLY
    try {
        std::vector<std::filesystem::path> dxf_files;
        for (const auto &entry : std::filesystem::directory_iterator(folder_path)) {
            if (!entry.is_regular_file())
                continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".dxf")
                dxf_files.push_back(entry.path());
        }
        std::sort(dxf_files.begin(), dxf_files.end());

        trigger_action(ActionID::NEW_DOCUMENT);
        if (m_core.tool_is_active())
            force_end_tool();
        m_group_export_paths.clear();
        m_sketcher_folder_path = folder_path;
        m_win.get_app().add_recent_folder(folder_path);

        m_workspace_browser->set_sketcher_folder_mode(path_to_string(folder_path.filename()));
        m_sketcher_opening_folder_batch = true;
        for (const auto &path : dxf_files)
            open_file(path);
        m_sketcher_opening_folder_batch = false;
        m_workspace_browser->update_documents(get_current_document_views());
    }
    catch (...) {
        m_workspace_browser->show_toast("Couldn't open folder");
    }
#else
    (void)folder_path;
#endif
}

void Editor::on_save_as(const ActionConnection &conn)
{
#ifdef DUNE_SKETCHER_ONLY
    if (m_sidebar_popover)
        m_sidebar_popover->popdown();
    auto dialog = Gtk::FileDialog::create();
    if (auto group_path = get_group_export_path(m_core.get_current_group())) {
        dialog->set_initial_file(Gio::File::create_for_path(path_to_string(*group_path)));
    }
    else if (m_core.get_current_idocument_info().has_path()) {
        dialog->set_initial_file(
                Gio::File::create_for_path(path_to_string(m_core.get_current_idocument_info().get_path())));
    }

    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto filter_dxf = Gtk::FileFilter::create();
    filter_dxf->set_name("DXF");
    filter_dxf->add_pattern("*.dxf");
    filter_dxf->add_pattern("*.DXF");
    filters->append(filter_dxf);
    auto filter_svg = Gtk::FileFilter::create();
    filter_svg->set_name("SVG");
    filter_svg->add_pattern("*.svg");
    filter_svg->add_pattern("*.SVG");
    filters->append(filter_svg);
    dialog->set_filters(filters);

    dialog->save(m_win, [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->save_finish(result);
            auto filename = path_from_string(file->get_path());
            auto ext = filename.extension().string();
            bool save_svg = (ext == ".svg" || ext == ".SVG");
            if (!save_svg && ext != ".dxf" && ext != ".DXF")
                filename = path_from_string(append_suffix_if_required(file->get_path(), ".dxf"));

            if (save_svg) {
                auto group_filter = [this](const Group &group) { return group.m_uuid == m_core.get_current_group(); };
                export_paths(filename, m_core.get_current_document(), m_core.get_current_group(), group_filter);
            }
            else {
                export_dxf(filename, m_core.get_current_document(), m_core.get_current_group());
            }
            auto &group = m_core.get_current_document().get_group(m_core.get_current_group());
            group.m_name = path_to_string(filename.filename());
            set_group_export_path(group.m_uuid, filename);
            m_core.set_current_document_path(filename);
            m_core.clear_needs_save();
            m_win.get_app().add_recent_item(filename);
            save_workspace_view(m_core.get_current_idocument_info().get_uuid());
            m_workspace_browser->update_documents(get_current_document_views());
            update_version_info();
            update_title();
            if (m_after_save_cb)
                m_after_save_cb();
            m_after_save_cb = nullptr;
        }
        catch (const Gtk::DialogError &err) {
            std::cout << "No file selected. " << err.what() << std::endl;
        }
        catch (const Glib::Error &err) {
            std::cout << "Unexpected exception. " << err.what() << std::endl;
        }
    });
#else
    auto dialog = Gtk::FileDialog::create();
    if (m_core.get_current_idocument_info().has_path()) {
        dialog->set_initial_file(
                Gio::File::create_for_path(path_to_string(m_core.get_current_idocument_info().get_path())));
    }

    // Add filters, so that only certain file types can be selected:
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();

    auto filter_any = Gtk::FileFilter::create();
    filter_any->set_name("Dune 3D documents");
    filter_any->add_pattern("*.d3ddoc");
    filters->append(filter_any);

    dialog->set_filters(filters);

    // Show the dialog and wait for a user response:
    dialog->save(m_win, [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->save_finish(result);
            // open_file_view(file);
            //  Notice that this is a std::string, not a Glib::ustring.
            auto filename = path_from_string(append_suffix_if_required(file->get_path(), ".d3ddoc"));
            // std::cout << "File selected: " << filename << std::endl;
            m_win.get_app().add_recent_item(filename);
            m_core.save_as(filename);
            save_workspace_view(m_core.get_current_idocument_info().get_uuid());
            m_workspace_browser->update_documents(get_current_document_views());
            update_version_info();
            update_title();
            if (m_after_save_cb)
                m_after_save_cb();
            m_after_save_cb = nullptr;
        }
        catch (const Gtk::DialogError &err) {
            // Can be thrown by dialog->open_finish(result).
            std::cout << "No file selected. " << err.what() << std::endl;
        }
        catch (const Glib::Error &err) {
            std::cout << "Unexpected exception. " << err.what() << std::endl;
        }
    });
#endif
}


void Editor::init_tool_popover()
{
    m_tool_popover = Gtk::make_managed<ToolPopover>();
    m_tool_popover->set_parent(get_canvas());
    m_tool_popover->signal_action_activated().connect([this](ActionToolID action_id) { trigger_action(action_id); });


    connect_action({ActionID::POPOVER}, [this](const auto &a) {
        Gdk::Rectangle rect;
        rect.set_x(m_last_x);
        rect.set_y(m_last_y);

        m_tool_popover->set_pointing_to(rect);

        this->update_action_sensitivity();
        std::map<ActionToolID, bool> can_begin;
        auto sel = get_canvas().get_selection();
        for (const auto &[id, it] : action_catalog) {
            if (std::holds_alternative<ToolID>(id)) {
                bool r = m_core.tool_can_begin(std::get<ToolID>(id), sel).get_can_begin();
                can_begin[id] = r;
            }
            else {
                can_begin[id] = this->get_action_sensitive(std::get<ActionID>(id));
            }
        }
        m_tool_popover->set_can_begin(can_begin);

        m_tool_popover->popup();
    });
}

Canvas &Editor::get_canvas()
{
    return m_win.get_canvas();
}

const Canvas &Editor::get_canvas() const
{
    return m_win.get_canvas();
}


void Editor::show_save_dialog(const std::string &doc_name, std::function<void()> save_cb,
                              std::function<void()> no_save_cb)
{
#ifdef DUNE_SKETCHER_ONLY
    if (m_sidebar_popover)
        m_sidebar_popover->popdown();
#endif
    auto dialog = Gtk::AlertDialog::create("Save changes to document \"" + doc_name + "\" before closing?");
    dialog->set_detail(
            "If you don't save, all your changes will be permanently "
            "lost.");
    dialog->set_buttons({"Cancel", "Close without saving", "Save"});
    dialog->set_cancel_button(0);
    dialog->set_default_button(0);
    dialog->choose(m_win, [dialog, save_cb, no_save_cb](Glib::RefPtr<Gio::AsyncResult> &result) {
        auto btn = dialog->choose_finish(result);
        if (btn == 1) {
            if (no_save_cb)
                no_save_cb();
        }
        else if (btn == 2)
            if (save_cb)
                save_cb();
    });
}

void Editor::close_document(const UUID &doc_uu, std::function<void()> save_cb, std::function<void()> no_save_cb)
{
    const auto &doci = m_core.get_idocument_info(doc_uu);
    if (doci.get_needs_save()) {
        show_save_dialog(
                doci.get_basename(),
                [this, doc_uu, save_cb, no_save_cb] {
                    m_after_save_cb = [this, doc_uu, save_cb] {
                        do_close_document(doc_uu);
                        if (save_cb)
                            save_cb();
                    };
                    trigger_action(ActionID::SAVE);
                },
                [this, doc_uu, no_save_cb] {
                    do_close_document(doc_uu);
                    if (no_save_cb)
                        no_save_cb();
                });
    }
    else {
        do_close_document(doc_uu);
    }
}

void Editor::do_close_document(const UUID &doc_uu)
{
    if (m_core.get_current_idocument_info().get_uuid() == doc_uu)
        force_end_tool();
    m_core.close_document(doc_uu);
    auto_close_workspace_views();
}

void Editor::update_group_editor()
{
    if (!m_group_editor_box)
        return;
    if (m_group_editor) {
        if (m_delayed_commit_connection.connected()) {
            commit_from_editor();
        }
        m_group_editor_box->remove(*m_group_editor);
        m_group_editor = nullptr;
    }
    if (!m_core.has_documents())
        return;
    m_group_editor = GroupEditor::create(m_core, m_core.get_current_group());
    m_group_editor->signal_changed().connect(sigc::mem_fun(*this, &Editor::handle_commit_from_editor));
    m_group_editor->signal_trigger_action().connect([this](auto act) { trigger_action(act); });
    m_group_editor_box->append(*m_group_editor);
}

void Editor::handle_commit_from_editor(CommitMode mode)
{
    if (mode == CommitMode::DELAYED) {
        m_core.get_current_document().update_pending(m_core.get_current_group());
        m_delayed_commit_connection.disconnect(); // stop old timer
        m_delayed_commit_connection = Glib::signal_timeout().connect(
                [this] {
                    commit_from_editor();
                    return false;
                },
                1000);
        if (m_group_commit_pending_revealer)
            m_group_commit_pending_revealer->set_reveal_child(true);
        if (m_selection_commit_pending_revealer)
            m_selection_commit_pending_revealer->set_reveal_child(true);
    }
    else if (mode == CommitMode::IMMEDIATE
             || (mode == CommitMode::EXECUTE_DELAYED && m_delayed_commit_connection.connected())) {
        commit_from_editor();
    }
    m_core.set_needs_save();
    canvas_update_keep_selection();
}

void Editor::commit_from_editor()
{
    m_delayed_commit_connection.disconnect();
    if (m_group_commit_pending_revealer)
        m_group_commit_pending_revealer->set_reveal_child(false);
    if (m_selection_commit_pending_revealer)
        m_selection_commit_pending_revealer->set_reveal_child(false);
    m_core.rebuild("group/selection edited");
}

void Editor::update_workplane_label()
{
    if (!m_core.has_documents()) {
        m_win.set_workplane_label("No documents");
        return;
    }
    auto wrkpl_uu = m_core.get_current_workplane();
    if (!wrkpl_uu) {
        m_win.set_workplane_label("No Workplane");
    }
    else {
        auto &wrkpl = m_core.get_current_document().get_entity<EntityWorkplane>(wrkpl_uu);
        auto &wrkpl_group = m_core.get_current_document().get_group<Group>(wrkpl.m_group);
        std::string s = "Workplane ";
        if (wrkpl.m_name.size())
            s += wrkpl.m_name + " ";
        s += "in group " + wrkpl_group.m_name;
        m_win.set_workplane_label(s);
    }
}

KeyMatchResult Editor::keys_match(const KeySequence &keys) const
{
    return key_sequence_match(m_keys_current, keys);
}

void Editor::apply_preferences()
{
    CanvasUpdater canvas_updater{*this};

    for (auto &[id, conn] : m_action_connections) {
        auto &act = action_catalog.at(id);
        if (!(act.flags & ActionCatalogItem::FLAGS_NO_PREFERENCES) && m_preferences.key_sequences.keys.count(id)) {
            conn.key_sequences = m_preferences.key_sequences.keys.at(id);
        }
    }
    m_in_tool_key_sequeces_preferences = m_preferences.in_tool_key_sequences;
    m_in_tool_key_sequeces_preferences.keys.erase(InToolActionID::LMB);
    m_in_tool_key_sequeces_preferences.keys.erase(InToolActionID::RMB);

    {
        const auto mod0 = static_cast<Gdk::ModifierType>(0);

        m_in_tool_key_sequeces_preferences.keys[InToolActionID::CANCEL] = {{{GDK_KEY_Escape, mod0}}};
        m_in_tool_key_sequeces_preferences.keys[InToolActionID::COMMIT] = {{{GDK_KEY_Return, mod0}},
                                                                           {{GDK_KEY_KP_Enter, mod0}}};
    }

    for (const auto &[id, it] : m_action_connections) {
        std::string tip = action_catalog.at(id).name.full;
        if (it.key_sequences.size()) {
            m_tool_popover->set_key_sequences(id, it.key_sequences);
            tip += " (" + key_sequences_to_string(it.key_sequences) + ")";
        }
        if (m_action_bar_buttons.contains(id)) {
            auto &button = *m_action_bar_buttons.at(id);
            button.set_tooltip_text(tip);
        }
    }

    auto dark = Gtk::Settings::get_default()->property_gtk_application_prefer_dark_theme().get_value();
    bool appearance_dark = dark;
    bool dark_blue = false;
    bool heaven = false;
    bool light = false;
#ifdef DUNE_SKETCHER_ONLY
    // In sketcher mode, Theme Variant drives both GTK and canvas theme.
    switch (m_preferences.canvas.theme_variant) {
    case CanvasPreferences::ThemeVariant::AUTO:
        appearance_dark = dark;
        break;
    case CanvasPreferences::ThemeVariant::MIX:
        dark = true;
        appearance_dark = false;
        break;
    case CanvasPreferences::ThemeVariant::HEAVEN:
        dark = false;
        appearance_dark = false;
        heaven = true;
        break;
    case CanvasPreferences::ThemeVariant::DARK_BLUE:
        dark = true;
        appearance_dark = true;
        dark_blue = true;
        break;
    case CanvasPreferences::ThemeVariant::DARK:
        dark = true;
        appearance_dark = true;
        break;
    case CanvasPreferences::ThemeVariant::LIGHT:
        dark = false;
        appearance_dark = false;
        light = true;
        break;
    }
    if (Gtk::Settings::get_default()->property_gtk_application_prefer_dark_theme().get_value() != dark)
        Gtk::Settings::get_default()->property_gtk_application_prefer_dark_theme().set_value(dark);
#else
    if (dark != m_preferences.canvas.dark_theme)
        Gtk::Settings::get_default()->property_gtk_application_prefer_dark_theme().set_value(
                m_preferences.canvas.dark_theme);
    dark = Gtk::Settings::get_default()->property_gtk_application_prefer_dark_theme().get_value();
    switch (m_preferences.canvas.theme_variant) {
    case CanvasPreferences::ThemeVariant::AUTO:
        appearance_dark = dark;
        break;
    case CanvasPreferences::ThemeVariant::MIX:
        dark = true;
        appearance_dark = false;
        break;
    case CanvasPreferences::ThemeVariant::HEAVEN:
        dark = false;
        appearance_dark = false;
        break;
    case CanvasPreferences::ThemeVariant::DARK_BLUE:
        dark = true;
        appearance_dark = true;
        break;
    case CanvasPreferences::ThemeVariant::DARK:
        dark = true;
        appearance_dark = true;
        break;
    case CanvasPreferences::ThemeVariant::LIGHT:
        dark = false;
        appearance_dark = false;
        break;
    }
#endif
#ifdef DUNE_SKETCHER_ONLY
    if (light)
        m_win.add_css_class("sketch-light");
    else
        m_win.remove_css_class("sketch-light");
    if (dark_blue)
        m_win.add_css_class("sketch-dark-blue");
    else
        m_win.remove_css_class("sketch-dark-blue");
    if (heaven)
        m_win.add_css_class("sketch-heaven");
    else
        m_win.remove_css_class("sketch-heaven");
    m_win.remove_css_class("sketch-mix");
    for (const auto accent : kSketchAccentOrder)
        m_win.remove_css_class(sketch_accent_css_class(accent));
    m_win.add_css_class(sketch_accent_css_class(m_preferences.canvas.accent_variant));

    const auto sync_aux_window_theme = [this](Gtk::Window *window) {
        if (window)
            sync_sketch_theme_classes(m_win, *window);
    };
    sync_aux_window_theme(m_gears_generator_window);
    sync_aux_window_theme(m_boxes_loading_window);
    sync_aux_window_theme(m_boxes_generator_window);
    sync_aux_window_theme(m_boxes_importing_window);
    sync_aux_window_theme(m_boxes_sample_window);
    sync_aux_window_theme(m_boxes_gallery_window);
    sync_aux_window_theme(m_image_trace_dialog.get());
#endif
    if (color_themes.contains(m_preferences.canvas.theme)) {
        Appearance appearance = m_preferences.canvas.appearance;
        appearance.colors = color_themes.at(m_preferences.canvas.theme).get(appearance_dark);
        get_canvas().set_appearance(appearance);
    }
    else {
        get_canvas().set_appearance(m_preferences.canvas.appearance);
    }
    get_canvas().set_enable_animations(m_preferences.canvas.enable_animations);
    get_canvas().set_zoom_to_cursor(m_preferences.canvas.zoom_to_cursor);
    get_canvas().set_rotation_scheme(m_preferences.canvas.rotation_scheme);

    m_win.tool_bar_set_vertical(m_preferences.tool_bar.vertical_layout);
    update_action_bar_visibility();
    update_error_overlay();
    sync_settings_popover_from_preferences();

    /*
        key_sequence_dialog->clear();
        for (const auto &it : action_connections) {
            if (it.second.key_sequences.size()) {
                key_sequence_dialog->add_sequence(it.second.key_sequences, action_catalog.at(it.first).name);
                tool_popover->set_key_sequences(it.first, it.second.key_sequences);
            }
        }
        preferences_apply_to_canvas(canvas, preferences);
        for (auto it : action_buttons) {
            it->update_key_sequences();
            it->set_keep_primary_action(!preferences.action_bar.remember);
        }
        main_window->set_use_action_bar(preferences.action_bar.enable);
        m_core.set_history_max(preferences.undo_redo.max_depth);
        m_core.set_history_never_forgets(preferences.undo_redo.never_forgets);
        selection_history_manager.set_never_forgets(preferences.undo_redo.never_forgets);
        preferences_apply_appearance(preferences);
        */
}

void Editor::update_error_overlay()
{
    if (m_core.has_documents()) {
        auto &doc = m_core.get_current_document();
        auto &group = doc.get_group(m_core.get_current_group());
        get_canvas().set_show_error_overlay(m_preferences.canvas.error_overlay
                                            && group.m_solve_result != SolveResult::OKAY);
    }
    else {
        get_canvas().set_show_error_overlay(false);
    }
}

void Editor::render_document(const IDocumentInfo &doc)
{
    auto &doc_view = get_current_document_views()[doc.get_uuid()];
    if (!doc_view.m_document_is_visible && (doc.get_uuid() != m_core.get_current_idocument_info().get_uuid()))
        return;
    std::optional<SelectableRef> sr;
    if (doc.get_uuid() != m_core.get_current_idocument_info().get_uuid()) {
        sr = SelectableRef{SelectableRef::Type::DOCUMENT, doc.get_uuid()};
    }
    Renderer renderer(get_canvas(), m_core);
    renderer.m_solid_model_edge_select_mode = m_solid_model_edge_select_mode;
    renderer.m_show_entity_points = m_show_technical_markers;
    renderer.m_show_constraints = m_show_technical_markers;
    renderer.m_connect_curvature_comb = m_preferences.canvas.connect_curvature_combs;
    renderer.m_first_group = m_update_groups_after;

    if (doc.get_uuid() == m_core.get_current_idocument_info().get_uuid() && m_show_technical_markers) {
        renderer.add_constraint_icons(m_constraint_tip_pos, m_constraint_tip_vec, m_constraint_tip_icons);
        renderer.m_overlay_construction_lines = get_symmetry_overlay_lines_world();
        renderer.m_overlay_construction_lines.insert(renderer.m_overlay_construction_lines.end(),
                                                     m_selection_snap_overlay_lines_world.begin(),
                                                     m_selection_snap_overlay_lines_world.end());
    }

    try {
        renderer.render(doc.get_document(), doc.get_current_group(), doc_view,
                        m_workspace_views.at(m_current_workspace_view), doc.get_dirname(), sr);
    }
    catch (const std::exception &ex) {
        Logger::log_critical("exception rendering document " + doc.get_basename(), Logger::Domain::RENDERER, ex.what());
    }
}

void Editor::draw_cup_template_overlay()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_show_technical_markers || !m_cup_template_enabled || !m_core.has_documents())
        return;
    const auto wrkpl_uu = m_core.get_current_workplane();
    if (!wrkpl_uu)
        return;

    auto &doc = m_core.get_current_document();
    const auto *wrkpl = doc.get_entity_ptr<EntityWorkplane>(wrkpl_uu);
    if (!wrkpl)
        return;

    const auto width = std::max(1.0, m_cup_template_circumference_mm);
    const auto height = std::max(1.0, m_cup_template_height_mm);
    const auto segments = std::clamp(m_cup_template_segments, 1, 12);
    auto &canvas = get_canvas();
    canvas.save();
    canvas.set_chunk(Renderer::get_chunk_from_group(doc.get_group(m_core.get_current_group())));
    canvas.set_selection_invisible(true);
    canvas.set_no_points(true);
    canvas.set_vertex_inactive(false);
    canvas.set_vertex_hover(false);
    canvas.set_vertex_constraint(false);
    canvas.set_vertex_construction(false);
    canvas.set_line_style(ICanvas::LineStyle::DEFAULT);
    canvas.set_line_wide(false);
    canvas.set_line_layer_color_index(0);

    const auto draw_workplane_line = [&canvas, wrkpl](const glm::dvec2 &a, const glm::dvec2 &b) {
        canvas.draw_line(glm::vec3(wrkpl->transform(a)), glm::vec3(wrkpl->transform(b)));
    };

    draw_workplane_line({0, 0}, {width, 0});
    draw_workplane_line({width, 0}, {width, height});
    draw_workplane_line({width, height}, {0, height});
    draw_workplane_line({0, height}, {0, 0});

    if (segments > 1) {
        canvas.set_line_style(ICanvas::LineStyle::THIN);
        canvas.set_line_wide(true);
        canvas.set_line_layer_color_index(aci_layer_color_slot(30)); // orange
        constexpr double dash_mm = 6.0;
        constexpr double gap_mm = 4.0;
        for (int i = 1; i < segments; i++) {
            const auto x = (width * static_cast<double>(i)) / static_cast<double>(segments);
            for (double y = 0; y < height; y += dash_mm + gap_mm) {
                const auto y2 = std::min(y + dash_mm, height);
                draw_workplane_line({x, y}, {x, y2});
            }
        }
    }

    canvas.set_line_layer_color_index(0);
    canvas.set_line_wide(false);
    canvas.restore();
#endif
}

void Editor::draw_layer_glow_overlay()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_show_technical_markers || !m_layers_mode_enabled || !m_core.has_documents())
        return;

    auto &doc = m_core.get_current_document();
    auto &group = doc.get_group(m_core.get_current_group());
    auto *sketch = dynamic_cast<GroupSketch *>(&group);
    if (!sketch)
        return;

    std::map<UUID, uint8_t> color_slots;
    for (const auto &layer : sketch->m_layers)
        color_slots[layer.m_uuid] = aci_layer_color_slot(layer.m_color);
    if (color_slots.empty())
        return;

    const auto default_layer = sketch->get_default_layer_uuid();
    auto &canvas = get_canvas();
    canvas.save();
    canvas.set_chunk(Renderer::get_chunk_from_group(group));
    canvas.set_selection_invisible(true);
    canvas.set_no_points(true);
    canvas.set_line_style(ICanvas::LineStyle::DEFAULT);
    canvas.set_line_wide(true);
    canvas.set_vertex_inactive(false);
    canvas.set_vertex_hover(false);
    canvas.set_vertex_constraint(false);
    canvas.set_vertex_construction(false);

    const auto draw_line_2d = [&canvas](const EntityWorkplane &wrkpl, const glm::dvec2 &a, const glm::dvec2 &b) {
        canvas.draw_line(glm::vec3(wrkpl.transform(a)), glm::vec3(wrkpl.transform(b)));
    };

    for (const auto &[uu, en] : doc.m_entities) {
        if (!en->m_visible || en->m_kind != ItemKind::USER || en->m_group != group.m_uuid)
            continue;

        auto layer_uu = en->m_layer;
        if (!layer_uu || !color_slots.contains(layer_uu))
            layer_uu = default_layer;
        if (!layer_uu || !color_slots.contains(layer_uu))
            continue;
        canvas.set_line_layer_color_index(color_slots.at(layer_uu));

        if (const auto *line = dynamic_cast<const EntityLine2D *>(en.get())) {
            const auto *wrkpl = doc.get_entity_ptr<EntityWorkplane>(line->m_wrkpl);
            if (!wrkpl)
                continue;
            draw_line_2d(*wrkpl, line->m_p1, line->m_p2);
            continue;
        }
        if (const auto *arc = dynamic_cast<const EntityArc2D *>(en.get())) {
            const auto *wrkpl = doc.get_entity_ptr<EntityWorkplane>(arc->m_wrkpl);
            if (!wrkpl)
                continue;
            const auto radius = glm::length(arc->m_from - arc->m_center);
            if (radius < 1e-9)
                continue;
            const auto a0 = wrap_angle_0_2pi(std::atan2(arc->m_from.y - arc->m_center.y, arc->m_from.x - arc->m_center.x));
            const auto a1 = wrap_angle_0_2pi(std::atan2(arc->m_to.y - arc->m_center.y, arc->m_to.x - arc->m_center.x));
            auto dphi = wrap_angle_0_2pi(a1 - a0);
            if (dphi < 1e-3)
                dphi = 2 * M_PI;
            constexpr int segments = 64;
            glm::dvec2 prev = arc->m_center + glm::dvec2(std::cos(a0), std::sin(a0)) * radius;
            for (int i = 1; i <= segments; i++) {
                const auto phi = a0 + (dphi * static_cast<double>(i)) / static_cast<double>(segments);
                const auto cur = arc->m_center + glm::dvec2(std::cos(phi), std::sin(phi)) * radius;
                draw_line_2d(*wrkpl, prev, cur);
                prev = cur;
            }
            continue;
        }
        if (const auto *circle = dynamic_cast<const EntityCircle2D *>(en.get())) {
            const auto *wrkpl = doc.get_entity_ptr<EntityWorkplane>(circle->m_wrkpl);
            if (!wrkpl || circle->m_radius <= 1e-9)
                continue;
            constexpr int segments = 64;
            glm::dvec2 prev = circle->m_center + glm::dvec2(circle->m_radius, 0);
            for (int i = 1; i <= segments; i++) {
                const auto phi = (2 * M_PI * static_cast<double>(i)) / static_cast<double>(segments);
                const auto cur = circle->m_center + glm::dvec2(std::cos(phi), std::sin(phi)) * circle->m_radius;
                draw_line_2d(*wrkpl, prev, cur);
                prev = cur;
            }
            continue;
        }
        if (const auto *bezier = dynamic_cast<const EntityBezier2D *>(en.get())) {
            const auto *wrkpl = doc.get_entity_ptr<EntityWorkplane>(bezier->m_wrkpl);
            if (!wrkpl)
                continue;
            constexpr int segments = 48;
            auto prev = bezier->get_interpolated(0);
            for (int i = 1; i <= segments; i++) {
                const auto t = static_cast<double>(i) / static_cast<double>(segments);
                const auto cur = bezier->get_interpolated(t);
                draw_line_2d(*wrkpl, prev, cur);
                prev = cur;
            }
            continue;
        }
    }

    canvas.set_line_layer_color_index(0);
    canvas.set_line_wide(false);
    canvas.restore();
#endif
}

void Editor::draw_selection_transform_overlay(const std::set<SelectableRef> &selection)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_selection_transform_enabled)
        return;
    if (!m_show_technical_markers)
        return;
    if (!m_core.has_documents())
        return;
    if (m_core.tool_is_active())
        return;

    const auto &doc = m_core.get_current_document();
    const EntityWorkplane *wrkpl = nullptr;
    if (!m_selection_transform_drag_active) {
        const bool had_overlay = m_selection_transform_overlay_valid;
        m_selection_transform_overlay_valid = false;
        const auto entities_equal = [](const std::vector<UUID> &a, const std::vector<UUID> &b) {
            return std::set<UUID>(a.begin(), a.end()) == std::set<UUID>(b.begin(), b.end());
        };
        SelectionTransformOverlayData overlay;
        if (!collect_selection_transform_overlay_data(doc, selection, m_selection_transform_frame_angle, overlay))
            return;
        const bool same_selection = had_overlay
                && m_selection_transform_group == overlay.group
                && m_selection_transform_workplane == overlay.workplane
                && entities_equal(m_selection_transform_entities, overlay.entities);
        if (!same_selection) {
            m_selection_transform_frame_angle = 0;
            if (!collect_selection_transform_overlay_data(doc, selection, m_selection_transform_frame_angle, overlay))
                return;
        }
        wrkpl = doc.get_entity_ptr<EntityWorkplane>(overlay.workplane);
        if (!wrkpl)
            return;

        m_selection_transform_group = overlay.group;
        m_selection_transform_workplane = overlay.workplane;
        m_selection_transform_entities = overlay.entities;
        m_selection_transform_bbox_min = overlay.bbox_min;
        m_selection_transform_bbox_max = overlay.bbox_max;
        m_selection_transform_center = overlay.center;
        m_selection_transform_rotate_handle = overlay.rotate_handle;
        m_selection_transform_scale_handles = overlay.scale_handles;
        m_selection_transform_base_bbox_min = overlay.bbox_min;
        m_selection_transform_base_bbox_max = overlay.bbox_max;
        m_selection_transform_base_rotate_handle = overlay.rotate_handle;
        m_selection_transform_base_scale_handles = overlay.scale_handles;
        m_selection_transform_drag_angle = 0;
        m_selection_transform_drag_scale = 1;
        m_selection_transform_overlay_valid = true;
    }
    else {
        if (!m_selection_transform_overlay_valid)
            return;
        wrkpl = doc.get_entity_ptr<EntityWorkplane>(m_selection_transform_workplane);
        if (!wrkpl)
            return;
    }

    const auto transform_overlay_point = [this](const glm::dvec2 &p) {
        if (!m_selection_transform_drag_active)
            return p;
        const auto scaled = m_selection_transform_center + (p - m_selection_transform_center) * m_selection_transform_drag_scale;
        return rotate_point_2d(scaled, m_selection_transform_center, m_selection_transform_drag_angle);
    };

    const auto rotate_handle = m_selection_transform_drag_active ? m_selection_transform_base_rotate_handle
                                                                 : m_selection_transform_rotate_handle;
    const auto scale_handles =
            m_selection_transform_drag_active ? m_selection_transform_base_scale_handles : m_selection_transform_scale_handles;

    const auto b00_2d = transform_overlay_point(scale_handles.at(0));
    const auto b01_2d = transform_overlay_point(scale_handles.at(1));
    const auto b11_2d = transform_overlay_point(scale_handles.at(2));
    const auto b10_2d = transform_overlay_point(scale_handles.at(3));
    const auto top_center_2d = (b01_2d + b11_2d) * 0.5;
    const auto rotate_handle_2d = transform_overlay_point(rotate_handle);

    m_selection_transform_rotate_handle = rotate_handle_2d;
    for (size_t i = 0; i < scale_handles.size(); i++)
        m_selection_transform_scale_handles.at(i) = transform_overlay_point(scale_handles.at(i));

    auto &canvas = get_canvas();
    canvas.save();
    canvas.set_chunk(Renderer::get_chunk_from_group(m_core.get_current_document().get_group(m_selection_transform_group)));
    canvas.set_selection_invisible(false);
    canvas.set_vertex_inactive(false);
    canvas.set_vertex_constraint(false);
    canvas.set_vertex_construction(false);
    canvas.set_vertex_hover(true);
    canvas.set_line_style(ICanvas::LineStyle::DEFAULT);

    const auto b00 = wrkpl->transform(b00_2d);
    const auto b01 = wrkpl->transform(b01_2d);
    const auto b11 = wrkpl->transform(b11_2d);
    const auto b10 = wrkpl->transform(b10_2d);
    const auto draw_dashed_line = [&canvas](const glm::vec3 &from, const glm::vec3 &to) {
        constexpr float dash_len = 6.f;
        constexpr float gap_len = 4.f;
        const auto delta = to - from;
        const auto len = glm::length(delta);
        if (len < 1e-6f)
            return;
        const auto dir = delta / len;
        for (float s = 0; s < len; s += dash_len + gap_len) {
            const auto e = std::min(s + dash_len, len);
            canvas.draw_line(from + dir * s, from + dir * e);
        }
    };
    draw_dashed_line(b00, b01);
    draw_dashed_line(b01, b11);
    draw_dashed_line(b11, b10);
    draw_dashed_line(b10, b00);

    const auto rotate_link_vr = canvas.draw_line(wrkpl->transform(top_center_2d), wrkpl->transform(rotate_handle_2d));
    canvas.add_selectable(rotate_link_vr,
                          SelectableRef{SelectableRef::Type::SOLID_MODEL_EDGE, selection_transform_rotate_uuid(), 0});

    canvas.set_vertex_hover(false);
    const auto rotate_vr = canvas.draw_point(wrkpl->transform(rotate_handle_2d), IconTexture::IconTextureID::POINT_CIRCLE);
    canvas.add_selectable(rotate_vr, SelectableRef{SelectableRef::Type::SOLID_MODEL_EDGE, selection_transform_rotate_uuid(), 0});

    canvas.set_vertex_icon_no_flip(true);
    for (size_t i = 0; i < m_selection_transform_scale_handles.size(); i++) {
        const auto handle_2d = m_selection_transform_scale_handles.at(i);
        auto outward_2d = handle_2d - m_selection_transform_center;
        const auto outward_len = glm::length(outward_2d);
        if (outward_len > 1e-9)
            outward_2d /= outward_len;
        else
            outward_2d = {0, 1};
        // Icon rotation is based on local +X axis; rotate outward by +90deg so triangle tip points outward.
        const auto icon_dir_2d = glm::dvec2{-outward_2d.y, outward_2d.x};
        const auto vr = canvas.draw_icon(IconTexture::IconTextureID::POINT_TRIANGLE_DOWN,
                                         glm::vec3(wrkpl->transform(handle_2d)), {0, 0},
                                         glm::vec3(wrkpl->transform_relative(icon_dir_2d)));
        canvas.add_selectable(vr, SelectableRef{SelectableRef::Type::SOLID_MODEL_EDGE, selection_transform_scale_uuid(),
                                                static_cast<unsigned int>(i + 1)});
    }
    canvas.set_vertex_icon_no_flip(false);
    canvas.restore();
#endif
}

void Editor::canvas_update()
{
    auto docs = m_core.get_documents();
    auto hover_sel = get_canvas().get_hover_selection();
    if (m_update_groups_after == UUID()) {
        get_canvas().clear();

        get_canvas().set_chunk(0);
        for (const auto doc : docs) {
            if (doc->get_uuid() != m_core.get_current_idocument_info().get_uuid())
                render_document(*doc);
        }
    }
    else {
        get_canvas().clear_chunks(
                Renderer::get_chunk_from_group(m_core.get_current_document().get_group(m_update_groups_after)));
    }

    if (m_core.has_documents()) {
#ifdef DUNE_SKETCHER_ONLY
        draw_layer_glow_overlay();
#endif
        render_document(m_core.get_current_idocument_info());
#ifdef DUNE_SKETCHER_ONLY
        draw_cup_template_overlay();
#endif
    }

#ifdef DUNE_SKETCHER_ONLY
    const auto &overlay_selection = m_selection_transform_overlay_selection_cache.empty()
            ? get_canvas().get_selection()
            : m_selection_transform_overlay_selection_cache;
    draw_selection_transform_overlay(overlay_selection);
#endif

    get_canvas().set_hover_selection(hover_sel);
    update_error_overlay();
    get_canvas().request_push();
    m_update_groups_after = UUID();
}

void Editor::canvas_update_keep_selection()
{
    auto sel = get_canvas().get_selection();
    m_selection_transform_overlay_selection_cache = sel;
    canvas_update();
    get_canvas().set_selection(sel, false);
    m_selection_transform_overlay_selection_cache.clear();
}

void Editor::enable_hover_selection(bool enable)
{
    get_canvas().set_selection_mode(enable ? SelectionMode::HOVER_ONLY : SelectionMode::NONE);
}

std::optional<SelectableRef> Editor::get_hover_selection() const
{
    return get_canvas().get_hover_selection();
}

glm::dvec3 Editor::get_cursor_pos() const
{
    return get_canvas().get_cursor_pos();
}

glm::vec3 Editor::get_cam_normal() const
{
    return get_canvas().get_cam_normal();
}

glm::quat Editor::get_cam_quat() const
{
    return get_canvas().get_cam_quat();
}

glm::dvec3 Editor::get_cursor_pos_for_plane(glm::dvec3 origin, glm::dvec3 normal) const
{
    return get_canvas().get_cursor_pos_for_plane(origin, normal);
}

void Editor::set_canvas_selection_mode(SelectionMode mode)
{
    m_last_selection_mode = mode;
}

bool Editor::begin_selection_transform_drag(const SelectableRef &handle)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_selection_transform_enabled)
        return false;
    if (!m_selection_transform_overlay_valid)
        return false;
    if (!is_selection_transform_handle(handle))
        return false;

    auto &doc = m_core.get_current_document();
    auto *wrkpl = doc.get_entity_ptr<EntityWorkplane>(m_selection_transform_workplane);
    if (!wrkpl)
        return false;

    m_selection_transform_drag_active = true;
    m_selection_transform_drag_dirty = false;
    m_selection_transform_constraints_removed = false;
    m_selection_transform_drag_mode = SelectionTransformDragMode::NONE;
    m_selection_transform_entities_before.clear();
    m_selection_transform_selection_before = get_canvas().get_selection();

    std::set<UUID> selected_entity_set(m_selection_transform_entities.begin(), m_selection_transform_entities.end());
    m_selection_transform_constraints_removed =
            remove_direction_constraints_for_entities(doc, m_selection_transform_group, selected_entity_set);

    for (const auto &uu : m_selection_transform_entities) {
        if (const auto *en = doc.get_entity_ptr(uu))
            m_selection_transform_entities_before.emplace(uu, en->clone());
    }
    if (m_selection_transform_entities_before.empty()) {
        m_selection_transform_drag_active = false;
        return false;
    }

    const auto cursor_world = get_canvas().get_cursor_pos_for_plane(wrkpl->m_origin, wrkpl->get_normal_vector());
    const auto cursor = wrkpl->project(cursor_world);
    m_selection_transform_start_angle = std::atan2(cursor.y - m_selection_transform_center.y,
                                                   cursor.x - m_selection_transform_center.x);
    m_selection_transform_drag_angle = 0;
    m_selection_transform_drag_scale = 1;
    m_selection_transform_scale_handle_index = 0;
    m_selection_transform_scale_base_vector = {1, 0};
    m_selection_transform_scale_start_factor = 1;
    if (handle.item == selection_transform_rotate_uuid()) {
        m_selection_transform_drag_mode = SelectionTransformDragMode::ROTATE;
    }
    else if (handle.item == selection_transform_scale_uuid()) {
        m_selection_transform_drag_mode = SelectionTransformDragMode::SCALE;
        if (handle.point >= 1 && handle.point <= m_selection_transform_base_scale_handles.size()) {
            m_selection_transform_scale_handle_index = handle.point - 1;
            m_selection_transform_scale_base_vector =
                    m_selection_transform_base_scale_handles.at(m_selection_transform_scale_handle_index)
                    - m_selection_transform_center;
            const auto denom = glm::dot(m_selection_transform_scale_base_vector, m_selection_transform_scale_base_vector);
            if (denom > 1e-9) {
                m_selection_transform_scale_start_factor =
                        glm::dot(cursor - m_selection_transform_center, m_selection_transform_scale_base_vector) / denom;
            }
            else {
                m_selection_transform_scale_start_factor = 1;
            }
            if (std::abs(m_selection_transform_scale_start_factor) < 1e-6)
                m_selection_transform_scale_start_factor = 1;
        }
    }
    else {
        m_selection_transform_drag_active = false;
        m_selection_transform_entities_before.clear();
        return false;
    }
    return true;
#else
    (void)handle;
    return false;
#endif
}

void Editor::update_selection_transform_drag()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_selection_transform_drag_active)
        return;
    if (m_selection_transform_drag_mode == SelectionTransformDragMode::NONE)
        return;
    if (!m_core.has_documents())
        return;

    auto &doc = m_core.get_current_document();
    auto *wrkpl = doc.get_entity_ptr<EntityWorkplane>(m_selection_transform_workplane);
    if (!wrkpl)
        return;

    const auto cursor_world = get_canvas().get_cursor_pos_for_plane(wrkpl->m_origin, wrkpl->get_normal_vector());
    const auto cursor = wrkpl->project(cursor_world);

    if (m_selection_transform_drag_mode == SelectionTransformDragMode::ROTATE) {
        auto angle = std::atan2(cursor.y - m_selection_transform_center.y, cursor.x - m_selection_transform_center.x)
                - m_selection_transform_start_angle;
        while (angle > M_PI)
            angle -= 2 * M_PI;
        while (angle < -M_PI)
            angle += 2 * M_PI;
        m_selection_transform_drag_angle = angle;
        m_selection_transform_drag_scale = 1;
        if (std::abs(m_selection_transform_drag_angle) > 1e-6)
            m_selection_transform_drag_dirty = true;
        for (const auto &[uu, src_entity] : m_selection_transform_entities_before) {
            auto *dst = doc.get_entity_ptr(uu);
            if (!dst)
                continue;
            transform_2d_entity(*dst, *src_entity,
                                [this](const glm::dvec2 &p) {
                                    const auto scaled = m_selection_transform_center
                                            + (p - m_selection_transform_center) * m_selection_transform_drag_scale;
                                    return rotate_point_2d(scaled, m_selection_transform_center,
                                                           m_selection_transform_drag_angle);
                                },
                                m_selection_transform_drag_scale, m_selection_transform_drag_angle);
        }
    }
    else if (m_selection_transform_drag_mode == SelectionTransformDragMode::SCALE) {
        const auto denom = glm::dot(m_selection_transform_scale_base_vector, m_selection_transform_scale_base_vector);
        auto factor = 1.0;
        if (denom > 1e-9) {
            const auto current_factor =
                    glm::dot(cursor - m_selection_transform_center, m_selection_transform_scale_base_vector) / denom;
            factor = current_factor / m_selection_transform_scale_start_factor;
        }
        factor = std::clamp(factor, 0.01, 100.0);
        m_selection_transform_drag_scale = factor;
        m_selection_transform_drag_angle = 0;
        if (std::abs(m_selection_transform_drag_scale - 1.0) > 1e-6)
            m_selection_transform_drag_dirty = true;
        for (const auto &[uu, src_entity] : m_selection_transform_entities_before) {
            auto *dst = doc.get_entity_ptr(uu);
            if (!dst)
                continue;
            transform_2d_entity(*dst, *src_entity,
                                [this](const glm::dvec2 &p) {
                                    const auto scaled = m_selection_transform_center
                                            + (p - m_selection_transform_center) * m_selection_transform_drag_scale;
                                    return rotate_point_2d(scaled, m_selection_transform_center,
                                                           m_selection_transform_drag_angle);
                                },
                                m_selection_transform_drag_scale, m_selection_transform_drag_angle);
        }
    }

    canvas_update_keep_selection();
#endif
}

void Editor::end_selection_transform_drag()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_selection_transform_drag_active)
        return;
    if (m_selection_transform_drag_mode == SelectionTransformDragMode::ROTATE) {
        m_selection_transform_frame_angle += m_selection_transform_drag_angle;
        while (m_selection_transform_frame_angle > M_PI)
            m_selection_transform_frame_angle -= 2 * M_PI;
        while (m_selection_transform_frame_angle < -M_PI)
            m_selection_transform_frame_angle += 2 * M_PI;
    }
    const bool changed = m_selection_transform_drag_dirty || m_selection_transform_constraints_removed;
    m_selection_transform_drag_active = false;
    m_selection_transform_drag_mode = SelectionTransformDragMode::NONE;
    m_selection_transform_entities_before.clear();
    m_selection_transform_drag_angle = 0;
    m_selection_transform_drag_scale = 1;
    m_selection_transform_scale_start_factor = 1;
    m_selection_transform_scale_handle_index = 0;

    if (changed) {
        m_core.set_needs_save();
        m_core.rebuild("selection transform");
        m_workspace_browser->update_documents(get_current_document_views());
        update_action_sensitivity();
    }
    if (!m_selection_transform_selection_before.empty())
        get_canvas().set_selection(m_selection_transform_selection_before, false);
    m_selection_transform_selection_before.clear();
    canvas_update_keep_selection();
#endif
}

void Editor::handle_cursor_move()
{
    if (m_selection_transform_drag_active) {
        update_selection_transform_drag();
        return;
    }

    if (m_core.tool_is_active()) {
        ToolArgs args;
        args.type = ToolEventType::MOVE;
        ToolResponse r = tool_update_with_symmetry(args);
        tool_process(r);
    }
    else {
        if (m_drag_tool == ToolID::NONE)
            return;
        if (m_selection_for_drag.size() == 0)
            return;
        if (get_canvas().get_is_long_click())
            return;
        auto pos = get_canvas().get_cursor_pos_win();
        auto delta = pos - m_cursor_pos_win_drag_begin;
        if (glm::length(delta) > 10) {
            ToolArgs args;
            args.selection = m_selection_for_drag;
            capture_symmetry_entities_before_tool(m_drag_tool);
            m_last_selection_mode = get_canvas().get_selection_mode();
            get_canvas().set_selection_mode(SelectionMode::NONE);
            ToolResponse r = m_core.tool_begin(m_drag_tool, args, true);
            tool_process(r);

            m_selection_for_drag.clear();
            m_drag_tool = ToolID::NONE;
        }
    }
}

void Editor::handle_view_changed()
{
    if (!m_core.tool_is_active())
        return;
    if (!m_core.tool_handles_view_changed())
        return;

    ToolArgs args;
    args.type = ToolEventType::VIEW_CHANGED;
    ToolResponse r = tool_update_with_symmetry(args);
    tool_process(r);
}

void Editor::handle_click(unsigned int button, unsigned int n)
{
    const bool is_doubleclick = n == 2;

    if (m_core.tool_is_active()) {
        // nop
    }
    else if (is_doubleclick && button == 1) {
        auto sel = get_canvas().get_hover_selection();
        if (sel) {
            if (auto action = get_doubleclick_action(*sel)) {
                get_canvas().set_selection({*sel}, false);
                get_canvas().inhibit_drag_selection();
                trigger_action(*action);
            }
        }
    }
    else if (button == 1) {
        auto hover_sel = get_canvas().get_hover_selection();
        if (!hover_sel)
            return;

#ifdef DUNE_SKETCHER_ONLY
        if (is_selection_transform_handle(*hover_sel)) {
            if (begin_selection_transform_drag(*hover_sel)) {
                get_canvas().inhibit_drag_selection();
                return;
            }
        }
#endif

        auto sel = get_canvas().get_selection();
        if (!sel.contains(hover_sel.value())) {
            sel = {*hover_sel};
        }

        m_drag_tool = get_tool_for_drag_move(false, sel);
        if (m_drag_tool != ToolID::NONE && m_core.tool_can_begin(m_drag_tool, sel).get_can_begin()) {
            get_canvas().inhibit_drag_selection();
            m_cursor_pos_win_drag_begin = get_canvas().get_cursor_pos_win();
            m_selection_for_drag = sel;
        }
    }
}

ToolID Editor::get_tool_for_drag_move(bool ctrl, const std::set<SelectableRef> &sel)
{
    return ToolID::MOVE;
}

void Editor::reset_key_hint_label()
{
    const auto act = ActionID::POPOVER;
    if (m_action_connections.count(act)) {
        if (m_action_connections.at(act).key_sequences.size()) {
            const auto keys = key_sequence_to_string(m_action_connections.at(act).key_sequences.front());
            m_win.set_key_hint_label_text("> " + keys + " for menu");
            return;
        }
    }
    m_win.set_key_hint_label_text(">");
}

void Editor::tool_bar_clear_actions()
{
    m_win.tool_bar_clear_actions();
    m_in_tool_action_label_infos.clear();
}

void Editor::tool_bar_set_actions(const std::vector<ActionLabelInfo> &labels)
{
    std::vector<ActionLabelInfo> filtered;
    filtered.reserve(labels.size());
    for (const auto &it : labels) {
        if (it.action1 == InToolActionID::LMB || it.action1 == InToolActionID::RMB)
            continue;
        filtered.push_back(it);
    }

    if (m_in_tool_action_label_infos != filtered) {
        tool_bar_clear_actions();
        for (const auto &it : filtered) {
            tool_bar_append_action(it.action1, it.action2, it.action3, it.label);
        }

        m_in_tool_action_label_infos = filtered;
    }
}

void Editor::tool_bar_append_action(InToolActionID action1, InToolActionID action2, InToolActionID action3,
                                    const std::string &s)
{
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
    if (action1 == InToolActionID::LMB || action1 == InToolActionID::RMB) {
        std::string icon_name = "action-";
        if (action1 == InToolActionID::LMB) {
            icon_name += "lmb";
        }
        else {
            icon_name += "rmb";
        }
        icon_name += "-symbolic";
        auto img = Gtk::manage(new Gtk::Image);
        img->set_from_icon_name(icon_name);
        img->show();
        box->append(*img);
    }
    else {
        auto key_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        for (const auto action : {action1, action2, action3}) {
            if (action != InToolActionID::NONE) {
                const auto &prefs = m_in_tool_key_sequeces_preferences.keys;
                if (prefs.count(action)) {
                    if (prefs.at(action).size()) {
                        auto seq = prefs.at(action).front();
                        auto kl = Gtk::manage(new Gtk::Label(key_sequence_to_string_short(seq)));
                        kl->set_valign(Gtk::Align::BASELINE);
                        key_box->append(*kl);
                    }
                }
            }
        }
        key_box->get_style_context()->add_class("editor-key-box");

        box->append(*key_box);
    }
    const auto &as = s.size() ? s : in_tool_action_catalog.at(action1).name;

    auto la = Gtk::manage(new Gtk::Label(as));
    la->set_valign(Gtk::Align::BASELINE);

    la->show();

    box->append(*la);

    m_win.tool_bar_append_action(*box);
}

void Editor::update_version_info()
{
    if (!m_core.has_documents()) {
        m_win.set_version_info("");
        return;
    }
    const auto &doc = m_core.get_current_document();
    auto &ver = doc.m_version;
    m_win.set_version_info(ver.get_message());
}

bool Editor::has_file(const std::filesystem::path &path)
{
    return m_core.get_idocument_info_by_path(path);
}

std::optional<std::filesystem::path> Editor::get_group_export_path(const UUID &group) const
{
    if (!m_group_export_paths.contains(group))
        return {};
    return m_group_export_paths.at(group);
}

void Editor::set_group_export_path(const UUID &group, const std::filesystem::path &path)
{
    m_group_export_paths[group] = normalize_group_path(path);
}

void Editor::open_file(const std::filesystem::path &path)
{
#ifdef DUNE_SKETCHER_ONLY
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".dxf" || ext == ".svg") {
        CanvasUpdater canvas_updater{*this};
        if (ext == ".svg") {
            m_workspace_browser->show_toast("SVG import isn't implemented yet. Please open DXF.");
            return;
        }

        if (!m_core.has_documents())
            trigger_action(ActionID::NEW_DOCUMENT);
        if (m_core.tool_is_active())
            force_end_tool();

        auto &doc = m_core.get_current_document();
        const auto normalized_path = normalize_group_path(path);
        const auto imported_group_name = path_to_string(path.filename());
        const auto group_has_user_entities = [&doc](const UUID &group_uuid) {
            for (const auto &[uu, en] : doc.m_entities) {
                if (en->m_group == group_uuid && en->m_kind == ItemKind::USER)
                    return true;
            }
            return false;
        };
        for (auto gr : doc.get_groups_sorted()) {
            auto sketch = dynamic_cast<const GroupSketch *>(gr);
            if (!sketch)
                continue;

            bool is_duplicate = false;
            if (auto it = m_group_export_paths.find(gr->m_uuid); it != m_group_export_paths.end()) {
                is_duplicate = (normalize_group_path(it->second) == normalized_path);
            }
            else if (gr->m_name == imported_group_name && group_has_user_entities(gr->m_uuid)) {
                is_duplicate = true;
            }
            if (!is_duplicate)
                continue;

            set_current_group(gr->m_uuid);
            m_core.set_current_document_path(normalized_path);
            set_group_export_path(gr->m_uuid, normalized_path);
            m_win.get_app().add_recent_item(path);
            m_workspace_browser->show_toast("File already imported: switched to existing sketch");
            update_title();
            return;
        }

        auto group_uu = m_core.get_current_group();
        auto &current_group = doc.get_group(group_uu);

        bool current_group_is_empty = true;
        current_group_is_empty = !group_has_user_entities(group_uu);

        if (!current_group_is_empty) {
            auto &new_group = doc.insert_group<GroupSketch>(UUID::random(), current_group.m_uuid);
            new_group.m_name = doc.find_next_group_name(Group::Type::SKETCH);
            if (current_group.m_active_wrkpl) {
                new_group.m_active_wrkpl = current_group.m_active_wrkpl;
            }
            else {
                for (auto gr : doc.get_groups_sorted()) {
                    if (auto ref = dynamic_cast<const GroupReference *>(gr)) {
                        new_group.m_active_wrkpl = ref->get_workplane_xy_uuid();
                        break;
                    }
                }
            }
            doc.set_group_generate_pending(new_group.m_uuid);
            m_core.rebuild("add group");
            set_current_group(new_group.m_uuid);
            group_uu = new_group.m_uuid;
        }

        auto wrkpl_uu = m_core.get_current_workplane();
        if (!wrkpl_uu) {
            m_workspace_browser->show_toast("Couldn't import DXF: no active workplane");
            return;
        }

        DXFImporter importer(doc, group_uu, wrkpl_uu);
        if (!importer.import(path)) {
            m_workspace_browser->show_toast("Couldn't import DXF");
            return;
        }

        auto export_path = normalized_path;
        if (m_sketcher_folder_path.has_value() && !m_sketcher_opening_folder_batch)
            export_path = normalize_group_path(*m_sketcher_folder_path / path.filename());

        doc.get_group(group_uu).m_name = imported_group_name;
        set_group_export_path(group_uu, export_path);
        bool export_written = true;
        if (m_sketcher_folder_path.has_value() && !m_sketcher_opening_folder_batch) {
            try {
                export_dxf(export_path, m_core.get_current_document(), group_uu);
            }
            catch (...) {
                export_written = false;
                m_workspace_browser->show_toast("Couldn't write imported file to folder");
            }
        }
        doc.set_group_generate_pending(group_uu);
        m_core.rebuild("import dxf");
        set_current_group(group_uu);
        m_core.set_current_document_path(export_path);
        if (export_written)
            m_core.clear_needs_save();
        else
            m_core.set_needs_save();
        m_win.get_app().add_recent_item(path);
        save_workspace_view(m_core.get_current_idocument_info().get_uuid());
        m_workspace_browser->update_documents(get_current_document_views());
        update_version_info();
        update_title();
        add_to_recent_docs(path);
        return;
    }
#endif

    if (has_file(path))
        return;
    for (auto win : m_win.get_app().get_windows()) {
        if (auto appwin = dynamic_cast<Dune3DAppWindow *>(win)) {
            if (appwin->has_file(path)) {
                appwin->present();
                return;
            }
        }
    }
    add_to_recent_docs(path);
    try {
        CanvasUpdater canvas_updater{*this};

        const UUID doc_uu = UUID::random();

        std::map<UUID, WorkspaceView> loaded_workspace_views;
        try {
            const auto workspace_filename = get_workspace_filename_from_document_filename(path);
            if (std::filesystem::is_regular_file(workspace_filename)) {
                const auto j = load_json_from_file(workspace_filename);
                loaded_workspace_views = WorkspaceView::load_from_json(j.at("workspace_views"));
            }
        }
        catch (...) {
            loaded_workspace_views.clear();
        }

        DocumentView *new_dv = nullptr;
        UUID current_wsv;
        if (loaded_workspace_views.size()) {
            for (const auto &[uu, wv] : loaded_workspace_views) {
                auto &dv = wv.m_documents.at({});
                auto r = m_workspace_views.emplace(uu, wv);
                auto &inserted_wv = r.first->second;
                inserted_wv.m_documents.emplace(doc_uu, dv);
                if (r.second) {
                    inserted_wv.m_current_document = doc_uu;
                    append_workspace_view_page(wv.m_name, uu);
                    set_current_workspace_view(uu);
                    current_wsv = uu;
                }
            }
        }
        else {
            current_wsv = create_workspace_view();
            m_workspace_views.at(current_wsv).m_current_document = doc_uu;
            set_current_workspace_view(current_wsv);
            auto &dv = m_workspace_views.at(current_wsv).m_documents[doc_uu];
            dv.m_document_is_visible = true;
            new_dv = &dv;
        }

        m_core.add_document(path, doc_uu);

        {
            auto &wv = m_workspace_views.at(m_current_workspace_view);
            m_core.set_current_document(wv.m_current_document);
            m_core.set_current_group(get_current_document_view().m_current_group);
        }

        if (new_dv) {
            new_dv->m_current_group = m_core.get_idocument_info(doc_uu).get_current_group();
        }
        if (current_wsv && m_core.get_current_idocument_info().get_uuid() == doc_uu) {
            auto &dv = m_workspace_views.at(current_wsv).m_documents[doc_uu];
            set_current_group(dv.m_current_group);
        }

        update_workspace_view_names();
        update_can_close_workspace_view_pages();
        m_win.get_app().add_recent_item(path);
        update_title();
        update_version_info();


        load_linked_documents(doc_uu);
    }
    CATCH_LOG(Logger::Level::WARNING, "error opening document" + path_to_string(path), Logger::Domain::DOCUMENT)
}

void Editor::load_linked_documents(const UUID &uu_doc)
{
    if (!m_core.has_documents())
        return;
    auto &doci = m_core.get_idocument_info(uu_doc);
    auto all_documents = m_core.get_documents();
    for (auto &[uu, en] : doci.get_document().m_entities) {
        if (auto en_doc = dynamic_cast<EntityDocument *>(en.get())) {
            // fill in referenced document
            const auto path = en_doc->get_path(doci.get_dirname());

            auto referenced_doc =
                    std::ranges::find_if(all_documents, [&path](const auto &x) { return x->get_path() == path; });
            if (referenced_doc == all_documents.end()) {
                open_file(path);
            }
        }
    }
}

void Editor::set_current_group(const UUID &uu_group)
{
    CanvasUpdater canvas_updater{*this};

    m_core.set_current_group(uu_group);
    ensure_current_group_layers_initialized();
    rebuild_layers_popover();
    m_workspace_browser->update_current_group(get_current_document_views());
    update_workplane_label();
    if (m_constraints_box)
        m_constraints_box->update();
    update_group_editor();
    update_action_sensitivity();
    update_action_bar_buttons_sensitivity();
    update_selection_editor();
    update_title();
}

void Editor::tool_bar_set_tool_tip(const std::string &s)
{
    m_win.tool_bar_set_tool_tip(s);
}

void Editor::tool_bar_flash(const std::string &s)
{
    m_win.tool_bar_flash(s);
}

void Editor::tool_bar_flash_replace(const std::string &s)
{
    m_win.tool_bar_flash_replace(s);
}

bool Editor::get_use_workplane() const
{
    return m_win.get_workplane_checkbutton().get_active();
}

bool Editor::get_selection_snap_enabled() const
{
    return m_selection_snap_enabled;
}

std::optional<SelectionSnapTemplateInfo> Editor::get_selection_snap_template_info() const
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_cup_template_enabled || !m_core.has_documents())
        return {};
    const auto wrkpl_uu = m_core.get_current_workplane();
    if (!wrkpl_uu)
        return {};
    SelectionSnapTemplateInfo info;
    info.workplane = wrkpl_uu;
    info.width = std::max(1.0, m_cup_template_circumference_mm);
    info.height = std::max(1.0, m_cup_template_height_mm);
    info.segments = std::clamp(m_cup_template_segments, 1, 12);
    return info;
#else
    return {};
#endif
}

void Editor::set_selection_snap_overlay_lines(const std::vector<std::pair<glm::dvec3, glm::dvec3>> &lines_world)
{
    m_selection_snap_overlay_lines_world = lines_world;
}

void Editor::set_constraint_icons(glm::vec3 p, glm::vec3 v, const std::vector<ConstraintType> &constraints)
{
    m_constraint_tip_icons = constraints;
    m_constraint_tip_pos = p;
    m_constraint_tip_vec = v;
}

DocumentView &Editor::get_current_document_view()
{
    return get_current_workspace_view().m_documents[m_core.get_current_idocument_info().get_uuid()];
}

std::map<UUID, DocumentView> &Editor::get_current_document_views()
{
    return get_current_workspace_view().m_documents;
}

WorkspaceView &Editor::get_current_workspace_view()
{
    return m_workspace_views.at(m_current_workspace_view);
}

void Editor::update_title()
{
    if (m_core.has_documents()) {
#ifdef DUNE_SKETCHER_ONLY
        const auto &group = m_core.get_current_document().get_group(m_core.get_current_group());
        m_win.set_window_title(group.m_name);
#else
        auto &doc = m_core.get_current_idocument_info();
        if (doc.has_path())
            m_win.set_window_title_from_path(doc.get_path());
        else
            m_win.set_window_title("New Document");
#endif
    }
    else {
        m_win.set_window_title("");
    }
}

Glib::RefPtr<Pango::Context> Editor::get_pango_context()
{
    return m_win.create_pango_context();
}


void Editor::set_buffer(std::unique_ptr<const Buffer> buffer)
{
    m_win.get_app().m_buffer = std::move(buffer);
}

const Buffer *Editor::get_buffer() const
{
    return m_win.get_app().m_buffer.get();
}

void Editor::set_first_update_group(const UUID &group)
{
    m_update_groups_after = group;
}

} // namespace dune3d

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
#include "logger/logger.hpp"
#include "document/constraint/constraint.hpp"
#include "util/fs_util.hpp"
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
#include "nlohmann/json.hpp"
#include "buffer.hpp"
#include "icon_texture_id.hpp"
#include <iostream>
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
#include <glm/gtx/quaternion.hpp>

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

glm::dvec2 normalize_dir(const glm::dvec2 &v)
{
    const auto len = glm::length(v);
    if (len < 1e-9)
        return {1, 0};
    return v / len;
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

std::string format_text_font_label(const Pango::FontDescription &desc)
{
    auto family = desc.get_family();
    if (family.empty())
        family = desc.to_string();
    return family;
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
                           std::function<bool()> right_click_only = {})
{
    auto state = std::make_shared<HoverPopoverState>();

    auto maybe_close = [state, &button, &popover] {
        // Keep popover while focused widget is inside opener button or inside the popover.
        if (widget_or_descendant_has_focus(button) || widget_or_descendant_has_focus(popover))
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

    m_core.signal_needs_save().connect([this] {
        update_action_sensitivity();
        m_workspace_browser->update_needs_save();
        update_workspace_view_names();
    });
    get_canvas().signal_selection_changed().connect([this] {
        update_action_sensitivity();
        sync_symmetry_popover_context();
        if (!m_core.tool_is_active())
            apply_symmetry_live_from_popover(false);
        sync_draw_text_popover_from_selection(true);
#ifdef DUNE_SKETCHER_ONLY
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
        }
#endif
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
    }
    sync_selection_mode_popover();
    m_win.add_action_button(*m_selection_mode_button);
#endif

    create_action_bar_button(ToolID::DRAW_CONTOUR);
    create_action_bar_button(ToolID::DRAW_RECTANGLE);
    create_action_bar_button(ToolID::DRAW_CIRCLE_2D);
    create_action_bar_button(ToolID::DRAW_REGULAR_POLYGON);
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
                [this](guint keyval, guint keycode, Gdk::ModifierType state) -> bool {
                    if (keyval == GDK_KEY_Tab
                        && (state & (Gdk::ModifierType::SHIFT_MASK | Gdk::ModifierType::CONTROL_MASK
                                     | Gdk::ModifierType::ALT_MASK))
                                   == static_cast<Gdk::ModifierType>(0)) {
                        toggle_sidebar_visibility();
                        return true;
                    }
                    return false;
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
    if (!m_selection_transform_switch || !m_selection_markers_switch)
        return;
    m_updating_selection_mode_popover = true;
    m_selection_transform_switch->set_active(m_selection_transform_enabled);
    m_selection_markers_switch->set_active(m_show_technical_markers);
    m_updating_selection_mode_popover = false;
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
                    return handle_action_key(controller, keyval, state);
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
                activate_selection_mode();
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
            if (button == 1)
                m_primary_button_pressed = false;
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
}

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
        if (auto header_action_box = m_win.get_header_action_box())
            m_win.get_header_bar().pack_start(*header_action_box);

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
                clone->m_kind = ItemKind::USER;
                clone->m_generated_from = uu;
                clone->m_selection_invisible = false;
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
                clone->m_kind = ItemKind::USER;
                clone->m_generated_from = uu;
                clone->m_selection_invisible = false;
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

    if (new_entities.empty()) {
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
    if (m_symmetry_mode == SymmetryMode::RADIAL) {
        const auto center = m_symmetry_axes.front().first;
        const auto count = std::max(3u, m_symmetry_radial_axes);
        const auto phase = radial_rotation_deg_to_rad(m_symmetry_radial_rotation_deg);
        for (const auto &uu : new_entities) {
            const auto &src = doc.get_entity(uu);
            for (unsigned int i = 1; i < count; i++) {
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
                clones.push_back(std::move(clone));
            }
        }
    }
    else {
        for (const auto &uu : new_entities) {
            const auto &src = doc.get_entity(uu);
            for (const auto &[axis_point, axis_dir] : m_symmetry_axes) {
                auto clone = src.clone();
                transform_entity(*clone, src,
                                 [&](const glm::dvec2 &p) { return reflect_point_2d(p, axis_point, axis_dir); }, true);
                clone->m_uuid = UUID::random();
                clone->m_group = m_symmetry_group;
                clone->m_kind = ItemKind::USER;
                clone->m_generated_from = UUID();
                clone->m_selection_invisible = false;
                clone->m_move_instead.clear();
                clones.push_back(std::move(clone));
            }
        }
    }

    if (clones.empty()) {
        for (const auto &uu : new_entities)
            m_symmetry_pre_tool_entities.insert(uu);
        return;
    }

    for (auto &clone : clones) {
        m_symmetry_pre_tool_entities.insert(clone->m_uuid);
        doc.m_entities.emplace(clone->m_uuid, std::move(clone));
    }
    for (const auto &uu : new_entities)
        m_symmetry_pre_tool_entities.insert(uu);

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

    m_theme_light_button = Gtk::make_managed<Gtk::ToggleButton>("Light");
    m_theme_dark_button = Gtk::make_managed<Gtk::ToggleButton>("Dark");
    m_theme_dark_button->set_group(*m_theme_light_button);
    m_theme_light_button->set_hexpand(true);
    m_theme_dark_button->set_hexpand(true);
    theme_row->append(*m_theme_light_button);
    theme_row->append(*m_theme_dark_button);

    m_theme_light_button->signal_toggled().connect([this] {
        if (m_updating_settings_popover || !m_theme_light_button->get_active())
            return;
        m_preferences.canvas.theme_variant = CanvasPreferences::ThemeVariant::LIGHT;
        m_preferences.canvas.dark_theme = false;
        m_preferences.signal_changed().emit();
    });
    m_theme_dark_button->signal_toggled().connect([this] {
        if (m_updating_settings_popover || !m_theme_dark_button->get_active())
            return;
        m_preferences.canvas.theme_variant = CanvasPreferences::ThemeVariant::DARK;
        m_preferences.canvas.dark_theme = true;
        m_preferences.signal_changed().emit();
    });

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
    if (!m_theme_light_button || !m_theme_dark_button || !m_line_width_scale || !m_line_width_value_label
        || !m_right_click_popovers_switch)
        return;
    m_updating_settings_popover = true;
    const bool dark = m_preferences.canvas.theme_variant == CanvasPreferences::ThemeVariant::DARK;
    m_theme_dark_button->set_active(dark);
    m_theme_light_button->set_active(!dark);
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
#ifdef DUNE_SKETCHER_ONLY
    // In sketcher mode, Theme Variant drives both GTK and canvas theme.
    switch (m_preferences.canvas.theme_variant) {
    case CanvasPreferences::ThemeVariant::AUTO:
        break;
    case CanvasPreferences::ThemeVariant::DARK:
        dark = true;
        break;
    case CanvasPreferences::ThemeVariant::LIGHT:
        dark = false;
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
        break;
    case CanvasPreferences::ThemeVariant::DARK:
        dark = true;
        break;
    case CanvasPreferences::ThemeVariant::LIGHT:
        dark = false;
        break;
    }
#endif
    if (color_themes.contains(m_preferences.canvas.theme)) {
        Appearance appearance = m_preferences.canvas.appearance;
        appearance.colors = color_themes.at(m_preferences.canvas.theme).get(dark);
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
    renderer.m_connect_curvature_comb = m_preferences.canvas.connect_curvature_combs;
    renderer.m_first_group = m_update_groups_after;

    if (doc.get_uuid() == m_core.get_current_idocument_info().get_uuid()) {
        renderer.add_constraint_icons(m_constraint_tip_pos, m_constraint_tip_vec, m_constraint_tip_icons);
        renderer.m_overlay_construction_lines = get_symmetry_overlay_lines_world();
    }

    try {
        renderer.render(doc.get_document(), doc.get_current_group(), doc_view,
                        m_workspace_views.at(m_current_workspace_view), doc.get_dirname(), sr);
    }
    catch (const std::exception &ex) {
        Logger::log_critical("exception rendering document " + doc.get_basename(), Logger::Domain::RENDERER, ex.what());
    }
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

    if (m_core.has_documents())
        render_document(m_core.get_current_idocument_info());

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

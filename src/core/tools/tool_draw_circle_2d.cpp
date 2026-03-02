#include "tool_draw_circle_2d.hpp"
#include "document/document.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_bezier2d.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "document/constraint/constraint_point_on_line.hpp"
#include "document/constraint/constraint_point_on_circle.hpp"
#include "editor/editor_interface.hpp"
#include "util/selection_util.hpp"
#include "util/action_label.hpp"
#include "tool_common_impl.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace dune3d {
namespace {
bool s_default_oval_mode = false;
double s_default_span_degrees = 360.0;
}

void ToolDrawCircle2D::set_default_oval_mode(bool enabled)
{
    s_default_oval_mode = enabled;
}

bool ToolDrawCircle2D::get_default_oval_mode()
{
    return s_default_oval_mode;
}

void ToolDrawCircle2D::set_default_span_degrees(double degrees)
{
    s_default_span_degrees = std::clamp(degrees, 1.0, 360.0);
}

double ToolDrawCircle2D::get_default_span_degrees()
{
    return s_default_span_degrees;
}

ToolResponse ToolDrawCircle2D::begin(const ToolArgs &args)
{
    m_wrkpl = get_workplane();
    m_intf.enable_hover_selection();
    m_oval_mode = s_default_oval_mode;
    m_span_degrees = s_default_span_degrees;
    m_temp_circle = nullptr;
    m_temp_arc = nullptr;
    m_temp_sector_lines = {nullptr, nullptr};
    m_temp_oval = {nullptr, nullptr, nullptr, nullptr};
    m_temp_oval_segments = 0;
    return ToolResponse();
}

ToolBase::CanBegin ToolDrawCircle2D::can_begin()
{
    return get_workplane_uuid() != UUID();
}

glm::dvec2 ToolDrawCircle2D::get_cursor_pos_in_plane() const
{
    return m_wrkpl->project(get_cursor_pos_for_workplane(*m_wrkpl));
}

bool ToolDrawCircle2D::has_temp_oval() const
{
    return m_temp_oval_segments > 0 && m_temp_oval.front() != nullptr;
}

bool ToolDrawCircle2D::has_temp_entity() const
{
    return m_temp_circle != nullptr || m_temp_arc != nullptr || has_temp_oval();
}

bool ToolDrawCircle2D::is_sector_mode() const
{
    return m_span_degrees < 359.999;
}

double ToolDrawCircle2D::get_arc_span_radians() const
{
    return std::clamp(m_span_degrees, 1.0, 360.0) * M_PI / 180.0;
}

void ToolDrawCircle2D::update_arc_preview(const glm::dvec2 &cursor)
{
    if (!m_temp_arc)
        return;
    const auto v = cursor - m_center;
    const auto r = glm::length(v);
    if (r <= 1e-9) {
        m_temp_arc->m_from = m_center;
        m_temp_arc->m_to = m_center;
        m_temp_arc->m_center = m_center;
        return;
    }
    const auto a0 = std::atan2(v.y, v.x);
    const auto a1 = a0 + get_arc_span_radians();
    m_temp_arc->m_center = m_center;
    m_temp_arc->m_from = m_center + glm::dvec2(std::cos(a0), std::sin(a0)) * r;
    m_temp_arc->m_to = m_center + glm::dvec2(std::cos(a1), std::sin(a1)) * r;
    if (m_temp_sector_lines.at(0)) {
        m_temp_sector_lines.at(0)->m_p1 = m_center;
        m_temp_sector_lines.at(0)->m_p2 = m_temp_arc->m_from;
    }
    if (m_temp_sector_lines.at(1)) {
        m_temp_sector_lines.at(1)->m_p1 = m_center;
        m_temp_sector_lines.at(1)->m_p2 = m_temp_arc->m_to;
    }
}

void ToolDrawCircle2D::update_oval_preview(const glm::dvec2 &cursor)
{
    if (!has_temp_oval())
        return;

    const auto d = cursor - m_center;
    const auto rx = std::max(std::abs(d.x), 1e-6);
    const auto ry = std::max(std::abs(d.y), 1e-6);
    const auto cx = m_center.x;
    const auto cy = m_center.y;

    const auto seg_count = std::min<unsigned int>(m_temp_oval_segments, 4);
    const auto span = get_arc_span_radians();
    const auto step = span / static_cast<double>(seg_count);
    for (unsigned int i = 0; i < seg_count; i++) {
        auto *b = m_temp_oval.at(i);
        if (!b)
            continue;
        const auto a0 = step * static_cast<double>(i);
        const auto a1 = step * static_cast<double>(i + 1);
        const auto delta = a1 - a0;
        const auto k = (4.0 / 3.0) * std::tan(delta / 4.0);

        const glm::dvec2 p0{cx + rx * std::cos(a0), cy + ry * std::sin(a0)};
        const glm::dvec2 p3{cx + rx * std::cos(a1), cy + ry * std::sin(a1)};
        const glm::dvec2 d0{-rx * std::sin(a0), ry * std::cos(a0)};
        const glm::dvec2 d1{-rx * std::sin(a1), ry * std::cos(a1)};
        b->m_p1 = p0;
        b->m_c1 = p0 + k * d0;
        b->m_c2 = p3 - k * d1;
        b->m_p2 = p3;
    }

    if (is_sector_mode()) {
        const glm::dvec2 start_pt{cx + rx, cy};
        const glm::dvec2 end_pt{cx + rx * std::cos(span), cy + ry * std::sin(span)};
        if (m_temp_sector_lines.at(0)) {
            m_temp_sector_lines.at(0)->m_p1 = m_center;
            m_temp_sector_lines.at(0)->m_p2 = start_pt;
        }
        if (m_temp_sector_lines.at(1)) {
            m_temp_sector_lines.at(1)->m_p1 = m_center;
            m_temp_sector_lines.at(1)->m_p2 = end_pt;
        }
    }
}

ToolResponse ToolDrawCircle2D::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        if (m_temp_circle) {
            m_temp_circle->m_radius = glm::length(get_cursor_pos_in_plane() - m_temp_circle->m_center);
        }
        else if (m_temp_arc) {
            update_arc_preview(get_cursor_pos_in_plane());
        }
        else if (has_temp_oval()) {
            update_oval_preview(get_cursor_pos_in_plane());
        }
        update_tip();
        set_first_update_group_current();
        return ToolResponse();
    }
    else if (args.type == ToolEventType::ACTION) {
        switch (args.action) {
        case InToolActionID::LMB: {
            if (has_temp_entity()) {
                if (m_temp_circle) {
                    m_temp_circle->m_selection_invisible = false;
                    if (m_constrain) {
                        if (auto hsel = m_intf.get_hover_selection()) {
                            if (hsel->type == SelectableRef::Type::ENTITY) {
                                const auto enp = hsel->get_entity_and_point();
                                if (get_doc().is_valid_point(enp)) {
                                    auto &constraint = add_constraint<ConstraintPointOnCircle>();
                                    constraint.m_circle = m_temp_circle->m_uuid;
                                    constraint.m_point = enp;
                                    constraint.m_modify_to_satisfy = true;
                                }
                            }
                        }
                    }
                }
                else if (m_temp_arc) {
                    m_temp_arc->m_selection_invisible = false;
                    for (auto *line : m_temp_sector_lines) {
                        if (line)
                            line->m_selection_invisible = false;
                    }
                }
                else if (has_temp_oval()) {
                    for (auto *bez : m_temp_oval) {
                        if (bez)
                            bez->m_selection_invisible = false;
                    }
                    for (auto *line : m_temp_sector_lines) {
                        if (line)
                            line->m_selection_invisible = false;
                    }
                }
                return ToolResponse::commit();
            }
            else {
                // Pick current UI defaults at shape start so first figure uses updated mode.
                m_oval_mode = s_default_oval_mode;
                m_span_degrees = s_default_span_degrees;
                m_center = get_cursor_pos_in_plane();
                if (m_oval_mode) {
                    if (is_sector_mode())
                        m_temp_oval_segments = std::clamp<unsigned int>(
                                static_cast<unsigned int>(std::ceil(get_arc_span_radians() / (M_PI / 2.0))), 1, 4);
                    else
                        m_temp_oval_segments = 4;

                    for (unsigned int i = 0; i < 4; i++) {
                        if (i >= m_temp_oval_segments) {
                            m_temp_oval.at(i) = nullptr;
                            continue;
                        }
                        auto &bez = m_temp_oval.at(i);
                        bez = &add_entity<EntityBezier2D>();
                        bez->m_selection_invisible = true;
                        bez->m_wrkpl = m_wrkpl->m_uuid;
                        bez->m_p1 = m_center;
                        bez->m_c1 = m_center;
                            bez->m_c2 = m_center;
                            bez->m_p2 = m_center;
                    }
                    if (is_sector_mode()) {
                        for (auto &line : m_temp_sector_lines) {
                            line = &add_entity<EntityLine2D>();
                            line->m_selection_invisible = true;
                            line->m_wrkpl = m_wrkpl->m_uuid;
                            line->m_p1 = m_center;
                            line->m_p2 = m_center;
                        }
                    }
                }
                else if (!is_sector_mode()) {
                    m_temp_circle = &add_entity<EntityCircle2D>();
                    m_temp_circle->m_selection_invisible = true;
                    m_temp_circle->m_radius = 0;
                    m_temp_circle->m_center = m_center;
                    m_temp_circle->m_wrkpl = m_wrkpl->m_uuid;

                    if (m_constrain) {
                        const EntityAndPoint circle_center{m_temp_circle->m_uuid, 1};
                        constrain_point(m_wrkpl->m_uuid, circle_center);
                    }
                }
                else {
                    m_temp_arc = &add_entity<EntityArc2D>();
                    m_temp_arc->m_selection_invisible = true;
                    m_temp_arc->m_center = m_center;
                    m_temp_arc->m_from = m_center;
                    m_temp_arc->m_to = m_center;
                    m_temp_arc->m_wrkpl = m_wrkpl->m_uuid;

                    if (m_constrain) {
                        const EntityAndPoint arc_center{m_temp_arc->m_uuid, 3};
                        constrain_point(m_wrkpl->m_uuid, arc_center);
                    }
                    for (auto &line : m_temp_sector_lines) {
                        line = &add_entity<EntityLine2D>();
                        line->m_selection_invisible = true;
                        line->m_wrkpl = m_wrkpl->m_uuid;
                        line->m_p1 = m_center;
                        line->m_p2 = m_center;
                    }
                }
                return ToolResponse();
            }
        } break;

        case InToolActionID::TOGGLE_CONSTRUCTION: {
            if (m_temp_circle)
                m_temp_circle->m_construction = !m_temp_circle->m_construction;
            if (m_temp_arc) {
                m_temp_arc->m_construction = !m_temp_arc->m_construction;
                for (auto *line : m_temp_sector_lines) {
                    if (line)
                        line->m_construction = m_temp_arc->m_construction;
                }
            }
            if (has_temp_oval()) {
                const bool construction = !m_temp_oval.front()->m_construction;
                for (auto *bez : m_temp_oval)
                    if (bez)
                        bez->m_construction = construction;
                for (auto *line : m_temp_sector_lines) {
                    if (line)
                        line->m_construction = construction;
                }
            }
        } break;

        case InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT: {
            m_constrain = !m_constrain;
        } break;

        case InToolActionID::TOGGLE_CIRCLE_OVAL: {
            m_oval_mode = !m_oval_mode;
            set_default_oval_mode(m_oval_mode);
        } break;

        case InToolActionID::TOGGLE_CIRCLE_SLICE: {
            if (is_sector_mode())
                m_span_degrees = 360.0;
            else
                m_span_degrees = 180.0;
            set_default_span_degrees(m_span_degrees);
            m_span_degrees = get_default_span_degrees();
        } break;

        case InToolActionID::CIRCLE_SPAN_INC: {
            m_span_degrees = std::clamp(m_span_degrees + 15.0, 1.0, 359.0);
            set_default_span_degrees(m_span_degrees);
            m_span_degrees = get_default_span_degrees();
            if (m_temp_arc)
                update_arc_preview(get_cursor_pos_in_plane());
            else if (has_temp_oval())
                update_oval_preview(get_cursor_pos_in_plane());
        } break;

        case InToolActionID::CIRCLE_SPAN_DEC: {
            m_span_degrees = std::clamp(m_span_degrees - 15.0, 1.0, 359.0);
            set_default_span_degrees(m_span_degrees);
            m_span_degrees = get_default_span_degrees();
            if (m_temp_arc)
                update_arc_preview(get_cursor_pos_in_plane());
            else if (has_temp_oval())
                update_oval_preview(get_cursor_pos_in_plane());
        } break;

        case InToolActionID::RMB:
        case InToolActionID::CANCEL:
            return ToolResponse::revert();

        default:;
        }
        update_tip();
    }

    return ToolResponse();
}

void ToolDrawCircle2D::update_tip()
{
    std::vector<ActionLabelInfo> actions;

    if (has_temp_entity()) {
        if (m_oval_mode)
            actions.emplace_back(InToolActionID::LMB, "place size");
        else
            actions.emplace_back(InToolActionID::LMB, "place radius");
    }
    else {
        actions.emplace_back(InToolActionID::LMB, "place center");
    }

    actions.emplace_back(InToolActionID::RMB, "end tool");

    if (has_temp_entity()) {
        bool construction = false;
        if (m_temp_circle)
            construction = m_temp_circle->m_construction;
        else if (m_temp_arc)
            construction = m_temp_arc->m_construction;
        else if (has_temp_oval())
            construction = m_temp_oval.front()->m_construction;

        if (construction)
            actions.emplace_back(InToolActionID::TOGGLE_CONSTRUCTION, "normal");
        else
            actions.emplace_back(InToolActionID::TOGGLE_CONSTRUCTION, "construction");
    }

    if (m_constrain)
        actions.emplace_back(InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT, "constraint off");
    else
        actions.emplace_back(InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT, "constraint on");

    actions.emplace_back(InToolActionID::TOGGLE_CIRCLE_OVAL, m_oval_mode ? "oval off" : "oval on");
    actions.emplace_back(InToolActionID::TOGGLE_CIRCLE_SLICE, is_sector_mode() ? "slice off" : "slice on");
    if (is_sector_mode())
        actions.emplace_back(InToolActionID::CIRCLE_SPAN_INC, InToolActionID::CIRCLE_SPAN_DEC, "angle");


    std::vector<ConstraintType> constraint_icons;
    glm::vec3 v = {NAN, NAN, NAN};

    std::ostringstream tip;
    tip << (m_oval_mode ? "oval" : "circle");
    if (is_sector_mode())
        tip << " a=" << std::fixed << std::setprecision(0) << m_span_degrees << "deg";
    if (m_constrain && !has_temp_entity() && !m_oval_mode) {
        tip << " " << get_constrain_tip("center");
        update_constraint_icons(constraint_icons);
    }
    if (m_constrain && m_temp_circle) {
        if (auto hsel = m_intf.get_hover_selection()) {
            if (hsel->type == SelectableRef::Type::ENTITY) {
                const auto enp = hsel->get_entity_and_point();
                if (get_doc().is_valid_point(enp)) {
                    const auto r = get_cursor_pos_in_plane() - m_temp_circle->m_center;
                    v = m_wrkpl->transform_relative({-r.y, r.x});
                    constraint_icons.push_back(Constraint::Type::POINT_ON_CIRCLE);
                    tip << " constrain radius on point";
                }
            }
        }
    }
    m_intf.tool_bar_set_tool_tip(tip.str());

    m_intf.set_constraint_icons(get_cursor_pos_for_workplane(*m_wrkpl), v, constraint_icons);

    m_intf.tool_bar_set_actions(actions);
}
} // namespace dune3d

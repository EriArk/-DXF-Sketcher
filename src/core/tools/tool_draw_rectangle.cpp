#include "tool_draw_rectangle.hpp"
#include "document/document.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_point2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "document/constraint/constraint_hv.hpp"
#include "document/constraint/constraint_midpoint.hpp"
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
bool s_default_rectangle_square = false;
bool s_default_rectangle_rounded = false;
double s_default_rectangle_round_radius = 1.0;
}

void ToolDrawRectangle::set_default_square(bool square)
{
    s_default_rectangle_square = square;
}

bool ToolDrawRectangle::get_default_square()
{
    return s_default_rectangle_square;
}

void ToolDrawRectangle::set_default_rounded(bool rounded)
{
    s_default_rectangle_rounded = rounded;
}

bool ToolDrawRectangle::get_default_rounded()
{
    return s_default_rectangle_rounded;
}

void ToolDrawRectangle::set_default_round_radius(double radius)
{
    s_default_rectangle_round_radius = std::clamp(radius, 0.0, 100000.0);
}

double ToolDrawRectangle::get_default_round_radius()
{
    return s_default_rectangle_round_radius;
}

ToolResponse ToolDrawRectangle::begin(const ToolArgs &args)
{
    m_wrkpl = get_workplane();
    m_intf.enable_hover_selection();
    m_lines = {nullptr};
    m_arcs = {nullptr};
    m_square = s_default_rectangle_square;
    m_rounded = s_default_rectangle_rounded;
    m_round_radius = s_default_rectangle_round_radius;
    return ToolResponse();
}

ToolBase::CanBegin ToolDrawRectangle::can_begin()
{
    return get_workplane_uuid() != UUID();
}

glm::dvec2 ToolDrawRectangle::get_cursor_pos_in_plane() const
{
    return m_wrkpl->project(get_cursor_pos_for_workplane(*m_wrkpl));
}

void ToolDrawRectangle::update_preview()
{
    if (!m_lines.front())
        return;

    glm::dvec2 pa;
    glm::dvec2 pb = get_cursor_pos_in_plane();

    if (m_mode == Mode::CORNER) {
        pa = m_first_point;
    }
    else {
        auto d = get_cursor_pos_in_plane() - m_first_point;
        pa = m_first_point - d;
    }
    if (m_square) {
        const auto d = pb - pa;
        const auto a = std::max(std::abs(d.x), std::abs(d.y));
        const auto sx = d.x >= 0 ? 1.0 : -1.0;
        const auto sy = d.y >= 0 ? 1.0 : -1.0;
        pb = pa + glm::dvec2(sx * a, sy * a);
    }
    const auto p1 = pa;
    const auto p2 = glm::dvec2(pb.x, pa.y);
    const auto p3 = pb;
    const auto p4 = glm::dvec2(pa.x, pb.y);
    if (!m_rounded || m_round_radius <= 1e-9) {
        m_lines.at(0)->m_p1 = p1;
        m_lines.at(0)->m_p2 = p2;
        m_lines.at(1)->m_p1 = p2;
        m_lines.at(1)->m_p2 = p3;
        m_lines.at(2)->m_p1 = p3;
        m_lines.at(2)->m_p2 = p4;
        m_lines.at(3)->m_p1 = p4;
        m_lines.at(3)->m_p2 = p1;
        return;
    }

    std::array<glm::dvec2, 4> vertices = {p1, p2, p3, p4};
    std::array<glm::dvec2, 4> p_in;
    std::array<glm::dvec2, 4> p_out;
    std::array<glm::dvec2, 4> c_arc;
    bool rounded_ok = true;
    for (size_t i = 0; i < 4; i++) {
        const auto &v_prev = vertices.at((i + 3) % 4);
        const auto &v = vertices.at(i);
        const auto &v_next = vertices.at((i + 1) % 4);
        const auto e_prev = v_prev - v;
        const auto e_next = v_next - v;
        const auto len_prev = glm::length(e_prev);
        const auto len_next = glm::length(e_next);
        if (len_prev <= 1e-9 || len_next <= 1e-9) {
            rounded_ok = false;
            break;
        }
        const auto u_prev = e_prev / len_prev;
        const auto u_next = e_next / len_next;
        const auto dotp = std::clamp(glm::dot(u_prev, u_next), -1.0, 1.0);
        const auto theta = std::acos(dotp);
        const auto tan_half = std::tan(theta * 0.5);
        const auto sin_half = std::sin(theta * 0.5);
        if (tan_half <= 1e-9 || sin_half <= 1e-9) {
            rounded_ok = false;
            break;
        }
        const auto t_limit = std::min(len_prev, len_next) * 0.49;
        const auto r_limit = t_limit * tan_half;
        const auto r = std::min(m_round_radius, r_limit);
        const auto t = r / tan_half;
        const auto d = r / sin_half;
        const auto bis = u_prev + u_next;
        const auto bis_len = glm::length(bis);
        if (bis_len <= 1e-9) {
            rounded_ok = false;
            break;
        }
        const auto bis_dir = bis / bis_len;
        p_in.at(i) = v + u_prev * t;
        p_out.at(i) = v + u_next * t;
        c_arc.at(i) = v + bis_dir * d;
    }

    if (!rounded_ok) {
        m_lines.at(0)->m_p1 = p1;
        m_lines.at(0)->m_p2 = p2;
        m_lines.at(1)->m_p1 = p2;
        m_lines.at(1)->m_p2 = p3;
        m_lines.at(2)->m_p1 = p3;
        m_lines.at(2)->m_p2 = p4;
        m_lines.at(3)->m_p1 = p4;
        m_lines.at(3)->m_p2 = p1;
        if (m_arcs.front()) {
            for (auto *arc : m_arcs) {
                arc->m_center = p1;
                arc->m_from = p1;
                arc->m_to = p1;
            }
        }
        return;
    }

    for (size_t i = 0; i < 4; i++) {
        m_lines.at(i)->m_p1 = p_out.at(i);
        m_lines.at(i)->m_p2 = p_in.at((i + 1) % 4);
    }
    if (m_arcs.front()) {
        for (size_t i = 0; i < 4; i++) {
            m_arcs.at(i)->m_center = c_arc.at(i);
            m_arcs.at(i)->m_from = p_in.at(i);
            m_arcs.at(i)->m_to = p_out.at(i);
            const auto a0 = std::atan2(m_arcs.at(i)->m_from.y - m_arcs.at(i)->m_center.y,
                                       m_arcs.at(i)->m_from.x - m_arcs.at(i)->m_center.x);
            const auto a1 = std::atan2(m_arcs.at(i)->m_to.y - m_arcs.at(i)->m_center.y,
                                       m_arcs.at(i)->m_to.x - m_arcs.at(i)->m_center.x);
            auto d = a1 - a0;
            while (d < 0)
                d += 2 * M_PI;
            while (d >= 2 * M_PI)
                d -= 2 * M_PI;
            // EntityArc2D is always CCW from "from" to "to", so keep only minor arc.
            if (d > M_PI)
                std::swap(m_arcs.at(i)->m_from, m_arcs.at(i)->m_to);
        }
    }
}

ToolResponse ToolDrawRectangle::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        update_preview();
        update_tip();
        set_first_update_group_current();
        return ToolResponse();
    }
    else if (args.type == ToolEventType::ACTION) {
        switch (args.action) {
        case InToolActionID::LMB: {
            if (m_lines.front()) {
                if (m_rounded) {
                    return ToolResponse::commit();
                }
                auto last_line = m_lines.back();
                size_t i = 0;
                for (auto line : m_lines) {
                    {
                        auto &constraint = add_constraint<ConstraintPointsCoincident>();
                        constraint.m_entity1 = {last_line->m_uuid, 2};
                        constraint.m_entity2 = {line->m_uuid, 1};
                        constraint.m_wrkpl = m_wrkpl->m_uuid;
                    }
                    {
                        ConstraintHV *constraint = nullptr;
                        if (i == 0 || i == 2)
                            constraint = &add_constraint<ConstraintHorizontal>();
                        else
                            constraint = &add_constraint<ConstraintVertical>();
                        constraint->m_entity1 = {line->m_uuid, 1};
                        constraint->m_entity2 = {line->m_uuid, 2};
                        constraint->m_wrkpl = m_wrkpl->m_uuid;
                    }

                    last_line = line;
                    i++;
                }
                if (m_constrain) {
                    constrain_point(m_wrkpl->m_uuid, {m_lines.at(2)->m_uuid, 1});
                }
                if (m_mode == Mode::CORNER) {
                    if (m_first_constraint)
                        constrain_point(m_first_constraint.value(), m_wrkpl->m_uuid, m_first_enp,
                                        {m_lines.at(0)->m_uuid, 1});
                }
                else {
                    auto &diagonal = add_entity<EntityLine2D>();
                    diagonal.m_wrkpl = m_wrkpl->m_uuid;
                    diagonal.m_construction = true;
                    diagonal.m_p1 = m_lines.at(0)->m_p1;
                    diagonal.m_p2 = m_lines.at(1)->m_p2;
                    {
                        auto &constraint = add_constraint<ConstraintPointsCoincident>();
                        constraint.m_entity1 = {diagonal.m_uuid, 1};
                        constraint.m_entity2 = {m_lines.at(0)->m_uuid, 1};
                        constraint.m_wrkpl = m_wrkpl->m_uuid;
                    }
                    {
                        auto &constraint = add_constraint<ConstraintPointsCoincident>();
                        constraint.m_entity1 = {diagonal.m_uuid, 2};
                        constraint.m_entity2 = {m_lines.at(1)->m_uuid, 2};
                        constraint.m_wrkpl = m_wrkpl->m_uuid;
                    }
                    if (m_first_constraint && *m_first_constraint == Constraint::Type::POINTS_COINCIDENT) {
                        auto &constraint = add_constraint<ConstraintMidpoint>();
                        constraint.m_line = diagonal.m_uuid;
                        constraint.m_point = m_first_enp;
                        constraint.m_wrkpl = m_wrkpl->m_uuid;
                    }
                    else {
                        auto &midpt = add_entity<EntityPoint2D>();
                        midpt.m_wrkpl = m_wrkpl->m_uuid;
                        midpt.m_construction = true;
                        midpt.m_p = (diagonal.m_p1 + diagonal.m_p2) / 2.;
                        {
                            auto &constraint = add_constraint<ConstraintMidpoint>();
                            constraint.m_line = diagonal.m_uuid;
                            constraint.m_point = {midpt.m_uuid, 0};
                            constraint.m_wrkpl = m_wrkpl->m_uuid;
                        }
                        if (m_first_constraint)
                            constrain_point(m_first_constraint.value(), m_wrkpl->m_uuid, m_first_enp,
                                            {midpt.m_uuid, 0});
                    }
                }
                return ToolResponse::commit();
            }
            else {
                m_square = s_default_rectangle_square;
                m_rounded = s_default_rectangle_rounded;
                m_round_radius = s_default_rectangle_round_radius;
                m_first_point = get_cursor_pos_in_plane();
                for (auto &it : m_lines) {
                    it = &add_entity<EntityLine2D>();
                    it->m_selection_invisible = true;
                    it->m_wrkpl = m_wrkpl->m_uuid;
                    it->m_p1 = m_first_point;
                    it->m_p2 = m_first_point;
                }
                if (m_rounded) {
                    for (auto &arc : m_arcs) {
                        arc = &add_entity<EntityArc2D>();
                        arc->m_selection_invisible = true;
                        arc->m_wrkpl = m_wrkpl->m_uuid;
                        arc->m_center = m_first_point;
                        arc->m_from = m_first_point;
                        arc->m_to = m_first_point;
                    }
                }

                if (m_constrain) {
                    m_first_constraint = get_constraint_type();
                    if (auto hsel = m_intf.get_hover_selection())
                        m_first_enp = hsel->get_entity_and_point();
                }

                return ToolResponse();
            }
        } break;

        case InToolActionID::TOGGLE_CONSTRUCTION: {
            if (m_lines.front()) {
                m_lines.front()->m_construction = !m_lines.front()->m_construction;
                for (auto it : m_lines) {
                    it->m_construction = m_lines.front()->m_construction;
                }
                for (auto it : m_arcs) {
                    if (it)
                        it->m_construction = m_lines.front()->m_construction;
                }
            }
        } break;

        case InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT: {
            m_constrain = !m_constrain;
        } break;

        case InToolActionID::TOGGLE_RECTANGLE_MODE: {
            if (m_mode == Mode::CENTER)
                m_mode = Mode::CORNER;
            else
                m_mode = Mode::CENTER;
            update_preview();
        } break;

        case InToolActionID::TOGGLE_RECTANGLE_SQUARE: {
            m_square = !m_square;
            set_default_square(m_square);
            update_preview();
        } break;

        case InToolActionID::TOGGLE_RECTANGLE_ROUNDED: {
            m_rounded = !m_rounded;
            set_default_rounded(m_rounded);
            if (m_lines.front()) {
                if (m_rounded) {
                    for (auto &arc : m_arcs) {
                        if (!arc) {
                            arc = &add_entity<EntityArc2D>();
                            arc->m_selection_invisible = true;
                            arc->m_wrkpl = m_wrkpl->m_uuid;
                            arc->m_construction = m_lines.front()->m_construction;
                            arc->m_center = m_first_point;
                            arc->m_from = m_first_point;
                            arc->m_to = m_first_point;
                        }
                    }
                }
                else {
                    for (auto &arc : m_arcs) {
                        if (arc) {
                            get_doc().m_entities.erase(arc->m_uuid);
                            arc = nullptr;
                        }
                    }
                }
            }
            update_preview();
        } break;

        case InToolActionID::RECTANGLE_CORNER_RADIUS_INC: {
            m_round_radius = std::clamp(m_round_radius + 0.5, 0.0, 100000.0);
            set_default_round_radius(m_round_radius);
            update_preview();
        } break;

        case InToolActionID::RECTANGLE_CORNER_RADIUS_DEC: {
            m_round_radius = std::clamp(m_round_radius - 0.5, 0.0, 100000.0);
            set_default_round_radius(m_round_radius);
            update_preview();
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

void ToolDrawRectangle::update_tip()
{
    std::vector<ActionLabelInfo> actions;

    std::string what;
    if (m_lines.front())
        what = "corner";
    else if (m_mode == Mode::CORNER)
        what = "corner";
    else
        what = "center";

    actions.emplace_back(InToolActionID::LMB, "place " + what);

    actions.emplace_back(InToolActionID::RMB, "end tool");

    if (m_lines.front()) {
        if (m_lines.front()->m_construction)
            actions.emplace_back(InToolActionID::TOGGLE_CONSTRUCTION, "normal");
        else
            actions.emplace_back(InToolActionID::TOGGLE_CONSTRUCTION, "construction");
    }

    if (m_mode == Mode::CENTER)
        actions.emplace_back(InToolActionID::TOGGLE_RECTANGLE_MODE, "from corner");
    else
        actions.emplace_back(InToolActionID::TOGGLE_RECTANGLE_MODE, "from center");

    actions.emplace_back(InToolActionID::TOGGLE_RECTANGLE_SQUARE, m_square ? "square off" : "square on");
    actions.emplace_back(InToolActionID::TOGGLE_RECTANGLE_ROUNDED, m_rounded ? "rounded off" : "rounded on");
    if (m_rounded) {
        actions.emplace_back(InToolActionID::RECTANGLE_CORNER_RADIUS_INC, InToolActionID::RECTANGLE_CORNER_RADIUS_DEC,
                             "corner radius");
    }

    if (!m_rounded) {
        if (m_constrain)
            actions.emplace_back(InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT, "constraint off");
        else
            actions.emplace_back(InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT, "constraint on");
    }


    std::string tip;
    if (m_square)
        tip += "square ";
    if (m_rounded) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << m_round_radius;
        tip += "r=" + ss.str() + " ";
    }
    m_intf.tool_bar_set_tool_tip(tip);
    std::vector<ConstraintType> constraint_icons;

    if (m_constrain && !m_rounded) {
        set_constrain_tip(what);
        update_constraint_icons(constraint_icons);
    }
    m_intf.tool_bar_set_actions(actions);
    m_intf.set_constraint_icons(get_cursor_pos_for_workplane(*m_wrkpl), m_wrkpl->transform_relative({1, 1}),
                                constraint_icons);
}
} // namespace dune3d

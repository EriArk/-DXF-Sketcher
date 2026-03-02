#include "tool_draw_regular_polygon.hpp"
#include "document/document.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "document/constraint/constraint_point_on_line.hpp"
#include "document/constraint/constraint_point_on_circle.hpp"
#include "document/constraint/constraint_equal_length.hpp"
#include "editor/editor_interface.hpp"
#include "util/selection_util.hpp"
#include "util/action_label.hpp"
#include "tool_common_impl.hpp"
#include "dialogs/dialogs.hpp"
#include "dialogs/enter_datum_window.hpp"
#include <glm/gtx/rotate_vector.hpp>
#include <iostream>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace dune3d {
namespace {
unsigned int s_default_polygon_sides = 6;
bool s_default_polygon_rounded = false;
double s_default_polygon_round_radius = 1.0;
}

void ToolDrawRegularPolygon::set_default_sides(unsigned int n)
{
    s_default_polygon_sides = std::clamp(n, 3u, 64u);
}

unsigned int ToolDrawRegularPolygon::get_default_sides()
{
    return s_default_polygon_sides;
}

void ToolDrawRegularPolygon::set_default_rounded(bool rounded)
{
    s_default_polygon_rounded = rounded;
}

bool ToolDrawRegularPolygon::get_default_rounded()
{
    return s_default_polygon_rounded;
}

void ToolDrawRegularPolygon::set_default_round_radius(double radius)
{
    s_default_polygon_round_radius = std::clamp(radius, 0.0, 100000.0);
}

double ToolDrawRegularPolygon::get_default_round_radius()
{
    return s_default_polygon_round_radius;
}

ToolResponse ToolDrawRegularPolygon::begin(const ToolArgs &args)
{
    m_wrkpl = get_workplane();
    m_rounded = s_default_polygon_rounded;
    m_round_radius = s_default_polygon_round_radius;
    m_intf.enable_hover_selection();
    return ToolResponse();
}

ToolBase::CanBegin ToolDrawRegularPolygon::can_begin()
{
    return get_workplane_uuid() != UUID();
}

glm::dvec2 ToolDrawRegularPolygon::get_cursor_pos_in_plane() const
{
    return m_wrkpl->project(get_cursor_pos_for_workplane(*m_wrkpl));
}

void ToolDrawRegularPolygon::set_n_sides(unsigned int n)
{
    set_default_sides(n);
    for (auto it : m_sides) {
        get_doc().m_entities.erase(it->m_uuid);
    }
    for (auto it : m_corner_arcs) {
        get_doc().m_entities.erase(it->m_uuid);
    }
    m_sides.clear();
    m_corner_arcs.clear();
    for (unsigned int i = 0; i < n; i++) {
        auto &it = add_entity<EntityLine2D>();
        it.m_wrkpl = m_wrkpl->m_uuid;
        it.m_selection_invisible = true;
        m_sides.push_back(&it);
        if (m_rounded) {
            auto &arc = add_entity<EntityArc2D>();
            arc.m_wrkpl = m_wrkpl->m_uuid;
            arc.m_selection_invisible = true;
            m_corner_arcs.push_back(&arc);
        }
    }
    update_sides(get_cursor_pos_in_plane());
    update_tip();
}

void ToolDrawRegularPolygon::update_sides(const glm::dvec2 &p)
{
    if (!m_temp_circle)
        return;
    const auto center = m_temp_circle->m_center;
    const auto vstart = p - center;
    m_temp_circle->m_radius = glm::length(vstart);

    std::vector<glm::dvec2> vertices;
    vertices.reserve(m_sides.size());
    const auto phidelta = M_PI * 2. / m_sides.size();
    for (unsigned int i = 0; i < m_sides.size(); i++) {
        vertices.push_back(center + glm::rotate(vstart, phidelta * i));
    }

    if (!m_rounded || m_round_radius <= 1e-9 || m_corner_arcs.size() != m_sides.size()) {
        for (unsigned int i = 0; i < m_sides.size(); i++) {
            const auto pos1 = vertices.at(i);
            const auto pos2 = vertices.at((i + 1) % m_sides.size());
            m_sides.at(i)->m_p1 = pos1;
            m_sides.at(i)->m_p2 = pos2;
        }
        return;
    }

    const auto n = m_sides.size();
    std::vector<glm::dvec2> p_in(n), p_out(n), c_arc(n);
    bool rounded_ok = true;
    for (size_t i = 0; i < n; i++) {
        const auto &v_prev = vertices.at((i + n - 1) % n);
        const auto &v = vertices.at(i);
        const auto &v_next = vertices.at((i + 1) % n);
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
        for (unsigned int i = 0; i < m_sides.size(); i++) {
            const auto pos1 = vertices.at(i);
            const auto pos2 = vertices.at((i + 1) % m_sides.size());
            m_sides.at(i)->m_p1 = pos1;
            m_sides.at(i)->m_p2 = pos2;
        }
        for (auto *arc : m_corner_arcs) {
            arc->m_center = center;
            arc->m_from = center;
            arc->m_to = center;
        }
        return;
    }

    for (size_t i = 0; i < n; i++) {
        m_sides.at(i)->m_p1 = p_out.at(i);
        m_sides.at(i)->m_p2 = p_in.at((i + 1) % n);
        auto *arc = m_corner_arcs.at(i);
        arc->m_center = c_arc.at(i);
        arc->m_from = p_in.at(i);
        arc->m_to = p_out.at(i);
    }
}

ToolResponse ToolDrawRegularPolygon::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        if (m_temp_circle) {
            m_temp_circle->m_radius = glm::length(get_cursor_pos_in_plane() - m_temp_circle->m_center);
            update_sides(get_cursor_pos_in_plane());
        }

        set_first_update_group_current();
        update_tip();
        return ToolResponse();
    }
    else if (args.type == ToolEventType::ACTION) {
        switch (args.action) {
        case InToolActionID::LMB: {
            if (m_temp_circle) {
                if (m_rounded) {
                    get_doc().m_entities.erase(m_temp_circle->m_uuid);
                    m_temp_circle = nullptr;
                    return ToolResponse::commit();
                }
                auto last_line = m_sides.back();
                for (auto line : m_sides) {
                    {
                        auto &constraint = add_constraint<ConstraintPointOnCircle>();
                        constraint.m_circle = m_temp_circle->m_uuid;
                        constraint.m_point = {line->m_uuid, 1};
                        constraint.m_modify_to_satisfy = true;
                    }
                    if (line != m_sides.front()) {
                        auto &constraint = add_constraint<ConstraintEqualLength>();
                        constraint.m_entity1 = m_sides.front()->m_uuid;
                        constraint.m_entity2 = line->m_uuid;
                        constraint.m_wrkpl = m_wrkpl->m_uuid;
                        constraint.m_modify_to_satisfy = true;
                    }
                    {
                        auto &constraint = add_constraint<ConstraintPointsCoincident>();
                        constraint.m_entity1 = {last_line->m_uuid, 2};
                        constraint.m_entity2 = {line->m_uuid, 1};
                        constraint.m_wrkpl = m_wrkpl->m_uuid;
                    }
                    last_line = line;
                }

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
                return ToolResponse::commit();
            }
            else {
                m_rounded = s_default_polygon_rounded;
                m_round_radius = s_default_polygon_round_radius;
                m_temp_circle = &add_entity<EntityCircle2D>();
                m_temp_circle->m_selection_invisible = true;
                m_temp_circle->m_construction = true;
                m_temp_circle->m_radius = 0;
                m_temp_circle->m_center = get_cursor_pos_in_plane();
                m_temp_circle->m_wrkpl = m_wrkpl->m_uuid;
                set_n_sides(s_default_polygon_sides);

                if (m_constrain && !m_rounded) {
                    const EntityAndPoint circle_center{m_temp_circle->m_uuid, 1};
                    constrain_point(m_wrkpl->m_uuid, circle_center);
                }

                return ToolResponse();
            }
        } break;

        case InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT: {
            m_constrain = !m_constrain;
        } break;

        case InToolActionID::N_SIDES_DEC: {
            if (!m_win) {
                if (m_temp_circle) {
                    if (m_sides.size() > 3)
                        set_n_sides(m_sides.size() - 1);
                }
                else {
                    set_default_sides(std::max(3u, get_default_sides() - 1));
                }
            }

        } break;

        case InToolActionID::N_SIDES_INC: {
            if (!m_win) {
                if (m_temp_circle)
                    set_n_sides(m_sides.size() + 1);
                else
                    set_default_sides(get_default_sides() + 1);
            }
        } break;

        case InToolActionID::TOGGLE_POLYGON_ROUNDED: {
            m_rounded = !m_rounded;
            set_default_rounded(m_rounded);
            if (m_temp_circle)
                set_n_sides(m_sides.size());
        } break;

        case InToolActionID::POLYGON_CORNER_RADIUS_INC: {
            m_round_radius = std::clamp(m_round_radius + 0.5, 0.0, 100000.0);
            set_default_round_radius(m_round_radius);
            if (m_temp_circle)
                update_sides(get_cursor_pos_in_plane());
        } break;

        case InToolActionID::POLYGON_CORNER_RADIUS_DEC: {
            m_round_radius = std::clamp(m_round_radius - 0.5, 0.0, 100000.0);
            set_default_round_radius(m_round_radius);
            if (m_temp_circle)
                update_sides(get_cursor_pos_in_plane());
        } break;

        case InToolActionID::ENTER_N_SIDES: {
            const auto current_sides = m_temp_circle ? static_cast<unsigned int>(m_sides.size()) : get_default_sides();
            m_last_sides = current_sides;
            m_win = m_intf.get_dialogs().show_enter_datum_window("Enter sides", DatumUnit::INTEGER, current_sides);
            m_win->set_range(3, 30);
            m_win->set_step_size(1);
        } break;

        case InToolActionID::RMB:
        case InToolActionID::CANCEL:
            return ToolResponse::revert();

        default:;
        }
        update_tip();
    }
    else if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataWindow *>(args.data.get())) {
            if (data->event == ToolDataWindow::Event::UPDATE) {
                if (auto d = dynamic_cast<const ToolDataEnterDatumWindow *>(args.data.get())) {
                    if (m_temp_circle)
                        set_n_sides(d->value);
                    else
                        set_default_sides(d->value);
                }
            }
            else if (data->event == ToolDataWindow::Event::OK) {
                m_win->close();
                m_win = nullptr;
                update_tip();
            }
            else if (data->event == ToolDataWindow::Event::CLOSE) {
                if (m_win) {
                    if (m_temp_circle)
                        set_n_sides(m_last_sides);
                    else
                        set_default_sides(m_last_sides);
                }
                m_win = nullptr;
                update_tip();
            }
        }
    }

    return ToolResponse();
}

void ToolDrawRegularPolygon::update_tip()
{
    std::vector<ActionLabelInfo> actions;

    if (m_temp_circle)
        actions.emplace_back(InToolActionID::LMB, "place radius");
    else
        actions.emplace_back(InToolActionID::LMB, "place center");

    actions.emplace_back(InToolActionID::RMB, "end tool");


    if (!m_rounded) {
        if (m_constrain)
            actions.emplace_back(InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT, "constraint off");
        else
            actions.emplace_back(InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT, "constraint on");
    }

    actions.emplace_back(InToolActionID::ENTER_N_SIDES);
    if (!m_win)
        actions.emplace_back(InToolActionID::N_SIDES_INC, InToolActionID::N_SIDES_DEC, "sides");
    actions.emplace_back(InToolActionID::TOGGLE_POLYGON_ROUNDED, m_rounded ? "rounded off" : "rounded on");
    if (m_rounded) {
        actions.emplace_back(InToolActionID::POLYGON_CORNER_RADIUS_INC, InToolActionID::POLYGON_CORNER_RADIUS_DEC,
                             "corner radius");
    }
    std::vector<ConstraintType> constraint_icons;
    glm::vec3 v = {NAN, NAN, NAN};
    const auto side_count = m_temp_circle ? static_cast<unsigned int>(m_sides.size()) : get_default_sides();
    auto tip = std::to_string(side_count) + " sides";
    if (m_rounded) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << m_round_radius;
        tip += ", r=" + ss.str();
    }
    tip += " ";
    if (m_constrain && !m_rounded && !m_temp_circle) {
        tip += get_constrain_tip("center");
        update_constraint_icons(constraint_icons);
    }
    if (m_constrain && !m_rounded && m_temp_circle) {
        if (auto hsel = m_intf.get_hover_selection()) {
            if (hsel->type == SelectableRef::Type::ENTITY) {
                const auto enp = hsel->get_entity_and_point();
                if (get_doc().is_valid_point(enp)) {
                    const auto r = get_cursor_pos_in_plane() - m_temp_circle->m_center;
                    v = m_wrkpl->transform_relative({-r.y, r.x});
                    constraint_icons.push_back(Constraint::Type::POINT_ON_CIRCLE);
                    tip += " constrain radius on point";
                }
            }
        }
    }
    m_intf.tool_bar_set_tool_tip(tip);
    m_intf.tool_bar_set_actions(actions);
    m_intf.set_constraint_icons(get_cursor_pos_for_workplane(*m_wrkpl), v, constraint_icons);
}
} // namespace dune3d

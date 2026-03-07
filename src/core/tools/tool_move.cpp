#include "tool_move.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/entity/ientity_movable2d.hpp"
#include "document/entity/ientity_movable3d.hpp"
#include "document/entity/ientity_movable2d_initial_pos.hpp"
#include "document/entity/ientity_in_workplane.hpp"
#include "document/entity/ientity_bounding_box2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/constraint/iconstraint_workplane.hpp"
#include "document/constraint/iconstraint_movable.hpp"
#include "document/constraint/constraint_angle.hpp"
#include "editor/editor_interface.hpp"
#include "tool_common_impl.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace dune3d {
namespace {
constexpr double c_selection_snap_threshold = 2.0;
constexpr double c_selection_snap_guide_pad = 6.0;

struct AxisSnapMatch {
    double offset = 0;
    double source = 0;
    double target = 0;
};

void add_bbox_axis_targets(std::vector<double> &targets, double min_v, double max_v)
{
    targets.push_back(min_v);
    targets.push_back((min_v + max_v) * .5);
    targets.push_back(max_v);
}

void sort_unique_targets(std::vector<double> &targets)
{
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end(), [](double a, double b) { return std::abs(a - b) < 1e-9; }),
                  targets.end());
}

std::optional<AxisSnapMatch> find_axis_snap_offset(const std::array<double, 3> &source_axis,
                                                   const std::vector<double> &targets_axis, double delta_axis)
{
    if (targets_axis.empty())
        return {};
    AxisSnapMatch best_match;
    double best_distance = c_selection_snap_threshold + 1e-9;
    bool found = false;
    for (const auto source : source_axis) {
        const auto moved = source + delta_axis;
        for (const auto target : targets_axis) {
            const auto offset = target - moved;
            const auto distance = std::abs(offset);
            if (distance < best_distance) {
                best_distance = distance;
                best_match.offset = offset;
                best_match.source = source;
                best_match.target = target;
                found = true;
            }
        }
    }
    if (!found)
        return {};
    return best_match;
}

bool try_get_entity_bbox_2d(const Entity &entity, UUID &wrkpl_uu, std::pair<glm::dvec2, glm::dvec2> &bbox)
{
    const auto *en_workplane = dynamic_cast<const IEntityInWorkplane *>(&entity);
    const auto *en_bbox = dynamic_cast<const IEntityBoundingBox2D *>(&entity);
    if (!en_workplane || !en_bbox)
        return false;
    wrkpl_uu = en_workplane->get_workplane();
    bbox = en_bbox->get_bbox();
    return std::isfinite(bbox.first.x) && std::isfinite(bbox.first.y) && std::isfinite(bbox.second.x)
            && std::isfinite(bbox.second.y);
}
} // namespace

ToolBase::CanBegin ToolMove::can_begin()
{
    for (const auto &sr : m_selection) {
        if (sr.type == SelectableRef::Type::ENTITY) {
            auto &entity = get_entity(sr.item);
            if (entity.can_move(get_doc()))
                return true;
        }
        else if (sr.type == SelectableRef::Type::CONSTRAINT) {
            auto &constr = get_doc().get_constraint(sr.item);
            if (dynamic_cast<const IConstraintMovable *>(&constr))
                return true;
        }
    }
    return false;
}

ToolResponse ToolMove::begin(const ToolArgs &args)
{
    auto &doc = get_doc();
    m_selection_snap_enabled = m_intf.get_selection_snap_enabled();
    m_snap_data_by_workplane.clear();
    m_intf.set_selection_snap_overlay_lines({});
    m_inital_pos = m_intf.get_cursor_pos();
    for (const auto &[uu, en] : doc.m_entities) {
        if (en->get_type() == Entity::Type::WORKPLANE) {
            auto &wrkpl = dynamic_cast<const EntityWorkplane &>(*en);
            m_inital_pos_wrkpl.emplace(uu, wrkpl.project(get_cursor_pos_for_workplane(wrkpl)));
        }
    }
    const Group *first_group = nullptr;
    const Group *first_group_render = nullptr;
    for (const auto &sr : m_selection) {
        if (sr.type == SelectableRef::Type::ENTITY) {
            auto entity = &get_entity(sr.item);
            auto point = sr.point;
            while (entity->m_move_instead.contains(point)) {
                auto &enp = entity->m_move_instead.at(point);
                entity = &get_entity(enp.entity);
                point = enp.point;
            }
            get_doc().accumulate_first_group(first_group, entity->m_group);
            m_entities.emplace(entity, point);
        }
        else if (sr.type == SelectableRef::Type::CONSTRAINT) {
            if (auto constraint = get_doc().get_constraint_ptr<ConstraintLinesAngle>(sr.item)) {
                if (!constraint->m_wrkpl) {
                    auto vecs = constraint->get_vectors(get_doc());
                    m_inital_pos_angle_constraint.emplace(
                            sr.item, m_intf.get_cursor_pos_for_plane(constraint->get_origin(get_doc()), vecs.n));
                }
            }
            get_doc().accumulate_first_group(first_group_render, get_doc().get_constraint(sr.item).m_group);
        }
        // we don't care about constraints since dragging them is pureley cosmetic
    }

    if (first_group) {
        get_doc().accumulate_first_group(first_group_render, first_group->m_uuid);
        m_first_group = first_group->m_uuid;
    }
    if (first_group_render)
        m_first_group_render = first_group_render->m_uuid;

    m_dragged_list.clear();
    for (auto [entity, point] : m_entities) {
        m_dragged_list.emplace_back(entity->m_uuid, point);
    }

    if (m_selection_snap_enabled) {
        std::set<UUID> selected_entities;
        std::map<UUID, std::pair<glm::dvec2, glm::dvec2>> selected_bbox_by_workplane;
        for (const auto &sr : m_selection) {
            if (sr.type != SelectableRef::Type::ENTITY)
                continue;
            selected_entities.insert(sr.item);
            const auto *entity = doc.get_entity_ptr(sr.item);
            if (!entity)
                continue;
            UUID wrkpl_uu;
            std::pair<glm::dvec2, glm::dvec2> bbox;
            if (!try_get_entity_bbox_2d(*entity, wrkpl_uu, bbox))
                continue;
            auto [it, inserted] = selected_bbox_by_workplane.emplace(wrkpl_uu, bbox);
            if (!inserted) {
                auto &[bmin, bmax] = it->second;
                bmin.x = std::min(bmin.x, bbox.first.x);
                bmin.y = std::min(bmin.y, bbox.first.y);
                bmax.x = std::max(bmax.x, bbox.second.x);
                bmax.y = std::max(bmax.y, bbox.second.y);
            }
        }

        for (const auto &[wrkpl_uu, bbox] : selected_bbox_by_workplane) {
            SnapData2D snap_data;
            snap_data.source_x = {bbox.first.x, (bbox.first.x + bbox.second.x) * .5, bbox.second.x};
            snap_data.source_y = {bbox.first.y, (bbox.first.y + bbox.second.y) * .5, bbox.second.y};
            m_snap_data_by_workplane.emplace(wrkpl_uu, std::move(snap_data));
        }

        for (const auto &[entity_uu, entity] : doc.m_entities) {
            if (!entity->m_visible || selected_entities.contains(entity_uu))
                continue;
            UUID wrkpl_uu;
            std::pair<glm::dvec2, glm::dvec2> bbox;
            if (!try_get_entity_bbox_2d(*entity, wrkpl_uu, bbox))
                continue;
            auto it = m_snap_data_by_workplane.find(wrkpl_uu);
            if (it == m_snap_data_by_workplane.end())
                continue;
            auto &snap_data = it->second;
            add_bbox_axis_targets(snap_data.targets_x, bbox.first.x, bbox.second.x);
            add_bbox_axis_targets(snap_data.targets_y, bbox.first.y, bbox.second.y);

            if (const auto *line = dynamic_cast<const EntityLine2D *>(entity.get())) {
                if (std::abs(line->m_p1.y - line->m_p2.y) < 1e-8)
                    snap_data.targets_y.push_back(line->m_p1.y);
                if (std::abs(line->m_p1.x - line->m_p2.x) < 1e-8)
                    snap_data.targets_x.push_back(line->m_p1.x);
            }
        }

        if (const auto tpl = m_intf.get_selection_snap_template_info()) {
            if (const auto it = m_snap_data_by_workplane.find(tpl->workplane); it != m_snap_data_by_workplane.end()) {
                auto &snap_data = it->second;
                const auto segment_w = tpl->width / static_cast<double>(std::max(tpl->segments, 1));
                for (int i = 0; i <= tpl->segments; i++)
                    snap_data.targets_x.push_back(segment_w * static_cast<double>(i));
                for (int i = 0; i < tpl->segments; i++)
                    snap_data.targets_x.push_back(segment_w * (static_cast<double>(i) + .5));

                // Also allow vertical placement against template bounds/center.
                snap_data.targets_y.push_back(0.0);
                snap_data.targets_y.push_back(tpl->height * .5);
                snap_data.targets_y.push_back(tpl->height);
            }
        }

        for (auto &[wrkpl_uu, snap_data] : m_snap_data_by_workplane) {
            sort_unique_targets(snap_data.targets_x);
            sort_unique_targets(snap_data.targets_y);
        }
    }

    return ToolResponse();
}


ToolResponse ToolMove::update(const ToolArgs &args)
{
    auto &doc = get_doc();
    auto &last_doc = m_core.get_current_last_document();
    if (args.type == ToolEventType::MOVE) {
        const auto delta = m_intf.get_cursor_pos() - m_inital_pos;
        std::map<UUID, glm::dvec2> delta2d_by_workplane;
        std::vector<std::pair<glm::dvec3, glm::dvec3>> snap_overlay_lines_world;
        auto get_delta2d_for_workplane =
                [this, &delta2d_by_workplane, &snap_overlay_lines_world](const EntityWorkplane &wrkpl) -> glm::dvec2 {
            if (const auto it = delta2d_by_workplane.find(wrkpl.m_uuid); it != delta2d_by_workplane.end())
                return it->second;

            auto delta2d = wrkpl.project(get_cursor_pos_for_workplane(wrkpl)) - m_inital_pos_wrkpl.at(wrkpl.m_uuid);
            if (m_selection_snap_enabled) {
                if (const auto it_snap = m_snap_data_by_workplane.find(wrkpl.m_uuid);
                    it_snap != m_snap_data_by_workplane.end()) {
                    const auto &snap_data = it_snap->second;
                    const auto snap_x = find_axis_snap_offset(snap_data.source_x, snap_data.targets_x, delta2d.x);
                    const auto snap_y = find_axis_snap_offset(snap_data.source_y, snap_data.targets_y, delta2d.y);
                    if (snap_x)
                        delta2d.x += snap_x->offset;
                    if (snap_y)
                        delta2d.y += snap_y->offset;

                    const auto min_x = snap_data.source_x[0] + delta2d.x;
                    const auto max_x = snap_data.source_x[2] + delta2d.x;
                    const auto min_y = snap_data.source_y[0] + delta2d.y;
                    const auto max_y = snap_data.source_y[2] + delta2d.y;
                    if (snap_x) {
                        const auto x = snap_x->target;
                        snap_overlay_lines_world.emplace_back(
                                wrkpl.transform({x, min_y - c_selection_snap_guide_pad}),
                                wrkpl.transform({x, max_y + c_selection_snap_guide_pad}));
                    }
                    if (snap_y) {
                        const auto y = snap_y->target;
                        snap_overlay_lines_world.emplace_back(
                                wrkpl.transform({min_x - c_selection_snap_guide_pad, y}),
                                wrkpl.transform({max_x + c_selection_snap_guide_pad, y}));
                    }
                }
            }
            delta2d_by_workplane.emplace(wrkpl.m_uuid, delta2d);
            return delta2d;
        };

        for (auto [entity, point] : m_entities) {
            if (!entity->can_move(doc))
                continue;
            if (auto en_movable = dynamic_cast<IEntityMovable2D *>(entity)) {
                const auto &wrkpl =
                        get_entity<EntityWorkplane>(dynamic_cast<const IEntityInWorkplane &>(*entity).get_workplane());
                const auto delta2d = get_delta2d_for_workplane(wrkpl);
                auto &en_last = *last_doc.m_entities.at(entity->m_uuid);
                en_movable->move(en_last, delta2d, point);
            }
            else if (auto en_movable3d = dynamic_cast<IEntityMovable3D *>(entity)) {
                auto &en_last = *last_doc.m_entities.at(entity->m_uuid);
                en_movable3d->move(en_last, delta, point);
            }
            else if (auto en_movable_initial_pos = dynamic_cast<IEntityMovable2DIntialPos *>(entity)) {
                const auto &wrkpl =
                        get_entity<EntityWorkplane>(dynamic_cast<const IEntityInWorkplane &>(*entity).get_workplane());
                auto &en_last = *last_doc.m_entities.at(entity->m_uuid);
                const auto delta2d = get_delta2d_for_workplane(wrkpl);
                en_movable_initial_pos->move(en_last, m_inital_pos_wrkpl.at(wrkpl.m_uuid),
                                             m_inital_pos_wrkpl.at(wrkpl.m_uuid) + delta2d, point);
            }
        }

        doc.set_group_solve_pending(m_first_group);
        m_core.solve_current(m_dragged_list);

        for (auto sr : m_selection) {
            if (sr.type == SelectableRef::Type::CONSTRAINT) {
                auto constraint = doc.m_constraints.at(sr.item).get();
                auto co_wrkpl = dynamic_cast<const IConstraintWorkplane *>(constraint);
                auto co_movable = dynamic_cast<IConstraintMovable *>(constraint);
                if (co_movable) {
                    auto cdelta = delta;
                    glm::dvec2 delta2d;
                    if (co_wrkpl) {
                        const auto wrkpl_uu = co_wrkpl->get_workplane(get_doc());
                        if (wrkpl_uu) {
                            auto &wrkpl = get_entity<EntityWorkplane>(wrkpl_uu);
                            delta2d = get_delta2d_for_workplane(wrkpl);
                            cdelta = wrkpl.transform_relative(delta2d);
                        }
                    }
                    auto &co_last = dynamic_cast<const IConstraintMovable &>(*last_doc.m_constraints.at(sr.item));
                    const auto odelta = (co_movable->get_origin(doc) - co_last.get_origin(last_doc));
                    if (co_movable->offset_is_in_workplane())
                        co_movable->set_offset(co_last.get_offset() + glm::dvec3(delta2d, 0) - odelta);
                    else
                        co_movable->set_offset(co_last.get_offset() + cdelta - odelta);
                }
            }
        }

        m_intf.set_selection_snap_overlay_lines(snap_overlay_lines_world);
        m_intf.set_first_update_group(m_first_group_render);
        return ToolResponse();
    }
    else if (args.type == ToolEventType::ACTION) {
        m_intf.set_selection_snap_overlay_lines({});
        switch (args.action) {
        case InToolActionID::LMB:
            return ToolResponse::commit();
            break;

        case InToolActionID::RMB:
        case InToolActionID::CANCEL:
            return ToolResponse::revert();

        default:;
        }
    }

    return ToolResponse();
}
} // namespace dune3d

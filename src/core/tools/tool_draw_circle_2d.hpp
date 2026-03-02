#pragma once
#include "tool_helper_constrain.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include <optional>
#include <array>

namespace dune3d {


class ToolDrawCircle2D : public virtual ToolCommon, public ToolHelperConstrain {
public:
    using ToolCommon::ToolCommon;

    static void set_default_oval_mode(bool enabled);
    static bool get_default_oval_mode();
    static void set_default_span_degrees(double degrees);
    static double get_default_span_degrees();

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    std::set<InToolActionID> get_actions() const override
    {
        using I = InToolActionID;
        auto actions = std::set<InToolActionID>{
                I::LMB,
                I::CANCEL,
                I::RMB,
                I::TOGGLE_CONSTRUCTION,
                I::TOGGLE_COINCIDENT_CONSTRAINT,
                I::TOGGLE_CIRCLE_OVAL,
                I::TOGGLE_CIRCLE_SLICE,
        };
        if (is_sector_mode()) {
            actions.insert(I::CIRCLE_SPAN_INC);
            actions.insert(I::CIRCLE_SPAN_DEC);
        }
        return actions;
    }

    CanBegin can_begin() override;


private:
    class EntityCircle2D *m_temp_circle = nullptr;
    class EntityArc2D *m_temp_arc = nullptr;
    std::array<class EntityLine2D *, 2> m_temp_sector_lines = {nullptr, nullptr};
    std::array<class EntityBezier2D *, 4> m_temp_oval = {nullptr, nullptr, nullptr, nullptr};
    unsigned int m_temp_oval_segments = 0;
    const class EntityWorkplane *m_wrkpl = nullptr;
    bool m_oval_mode = false;
    double m_span_degrees = 360.0;
    glm::dvec2 m_center = {0, 0};

    void update_tip();
    void update_oval_preview(const glm::dvec2 &cursor);
    void update_arc_preview(const glm::dvec2 &cursor);
    bool has_temp_oval() const;
    bool has_temp_entity() const;
    bool is_sector_mode() const;
    double get_arc_span_radians() const;

    glm::dvec2 get_cursor_pos_in_plane() const;
    bool m_constrain = true;
};
} // namespace dune3d

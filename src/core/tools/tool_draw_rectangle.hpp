#pragma once
#include "tool_helper_constrain.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include <optional>
#include <array>

namespace dune3d {

class ToolDrawRectangle : public virtual ToolCommon, public ToolHelperConstrain {
public:
    using ToolCommon::ToolCommon;

    static void set_default_square(bool square);
    static bool get_default_square();
    static void set_default_rounded(bool rounded);
    static bool get_default_rounded();
    static void set_default_round_radius(double radius);
    static double get_default_round_radius();

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    std::set<InToolActionID> get_actions() const override
    {
        using I = InToolActionID;
        return {
                I::LMB,
                I::CANCEL,
                I::RMB,
                I::TOGGLE_CONSTRUCTION,
                I::TOGGLE_COINCIDENT_CONSTRAINT,
                I::TOGGLE_RECTANGLE_MODE,
                I::TOGGLE_RECTANGLE_SQUARE,
                I::TOGGLE_RECTANGLE_ROUNDED,
                I::RECTANGLE_CORNER_RADIUS_INC,
                I::RECTANGLE_CORNER_RADIUS_DEC,
        };
    }

    CanBegin can_begin() override;


private:
    std::array<class EntityLine2D *, 4> m_lines;
    std::array<class EntityArc2D *, 4> m_arcs;
    const class EntityWorkplane *m_wrkpl = nullptr;

    glm::dvec2 m_first_point;
    enum class Mode { CENTER, CORNER };
    Mode m_mode = Mode::CORNER;
    std::optional<ConstraintType> m_first_constraint;
    EntityAndPoint m_first_enp;
    bool m_square = false;
    bool m_rounded = false;
    double m_round_radius = 1.0;
    void update_preview();

    void update_tip();

    glm::dvec2 get_cursor_pos_in_plane() const;
    bool m_constrain = true;
};
} // namespace dune3d

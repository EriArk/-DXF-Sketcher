#pragma once
#include "tool_helper_constrain.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include <optional>

namespace dune3d {


class ToolDrawRegularPolygon : public virtual ToolCommon, public ToolHelperConstrain {
public:
    using ToolCommon::ToolCommon;

    static void set_default_sides(unsigned int n);
    static unsigned int get_default_sides();
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
                I::TOGGLE_COINCIDENT_CONSTRAINT,
                I::N_SIDES_INC,
                I::N_SIDES_DEC,
                I::ENTER_N_SIDES,
                I::TOGGLE_POLYGON_ROUNDED,
                I::POLYGON_CORNER_RADIUS_INC,
                I::POLYGON_CORNER_RADIUS_DEC,
        };
    }

    CanBegin can_begin() override;


private:
    class EntityCircle2D *m_temp_circle = nullptr;
    std::vector<class EntityLine2D *> m_sides;
    std::vector<class EntityArc2D *> m_corner_arcs;
    void set_n_sides(unsigned int n);
    void update_sides(const glm::dvec2 &p);
    unsigned int m_last_sides = 6;
    const class EntityWorkplane *m_wrkpl = nullptr;
    class EnterDatumWindow *m_win = nullptr;
    bool m_rounded = false;
    double m_round_radius = 1.0;


    void update_tip();

    glm::dvec2 get_cursor_pos_in_plane() const;
    bool m_constrain = true;
};
} // namespace dune3d

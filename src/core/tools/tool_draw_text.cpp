#include "tool_draw_text.hpp"
#include "document/document.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_text.hpp"
#include "util/text_render.hpp"
#include "document/group/group.hpp"
#include "editor/editor_interface.hpp"
#include "util/action_label.hpp"
#include "util/selection_util.hpp"
#include "tool_common_impl.hpp"
#include "dialogs/dialogs.hpp"
#include "dialogs/enter_text_window.hpp"
#include <pangomm/fontdescription.h>

namespace dune3d {
namespace {
std::string s_default_text_font = "sans 10";
std::string s_default_text_font_features;

Pango::FontDescription get_default_text_font_desc()
{
    return Pango::FontDescription(s_default_text_font);
}
}

void ToolDrawText::set_default_font(const std::string &font)
{
    s_default_text_font = font;
}

const std::string &ToolDrawText::get_default_font()
{
    return s_default_text_font;
}

void ToolDrawText::set_default_font_features(const std::string &features)
{
    s_default_text_font_features = features;
}

const std::string &ToolDrawText::get_default_font_features()
{
    return s_default_text_font_features;
}


ToolBase::CanBegin ToolDrawText::can_begin()
{
    return get_workplane_uuid() != UUID();
}

ToolResponse ToolDrawText::begin(const ToolArgs &args)
{
    m_win = m_intf.get_dialogs().show_enter_text_window("Enter text", "text");
    m_wrkpl = get_workplane();

    return ToolResponse{};
}

ToolResponse ToolDrawText::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataWindow *>(args.data.get())) {
            if (data->event == ToolDataWindow::Event::OK) {
                m_entity = &add_entity<EntityText>();
                m_entity->m_text = m_win->get_text();
                m_entity->m_font = s_default_text_font;
                m_entity->m_font_features = s_default_text_font_features;
                m_entity->m_wrkpl = get_workplane_uuid();
                m_entity->m_origin = get_cursor_pos_for_workplane(*m_wrkpl);
                m_entity->m_selection_invisible = true;

                render_text(*m_entity, m_intf.get_pango_context(), get_doc());
                m_win->close();
                update_tip();
                m_intf.enable_hover_selection();
            }
            else if (data->event == ToolDataWindow::Event::CLOSE) {
                if (!m_entity)
                    return ToolResponse::end();
            }
        }
    }
    else if (args.type == ToolEventType::MOVE) {
        if (m_entity) {
            m_entity->m_origin = m_wrkpl->project(get_cursor_pos_for_workplane(*m_wrkpl));
            update_tip();
        }

        set_first_update_group_current();
        return ToolResponse();
    }
    else if (args.type == ToolEventType::ACTION) {
        switch (args.action) {
        case InToolActionID::LMB:
            if (m_entity) {
                m_entity->m_selection_invisible = false;
                if (m_constrain) {
                    const EntityAndPoint text_origin{m_entity->m_uuid, 1};
                    constrain_point(m_wrkpl->m_uuid, text_origin);
                }

                set_current_group_generate_pending();

                return ToolResponse::commit();
            }
            break;

        case InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT: {
            m_constrain = !m_constrain;
            update_tip();
        } break;

        case InToolActionID::TOGGLE_TEXT_BOLD: {
            auto desc = get_default_text_font_desc();
            const bool is_bold = desc.get_weight() >= Pango::Weight::BOLD;
            desc.set_weight(is_bold ? Pango::Weight::NORMAL : Pango::Weight::BOLD);
            s_default_text_font = desc.to_string();
            if (m_entity) {
                m_entity->m_font = s_default_text_font;
                render_text(*m_entity, m_intf.get_pango_context(), get_doc());
            }
            update_tip();
        } break;

        case InToolActionID::TOGGLE_TEXT_ITALIC: {
            auto desc = get_default_text_font_desc();
            const auto style = desc.get_style();
            const bool is_italic = style == Pango::Style::ITALIC || style == Pango::Style::OBLIQUE;
            desc.set_style(is_italic ? Pango::Style::NORMAL : Pango::Style::ITALIC);
            s_default_text_font = desc.to_string();
            if (m_entity) {
                m_entity->m_font = s_default_text_font;
                render_text(*m_entity, m_intf.get_pango_context(), get_doc());
            }
            update_tip();
        } break;

        case InToolActionID::RMB:
        case InToolActionID::CANCEL:
            return ToolResponse::revert();

        default:;
        }
    }
    return ToolResponse();
}

void ToolDrawText::update_tip()
{
    std::vector<ActionLabelInfo> actions;

    if (!m_entity)
        return;

    actions.emplace_back(InToolActionID::LMB, "place text");
    actions.emplace_back(InToolActionID::RMB, "end tool");


    if (m_constrain)
        actions.emplace_back(InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT, "constraint off");
    else
        actions.emplace_back(InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT, "constraint on");

    const auto desc = get_default_text_font_desc();
    const bool is_bold = desc.get_weight() >= Pango::Weight::BOLD;
    const auto style = desc.get_style();
    const bool is_italic = style == Pango::Style::ITALIC || style == Pango::Style::OBLIQUE;
    actions.emplace_back(InToolActionID::TOGGLE_TEXT_BOLD, is_bold ? "bold off" : "bold on");
    actions.emplace_back(InToolActionID::TOGGLE_TEXT_ITALIC, is_italic ? "italic off" : "italic on");


    std::vector<ConstraintType> constraint_icons;
    glm::vec3 v = {NAN, NAN, NAN};

    m_intf.tool_bar_set_tool_tip("");
    if (m_constrain) {
        set_constrain_tip("origin");
        update_constraint_icons(constraint_icons);
    }

    m_intf.set_constraint_icons(get_cursor_pos_for_workplane(*m_wrkpl), v, constraint_icons);

    m_intf.tool_bar_set_actions(actions);
}

} // namespace dune3d

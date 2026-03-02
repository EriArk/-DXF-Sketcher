#include "editor.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include "logger/logger.hpp"
#include "dune3d_appwindow.hpp"
#include "canvas/canvas.hpp"
#include "core/tool_id.hpp"

namespace dune3d {

ToolResponse Editor::tool_update_with_symmetry(ToolArgs &args)
{
#ifdef DUNE_SKETCHER_ONLY
    const auto tool_before_update = m_core.get_tool_id();
    if (m_core.tool_is_active())
        clear_symmetry_live_preview_entities();
#endif
    auto response = m_core.tool_update(args);
#ifdef DUNE_SKETCHER_ONLY
    if (tool_before_update == ToolID::IMPORT_PICTURE && response.result == ToolResponse::Result::COMMIT
        && !m_core.tool_is_active()) {
        m_sticky_draw_tool = ToolID::NONE;
        m_sticky_tool_restart_connection.disconnect();
        m_restarting_sticky_tool = false;
        m_activate_selection_after_import_picture = true;
    }
#endif
    return response;
}

bool Editor::force_end_tool()
{
    if (!m_core.tool_is_active())
        return true;

    for (auto i = 0; i < 5; i++) {
        ToolArgs args;
        args.type = ToolEventType::ACTION;
        args.action = InToolActionID::CANCEL;
        ToolResponse r = tool_update_with_symmetry(args);
        tool_process(r);
        if (!m_core.tool_is_active())
            return true;
    }
    Logger::get().log_critical("Tool didn't end", Logger::Domain::EDITOR, "end the tool and repeat the last action");
    return false;
}


void Editor::tool_begin(ToolID id /*bool override_selection, const std::set<SelectableRef> &sel,
                         std::unique_ptr<ToolData> data*/)
{
    if (m_core.tool_is_active()) {
        Logger::log_critical("can't begin tool while tool is active", Logger::Domain::EDITOR);
        return;
    }
    m_win.hide_delete_items_popup();

    ToolArgs args;
    // args.data = std::move(data);

    //    if (override_selection)
    //        args.selection = sel;
    //   else
    args.selection = get_canvas().get_selection();
    capture_symmetry_entities_before_tool(id);
    m_last_selection_mode = get_canvas().get_selection_mode();
    get_canvas().set_selection_mode(SelectionMode::NONE);
    ToolResponse r = m_core.tool_begin(id, args);
    tool_process(r);
}


void Editor::tool_update_data(std::unique_ptr<ToolData> data)
{
    if (m_core.tool_is_active()) {
        ToolArgs args;
        args.type = ToolEventType::DATA;
        args.data = std::move(data);
        ToolResponse r = tool_update_with_symmetry(args);
        tool_process(r);
    }
}


void Editor::tool_process(ToolResponse &resp)
{
    sync_symmetry_for_move_selection();
    update_symmetry_live_preview_entities();
    tool_process_one();
    if (resp.result == ToolResponse::Result::COMMIT || resp.result == ToolResponse::Result::END)
        apply_symmetry_to_new_entities_after_commit();
    while (auto args = m_core.get_pending_tool_args()) {
        auto r = tool_update_with_symmetry(*args);
        sync_symmetry_for_move_selection();
        update_symmetry_live_preview_entities();
        if (r.result == ToolResponse::Result::COMMIT || r.result == ToolResponse::Result::END)
            apply_symmetry_to_new_entities_after_commit();

        tool_process_one();
    }
#ifdef DUNE_SKETCHER_ONLY
    if (m_activate_selection_after_import_picture && !m_core.tool_is_active()) {
        m_activate_selection_after_import_picture = false;
        activate_selection_mode();
    }
#endif
}

void Editor::canvas_update_from_tool()
{
    canvas_update();
    get_canvas().set_selection(m_core.get_tool_selection(), false);
}

void Editor::tool_process_one()
{
    if (!m_core.tool_is_active()) {
        m_no_canvas_update = false;
        m_constraint_tip_icons.clear();
        m_solid_model_edge_select_mode = false;
    }
    if (!m_no_canvas_update)
        canvas_update();
    get_canvas().set_selection(m_core.get_tool_selection(), false);
    if (!m_core.tool_is_active()) {
        m_dialogs.close_nonmodal();
        // imp_interface->dialogs.close_nonmodal();
        // reset_tool_hint_label();
        // canvas->set_cursor_external(false);
        // canvas->snap_filter.clear();
        update_workplane_label();
        update_selection_editor();
        update_action_bar_buttons_sensitivity(); // due to workplane change
    }

    if (!m_core.tool_is_active())
        get_canvas().set_selection_mode(m_last_selection_mode);

#ifdef DUNE_SKETCHER_ONLY
    if (!m_core.tool_is_active() && m_sticky_draw_tool != ToolID::NONE && !m_restarting_sticky_tool
        && m_core.has_documents()) {
        const auto sticky_tool = m_sticky_draw_tool;
        if (m_core.tool_can_begin(sticky_tool, {}).get_can_begin()) {
            m_restarting_sticky_tool = true;
            m_sticky_tool_restart_connection.disconnect();
            m_sticky_tool_restart_connection = Glib::signal_idle().connect([this, sticky_tool] {
                m_sticky_tool_restart_connection.disconnect();
                m_restarting_sticky_tool = false;
                if (!m_core.has_documents())
                    return false;
                if (m_core.tool_is_active())
                    return false;
                if (m_sticky_draw_tool != sticky_tool)
                    return false;
                if (!m_core.tool_can_begin(sticky_tool, {}).get_can_begin())
                    return false;
                tool_begin(sticky_tool);
                return false;
            });
        }
    }
    update_sketcher_toolbar_button_states();
#endif

    /*  if (m_core.tool_is_active()) {
          canvas->set_selection(m_core.get_tool_selection());
      }
      else {
          canvas->set_selection(m_core.get_tool_selection(),
                                canvas->get_selection_mode() == CanvasGL::SelectionMode::NORMAL);
      }*/
}

void Editor::handle_tool_change()
{
    const auto tool_id = m_core.get_tool_id();
    // panels->set_sensitive(id == ToolID::NONE);
    // canvas->set_selection_allowed(id == ToolID::NONE);
    // main_window->tool_bar_set_use_actions(core->get_tool_actions().size());
    if (tool_id != ToolID::NONE) {
        m_win.tool_bar_set_tool_name(action_catalog.at(tool_id).name.full);
        tool_bar_set_tool_tip("");
    }
    m_win.tool_bar_set_visible(tool_id != ToolID::NONE);
    tool_bar_clear_actions();
    update_action_bar_visibility();
#ifdef DUNE_SKETCHER_ONLY
    update_sketcher_toolbar_button_states();
#endif
}

} // namespace dune3d

#include "editor.hpp"
#include "workspace_browser.hpp"
#include "dune3d_appwindow.hpp"
#include "dune3d_application.hpp"
#include "widgets/constraints_box.hpp"
#include "document/group/all_groups.hpp"
#include "document/entity/entity_workplane.hpp"
#include "util/selection_util.hpp"
#include "canvas/canvas.hpp"
#include "workspace_browser.hpp"
#include "util/template_util.hpp"
#include "util/gtk_util.hpp"
#include "core/tool_id.hpp"
#include "nlohmann/json.hpp"
#include "action/action_id.hpp"
#include "document/solid_model/solid_model.hpp"
#include "widgets/select_groups_dialog.hpp"
#include "util/fs_util.hpp"

namespace dune3d {
using json = nlohmann::json;

void Editor::init_workspace_browser()
{
    m_workspace_browser = Gtk::make_managed<WorkspaceBrowser>(m_core);
#ifdef DUNE_SKETCHER_ONLY
    m_sidebar_popover = Gtk::make_managed<Gtk::Popover>();
    m_sidebar_popover->add_css_class("sketch-sidebar-popover");
    m_sidebar_popover->set_autohide(true);
    m_sidebar_popover->set_has_arrow(true);
    m_sidebar_popover->set_position(Gtk::PositionType::TOP);
    m_sidebar_popover->set_size_request(280, 560);
    m_sidebar_popover->set_child(*m_workspace_browser);
    m_sidebar_popover->signal_hide().connect([this] { m_win.get_app().m_user_config.sidebar_visible = false; });
    m_sidebar_popover->signal_show().connect([this] { m_win.get_app().m_user_config.sidebar_visible = true; });
    m_workspace_browser->set_sketcher_open_controls(m_win.get_open_button(), m_win.get_open_menu_button());
    if (auto open_popover = m_win.get_open_popover()) {
        open_popover->signal_hide().connect([this] {
            if (!m_sidebar_popover)
                return;
            const bool restore_sidebar = m_sidebar_popover->is_visible();
            if (!restore_sidebar)
                return;
            // Work around nested-popover grab getting stuck after closing Recent via second click.
            m_sidebar_popover->popdown();
            Glib::signal_idle().connect_once([this] {
                if (!m_sidebar_popover)
                    return;
                m_sidebar_popover->popup();
            });
        });
    }
    if (auto sidebar_button = m_win.get_sidebar_floating_button()) {
        m_sidebar_popover->set_parent(*sidebar_button);
        sidebar_button->signal_clicked().connect([this] { toggle_sidebar_visibility(); });
    }
#else
    m_workspace_browser_revealer = Gtk::make_managed<Gtk::Revealer>();
    m_workspace_browser_revealer->set_transition_type(Gtk::RevealerTransitionType::NONE);
    m_workspace_browser_revealer->set_reveal_child(true);
    m_workspace_browser_revealer->set_child(*m_workspace_browser);
#endif
    m_workspace_browser->signal_close_document().connect([this](const UUID &doc_uu) {
        get_canvas().grab_focus();
        close_document(doc_uu, nullptr, nullptr);
    });

    m_workspace_browser->signal_group_selected().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_group_selected));
    m_workspace_browser->signal_add_group().connect(sigc::mem_fun(*this, &Editor::on_add_group));
    m_workspace_browser->signal_open_folder().connect(sigc::mem_fun(*this, &Editor::on_open_folder));
    m_workspace_browser->signal_delete_current_group().connect(sigc::mem_fun(*this, &Editor::on_delete_current_group));
    m_workspace_browser->signal_move_group().connect(sigc::mem_fun(*this, &Editor::on_move_group));
    m_workspace_browser->signal_document_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_document_checked));
    m_workspace_browser->signal_group_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_group_checked));
    m_workspace_browser->signal_body_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_body_checked));
    m_workspace_browser->signal_body_solid_model_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_body_solid_model_checked));
    m_workspace_browser->signale_active_link().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_activate_link));
    m_workspace_browser->signal_rename_body().connect(sigc::mem_fun(*this, &Editor::on_workspace_browser_rename_body));
    m_workspace_browser->signal_reset_body_color().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_reset_body_color));
    m_workspace_browser->signal_set_body_color().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_set_body_color));
    m_workspace_browser->signal_body_expanded().connect([this](const UUID &body_uu, bool expanded) {
        if (m_core.get_current_document()
                            .get_group(m_core.get_current_group())
                            .find_body(m_core.get_current_document())
                            .group.m_uuid
                    == body_uu
            && expanded == false) {
            return false;
        }
        get_current_document_view().m_body_views[body_uu].m_expanded = expanded;
        return true;
    });

    m_workspace_browser->set_sensitive(m_core.has_documents());

    m_core.signal_rebuilt().connect([this] {
        Glib::signal_idle().connect_once([this] {
            if (!m_current_workspace_view)
                return;
            m_workspace_browser->update_documents(m_workspace_views.at(m_current_workspace_view).m_documents);
        });
    });

#ifdef DUNE_SKETCHER_ONLY
    auto empty = Gtk::make_managed<Gtk::Box>();
    empty->set_visible(false);
    m_win.get_left_bar().set_start_child(*empty);
    set_sidebar_visible(m_win.get_app().m_user_config.sidebar_visible);
#else
    m_win.get_left_bar().set_start_child(*m_workspace_browser_revealer);
#endif
}

void Editor::set_sidebar_visible(bool visible)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_sidebar_popover)
        return;
    if (visible) {
        const auto max_height = std::max(220, static_cast<int>((m_win.get_height() - 120) * 0.8));
        m_sidebar_popover->set_size_request(270, max_height);
        m_sidebar_popover->popup();
    }
    else {
        if (auto open_popover = m_win.get_open_popover())
            open_popover->popdown();
        m_sidebar_popover->popdown();
    }
#else
    if (!m_workspace_browser_revealer)
        return;
    m_workspace_browser_revealer->set_reveal_child(visible);
#endif
    m_win.get_app().m_user_config.sidebar_visible = visible;
}

void Editor::toggle_sidebar_visibility()
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_sidebar_popover)
        return;
    set_sidebar_visible(!m_sidebar_popover->is_visible());
#else
    if (!m_workspace_browser_revealer)
        return;
    set_sidebar_visible(!m_workspace_browser_revealer->get_reveal_child());
#endif
}

void Editor::on_workspace_browser_group_selected(const UUID &uu_doc, const UUID &uu_group)
{
    if (m_core.tool_is_active()) {
#ifdef DUNE_SKETCHER_ONLY
        if (!force_end_tool())
            return;
#else
        return;
#endif
    }
    auto &idoc = m_core.get_current_idocument_info();
    if (idoc.get_uuid() == uu_doc && idoc.get_current_group() == uu_group)
        return;
    m_core.set_current_document(uu_doc);
    m_workspace_views.at(m_current_workspace_view).m_current_document = uu_doc;
    update_version_info();
    get_current_document_view().m_current_group = uu_group;
    set_current_group(uu_group);
}

void Editor::on_add_group(Group::Type group_type, WorkspaceBrowserAddGroupMode add_group_mode)
{
    if (m_core.tool_is_active())
        return;
    auto &doc = m_core.get_current_document();
    auto &current_group = doc.get_group(m_core.get_current_group());
    Group *new_group = nullptr;
    static const std::string toast_prefix = "Couldn't create group\n";
    if (group_type == Group::Type::SKETCH) {
        auto &group = doc.insert_group<GroupSketch>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        if (current_group.m_active_wrkpl) {
            group.m_active_wrkpl = current_group.m_active_wrkpl;
        }
        else {
            for (auto gr : doc.get_groups_sorted()) {
                if (auto ref = dynamic_cast<const GroupReference *>(gr)) {
                    group.m_active_wrkpl = ref->get_workplane_xy_uuid();
                    break;
                }
            }
        }
    }
    else if (group_type == Group::Type::EXTRUDE) {
        if (!current_group.m_active_wrkpl) {
            m_workspace_browser->show_toast(toast_prefix + "Current group needs an active workplane");
            return;
        }
        auto &group = doc.insert_group<GroupExtrude>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_wrkpl = current_group.m_active_wrkpl;
        group.m_dvec = doc.get_entity<EntityWorkplane>(group.m_wrkpl).get_normal_vector();
        group.m_source_group = current_group.m_uuid;
    }
    else if (any_of(group_type, Group::Type::LATHE, Group::Type::REVOLVE)) {
        if (!current_group.m_active_wrkpl) {
            m_workspace_browser->show_toast(toast_prefix + "Current group needs an active workplane");
            return;
        }
        auto sel = get_canvas().get_selection();
        auto axis_enp = point_from_selection(doc, sel);
        if (!axis_enp) {
            m_workspace_browser->show_toast(toast_prefix + "Select an axis entity (workplane or line)");
            return;
        }

        if (axis_enp->point != 0) {
            m_workspace_browser->show_toast(toast_prefix + "Select the body of the axis entity");
            return;
        }
        const auto &axis = doc.get_entity(axis_enp->entity);
        if (!axis.of_type(Entity::Type::WORKPLANE, Entity::Type::LINE_2D, Entity::Type::LINE_3D)) {
            m_workspace_browser->show_toast(toast_prefix + "Axis entity must be a line or a workplane");
            return;
        }

        if (group_type == Group::Type::LATHE) {
            auto &group = doc.insert_group<GroupLathe>(UUID::random(), current_group.m_uuid);
            new_group = &group;
            group.m_wrkpl = current_group.m_active_wrkpl;
            group.m_source_group = current_group.m_uuid;
            group.m_origin = {axis_enp->entity, 1};
            group.m_normal = axis_enp->entity;
        }
        else {
            auto &group = doc.insert_group<GroupRevolve>(UUID::random(), current_group.m_uuid);
            new_group = &group;
            group.m_wrkpl = current_group.m_active_wrkpl;
            group.m_source_group = current_group.m_uuid;
            group.m_origin = {axis_enp->entity, 1};
            group.m_normal = axis_enp->entity;
        }
    }
    else if (any_of(group_type, Group::Type::FILLET, Group::Type::CHAMFER)) {
        auto solid_model = SolidModel::get_last_solid_model(doc, current_group, SolidModel::IncludeGroup::YES);
        if (!solid_model) {
            m_workspace_browser->show_toast(toast_prefix + "Body has no solid model");
            return;
        }
        if (group_type == Group::Type::FILLET) {
            auto &group = doc.insert_group<GroupFillet>(UUID::random(), current_group.m_uuid);
            new_group = &group;
        }
        else {
            auto &group = doc.insert_group<GroupChamfer>(UUID::random(), current_group.m_uuid);
            new_group = &group;
        }
    }
    else if (group_type == Group::Type::LINEAR_ARRAY) {
        auto &group = doc.insert_group<GroupLinearArray>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_active_wrkpl = current_group.m_active_wrkpl;
        group.m_source_group = current_group.m_uuid;
    }
    else if (any_of(group_type, Group::Type::POLAR_ARRAY, Group::Type::MIRROR_HORIZONTAL,
                    Group::Type::MIRROR_VERTICAL)) {
        UUID wrkpl;
        if (current_group.m_active_wrkpl) {
            wrkpl = current_group.m_active_wrkpl;
        }
        else {
            auto sel = get_canvas().get_selection();
            auto owrkpl = point_from_selection(doc, sel, Entity::Type::WORKPLANE);
            if (owrkpl)
                wrkpl = owrkpl->entity;
        }
        if (!wrkpl) {
            m_workspace_browser->show_toast(toast_prefix + "Current group needs an active workplane or select one");
            return;
        }
        GroupReplicate *group = nullptr;
        if (group_type == GroupType::POLAR_ARRAY)
            group = &doc.insert_group<GroupPolarArray>(UUID::random(), current_group.m_uuid);
        else if (group_type == GroupType::MIRROR_HORIZONTAL)
            group = &doc.insert_group<GroupMirrorHorizontal>(UUID::random(), current_group.m_uuid);
        else if (group_type == GroupType::MIRROR_VERTICAL)
            group = &doc.insert_group<GroupMirrorVertical>(UUID::random(), current_group.m_uuid);
        new_group = group;
        group->m_active_wrkpl = wrkpl;
        group->m_source_group = current_group.m_uuid;
    }
    else if (group_type == Group::Type::LOFT) {
        auto dia = SelectGroupsDialog::create(m_core.get_current_document(), m_core.get_current_group(), {});
        dia->set_transient_for(m_win);
        dia->present();
        dia->signal_changed().connect([this, dia, &current_group, &doc] {
            auto groups = dia->get_selected_groups();
            if (groups.size() < 2) {
                m_workspace_browser->show_toast(toast_prefix + "Select at least two groups");
                return;
            }
            auto &group = doc.insert_group<GroupLoft>(UUID::random(), current_group.m_uuid);
            for (const auto &uu : groups) {
                const auto &src = doc.get_group(uu);
                group.m_sources.emplace_back(src.m_active_wrkpl, src.m_uuid);
            }
            finish_add_group(&group);
        });
    }
    else if (group_type == Group::Type::SOLID_MODEL_OPERATION) {
        auto &group = doc.insert_group<GroupSolidModelOperation>(UUID::random(), current_group.m_uuid);
        new_group = &group;
    }
    else if (group_type == Group::Type::CLONE) {
        if (!current_group.m_active_wrkpl) {
            m_workspace_browser->show_toast(toast_prefix + "Current group needs an active workplane");
            return;
        }
        GroupClone &group = doc.insert_group<GroupClone>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_source_group = current_group.m_uuid;
        group.m_source_wrkpl = current_group.m_active_wrkpl;
    }
    else if (group_type == Group::Type::PIPE) {
        if (!current_group.m_active_wrkpl) {
            m_workspace_browser->show_toast(toast_prefix + "Current group needs an active workplane");
            return;
        }
        auto &group = doc.insert_group<GroupPipe>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_wrkpl = current_group.m_active_wrkpl;
        group.m_source_group = current_group.m_uuid;
    }
    if (new_group && add_group_mode == WorkspaceBrowserAddGroupMode::WITH_BODY)
        new_group->m_body.emplace();
    finish_add_group(new_group);
}

void Editor::finish_add_group(Group *new_group)
{
    if (!new_group)
        return;
    CanvasUpdater canvas_updater{*this};
    auto &doc = m_core.get_current_document();
    auto group_type = new_group->get_type();
    new_group->m_name = doc.find_next_group_name(group_type);
    doc.set_group_generate_pending(new_group->m_uuid);
    m_core.set_needs_save();
    m_core.rebuild("add group");
    m_workspace_browser->update_documents(get_current_document_views());
    m_workspace_browser->select_group(new_group->m_uuid);
    if (any_of(group_type, Group::Type::FILLET, Group::Type::CHAMFER)) {
        trigger_action(ToolID::SELECT_EDGES);
    }
    else if (group_type == Group::Type::PIPE) {
        trigger_action(ToolID::SELECT_SPINE_ENTITIES);
    }
}

void Editor::on_delete_current_group(bool delete_file_too)
{
    if (m_core.tool_is_active())
        return;

    auto &doc = m_core.get_current_document();

    auto &group = doc.get_group(m_core.get_current_group());
    if (!group.can_delete())
        return;

    UUID previous_group;
    previous_group = doc.get_group_rel(group.m_uuid, -1);
    if (!previous_group)
        previous_group = doc.get_group_rel(group.m_uuid, 1);

    if (!previous_group)
        return;
    const auto group_uuid = group.m_uuid;
    const auto file_path = get_group_export_path(group_uuid);

    auto perform_delete = [this, previous_group, group_uuid, file_path, delete_file_too] {
        auto &doc = m_core.get_current_document();
        try {
            doc.get_group(group_uuid);
        }
        catch (...) {
            return;
        }

        if (delete_file_too) {
            if (!file_path.has_value()) {
                m_workspace_browser->show_toast("Sketch has no file to delete");
                return;
            }
            std::error_code ec;
            if (std::filesystem::exists(*file_path, ec) && !std::filesystem::remove(*file_path, ec)) {
                m_workspace_browser->show_toast("Couldn't delete file");
                return;
            }
        }

        doc.set_group_generate_pending(previous_group);
        {
            ItemsToDelete items;
            items.groups = {group_uuid};
            const auto items_initial = items;
            auto extra_items = doc.get_additional_items_to_delete(items);
            items.append(extra_items);
            show_delete_items_popup(items_initial, items);
            doc.delete_items(items);
        }
        m_group_export_paths.erase(group_uuid);

        get_current_document_view().m_current_group = previous_group;
        m_core.set_current_group(previous_group);
        m_workspace_browser->update_documents(get_current_document_views());
        set_current_group(previous_group);

        m_core.set_needs_save();
        m_core.rebuild("delete group");
    };

    if (delete_file_too) {
        if (!file_path.has_value()) {
            m_workspace_browser->show_toast("Sketch has no file to delete");
            return;
        }
#ifdef DUNE_SKETCHER_ONLY
        if (m_sidebar_popover)
            m_sidebar_popover->popdown();
#endif
        auto dialog = Gtk::AlertDialog::create("Delete file \"" + path_to_string(file_path->filename()) + "\"?");
        dialog->set_detail("This will permanently delete the file from disk.");
        dialog->set_buttons({"Cancel", "Delete file"});
        dialog->set_cancel_button(0);
        dialog->set_default_button(0);
        dialog->choose(m_win, [dialog, perform_delete](Glib::RefPtr<Gio::AsyncResult> &result) {
            if (dialog->choose_finish(result) == 1)
                perform_delete();
        });
        return;
    }

    perform_delete();
}

void Editor::on_move_group(Document::MoveGroup op)
{
    if (m_core.tool_is_active())
        return;
    CanvasUpdater canvas_updater{*this};
    auto &doc = m_core.get_current_document();
    auto group = m_core.get_current_group();

    UUID group_after = doc.get_group_after(group, op);
    if (!group_after) {
        m_workspace_browser->show_toast("Couldn't move group");
        return;
    }

    if (!doc.reorder_group(group, group_after)) {
        m_workspace_browser->show_toast("Couldn't move group");
        return;
    }
    m_core.set_needs_save();
    m_core.rebuild("reorder_group");
    m_workspace_browser->update_documents(get_current_document_views());
}

void Editor::on_workspace_browser_document_checked(const UUID &uu_doc, bool checked)
{
    CanvasUpdater canvas_updater{*this};
    get_current_document_views()[uu_doc].m_document_is_visible = checked;
    m_workspace_browser->update_current_group(get_current_document_views());
    update_workspace_view_names();
}

void Editor::on_workspace_browser_group_checked(const UUID &uu_doc, const UUID &uu_group, bool checked)
{
    CanvasUpdater canvas_updater{*this};
    get_current_document_views()[uu_doc].m_group_views[uu_group].m_visible = checked;
    m_workspace_browser->update_current_group(get_current_document_views());
}

void Editor::on_workspace_browser_body_checked(const UUID &uu_doc, const UUID &uu_group, bool checked)
{
    CanvasUpdater canvas_updater{*this};
    get_current_document_views()[uu_doc].m_body_views[uu_group].m_visible = checked;
    m_workspace_browser->update_current_group(get_current_document_views());
}

void Editor::on_workspace_browser_body_solid_model_checked(const UUID &uu_doc, const UUID &uu_group, bool checked)
{
    CanvasUpdater canvas_updater{*this};
    get_current_document_views()[uu_doc].m_body_views[uu_group].m_solid_model_visible = checked;
    m_workspace_browser->update_current_group(get_current_document_views());
}

void Editor::on_workspace_browser_activate_link(const std::string &link)
{
    const auto j = json::parse(link);
    const auto op = j.at("op").get<std::string>();
    if (op == "find-redundant-constraints") {
        if (m_properties_notebook && m_constraints_box) {
            m_properties_notebook->set_current_page(m_properties_notebook->page_num(*m_constraints_box));
            m_constraints_box->set_redundant_only();
        }
    }
    else if (op == "undo") {
        if (!m_core.tool_is_active())
            trigger_action(ActionID::UNDO);
    }
}

void Editor::on_workspace_browser_rename_body(const UUID &uu_doc, const UUID &uu_group)
{
    auto &doc = m_core.get_idocument_info(uu_doc).get_document();
    auto &body = doc.get_group(uu_group).m_body.value();

    auto win = new RenameWindow("Rename body");
    win->set_text(body.m_name);
    win->set_transient_for(m_win);
    win->set_modal(true);
    win->present();
    win->signal_changed().connect([this, win, &body, &doc, &uu_group] {
        auto txt = win->get_text();
        body.m_name = txt;

        doc.set_group_update_solid_model_pending(uu_group);
        m_core.rebuild("rename body");
        canvas_update_keep_selection();
    });
}

void Editor::on_workspace_browser_set_body_color(const UUID &uu_doc, const UUID &uu_group)
{

    auto dia = Gtk::ColorDialog::create();
    dia->set_with_alpha(false);
    Color initial{.5, .5, .5};
    auto &doc = m_core.get_idocument_info(uu_doc).get_document();
    auto &body = doc.get_group(uu_group).m_body.value();

    if (body.m_color)
        initial = body.m_color.value();

    dia->choose_rgba(m_win, rgba_from_color(initial),
                     [this, dia, &body, &doc, &uu_group](Glib::RefPtr<Gio::AsyncResult> &result) {
                         try {
                             auto rgba = dia->choose_rgba_finish(result);
                             body.m_color = color_from_rgba(rgba);

                             doc.set_group_update_solid_model_pending(uu_group);
                             m_core.rebuild("set body color");
                             canvas_update_keep_selection();
                         }
                         catch (const Gtk::DialogError &err) {
                             // Can be thrown by dialog->open_finish(result).
                         }
                     });
}

void Editor::on_workspace_browser_reset_body_color(const UUID &uu_doc, const UUID &uu_group)
{
    auto &doc = m_core.get_idocument_info(uu_doc).get_document();
    doc.get_group(uu_group).m_body.value().m_color.reset();
    doc.set_group_update_solid_model_pending(uu_group);
    m_core.rebuild("reset body color");
    canvas_update_keep_selection();
}

} // namespace dune3d

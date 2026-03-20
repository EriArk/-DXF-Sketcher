#include "workspace_browser.hpp"
#include "core/core.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/group/igroup_source_group.hpp"
#include "workspace/document_view.hpp"
#include "util/fs_util.hpp"
#include <iostream>
#include <memory>

namespace dune3d {

namespace {
struct HoverFlyoutState {
    bool pointer_on_button = false;
    bool pointer_on_flyout = false;
    sigc::connection close_timeout;
};

void install_hover_flyout(Gtk::Widget &button, Gtk::Widget &flyout)
{
    auto state = std::make_shared<HoverFlyoutState>();

    auto maybe_close = [state, &flyout] {
        if (!state->pointer_on_button && !state->pointer_on_flyout && flyout.get_visible())
            flyout.set_visible(false);
    };

    auto schedule_close = [state, maybe_close] {
        state->close_timeout.disconnect();
        state->close_timeout = Glib::signal_timeout().connect(
                [maybe_close] {
                    maybe_close();
                    return false;
                },
                120);
    };

    auto button_motion = Gtk::EventControllerMotion::create();
    button_motion->signal_enter().connect([state, &button, &flyout](double, double) {
        state->pointer_on_button = true;
        state->close_timeout.disconnect();
        const auto w = button.get_allocated_width();
        if (w > 0)
            flyout.set_size_request(w, -1);
        if (!flyout.get_visible())
            flyout.set_visible(true);
    });
    button_motion->signal_leave().connect([state, schedule_close] {
        state->pointer_on_button = false;
        schedule_close();
    });
    button.add_controller(button_motion);

    auto flyout_motion = Gtk::EventControllerMotion::create();
    flyout_motion->signal_enter().connect([state](double, double) {
        state->pointer_on_flyout = true;
        state->close_timeout.disconnect();
    });
    flyout_motion->signal_leave().connect([state, schedule_close] {
        state->pointer_on_flyout = false;
        schedule_close();
    });
    flyout.add_controller(flyout_motion);

    flyout.property_visible().signal_changed().connect([state, &flyout] {
        if (flyout.get_visible())
            return;
        state->pointer_on_button = false;
        state->pointer_on_flyout = false;
        state->close_timeout.disconnect();
    });
}
} // namespace

class WorkspaceBrowser::GroupItem : public Glib::Object {
public:
    static Glib::RefPtr<GroupItem> create()
    {
        return Glib::make_refptr_for_instance<GroupItem>(new GroupItem);
    }


    Glib::Property<Glib::ustring> m_name;
    Glib::Property<bool> m_active;
    Glib::Property<bool> m_check_active;
    Glib::Property<bool> m_check_sensitive;
    Glib::Property<int> m_dof;
    Glib::Property<GroupStatusMessage::Status> m_status;
    Glib::Property<Glib::ustring> m_status_message;
    Glib::Property<bool> m_source_group;
    Glib::Property<bool> m_has_color;
    Glib::Property<Gdk::RGBA> m_color;
    UUID m_uuid;
    UUID m_doc;

    // No idea why the ObjectBase::get_type won't work for us but
    // reintroducing the method and using the name used by gtkmm seems
    // to work.
    static GType get_type()
    {
        // Let's cache once the type does exist.
        if (!gtype)
            gtype = g_type_from_name("gtkmm__CustomObject_GroupItem");
        return gtype;
    }

private:
    GroupItem()
        : Glib::ObjectBase("GroupItem"), m_name(*this, "name"), m_active(*this, "active", false),
          m_check_active(*this, "check_active", false), m_check_sensitive(*this, "check_sensitive", true),
          m_dof(*this, "dof"), m_status(*this, "status", GroupStatusMessage::Status::NONE),
          m_status_message(*this, "status_message"), m_source_group(*this, "source_group"),
          m_has_color(*this, "has_color", false), m_color(*this, "color")
    {
    }

    static GType gtype;
};

GType WorkspaceBrowser::GroupItem::gtype;


class WorkspaceBrowser::BodyItem : public Glib::Object {
public:
    static Glib::RefPtr<BodyItem> create()
    {
        return Glib::make_refptr_for_instance<BodyItem>(new BodyItem);
    }


    Glib::Property<Glib::ustring> m_name;
    UUID m_uuid;
    UUID m_doc;
    Glib::Property<bool> m_check_active;
    Glib::Property<bool> m_expanded;
    Glib::Property<bool> m_check_sensitive;
    Glib::Property<bool> m_solid_model_active;
    Glib::Property<bool> m_has_color;
    Glib::Property<Gdk::RGBA> m_color;
    bool m_show_in_sketcher = true;

    Glib::RefPtr<Gio::ListStore<GroupItem>> m_group_store;

    // No idea why the ObjectBase::get_type won't work for us but
    // reintroducing the method and using the name used by gtkmm seems
    // to work.
    static GType get_type()
    {
        // Let's cache once the type does exist.
        if (!gtype)
            gtype = g_type_from_name("gtkmm__CustomObject_BodyItem");
        return gtype;
    }

private:
    BodyItem()
        : Glib::ObjectBase("BodyItem"), m_name(*this, "name"), m_check_active(*this, "check_active", false),
          m_expanded(*this, "expanded", true), m_check_sensitive(*this, "check_sensitive", true),
          m_solid_model_active(*this, "m_solid_model_active", true), m_has_color(*this, "has_color", false),
          m_color(*this, "color")
    {
        m_group_store = Gio::ListStore<GroupItem>::create();
    }

    static GType gtype;
};

GType WorkspaceBrowser::BodyItem::gtype;

class WorkspaceBrowser::DocumentItem : public Glib::Object {
public:
    static Glib::RefPtr<DocumentItem> create()
    {
        return Glib::make_refptr_for_instance<DocumentItem>(new DocumentItem);
    }


    Glib::Property<Glib::ustring> m_name;
    Glib::Property<Glib::ustring> m_tooltip;
    Glib::Property<bool> m_active;
    Glib::Property<bool> m_check_active;
    Glib::Property<bool> m_check_sensitive;
    Glib::Property<bool> m_close_sensitive;
    UUID m_uuid;
    Glib::RefPtr<Gio::ListStore<BodyItem>> m_body_store;

    // No idea why the ObjectBase::get_type won't work for us but
    // reintroducing the method and using the name used by gtkmm seems
    // to work.
    static GType get_type()
    {
        // Let's cache once the type does exist.
        if (!gtype)
            gtype = g_type_from_name("gtkmm__CustomObject_DocumentItem");
        return gtype;
    }

private:
    DocumentItem()
        : Glib::ObjectBase("DocumentItem"), m_name(*this, "name"), m_tooltip(*this, "tooltip"),
          m_active(*this, "active"), m_check_active(*this, "check_active", false),
          m_check_sensitive(*this, "check_sensitive", true), m_close_sensitive(*this, "close_sensitive", true)
    {
        m_body_store = Gio::ListStore<BodyItem>::create();
    }

    static GType gtype;
};

static Gdk::RGBA rgba_from_color(const Color &c)
{
    Gdk::RGBA r;
    r.set_rgba(c.r, c.g, c.b);
    return r;
}


GType WorkspaceBrowser::DocumentItem::gtype;

void WorkspaceBrowser::update_documents(const std::map<UUID, DocumentView> &doc_views)
{
    block_signals();
    auto store = Gio::ListStore<DocumentItem>::create();
    for (auto doci : m_core.get_documents()) {
#ifdef DUNE_SKETCHER_ONLY
        if (doci->get_uuid() != m_core.get_current_idocument_info().get_uuid())
            continue;
#endif
        auto mi = DocumentItem::create();
        mi->m_uuid = doci->get_uuid();
        mi->m_close_sensitive = doci->can_close();
        const auto &doc_view = doc_views.at(mi->m_uuid);
        Glib::RefPtr<BodyItem> body_item = BodyItem::create();
        body_item->m_doc = mi->m_uuid;
        body_item->m_name = "Folder";
#ifdef DUNE_SKETCHER_ONLY
        body_item->m_show_in_sketcher = false;
#endif
        for (auto gr : doci->get_document().get_groups_sorted()) {
            if (gr->m_body.has_value()) {
                body_item = BodyItem::create();
                body_item->m_name = gr->m_body->m_name;
#ifdef DUNE_SKETCHER_ONLY
                if (auto it = doc_view.m_body_views.find(gr->m_uuid); it != doc_view.m_body_views.end()
                    && it->second.m_color.has_value()) {
                    body_item->m_has_color = true;
                    body_item->m_color = rgba_from_color(*it->second.m_color);
                }
#else
                body_item->m_has_color = gr->m_body->m_color.has_value();
                if (gr->m_body->m_color.has_value())
                    body_item->m_color = rgba_from_color(gr->m_body->m_color.value());
#endif
                body_item->m_uuid = gr->m_uuid;
                body_item->m_doc = mi->m_uuid;
#ifdef DUNE_SKETCHER_ONLY
                body_item->m_show_in_sketcher = m_sketcher_folder_groups.contains(gr->m_uuid);
#endif
                mi->m_body_store->append(body_item);
            }

            auto gi = GroupItem::create();
            gi->m_name = gr->m_name;
            gi->m_uuid = gr->m_uuid;
            gi->m_doc = doci->get_uuid();
            if (auto it = doc_view.m_group_views.find(gr->m_uuid); it != doc_view.m_group_views.end()
                && it->second.m_color.has_value()) {
                gi->m_has_color = true;
                gi->m_color = rgba_from_color(*it->second.m_color);
            }
#ifdef DUNE_SKETCHER_ONLY
            if (gr->get_type() == Group::Type::REFERENCE)
                continue;
#endif
            body_item->m_group_store->append(gi);
        }
        store->append(mi);
    }
    m_document_store = store;
    m_model = Gtk::TreeListModel::create(m_document_store, sigc::mem_fun(*this, &WorkspaceBrowser::create_model),
                                         /* passthrough */ false, /* autoexpand */ true);
    m_selection_model->set_model(m_model);
    unblock_signals();
    update_current_group(doc_views);
    // m_selection_model->set_selected(sel);
}

void WorkspaceBrowser::block_signals()
{
    m_blocked_count++;

    m_signal_group_checked.block();
    m_signal_document_checked.block();
    m_signal_body_checked.block();
    m_signal_group_selected.block();
    m_signal_body_solid_model_checked.block();
    m_signal_body_expanded.block();
}

void WorkspaceBrowser::unblock_signals()
{
    if (m_blocked_count > 0)
        m_blocked_count--;

    if (m_blocked_count != 0)
        return;

    m_signal_group_checked.unblock();
    m_signal_document_checked.unblock();
    m_signal_body_checked.unblock();
    m_signal_group_selected.unblock();
    m_signal_body_solid_model_checked.unblock();
    m_signal_body_expanded.unblock();
}

static std::string icon_name_from_status(GroupStatusMessage::Status st)
{
    using S = GroupStatusMessage::Status;
    switch (st) {
    case S::NONE:
        return "";
    case S::INFO:
        return "dialog-information-symbolic";

    case S::WARN:
        return "dialog-warning-symbolic";

    case S::ERR:
        return "dialog-error-symbolic";
    }

    return "face-worried-symbolic";
}

void WorkspaceBrowser::update_name(DocumentItem &it_doc, IDocumentInfo &doci)
{
    if (doci.get_path().empty())
        it_doc.m_tooltip = "Not saved yet";
    else
        it_doc.m_tooltip = path_to_string(doci.get_path());

    it_doc.m_name = doci.get_name();
    if (doci.get_needs_save())
        it_doc.m_name = it_doc.m_name + " *";
}

void WorkspaceBrowser::update_current_group(const std::map<UUID, DocumentView> &doc_views)
{
    block_signals();
    for (size_t i_doc = 0; i_doc < m_document_store->get_n_items(); i_doc++) {
        auto &it_doc = *m_document_store->get_item(i_doc);
        auto &doci = m_core.get_idocument_info(it_doc.m_uuid);
        auto &doc_view = doc_views.at(doci.get_uuid());
        const auto is_current_doc = doci.get_uuid() == m_core.get_current_idocument_info().get_uuid();
        it_doc.m_check_sensitive = !is_current_doc;
        it_doc.m_check_active = is_current_doc || doc_view.document_is_visible();
        it_doc.m_active = is_current_doc;
        update_name(it_doc, doci);
        const auto &doc = doci.get_document();
        const auto &current_group = doc.get_group(doci.get_current_group());
        std::set<UUID> source_groups;
        if (auto group_src = dynamic_cast<const IGroupSourceGroup *>(&current_group))
            source_groups = group_src->get_source_groups(doc);
        auto body = current_group.find_body(doc);
        UUID body_uu = body.group.m_uuid;
#ifndef DUNE_SKETCHER_ONLY
        bool after_active = false;
#endif
        for (size_t i_body = 0; i_body < it_doc.m_body_store->get_n_items(); i_body++) {
            auto &it_body = *it_doc.m_body_store->get_item(i_body);
            const bool is_current_body = body_uu == it_body.m_uuid && is_current_doc;
#ifdef DUNE_SKETCHER_ONLY
            it_body.m_check_sensitive = true;
            it_body.m_check_active = doc_view.body_is_visible(it_body.m_uuid);
#else
            it_body.m_check_sensitive = (body_uu != it_body.m_uuid) || !is_current_doc;

            if (is_current_body)
                it_body.m_check_active = true;
            else
                it_body.m_check_active = doc_view.body_is_visible(it_body.m_uuid);
#endif

#ifdef DUNE_SKETCHER_ONLY
            if (auto it = doc_view.m_body_views.find(it_body.m_uuid); it != doc_view.m_body_views.end()
                && it->second.m_color.has_value()) {
                it_body.m_has_color = true;
                it_body.m_color = rgba_from_color(*it->second.m_color);
            }
            else {
                it_body.m_has_color = false;
            }
#endif

            it_body.m_solid_model_active = doc_view.body_solid_model_is_visible(it_body.m_uuid);
            it_body.m_expanded = doc_view.body_is_expanded(it_body.m_uuid) | is_current_body;


            for (size_t i_group = 0; i_group < it_body.m_group_store->get_n_items(); i_group++) {
                auto &it_group = *it_body.m_group_store->get_item(i_group);
                bool is_current = doci.get_current_group() == it_group.m_uuid;
                it_group.m_active = is_current && is_current_doc;
                auto &gr = doc.get_group(it_group.m_uuid);
                it_group.m_dof = gr.m_dof;
                it_group.m_name = gr.m_name;
                it_group.m_source_group = source_groups.contains(it_group.m_uuid);
#ifdef DUNE_SKETCHER_ONLY
                it_group.m_check_sensitive = true;
                it_group.m_check_active = doc_view.group_is_visible(it_group.m_uuid);
#else
                if (is_current && is_current_doc) {
                    it_group.m_check_active = true;
                    it_group.m_check_sensitive = false;
                }
                else if (after_active) {
                    it_group.m_check_active = false;
                    it_group.m_check_sensitive = false;
                }
                else {
                    it_group.m_check_sensitive = true;
                    it_group.m_check_active = doc_view.group_is_visible(it_group.m_uuid);
                }
#endif
                {
                    auto msgs = gr.get_messages();
                    it_group.m_status = GroupStatusMessage::summarize(msgs);
                    Glib::ustring txt;
                    for (auto &msg : msgs) {
                        if (txt.size())
                            txt += "\n";
                        txt += msg.message;
                    }
                    it_group.m_status_message = txt;
                }
                if (auto it = doc_view.m_group_views.find(it_group.m_uuid); it != doc_view.m_group_views.end()
                    && it->second.m_color.has_value()) {
                    it_group.m_has_color = true;
                    it_group.m_color = rgba_from_color(*it->second.m_color);
                }
                else {
                    it_group.m_has_color = false;
                }
#ifndef DUNE_SKETCHER_ONLY
                if (is_current)
                    after_active = true;
#endif
            }
        }
        select_group(doci.get_uuid(), doci.get_current_group());
    }
    if (m_core.has_documents()) {
        auto &current_group = m_core.get_current_document().get_group(m_core.get_current_group());
        auto msgs = current_group.get_messages();
        auto st = GroupStatusMessage::summarize(msgs);
        if (st != GroupStatusMessage::Status::NONE) {
            m_info_bar->set_revealed(true);
            m_info_bar_icon->set_from_icon_name(icon_name_from_status(st));
            Glib::ustring txt;
            for (auto &msg : msgs) {
                if (txt.size())
                    txt += "\n";
                txt += msg.message;
            }
            m_info_bar_label->set_markup(txt);
        }
        else {
            m_info_bar->set_revealed(false);
        }
    }
    else {
        m_info_bar->set_revealed(false);
    }

    unblock_signals();
}

void WorkspaceBrowser::update_needs_save()
{
    for (size_t i_doc = 0; i_doc < m_document_store->get_n_items(); i_doc++) {
        auto &it_doc = *m_document_store->get_item(i_doc);
        auto &doci = m_core.get_idocument_info(it_doc.m_uuid);
        update_name(it_doc, doci);
    }
}

void WorkspaceBrowser::set_sketcher_folder_groups(const std::set<UUID> &folder_groups)
{
#ifdef DUNE_SKETCHER_ONLY
    m_sketcher_folder_groups = folder_groups;
#else
    (void)folder_groups;
#endif
}

class SolidModelToggleButton : public Gtk::ToggleButton {
public:
    SolidModelToggleButton()
    {
        add_css_class("solid-model-toggle-button");
        set_has_frame(false);
        signal_toggled().connect(sigc::mem_fun(*this, &SolidModelToggleButton::update_icon));
        m_area = Gtk::make_managed<Gtk::DrawingArea>();
        m_area->set_content_height(16);
        m_area->set_content_width(16);
        m_area->set_valign(Gtk::Align::CENTER);
        m_area->set_draw_func(sigc::mem_fun(*this, &SolidModelToggleButton::render_icon));
        set_child(*m_area);
        update_icon();
    }

    void set_body_color(const std::optional<Gdk::RGBA> &color)
    {
        m_body_color = color;
        m_area->queue_draw();
    }

private:
    void update_icon()
    {
        m_area->queue_draw();
    }

    void render_icon(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h)
    {
        static const std::vector<glm::vec2> points = {
                {.5, 4.}, {.5, 14.}, {10.5, 15.5}, {15.5, 11.}, {15.5, 1.5}, {6.5, .5},
        };
        static const glm::vec2 center = {10.5, 5.5};
        cr->set_line_width(1);
        cr->set_line_cap(Cairo::Context::LineCap::ROUND);
        cr->set_line_join(Cairo::Context::LineJoin::ROUND);
        const auto line_color = get_color();

        for (const auto &pt : points) {
            cr->line_to(pt.x, pt.y);
        }
        cr->close_path();

        if (get_active()) {
            auto solid_color = line_color;
            solid_color.set_alpha(.3);
            if (m_body_color.has_value()) {
                solid_color = *m_body_color;
            }
            Gdk::Cairo::set_source_rgba(cr, solid_color);
            cr->fill_preserve();
        }

        Gdk::Cairo::set_source_rgba(cr, line_color);

        cr->stroke();

        for (size_t i = 0; i < points.size(); i += 2) {
            cr->move_to(center.x, center.y);
            const auto &pt = points.at(i);
            cr->line_to(pt.x, pt.y);
            cr->stroke();
        }
    }

    Gtk::DrawingArea *m_area = nullptr;
    std::optional<Gdk::RGBA> m_body_color;
};

class WorkspaceBrowser::WorkspaceRow : public Gtk::TreeExpander {
public:
    WorkspaceRow(WorkspaceBrowser &browser) : m_browser(browser)
    {
        set_indent_for_depth(false);
        m_checkbutton = Gtk::make_managed<Gtk::CheckButton>();
        m_checkbutton->set_active(true);

        m_solid_toggle = Gtk::make_managed<SolidModelToggleButton>();
        m_solid_toggle->signal_toggled().connect([this] {
            m_browser.signal_body_solid_model_checked().emit(m_body->m_doc, m_body->m_uuid,
                                                             m_solid_toggle->get_active());
        });

        m_label = Gtk::make_managed<Gtk::Label>();
        m_label->set_halign(Gtk::Align::START);
        m_label->set_hexpand(true);
        m_label->set_ellipsize(Pango::EllipsizeMode::END);
        m_label->set_single_line_mode(true);
        m_label->set_has_tooltip();

        m_source_group_image = Gtk::make_managed<Gtk::Image>();
        m_source_group_image->set_hexpand(true);
        m_source_group_image->set_from_icon_name("action-link-symbolic");
        m_source_group_image->set_halign(Gtk::Align::START);
        m_source_group_image->set_tooltip_text("Source of current group");

        m_status_button = Gtk::make_managed<Gtk::MenuButton>();
        m_status_button->set_icon_name("dialog-information-symbolic");
        m_status_button->set_has_frame(false);

        m_status_label = Gtk::make_managed<Gtk::Label>();
        m_status_label->set_hexpand(true);
        m_status_label->set_halign(Gtk::Align::START);
        m_status_label->set_use_markup(true);
        m_status_label->signal_activate_link().connect(
                [this](const std::string &link) {
                    m_browser.m_signal_activate_link.emit(link);
                    return true;
                },
                false);


        m_close_button = Gtk::make_managed<Gtk::Button>();
        m_close_button->set_icon_name("window-close-symbolic");
        m_close_button->set_has_frame(false);
        m_close_button->signal_clicked().connect([this] { m_browser.m_signal_close_document.emit(m_doc->m_uuid); });

        {
            auto popover = Gtk::make_managed<Gtk::Popover>();
            popover->set_child(*m_status_label);
            m_status_button->set_popover(*popover);
        }


        {
            auto attr = Pango::Attribute::create_attr_weight(Pango::Weight::BOLD);
            m_attrs_bold.insert(attr);
        }

        m_checkbutton->signal_toggled().connect([this] {
            if (m_body)
                m_browser.signal_body_checked().emit(m_body->m_doc, m_body->m_uuid, m_checkbutton->get_active());
            if (m_group)
                m_browser.signal_group_checked().emit(m_group->m_doc, m_group->m_uuid, m_checkbutton->get_active());
            if (m_doc)
                m_browser.signal_document_checked().emit(m_doc->m_uuid, m_checkbutton->get_active());
        });

        m_dof_label = Gtk::make_managed<Gtk::Label>("0");
        m_dof_label->add_css_class("dim-label");
        m_dof_label->set_width_chars(2);

        m_color_indicator = Gtk::make_managed<Gtk::DrawingArea>();
        m_color_indicator->set_content_width(10);
        m_color_indicator->set_content_height(10);
        m_color_indicator->set_valign(Gtk::Align::CENTER);
        m_color_indicator->set_visible(false);
        m_color_indicator->set_draw_func([this](const Cairo::RefPtr<Cairo::Context> &cr, int w, int h) {
            if (!m_row_color.has_value())
                return;
            const double radius = std::max(2.0, (std::min(w, h) / 2.0) - 1.0);
            cr->arc(w / 2.0, h / 2.0, radius, 0, 2.0 * M_PI);
            Gdk::Cairo::set_source_rgba(cr, *m_row_color);
            cr->fill_preserve();

            auto border = get_color();
            border.set_alpha(0.35);
            Gdk::Cairo::set_source_rgba(cr, border);
            cr->set_line_width(1.0);
            cr->stroke();
        });

        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
        auto box2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
        box2->set_hexpand(true);
        box->append(*m_checkbutton);
        box->append(*m_solid_toggle);
        box2->append(*m_color_indicator);
        box2->append(*m_label);
        box2->append(*m_source_group_image);
        box->append(*box2);

        box->append(*m_status_button);
        box->append(*m_close_button);
        box->append(*m_dof_label);

        set_child(*box);

        auto controller = Gtk::GestureClick::create();
        controller->set_button(3);
        controller->signal_pressed().connect([this](int n_press, double x, double y) {
            if (!m_body && !m_group)
                return;
            const graphene_point_t pt_in{(float)x, (float)y};
            graphene_point_t pt_out;
            if (!gtk_widget_compute_point(GTK_WIDGET(gobj()), GTK_WIDGET(m_browser.gobj()), &pt_in, &pt_out))
                return;
            Gdk::Rectangle rect;
            rect.set_x(pt_out.x);
            rect.set_y(pt_out.y);
            if (m_group) {
                m_browser.m_group_menu_document = m_group->m_doc;
                m_browser.m_group_menu_group = m_group->m_uuid;
                m_browser.m_reset_group_color_action->set_enabled(m_group->m_has_color);
                m_browser.m_group_popover->set_pointing_to(rect);
                m_browser.m_group_popover->popup();
                return;
            }

            m_browser.m_body_menu_document = m_body->m_doc;
            m_browser.m_body_menu_body = m_body->m_uuid;
            m_browser.m_reset_body_color_action->set_enabled(m_body->m_has_color);
            m_browser.m_body_popover->set_pointing_to(rect);
            m_browser.m_body_popover->popup();
        });
        add_controller(controller);
    }

    void bind(DocumentItem &it)
    {
        m_doc = &it;
        m_browser.block_signals();
#ifdef DUNE_SKETCHER_ONLY
        set_visible(false);
        if (auto row_widget = get_parent())
            row_widget->set_visible(false);
#else
        set_visible(true);
        if (auto row_widget = get_parent())
            row_widget->set_visible(true);
#endif
        m_checkbutton->set_visible(true);
        m_solid_toggle->set_visible(false);
        m_dof_label->set_visible(false);
        m_status_button->set_visible(false);
        m_close_button->set_visible(true);
        m_source_group_image->set_visible(false);
        m_label->set_attributes(m_attrs_normal);
        set_color_indicator({});
        m_bindings.push_back(Glib::Binding::bind_property_value(
                it.m_check_active.get_proxy(), m_checkbutton->property_active(), Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_check_sensitive.get_proxy(),
                                                                m_checkbutton->property_sensitive(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_name.get_proxy(), m_label->property_label(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_close_sensitive.get_proxy(),
                                                                m_close_button->property_sensitive(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(
                it.m_tooltip.get_proxy(), m_label->property_tooltip_text(), Glib::Binding::Flags::SYNC_CREATE));
        update_label_attrs(it);


        m_connections.push_back(
                it.m_active.get_proxy().signal_changed().connect([this, &it] { update_label_attrs(it); }));

        set_hide_expander(true);

        m_browser.unblock_signals();
    }
    void bind(BodyItem &it)
    {
        m_browser.block_signals();
#ifdef DUNE_SKETCHER_ONLY
        const bool is_current_doc = it.m_doc == m_browser.m_core.get_current_idocument_info().get_uuid();
        const bool show_row = it.m_show_in_sketcher && is_current_doc;
        set_visible(show_row);
        if (auto row_widget = get_parent())
            row_widget->set_visible(show_row);
#else
        set_visible(true);
        if (auto row_widget = get_parent())
            row_widget->set_visible(true);
#endif
        m_body = &it;
#ifdef DUNE_SKETCHER_ONLY
        const bool is_folder_row = it.m_show_in_sketcher;
        const bool show_folder_checkbox =
                is_folder_row && (m_browser.m_sketcher_folder_groups.size() > 1 || !it.m_check_active.get_value());
        m_checkbutton->set_visible(!is_folder_row || show_folder_checkbox);
        m_checkbutton->set_active(true);
        m_checkbutton->set_sensitive(!is_folder_row || show_folder_checkbox);
        m_solid_toggle->set_visible(false);
#else
        m_checkbutton->set_active(true);
        m_checkbutton->set_sensitive(true);
        m_solid_toggle->set_visible(true);
#endif
        m_dof_label->set_visible(false);
        m_status_button->set_visible(false);
        m_close_button->set_visible(false);
        m_source_group_image->set_visible(false);
        m_label->set_attributes(m_attrs_normal);
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_name.get_proxy(), m_label->property_label(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(
                it.m_check_active.get_proxy(), m_checkbutton->property_active(), Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_check_sensitive.get_proxy(),
                                                                m_checkbutton->property_sensitive(),
                                                                Glib::Binding::Flags::SYNC_CREATE));

        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_solid_model_active.get_proxy(),
                                                                m_solid_toggle->property_active(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        get_list_row()->set_expanded(it.m_expanded);
        m_connections.push_back(it.m_expanded.get_proxy().signal_changed().connect(
                [this, &it] { get_list_row()->set_expanded(it.m_expanded); }));
        const bool has_color = it.m_has_color.get_value();
        if (has_color)
            m_solid_toggle->set_body_color(it.m_color.get_value());
        else
            m_solid_toggle->set_body_color({});
        update_body_color(it);

        m_connections.push_back(
                it.m_color.get_proxy().signal_changed().connect([this, &it] { update_body_color(it); }));
        m_connections.push_back(
                it.m_has_color.get_proxy().signal_changed().connect([this, &it] { update_body_color(it); }));

        const auto body_uu = it.m_uuid;
        m_connections.push_back(get_list_row()->property_expanded().signal_changed().connect([this, body_uu, &it] {
            const auto expanded = get_list_row()->get_expanded();
            if (m_browser.emit_body_expanded(body_uu, expanded))
                it.m_expanded = expanded;
            else
                it.m_expanded = true;
        }));

        m_browser.unblock_signals();
    }
    void bind(GroupItem &it)
    {
        m_browser.block_signals();
        set_visible(true);
        if (auto row_widget = get_parent())
            row_widget->set_visible(true);
        m_solid_toggle->set_visible(false);
        m_dof_label->set_visible(true);
        m_status_button->set_visible(true);
        m_close_button->set_visible(false);
        m_group = &it;
        set_group_color(it);
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_name.get_proxy(), m_label->property_label(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(
                it.m_status_message.get_proxy(), m_status_label->property_label(), Glib::Binding::Flags::SYNC_CREATE));
        update_label_attrs(it);
        m_connections.push_back(
                it.m_active.get_proxy().signal_changed().connect([this, &it] { update_label_attrs(it); }));

        m_bindings.push_back(Glib::Binding::bind_property_value(
                it.m_check_active.get_proxy(), m_checkbutton->property_active(), Glib::Binding::Flags::SYNC_CREATE));

        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_check_sensitive.get_proxy(),
                                                                m_checkbutton->property_sensitive(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_bindings.push_back(Glib::Binding::bind_property_value(it.m_source_group.get_proxy(),
                                                                m_source_group_image->property_visible(),
                                                                Glib::Binding::Flags::SYNC_CREATE));
        m_dof_label->set_text(std::to_string(it.m_dof.get_value()));
        m_connections.push_back(it.m_dof.get_proxy().signal_changed().connect(
                [this, &it] { m_dof_label->set_text(std::to_string(it.m_dof.get_value())); }));

        set_status(it.m_status.get_value());
        m_connections.push_back(
                it.m_status.get_proxy().signal_changed().connect([this, &it] { set_status(it.m_status.get_value()); }));
        m_connections.push_back(
                it.m_color.get_proxy().signal_changed().connect([this, &it] { set_group_color(it); }));
        m_connections.push_back(
                it.m_has_color.get_proxy().signal_changed().connect([this, &it] { set_group_color(it); }));

        m_browser.unblock_signals();
    }

    void unbind()
    {
        for (auto &conn : m_connections)
            conn.disconnect();
        m_connections.clear();
        for (auto bind : m_bindings)
            bind->unbind();
        m_bindings.clear();
        m_group = nullptr;
        m_body = nullptr;
        m_doc = nullptr;
        set_hide_expander(false);
    }

private:
    WorkspaceBrowser &m_browser;

    const DocumentItem *m_doc = nullptr;
    const GroupItem *m_group = nullptr;
    const BodyItem *m_body = nullptr;

    Gtk::CheckButton *m_checkbutton = nullptr;
    SolidModelToggleButton *m_solid_toggle = nullptr;
    Gtk::Label *m_label = nullptr;
    Gtk::DrawingArea *m_color_indicator = nullptr;
    Gtk::Image *m_source_group_image = nullptr;
    Gtk::Label *m_dof_label = nullptr;
    Gtk::MenuButton *m_status_button = nullptr;
    Gtk::Label *m_status_label = nullptr;
    Gtk::Button *m_close_button = nullptr;

    std::vector<Glib::RefPtr<Glib::Binding>> m_bindings;
    std::vector<sigc::connection> m_connections;

    Pango::AttrList m_attrs_normal;
    Pango::AttrList m_attrs_bold;
    std::optional<Gdk::RGBA> m_row_color;

    void update_label_attrs(GroupItem &it)
    {
        update_label_attrs(it.m_active);
    }
    void update_label_attrs(DocumentItem &it)
    {
        update_label_attrs(it.m_active);
    }
    void update_label_attrs(const Glib::Property<bool> &prop)
    {
        if (prop.get_value())
            m_label->set_attributes(m_attrs_bold);
        else
            m_label->set_attributes(m_attrs_normal);
    }

    void set_status(GroupStatusMessage::Status st)
    {
        using S = GroupStatusMessage::Status;
        m_status_button->set_visible(st != S::NONE);
        m_status_button->set_icon_name(icon_name_from_status(st));
    }

    void set_color_indicator(const std::optional<Gdk::RGBA> &color)
    {
        m_row_color = color;
        m_color_indicator->set_visible(color.has_value());
        m_color_indicator->queue_draw();
    }

    void update_body_color(const BodyItem &it)
    {
        const bool has_color = it.m_has_color.get_value();
        if (has_color)
            m_solid_toggle->set_body_color(it.m_color.get_value());
        else
            m_solid_toggle->set_body_color({});
        if (has_color)
            set_color_indicator(it.m_color.get_value());
        else
            set_color_indicator({});
    }

    void set_group_color(const GroupItem &it)
    {
        const bool has_color = it.m_has_color.get_value();
        if (has_color)
            set_color_indicator(it.m_color.get_value());
        else
            set_color_indicator({});
    }
};

WorkspaceBrowser::WorkspaceBrowser(Core &core) : Gtk::Box(Gtk::Orientation::VERTICAL), m_core(core)
{
    m_document_store = Gio::ListStore<DocumentItem>::create();


    // Set list model and selection model.
    // passthrough must be false when Gtk::TreeExpander is used in the view.
    m_model = Gtk::TreeListModel::create(m_document_store, sigc::mem_fun(*this, &WorkspaceBrowser::create_model),
                                         /* passthrough */ false, /* autoexpand */ true);
    m_selection_model = Gtk::SingleSelection::create(m_model);

    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect([this](const Glib::RefPtr<Gtk::ListItem> &list_item) {
        // Each ListItem contains a TreeExpander, which contains a Label.
        // The Label shows the StringObject's string. That's done in on_bind_name().
        auto expander = Gtk::make_managed<WorkspaceRow>(*this);
        list_item->set_child(*expander);
    });
    factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem> &list_item) {
        // When TreeListModel::property_passthrough() is false, ListItem::get_item()
        // is a TreeListRow. TreeExpander needs the TreeListRow.
        // The StringObject item is returned by TreeListRow::get_item().
        auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(list_item->get_item());
        if (!row)
            return;
        auto expander = dynamic_cast<WorkspaceRow *>(list_item->get_child());
        if (!expander)
            return;
        expander->set_list_row(row);
        if (auto col = std::dynamic_pointer_cast<DocumentItem>(row->get_item())) {
            expander->bind(*col);
            // expander->set_label(col->m_name.get_value());
        }
        else if (auto col = std::dynamic_pointer_cast<BodyItem>(row->get_item())) {
            expander->bind(*col);
            // expander->set_label("Body " + col->m_name.get_value());
        }
        else if (auto col = std::dynamic_pointer_cast<GroupItem>(row->get_item())) {
            expander->bind(*col);
            // expander->set_label("Group " + col->m_name.get_value());
        }
    });
    factory->signal_unbind().connect([](const Glib::RefPtr<Gtk::ListItem> &list_item) {
        // When TreeListModel::property_passthrough() is false, ListItem::get_item()
        // is a TreeListRow. TreeExpander needs the TreeListRow.
        // The StringObject item is returned by TreeListRow::get_item().
        auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(list_item->get_item());
        if (!row)
            return;
        auto expander = dynamic_cast<WorkspaceRow *>(list_item->get_child());
        if (!expander)
            return;
        expander->unbind();
    });


    m_view = Gtk::make_managed<Gtk::ListView>(m_selection_model, factory);
    // m_view->set_single_click_activate(true);
    /*  m_view->signal_activate().connect([this](guint index) {
          auto it = m_model->get_row(index);
          auto tr = std::dynamic_pointer_cast<Gtk::TreeListRow>(it);
          if (!tr)
              return;
          if (auto gr = std::dynamic_pointer_cast<WorkspaceBrowser::GroupItem>(tr->get_item())) {
              std::cout << "sel gr " << gr->m_name << std::endl;
              m_signal_group_selected.emit(gr->m_doc, gr->m_uuid);
          }
      });*/
    m_selection_model->signal_selection_changed().connect([this](guint, guint) {
        auto sel = m_selection_model->get_selected_item();
        auto tr = std::dynamic_pointer_cast<Gtk::TreeListRow>(sel);
        if (!tr)
            return;
        if (auto gr = std::dynamic_pointer_cast<WorkspaceBrowser::GroupItem>(tr->get_item())) {
            m_signal_group_selected.emit(gr->m_doc, gr->m_uuid);
        }
    });
    m_view->add_css_class("navigation-sidebar");
#ifdef DUNE_SKETCHER_ONLY
    m_sketcher_open_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
    m_sketcher_open_box->set_margin_start(6);
    m_sketcher_open_box->set_margin_end(6);
    m_sketcher_open_box->set_margin_top(6);
    m_sketcher_open_box->set_margin_bottom(6);
    m_sketcher_open_box->set_halign(Gtk::Align::CENTER);
    m_sketcher_open_box->set_hexpand(true);
    append(*m_sketcher_open_box);
#endif
    {
        auto sc = Gtk::make_managed<Gtk::ScrolledWindow>();
        sc->set_child(*m_view);
        sc->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        sc->set_vexpand(true);

        auto overlay = Gtk::make_managed<Gtk::Overlay>();
        overlay->set_vexpand(true);
        overlay->set_child(*sc);

        m_toast_label = Gtk::make_managed<Gtk::Label>();
        m_toast_label->set_wrap(true);
        m_toast_revealer = Gtk::make_managed<Gtk::Revealer>();
        {
            auto ctrl = Gtk::EventControllerMotion::create();
            ctrl->signal_enter().connect([this](double, double) { m_toast_connection.disconnect(); });
            ctrl->signal_leave().connect([this] {
                m_toast_connection.disconnect();
                m_toast_connection = Glib::signal_timeout().connect(
                        [this] {
                            m_toast_revealer->set_reveal_child(false);
                            return false;
                        },
                        3000);
            });
            m_toast_revealer->add_controller(ctrl);
        }
        m_toast_revealer->set_child(*m_toast_label);
        m_toast_revealer->set_visible(false);
        m_toast_revealer->set_halign(Gtk::Align::CENTER);
        m_toast_revealer->set_valign(Gtk::Align::END);
        m_toast_revealer->set_transition_type(Gtk::RevealerTransitionType::CROSSFADE);
        m_toast_revealer->add_css_class("osd");
        m_toast_revealer->add_css_class("workspace-browser-toast");
        m_toast_revealer->set_margin(20);
        m_toast_revealer->property_child_revealed().signal_changed().connect(
                [this] { m_toast_revealer->set_visible(m_toast_revealer->get_child_revealed()); });
        overlay->add_overlay(*m_toast_revealer);

        append(*overlay);
    }

    m_info_bar = Gtk::make_managed<Gtk::InfoBar>();
    m_info_bar->set_revealed(false);
    m_info_bar->set_message_type(Gtk::MessageType::ERROR);
    m_info_bar_icon = Gtk::make_managed<Gtk::Image>();
    m_info_bar_label = Gtk::make_managed<Gtk::Label>("foo");
    m_info_bar_label->signal_activate_link().connect(
            [this](const std::string &link) {
                m_signal_activate_link.emit(link);
                return true;
            },
            false);

    m_info_bar_label->set_wrap(true);
    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        box->append(*m_info_bar_icon);
        box->append(*m_info_bar_label);
        m_info_bar->add_child(*box);
    }


    append(*m_info_bar);

    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
#ifdef DUNE_SKETCHER_ONLY
        m_sketcher_actions_box = box;
#endif
        box->add_css_class("toolbar");
        box->set_homogeneous(true);
        box->set_hexpand(true);
        {
            auto button = Gtk::make_managed<Gtk::Button>();
            button->set_icon_name("list-add-symbolic");
            button->set_tooltip_text("Add sketch");
            button->set_hexpand(true);
            button->signal_clicked().connect([this] { emit_add_group(Group::Type::SKETCH); });
#ifdef DUNE_SKETCHER_ONLY
            m_sketcher_add_button = button;
#endif
            box->append(*button);
        }
        {
            auto button = Gtk::make_managed<Gtk::Button>();
            button->set_icon_name("list-remove-symbolic");
            button->set_tooltip_text("Delete current group");
            button->set_hexpand(true);
            auto delete_with_file = std::make_shared<bool>(false);
            auto controller = Gtk::GestureClick::create();
            controller->set_button(GDK_BUTTON_PRIMARY);
            controller->signal_pressed().connect([controller, delete_with_file](int, double, double) {
                *delete_with_file = static_cast<bool>(controller->get_current_event_state()
                                                      & Gdk::ModifierType::SHIFT_MASK);
            });
            button->add_controller(controller);
            button->signal_clicked().connect([this, delete_with_file] {
                m_signal_delete_current_group.emit(*delete_with_file);
                *delete_with_file = false;
            });
            box->append(*button);
        }
        {
            auto button = Gtk::make_managed<Gtk::Button>();
            button->set_icon_name("go-up-symbolic");
            button->set_tooltip_text("Move group up");
            button->set_hexpand(true);
            button->signal_clicked().connect([this] { m_signal_move_group.emit(Document::MoveGroup::UP); });
            box->append(*button);
        }
        {
            auto button = Gtk::make_managed<Gtk::Button>();
            button->set_icon_name("go-down-symbolic");
            button->set_tooltip_text("Move group down");
            button->set_hexpand(true);
            button->signal_clicked().connect([this] { m_signal_move_group.emit(Document::MoveGroup::DOWN); });
            box->append(*button);
        }
        {
#ifndef DUNE_SKETCHER_ONLY
            auto button = Gtk::make_managed<Gtk::Button>();
            button->set_icon_name("folder-open-symbolic");
            button->set_tooltip_text("Open folder");
            button->set_hexpand(true);
            button->signal_clicked().connect([this] { m_signal_open_folder.emit(); });
            box->append(*button);
#endif
        }
#ifndef DUNE_SKETCHER_ONLY
        {
            auto button = Gtk::make_managed<Gtk::Button>();
            button->set_icon_name("action-move-group-down2-symbolic");
            button->set_tooltip_text("Move group to end of body / next body");
            button->set_hexpand(true);
            button->signal_clicked().connect([this] { m_signal_move_group.emit(Document::MoveGroup::END_OF_BODY); });
            box->append(*button);
        }
        {
            auto button = Gtk::make_managed<Gtk::Button>();
            button->set_icon_name("action-move-group-down3-symbolic");
            button->set_tooltip_text("Move group to end of document");
            button->set_hexpand(true);
            button->signal_clicked().connect(
                    [this] { m_signal_move_group.emit(Document::MoveGroup::END_OF_DOCUMENT); });
            box->append(*button);
        }
#endif
#ifdef DUNE_SKETCHER_ONLY
        auto actions_overlay = Gtk::make_managed<Gtk::Overlay>();
        actions_overlay->set_child(*box);
        if (m_sketcher_add_button) {
            m_sketcher_add_flyout = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
            m_sketcher_add_flyout->set_halign(Gtk::Align::START);
            m_sketcher_add_flyout->set_valign(Gtk::Align::END);
            m_sketcher_add_flyout->set_margin_start(6);
            m_sketcher_add_flyout->set_margin_bottom(46);
            m_sketcher_add_flyout->set_visible(false);

            m_sketcher_open_folder_button = Gtk::make_managed<Gtk::Button>();
            m_sketcher_open_folder_button->set_icon_name("folder-open-symbolic");
            m_sketcher_open_folder_button->set_tooltip_text("Open folder");
            m_sketcher_open_folder_button->set_has_frame(true);
            m_sketcher_open_folder_button->set_hexpand(true);
            m_sketcher_open_folder_button->set_halign(Gtk::Align::FILL);
            m_sketcher_open_folder_button->signal_clicked().connect([this] {
                if (m_sketcher_add_flyout)
                    m_sketcher_add_flyout->set_visible(false);
                m_signal_open_folder.emit();
            });
            m_sketcher_add_flyout->append(*m_sketcher_open_folder_button);
            actions_overlay->add_overlay(*m_sketcher_add_flyout);
            install_hover_flyout(*m_sketcher_add_button, *m_sketcher_add_flyout);
        }
        append(*actions_overlay);
#else
        append(*box);
#endif
    }

    m_body_menu = Gio::Menu::create();
    auto actions = Gio::SimpleActionGroup::create();
    m_reset_body_color_action = actions->add_action(
            "reset_color", [this] { signal_reset_body_color().emit(m_body_menu_document, m_body_menu_body); });
    actions->add_action("rename", [this] { signal_rename_body().emit(m_body_menu_document, m_body_menu_body); });
    actions->add_action("set_color", [this] { signal_set_body_color().emit(m_body_menu_document, m_body_menu_body); });
    insert_action_group("body", actions);
    m_body_menu->append("Set color", "body.set_color");
    m_body_menu->append("Reset color", "body.reset_color");
    m_body_menu->append("Rename", "body.rename");


    m_body_popover = Gtk::make_managed<Gtk::PopoverMenu>();
    m_body_popover->set_menu_model(m_body_menu);

    m_body_popover->set_parent(*this);

    m_group_menu = Gio::Menu::create();
    auto group_actions = Gio::SimpleActionGroup::create();
    m_reset_group_color_action = group_actions->add_action(
            "reset_color", [this] { signal_reset_group_color().emit(m_group_menu_document, m_group_menu_group); });
    group_actions->add_action("rename", [this] { signal_rename_group().emit(m_group_menu_document, m_group_menu_group); });
    group_actions->add_action(
            "set_color", [this] { signal_set_group_color().emit(m_group_menu_document, m_group_menu_group); });
    insert_action_group("group", group_actions);
    m_group_menu->append("Set color", "group.set_color");
    m_group_menu->append("Reset color", "group.reset_color");
    m_group_menu->append("Rename", "group.rename");

    m_group_popover = Gtk::make_managed<Gtk::PopoverMenu>();
    m_group_popover->set_menu_model(m_group_menu);
    m_group_popover->set_parent(*this);
}

void WorkspaceBrowser::set_sketcher_open_controls(Gtk::Button &open_button, Gtk::MenuButton &open_menu_button)
{
#ifdef DUNE_SKETCHER_ONLY
    if (!m_sketcher_open_box || !m_sketcher_add_flyout)
        return;
    if (auto parent = dynamic_cast<Gtk::Box *>(open_button.get_parent())) {
        parent->remove(open_button);
        if (!parent->get_first_child())
            parent->set_visible(false);
    }
    if (auto parent = dynamic_cast<Gtk::Box *>(open_menu_button.get_parent())) {
        parent->remove(open_menu_button);
        if (!parent->get_first_child())
            parent->set_visible(false);
    }

    open_menu_button.unset_child();
    open_menu_button.set_label("Recent");
    open_menu_button.set_always_show_arrow(true);
    open_menu_button.set_has_frame(false);
    open_menu_button.add_css_class("flat");
    open_menu_button.set_tooltip_text("Recent files and folders");
    open_menu_button.set_hexpand(true);
    auto open_icon = Gtk::make_managed<Gtk::Image>();
    open_icon->set_from_icon_name("document-open-symbolic");
    open_button.set_child(*open_icon);
    open_button.set_tooltip_text("Open file");
    open_button.set_hexpand(true);
    open_button.set_halign(Gtk::Align::FILL);
    open_button.set_has_frame(true);
    open_button.remove_css_class("flat");
    open_button.set_visible(true);
    open_menu_button.set_visible(true);
    if (open_menu_button.get_parent() != m_sketcher_open_box)
        m_sketcher_open_box->append(open_menu_button);
    if (!m_sketcher_open_project_button) {
        m_sketcher_open_project_button = Gtk::make_managed<Gtk::Button>();
        m_sketcher_open_project_button->set_icon_name("document-open-symbolic");
        m_sketcher_open_project_button->set_tooltip_text("Open project");
        m_sketcher_open_project_button->set_has_frame(false);
        m_sketcher_open_project_button->add_css_class("flat");
        m_sketcher_open_project_button->signal_clicked().connect([this] { m_signal_open_project.emit(); });
    }
    if (!m_sketcher_save_project_button) {
        m_sketcher_save_project_button = Gtk::make_managed<Gtk::Button>();
        m_sketcher_save_project_button->set_icon_name("document-save-symbolic");
        m_sketcher_save_project_button->set_tooltip_text("Save project");
        m_sketcher_save_project_button->set_has_frame(false);
        m_sketcher_save_project_button->add_css_class("flat");
        m_sketcher_save_project_button->signal_clicked().connect([this] { m_signal_save_project.emit(); });
    }
    if (m_sketcher_open_project_button->get_parent() != m_sketcher_open_box)
        m_sketcher_open_box->append(*m_sketcher_open_project_button);
    if (m_sketcher_save_project_button->get_parent() != m_sketcher_open_box)
        m_sketcher_open_box->append(*m_sketcher_save_project_button);
    if (open_button.get_parent() != m_sketcher_add_flyout)
        m_sketcher_add_flyout->prepend(open_button);
    open_button.signal_clicked().connect([this] {
        if (m_sketcher_add_flyout)
            m_sketcher_add_flyout->set_visible(false);
    });
#else
    (void)open_button;
    (void)open_menu_button;
#endif
}

void WorkspaceBrowser::emit_add_group(GroupType type, AddGroupMode add_group_mode)
{
    m_signal_add_group.emit(type, add_group_mode);
}

bool WorkspaceBrowser::emit_body_expanded(const UUID &body_uu, bool expanded)
{
    if (m_blocked_count)
        return true;
    return m_signal_body_expanded.emit(body_uu, expanded);
}

Glib::RefPtr<Gio::ListModel> WorkspaceBrowser::create_model(const Glib::RefPtr<Glib::ObjectBase> &item)
{
    // The items in a StringList are StringObjects.
    if (auto col = std::dynamic_pointer_cast<DocumentItem>(item))
        return col->m_body_store;
    if (auto col = std::dynamic_pointer_cast<BodyItem>(item))
        return col->m_group_store;
    return nullptr;
    /*Glib::RefPtr<Gio::ListModel> result;
    if (!col)
        // Top names
        result = Gtk::StringList::create({"Billy Bob", "Joey Jojo", "Rob McRoberts"});
    else if (col->get_string() == "Billy Bob")
        result = Gtk::StringList::create({"Billy Bob Junior", "Sue Bob"});
    else if (col->get_string() == "Rob McRoberts")
        result = Gtk::StringList::create({"Xavier McRoberts"});
*/
    // If result is empty, it's a leaf in the tree, i.e. an item without children.
    // Returning an empty RefPtr (not a RefPtr with an empty StringList)
    // signals that the item is not expandable.
    // return result;
}

void WorkspaceBrowser::group_prev_next(int dir)
{
    auto next_group = m_core.get_current_document().get_group_rel(m_core.get_current_group(), dir);
    if (!next_group)
        return;

    select_group(next_group);
}

void WorkspaceBrowser::select_group(const UUID &uu)
{
    select_group(m_core.get_current_idocument_info().get_uuid(), uu);
}

void WorkspaceBrowser::select_group(const UUID &doc_uu, const UUID &uu)
{
    {
        const auto n = m_selection_model->get_n_items();

        auto &doc = m_core.get_idocument_info(doc_uu).get_document();
        const auto body_group = doc.get_group(uu).find_body(doc).group.m_uuid;
        {
            for (size_t i = 0; i < n; i++) {
                auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(m_selection_model->get_object(i));
                if (!row)
                    continue;
                auto it = std::dynamic_pointer_cast<BodyItem>(row->get_item());
                if (!it)
                    continue;
                if (it->m_doc == doc_uu && it->m_uuid == body_group)
                    row->set_expanded(true);
            }
        }
    }
    const auto n = m_selection_model->get_n_items();

    for (size_t i = 0; i < n; i++) {
        auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(m_selection_model->get_object(i));
        if (!row)
            continue;
        auto it = std::dynamic_pointer_cast<GroupItem>(row->get_item());
        if (!it)
            continue;

        if (it->m_doc == m_core.get_current_idocument_info().get_uuid() && it->m_uuid == uu) {
            m_selection_model->select_item(i, true);
            return;
        }
    }
}

void WorkspaceBrowser::show_toast(const std::string &msg)
{
    m_toast_connection.disconnect();
    m_toast_label->set_label(msg);
    m_toast_revealer->set_reveal_child(true);
    m_toast_revealer->set_visible(true);
    m_toast_connection = Glib::signal_timeout().connect(
            [this] {
                m_toast_revealer->set_reveal_child(false);
                return false;
            },
            3000);
}

} // namespace dune3d

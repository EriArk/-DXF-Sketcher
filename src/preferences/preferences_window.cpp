#include "preferences_window.hpp"
#include "preferences.hpp"
#include "preferences_window_keys.hpp"
#include "preferences_window_in_tool_keys.hpp"
#include "preferences_window_canvas.hpp"
#include "preferences_window_misc.hpp"

namespace dune3d {

PreferencesWindow::PreferencesWindow(Preferences &prefs) : Gtk::Window(), m_preferences(prefs)
{
    set_default_size(600, 500);
    // set_type_hint();
    auto header = Gtk::make_managed<Gtk::HeaderBar>();
    header->set_show_title_buttons(true);
    set_title("Preferences");
    set_titlebar(*header);
    // header->show();

    m_keys_header_actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    m_keys_header_actions->add_css_class("linked");
    m_keys_header_actions->set_visible(false);
    header->pack_end(*m_keys_header_actions);

    auto defaults_button = Gtk::make_managed<Gtk::Button>();
    defaults_button->set_icon_name("document-revert-symbolic");
    defaults_button->set_tooltip_text("Load defaults");
    m_keys_header_actions->append(*defaults_button);

    auto import_button = Gtk::make_managed<Gtk::Button>();
    import_button->set_icon_name("document-open-symbolic");
    import_button->set_tooltip_text("Import key bindings");
    m_keys_header_actions->append(*import_button);

    auto export_button = Gtk::make_managed<Gtk::Button>();
    export_button->set_icon_name("document-save-as-symbolic");
    export_button->set_tooltip_text("Export key bindings");
    m_keys_header_actions->append(*export_button);

    defaults_button->signal_clicked().connect([this] {
        if (auto ed = get_active_keys_editor())
            ed->load_defaults();
    });
    import_button->signal_clicked().connect([this] {
        if (auto ed = get_active_keys_editor())
            ed->import_keys();
    });
    export_button->signal_clicked().connect([this] {
        if (auto ed = get_active_keys_editor())
            ed->export_keys();
    });

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);

    m_stack = Gtk::make_managed<Gtk::Stack>();
    m_stack->set_hhomogeneous(false);
    box->append(*m_stack);

    {
        auto ed = CanvasPreferencesEditor::create(m_preferences);
        m_stack->add(*ed, "canvas", "Appearance");
        ed->unreference();
    }
    {
        auto ed = Gtk::make_managed<MiscPreferencesEditor>(m_preferences);
        m_stack->add(*ed, "editor", "Editor");
    }
    {
        auto keys_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        m_keys_notebook = Gtk::make_managed<Gtk::Notebook>();
        m_keys_notebook->set_hexpand(true);
        m_keys_notebook->set_vexpand(true);

        {
            m_keys_editor = KeySequencesPreferencesEditor::create(m_preferences);
            m_keys_notebook->append_page(*m_keys_editor, "Keys");
            m_keys_editor->unreference();
        }
        {
            m_in_tool_keys_editor = InToolKeySequencesPreferencesEditor::create(m_preferences);
            m_keys_notebook->append_page(*m_in_tool_keys_editor, "In-tool Keys");
            m_in_tool_keys_editor->unreference();
        }

        keys_box->append(*m_keys_notebook);
        m_stack->add(*keys_box, "keys", "Keys");
    }

    // Keep this static in sketcher mode: page switches are programmatic (show_page()).
    update_keys_header_actions_visibility();

    set_child(*box);
}

void PreferencesWindow::show_page(const std::string &pg)
{
    m_stack->set_visible_child(pg);
    update_keys_header_actions_visibility();
}

void PreferencesWindow::update_keys_header_actions_visibility()
{
    if (!m_keys_header_actions || !m_stack)
        return;
    m_keys_header_actions->set_visible(m_stack->get_visible_child_name() == "keys");
}

KeySequencesPreferencesEditorBase *PreferencesWindow::get_active_keys_editor()
{
    if (!m_keys_notebook)
        return nullptr;
    return m_keys_notebook->get_current_page() == 1 ? static_cast<KeySequencesPreferencesEditorBase *>(m_in_tool_keys_editor)
                                                    : static_cast<KeySequencesPreferencesEditorBase *>(m_keys_editor);
}

} // namespace dune3d

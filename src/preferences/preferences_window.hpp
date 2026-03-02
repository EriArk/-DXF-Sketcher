#pragma once
#include <gtkmm.h>

namespace dune3d {

class KeySequencesPreferencesEditorBase;
class KeySequencesPreferencesEditor;
class InToolKeySequencesPreferencesEditor;

class PreferencesWindow : public Gtk::Window {
public:
    PreferencesWindow(class Preferences &pr);
    void show_page(const std::string &pg);

private:
    void update_keys_header_actions_visibility();
    KeySequencesPreferencesEditorBase *get_active_keys_editor();

    class Preferences &m_preferences;
    Gtk::Stack *m_stack = nullptr;
    Gtk::Notebook *m_keys_notebook = nullptr;
    KeySequencesPreferencesEditor *m_keys_editor = nullptr;
    InToolKeySequencesPreferencesEditor *m_in_tool_keys_editor = nullptr;
    Gtk::Box *m_keys_header_actions = nullptr;
};
} // namespace dune3d

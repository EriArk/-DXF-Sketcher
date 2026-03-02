#include "dune3d_application.hpp"
#include "dune3d_appwindow.hpp"
#include "util/util.hpp"
#include "util/fs_util.hpp"
#include "nlohmann/json.hpp"
#include "preferences/preferences_window.hpp"
#include "widgets/about_dialog.hpp"
#include "logger/logger.hpp"
#include "widgets/log_window.hpp"
#include "widgets/log_view.hpp"
#include "logger/log_util.hpp"
#include "editor/buffer.hpp"
#include "action/action.hpp"
#include "action/action_catalog.hpp"
#include "action/action_id.hpp"
#include "core/tool_id.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include "in_tool_action/in_tool_action_catalog.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <set>
#include <map>

namespace dune3d {
namespace {

#ifdef DUNE_SKETCHER_ONLY
static bool key_item_visible_in_sketcher(const ActionToolID &id)
{
    if (!action_catalog.contains(id))
        return false;
    const auto &cat = action_catalog.at(id);
    if (cat.group == ActionGroup::GROUP)
        return false;
    if (cat.flags & ActionCatalogItem::FLAGS_HIDDEN)
        return false;

    if (const auto action = std::get_if<ActionID>(&id)) {
        switch (*action) {
        case ActionID::VIEW_ROTATE_UP:
        case ActionID::VIEW_ROTATE_DOWN:
        case ActionID::VIEW_ROTATE_LEFT:
        case ActionID::VIEW_ROTATE_RIGHT:
        case ActionID::VIEW_TILT_LEFT:
        case ActionID::VIEW_TILT_RIGHT:
        case ActionID::VIEW_TOGGLE_PERSP_ORTHO:
        case ActionID::VIEW_PERSP:
        case ActionID::VIEW_ORTHO:
        case ActionID::VIEW_RESET_TILT:
        case ActionID::VIEW_FRONT:
        case ActionID::VIEW_BACK:
        case ActionID::VIEW_BOTTOM:
        case ActionID::VIEW_LEFT:
        case ActionID::VIEW_RIGHT:
        case ActionID::TOGGLE_WORKPLANE:
        case ActionID::PREVIOUS_GROUP:
        case ActionID::NEXT_GROUP:
            return false;
        default:
            return true;
        }
    }
    if (const auto tool = std::get_if<ToolID>(&id)) {
        switch (*tool) {
        case ToolID::SET_WORKPLANE:
        case ToolID::UNSET_WORKPLANE:
        case ToolID::DRAW_WORKPLANE:
        case ToolID::DRAW_LINE_3D:
        case ToolID::IMPORT_STEP:
        case ToolID::SELECT_EDGES:
            return false;
        default:
            return true;
        }
    }
    return true;
}

static bool in_tool_group_visible_in_sketcher(ToolID tool_id)
{
    if (tool_id == ToolID::NONE)
        return true;
    ActionToolID action_id = tool_id;
    if (!action_catalog.contains(action_id))
        return false;
    const auto &cat = action_catalog.at(action_id);
    if (cat.group == ActionGroup::GROUP)
        return false;
    if (cat.flags & ActionCatalogItem::FLAGS_HIDDEN)
        return false;
    return true;
}

static bool in_tool_action_visible_in_sketcher(InToolActionID action)
{
    switch (action) {
    case InToolActionID::ROTATE_X:
    case InToolActionID::ROTATE_Y:
    case InToolActionID::ROTATE_Z:
    case InToolActionID::TOGGLE_AUTO_NORMAL:
        return false;
    default:
        return true;
    }
}
#endif

static std::string get_in_tool_section_name(ToolID tool_id)
{
    if (tool_id == ToolID::NONE)
        return "Common";
    ActionToolID action_id = tool_id;
    if (action_catalog.contains(action_id))
        return action_catalog.at(action_id).name.full;
    return "Other tools";
}

struct HelpColumns {
    std::string left;
    std::string right;
};

static HelpColumns build_help_columns(const Preferences &prefs)
{
    HelpColumns out;
    std::ostringstream left;
    std::ostringstream right;
    left << "Main keys\n\n";
    right << "In-tool keys\n\n";

    std::map<ActionGroup, std::vector<std::string>> grouped_actions;
    for (const auto &[id, cat] : action_catalog) {
        if (cat.flags & ActionCatalogItem::FLAGS_NO_PREFERENCES)
            continue;
        if (cat.flags & ActionCatalogItem::FLAGS_HIDDEN)
            continue;
#ifdef DUNE_SKETCHER_ONLY
        if (!key_item_visible_in_sketcher(id))
            continue;
#endif
        auto it = prefs.key_sequences.keys.find(id);
        if (it == prefs.key_sequences.keys.end() || it->second.empty())
            continue;
        grouped_actions[cat.group].push_back(cat.name.full + " - " + key_sequences_to_string(it->second));
    }

    bool has_main = false;
    for (const auto &[group, group_name] : action_group_catalog) {
        auto it = grouped_actions.find(group);
        if (it == grouped_actions.end() || it->second.empty())
            continue;
        has_main = true;
        left << group_name << "\n";
        for (const auto &line : it->second)
            left << "  • " << line << "\n";
        left << "\n";
    }

    std::map<std::string, std::vector<std::string>> grouped_in_tool;
    std::set<ToolID> tool_order;
    for (const auto &[action_id, item] : in_tool_action_catalog) {
        if (item.flags & InToolActionCatalogItem::FLAGS_NO_PREFERENCES)
            continue;
#ifdef DUNE_SKETCHER_ONLY
        if (!in_tool_group_visible_in_sketcher(item.tool))
            continue;
        if (!in_tool_action_visible_in_sketcher(action_id))
            continue;
#endif
        auto it = prefs.in_tool_key_sequences.keys.find(action_id);
        if (it == prefs.in_tool_key_sequences.keys.end() || it->second.empty())
            continue;

        const auto section = get_in_tool_section_name(item.tool);
        grouped_in_tool[section].push_back(item.name + " - " + key_sequences_to_string(it->second));
        tool_order.insert(item.tool);
    }

    if (!grouped_in_tool.empty()) {
        right << "";
        for (const auto tool_id : tool_order) {
            const auto section = get_in_tool_section_name(tool_id);
            auto it = grouped_in_tool.find(section);
            if (it == grouped_in_tool.end() || it->second.empty())
                continue;
            right << section << "\n";
            for (const auto &line : it->second)
                right << "  • " << line << "\n";
            right << "\n";
        }
    }

    if (!has_main)
        left << "No assigned main keys.\n";
    if (grouped_in_tool.empty())
        right << "No assigned in-tool keys.\n";

    out.left = left.str();
    out.right = right.str();
    return out;
}
} // namespace

Dune3DApplication::Dune3DApplication() : Gtk::Application("org.dune3d.dune3d", Gio::Application::Flags::HANDLES_OPEN)
{
}

Glib::RefPtr<Dune3DApplication> Dune3DApplication::create()
{
    return Glib::make_refptr_for_instance<Dune3DApplication>(new Dune3DApplication());
}

Dune3DAppWindow *Dune3DApplication::create_appwindow()
{
    auto appwindow = Dune3DAppWindow::create(*this);

    // Make sure that the application runs for as long this window is still open.
    add_window(*appwindow);

    // A window can be added to an application with Gtk::Application::add_window()
    // or Gtk::Window::set_application(). When all added windows have been hidden
    // or removed, the application stops running (Gtk::Application::run() returns()),
    // unless Gio::Application::hold() has been called.

    // Delete the window when it is hidden.
    appwindow->signal_hide().connect([appwindow]() { delete appwindow; });

    return appwindow;
}

void Dune3DApplication::on_activate()
{
    // The application has been started, so let's show a window.
    auto appwindow = create_appwindow();
    appwindow->present();
}

void Dune3DApplication::on_open(const Gio::Application::type_vec_files &files, const Glib::ustring & /* hint */)
{
    // The application has been asked to open some files,
    // so let's open a new view for each one.
    Dune3DAppWindow *appwindow = nullptr;
    auto windows = get_windows();
    if (windows.size() > 0)
        appwindow = dynamic_cast<Dune3DAppWindow *>(windows[0]);

    if (!appwindow)
        appwindow = create_appwindow();

    for (const auto &file : files)
        appwindow->open_file_view(file);

    appwindow->present();
}

const Preferences *the_preferences = nullptr;

void Dune3DApplication::on_startup()
{
    Gtk::Application::on_startup();
    create_config_dir();
    m_preferences.load_default();
    the_preferences = &m_preferences;
    try {
        m_preferences.load();
    }
    CATCH_LOG(Logger::Level::CRITICAL, "error loading preferences", Logger::Domain::UNSPECIFIED)

    // std::cout << std::setw(4) << m_preferences.serialize() << std::endl;


    add_action("preferences", [this] {
        auto pwin = show_preferences_window();
        pwin->show_page("editor");
        if (auto win = get_active_window()) {
            pwin->set_transient_for(*win);
        }
        pwin->present();
    });
#ifndef DUNE_SKETCHER_ONLY
    add_action("logger", [this] { show_log_window(); });
#endif
    // add_action("quit", sigc::mem_fun(*this, &PoolProjectManagerApplication::on_action_quit));
    // add_action("new_window", sigc::mem_fun(*this, &PoolProjectManagerApplication::on_action_new_window));
    add_action("help", sigc::mem_fun(*this, &Dune3DApplication::on_action_help));
    add_action("about", sigc::mem_fun(*this, &Dune3DApplication::on_action_about));
    add_action("theme_light", sigc::mem_fun(*this, &Dune3DApplication::on_action_theme_light));
    add_action("theme_dark", sigc::mem_fun(*this, &Dune3DApplication::on_action_theme_dark));
    // add_action("view_log", [this] { show_log_window(); });

    if (std::filesystem::is_regular_file(get_user_config_filename())) {
        m_user_config.load(get_user_config_filename());
    }

#ifndef DUNE_SKETCHER_ONLY
    m_log_window = new LogWindow();
    m_log_window->set_hide_on_close(true);
    m_log_dispatcher.set_handler([this](const auto &it) { m_log_window->get_view().push_log(it); });
    Logger::get().set_log_handler([this](const Logger::Item &it) { m_log_dispatcher.log(it); });
    property_active_window().signal_changed().connect([this] {
        auto win = get_active_window();
        if (win && win != m_log_window->get_transient_for())
            m_log_window->set_transient_for(*win);
    });
#endif

    auto cssp = Gtk::CssProvider::create();
    cssp->load_from_resource("/org/dune3d/dune3d/dune3d.css");
    Gtk::StyleContext::add_provider_for_display(Gdk::Display::get_default(), cssp,
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Gtk::IconTheme::get_default()->add_resource_path("/org/horizon-eda/horizon/icons");
    Gtk::IconTheme::get_for_display(Gdk::Display::get_default())->add_resource_path("/org/dune3d/dune3d/icons");
    Gtk::Window::set_default_icon_name("dxfsketcher");
}

void Dune3DApplication::on_shutdown()
{
    m_user_config.save(get_user_config_filename());
#ifdef DUNE_SKETCHER_ONLY
    try {
        const auto cache_dir = std::filesystem::path(Glib::get_user_cache_dir()) / "dune3d-sketcher" / "workspaces";
        std::filesystem::remove_all(cache_dir);
    }
    catch (...) {
        // Best-effort cleanup of temporary workspace files.
    }
#endif
    Gtk::Application::on_shutdown();
}

PreferencesWindow *Dune3DApplication::show_preferences_window(guint32 timestamp)
{
    if (!m_preferences_window) {
        m_preferences_window = new PreferencesWindow(m_preferences);
        m_preferences_window->set_hide_on_close(true);
        m_preferences_window->signal_hide().connect([this] {
            std::cout << "pref save" << std::endl;
            m_preferences.save();
            delete m_preferences_window;
            m_preferences_window = nullptr;
        });
    }
    m_preferences_window->present(timestamp);
    return m_preferences_window;
}

LogWindow *Dune3DApplication::show_log_window(guint32 timestamp)
{
    m_log_window->present(timestamp);
    return m_log_window;
}

void Dune3DApplication::on_action_about()
{
    auto dia = new AboutDialog();
    auto win = get_active_window();
    if (win)
        dia->set_transient_for(*win);
    dia->set_modal(true);
    dia->present();
}

void Dune3DApplication::on_action_help()
{
    auto help_win = new Gtk::Window();
    help_win->set_title("DXF Sketcher help");
    help_win->set_default_size(980, 620);
    help_win->set_modal(true);
    if (auto win = get_active_window())
        help_win->set_transient_for(*win);
    help_win->signal_hide().connect([help_win] { delete help_win; });

    auto header = Gtk::make_managed<Gtk::HeaderBar>();
    header->set_show_title_buttons(true);
    header->set_title_widget(*Gtk::make_managed<Gtk::Label>("DXF Sketcher help"));
    help_win->set_titlebar(*header);

    auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    root->set_margin_start(12);
    root->set_margin_end(12);
    root->set_margin_top(12);
    root->set_margin_bottom(12);

    auto intro = Gtk::make_managed<Gtk::Label>("Current key bindings. Only actions with assigned keys are shown.");
    intro->set_halign(Gtk::Align::START);
    intro->add_css_class("dim-label");
    root->append(*intro);

    auto columns = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    columns->set_homogeneous(true);
    columns->set_vexpand(true);

    const auto help = build_help_columns(m_preferences);
    auto make_column = [](const std::string &title, const std::string &body) {
        auto col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        auto title_label = Gtk::make_managed<Gtk::Label>(title);
        title_label->set_halign(Gtk::Align::START);
        title_label->set_use_markup(true);
        title_label->set_markup("<b>" + Glib::Markup::escape_text(title) + "</b>");
        col->append(*title_label);

        auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        scroll->set_vexpand(true);
        auto text = Gtk::make_managed<Gtk::TextView>();
        text->set_editable(false);
        text->set_cursor_visible(false);
        text->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
        text->set_monospace(false);
        text->get_buffer()->set_text(body);
        scroll->set_child(*text);
        col->append(*scroll);
        return col;
    };

    columns->append(*make_column("Main keys", help.left));
    columns->append(*make_column("In-tool keys", help.right));
    root->append(*columns);

    auto buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    buttons->set_halign(Gtk::Align::END);
    auto edit_button = Gtk::make_managed<Gtk::Button>("Edit");
    auto close_button = Gtk::make_managed<Gtk::Button>("Close");
    buttons->append(*edit_button);
    buttons->append(*close_button);
    root->append(*buttons);

    close_button->signal_clicked().connect([help_win] { help_win->close(); });
    edit_button->signal_clicked().connect([this, help_win] {
        help_win->close();
        auto pwin = show_preferences_window();
        if (auto active = get_active_window())
            pwin->set_transient_for(*active);
        pwin->show_page("keys");
        pwin->present();
    });

    help_win->set_child(*root);
    help_win->present();
}

void Dune3DApplication::on_action_theme_light()
{
    m_preferences.canvas.theme_variant = CanvasPreferences::ThemeVariant::LIGHT;
    m_preferences.canvas.dark_theme = false;
    m_preferences.signal_changed().emit();
}

void Dune3DApplication::on_action_theme_dark()
{
    m_preferences.canvas.theme_variant = CanvasPreferences::ThemeVariant::DARK;
    m_preferences.canvas.dark_theme = true;
    m_preferences.signal_changed().emit();
}

const Preferences &Preferences::get()
{
    assert(the_preferences);
    return *the_preferences;
}

std::filesystem::path Dune3DApplication::get_user_config_filename()
{
    return get_config_dir() / "user_config.json";
}
void Dune3DApplication::UserConfig::load(const std::filesystem::path &filename)
{
    json j = load_json_from_file(filename);
    sidebar_visible = j.value("sidebar_visible", true);
    if (j.count("recent")) {
        const json &o = j["recent"];
        for (const auto &[fn, v] : o.items()) {
            auto path = path_from_string(fn);
            try {
                if (std::filesystem::is_regular_file(path))
                    recent_items.emplace(path, Glib::DateTime::create_now_local(v.get<int64_t>()));
            }
            catch (...) {
                // nop
            }
        }
    }
    if (j.count("export_paths")) {
        const json &o = j.at("export_paths");
        for (const auto &[fn, v] : o.items()) {
            for (const auto &[group, jpaths] : v.items()) {
                const auto k = std::make_pair(path_from_string(fn), UUID{group});
                auto &it = export_paths[k];
                jpaths.at("stl").get_to(it.stl);
                jpaths.at("step").get_to(it.step);
                jpaths.at("paths").get_to(it.paths);
                jpaths.at("projection").get_to(it.projection);
            }
        }
    }
}

void Dune3DApplication::UserConfig::save(const std::filesystem::path &filename)
{
    json j;
    j["sidebar_visible"] = sidebar_visible;
    for (const auto &[path, mod] : recent_items) {
        j["recent"][path_to_string(path)] = mod.to_unix();
    }
    for (const auto &[k, it] : export_paths) {
        j["export_paths"][path_to_string(k.first)][k.second] = {
                {"stl", it.stl},
                {"step", it.step},
                {"paths", it.paths},
                {"projection", it.projection},
        };
    }
    save_json_to_file(filename, j);
}

void Dune3DApplication::add_recent_item(const std::filesystem::path &path)
{
    m_user_config.recent_items[path] = Glib::DateTime::create_now_local();
    m_signal_recent_items_changed.emit();
}

Dune3DApplication::~Dune3DApplication() = default;

} // namespace dune3d

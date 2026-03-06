#pragma once
#include "editor_interface.hpp"
#include "core/core.hpp"
#include <gtkmm.h>
#include "action/action.hpp"
#include "preferences/preferences.hpp"
#include "workspace/workspace_view.hpp"
#include "dialogs/dialogs.hpp"
#include "util/action_label.hpp"
#include "document/group/group.hpp"
#include "selection_menu_creator.hpp"
#include "idocument_view_provider.hpp"
#include "core/tool_id.hpp"
#include <array>

namespace dune3d {

class Dune3DApplication;
class Preferences;
class ActionLabelInfo;
class ToolPopover;
class ConstraintsBox;
class GroupEditor;
class SelectionEditor;
class WorkspaceBrowser;
class Dune3DAppWindow;
class Canvas;
class ClippingPlaneWindow;
class SelectionFilterWindow;
class ImageTraceDialog;
class Buffer;
class Entity;
enum class SelectionMode;
enum class CommitMode;
enum class WorkspaceBrowserAddGroupMode;

class Editor : private EditorInterface, private IDocumentViewProvider {
public:
    Editor(Dune3DAppWindow &win, Preferences &prefs);

    void init();


    void tool_bar_set_actions(const std::vector<ActionLabelInfo> &labels) override;
    void tool_bar_set_tool_tip(const std::string &s) override;
    void tool_bar_flash(const std::string &s) override;
    void tool_bar_flash_replace(const std::string &s) override;
    glm::dvec3 get_cursor_pos() const override;
    glm::vec3 get_cam_normal() const override;
    glm::quat get_cam_quat() const override;
    glm::dvec3 get_cursor_pos_for_plane(glm::dvec3 origin, glm::dvec3 normal) const override;
    void tool_update_data(std::unique_ptr<ToolData> data) override;
    void enable_hover_selection(bool enable) override;
    std::optional<SelectableRef> get_hover_selection() const override;
    void set_no_canvas_update(bool v) override
    {
        m_no_canvas_update = v;
    }
    void canvas_update_from_tool() override;

    void set_solid_model_edge_select_mode(bool v) override
    {
        m_solid_model_edge_select_mode = v;
    }

    bool get_use_workplane() const override;

    void set_canvas_selection_mode(SelectionMode mode) override;

    void show_delete_items_popup(const ItemsToDelete &items_selected, const ItemsToDelete &items_all) override;

    Glib::RefPtr<Pango::Context> get_pango_context() override;

    void set_buffer(std::unique_ptr<const Buffer> buffer) override;
    const Buffer *get_buffer() const override;
    void set_first_update_group(const UUID &group) override;


    void open_file(const std::filesystem::path &path);
    void open_folder(const std::filesystem::path &folder_path);
    bool has_file(const std::filesystem::path &path);

    ~Editor();

private:
    std::optional<std::filesystem::path> get_group_export_path(const UUID &group) const;
    void set_group_export_path(const UUID &group, const std::filesystem::path &path);

    void init_workspace_browser();
    void init_properties_notebook();
    void init_header_bar();
    void init_symmetry_popover();
    void sync_symmetry_popover_context();
    void apply_symmetry_from_popover();
    void apply_symmetry_live_from_popover(bool show_toast_on_fail);
    void activate_selection_mode();
    void sync_selection_mode_popover();
    std::optional<UUID> get_single_selected_text_entity();
    void sync_draw_text_popover_from_font_desc();
    void sync_draw_text_popover_from_selection(bool show_popover);
    void apply_draw_text_popover_change(bool apply_to_selected_text);
    void draw_selection_transform_overlay(const std::set<SelectableRef> &selection);
    bool begin_selection_transform_drag(const SelectableRef &handle);
    void update_selection_transform_drag();
    void end_selection_transform_drag();
    void update_sketcher_toolbar_button_states();
    bool is_sticky_draw_tool(ToolID id) const;
    bool configure_symmetry_from_current_context(bool show_toast_on_fail);
    void set_symmetry_enabled(bool enabled, bool reconfigure);
    bool should_apply_symmetry_for_tool(ToolID id) const;
    void capture_symmetry_entities_before_tool(ToolID id);
    void apply_symmetry_to_new_entities_after_commit();
    void clear_symmetry_live_preview_entities();
    void update_symmetry_live_preview_entities();
    void sync_symmetry_for_move_selection();
    ToolResponse tool_update_with_symmetry(ToolArgs &args);
    std::vector<std::pair<glm::dvec3, glm::dvec3>> get_symmetry_overlay_lines_world();
    void init_settings_popover();
    void sync_settings_popover_from_preferences();
#ifdef DUNE_SKETCHER_ONLY
    void init_radial_menu();
    void open_radial_menu(double x, double y);
    void close_radial_menu();
    bool matches_radial_menu_trigger(unsigned int button, Gdk::ModifierType state) const;
    void trigger_radial_tool(ToolID tool_id);
    void toggle_radial_grid();
    void toggle_radial_symmetry();
    void update_radial_menu_button_states();
#endif
    void init_actions();
    void init_tool_popover();
    void init_canvas();
    void init_view_options();

    void on_workspace_browser_group_selected(const UUID &uu_doc, const UUID &uu_group);
    void on_add_group(Group::Type group_type, WorkspaceBrowserAddGroupMode add_group_mode);
    void finish_add_group(Group *new_group);
    void on_delete_current_group(bool delete_file_too = false);
    void on_move_group(Document::MoveGroup op);
    void on_workspace_browser_document_checked(const UUID &uu_doc, bool checked);
    void on_workspace_browser_group_checked(const UUID &uu_doc, const UUID &uu_group, bool checked);
    void on_workspace_browser_body_checked(const UUID &uu_doc, const UUID &uu_group, bool checked);
    void on_workspace_browser_body_solid_model_checked(const UUID &uu_doc, const UUID &uu_group, bool checked);
    void on_workspace_browser_activate_link(const std::string &link);
    void on_view_rotate(const ActionConnection &conn);
    void on_view_zoom(const ActionConnection &conn);
    void on_view_pan(const ActionConnection &conn);
    void on_view_set(const ActionConnection &conn);

    void on_workspace_browser_rename_body(const UUID &uu_doc, const UUID &uu_group);
    void on_workspace_browser_set_body_color(const UUID &uu_doc, const UUID &uu_group);
    void on_workspace_browser_reset_body_color(const UUID &uu_doc, const UUID &uu_group);

    void on_export_solid_model(const ActionConnection &conn);
    void on_export_paths(const ActionConnection &conn);
    void on_export_projection(const ActionConnection &conn);
    void on_open_document(const ActionConnection &conn);
    void on_open_folder();
    void on_trace_image_button();
    void apply_traced_svg(const std::string &svg);
    void on_save_as(const ActionConnection &conn);
    void on_create_group_action(const ActionConnection &conn);
    void on_move_group_action(const ActionConnection &conn);
    void on_align_to_workplane(const ActionConnection &conn);
    void on_center_to_workplane(const ActionConnection &conn);
    void on_look_here(const ActionConnection &conn);
    void on_go_to_group(const ActionConnection &conn);
    void on_go_to_source_group(const ActionConnection &conn);

    void on_explode_cluster(const ActionConnection &conn);
    void on_unexplode_cluster(const ActionConnection &conn);


    Canvas &get_canvas();
    const Canvas &get_canvas() const;

    Gtk::PopoverMenu *m_context_menu = nullptr;
    Gtk::Popover *m_settings_popover = nullptr;
    Gtk::Button *m_theme_prev_button = nullptr;
    Gtk::Button *m_theme_next_button = nullptr;
    Gtk::Label *m_theme_value_label = nullptr;
    Gtk::Scale *m_line_width_scale = nullptr;
    Gtk::Label *m_line_width_value_label = nullptr;
    Gtk::Switch *m_right_click_popovers_switch = nullptr;
    bool m_right_click_popovers_only = false;
#ifdef DUNE_SKETCHER_ONLY
    std::map<ToolID, Gtk::Popover *> m_quick_tool_option_popovers;
    Gtk::Popover *m_quick_grid_popover = nullptr;
    Gtk::Popover *m_quick_symmetry_popover = nullptr;
    Gtk::Popover *m_radial_menu_popover = nullptr;
    std::map<ToolID, Gtk::Button *> m_radial_tool_buttons;
    Gtk::Button *m_radial_grid_button = nullptr;
    Gtk::Button *m_radial_symmetry_button = nullptr;
#endif
    Gtk::Button *m_grid_menu_button = nullptr;
    Gtk::SpinButton *m_grid_spacing_spin = nullptr;
    Gtk::Switch *m_grid_snap_button = nullptr;
    Gtk::Button *m_symmetry_menu_button = nullptr;
    Gtk::Popover *m_symmetry_popover = nullptr;
    Gtk::Box *m_symmetry_mode_row = nullptr;
    Gtk::Button *m_symmetry_mode_prev_button = nullptr;
    Gtk::Label *m_symmetry_mode_value_label = nullptr;
    Gtk::Button *m_symmetry_mode_next_button = nullptr;
    unsigned int m_symmetry_mode_selected = 0;
    Gtk::Switch *m_symmetry_radial_switch = nullptr;
    Gtk::Box *m_symmetry_axes_row = nullptr;
    Gtk::SpinButton *m_symmetry_axes_spin = nullptr;
    Gtk::Box *m_symmetry_rotation_row = nullptr;
    Gtk::SpinButton *m_symmetry_rotation_spin = nullptr;
    Gtk::Button *m_symmetry_apply_button = nullptr;
    Gtk::Label *m_symmetry_context_label = nullptr;
    bool m_updating_symmetry_popover = false;
    bool m_symmetry_enabled = false;
    enum class SymmetryMode { HORIZONTAL, VERTICAL, RADIAL };
    SymmetryMode m_symmetry_mode = SymmetryMode::HORIZONTAL;
    UUID m_symmetry_group;
    UUID m_symmetry_workplane;
    unsigned int m_symmetry_radial_axes = 4;
    double m_symmetry_radial_rotation_deg = 0;
    std::vector<std::pair<glm::dvec2, glm::dvec2>> m_symmetry_axes;
    ToolID m_symmetry_capture_tool_id = ToolID::NONE;
    std::set<UUID> m_symmetry_pre_tool_entities;
    bool m_symmetry_pre_tool_entities_captured = false;
    std::map<UUID, std::unique_ptr<Entity>> m_symmetry_move_roots_before;
    std::set<UUID> m_symmetry_live_preview_entities;
    bool m_updating_settings_popover = false;
    double m_rmb_last_x = NAN;
    double m_rmb_last_y = NAN;
    std::set<SelectableRef> m_context_menu_selection;
    enum class ContextMenuMode { ALL, CONSTRAIN };
    void open_context_menu(ContextMenuMode mode = ContextMenuMode::ALL);
    void install_hover(Gtk::Button &button, ToolID id);
    sigc::connection m_context_menu_hover_timeout;

    void set_current_group(const UUID &group);
    void canvas_update();
    void canvas_update_keep_selection();
    void render_document(const IDocumentInfo &doc);
    unsigned int m_canvas_update_pending = 0;

    class CanvasUpdater {
    public:
        [[nodiscard]] CanvasUpdater(Editor &editor);

        ~CanvasUpdater();

    private:
        Editor &m_editor;
    };

    void tool_begin(ToolID id);
    void tool_process(ToolResponse &resp);
    void tool_process_one();
    void handle_cursor_move();
    void handle_view_changed();
    double m_last_x = NAN;
    double m_last_y = NAN;
    void handle_click(unsigned int button, unsigned int n);

    void apply_preferences();

    Gtk::Button *create_action_button(ActionToolID action);
    void attach_action_button(Gtk::Button &button, ActionToolID action);
    void attach_action_sensitive(Gtk::Widget &widget, ActionToolID action);

    Gtk::Button &create_action_bar_button(ActionToolID action);
    std::map<ActionToolID, Gtk::Button *> m_action_bar_buttons;
    Gtk::Button *m_selection_mode_button = nullptr;
    Gtk::Popover *m_selection_mode_popover = nullptr;
    Gtk::Switch *m_selection_transform_switch = nullptr;
    Gtk::Switch *m_selection_markers_switch = nullptr;
    Gtk::Popover *m_draw_text_popover = nullptr;
    Gtk::Button *m_draw_text_font_button = nullptr;
    Gtk::Switch *m_draw_text_bold_switch = nullptr;
    Gtk::Switch *m_draw_text_italic_switch = nullptr;
    Glib::RefPtr<Gtk::FontDialog> m_draw_text_font_dialog;
    Pango::FontDescription m_draw_text_font_desc;
    Glib::ustring m_draw_text_font_features;
    bool m_updating_draw_text_popover = false;
    bool m_updating_selection_mode_popover = false;
    bool m_selection_transform_enabled = false;
    bool m_show_technical_markers = true;
    enum class SelectionTransformDragMode { NONE, ROTATE, SCALE };
    SelectionTransformDragMode m_selection_transform_drag_mode = SelectionTransformDragMode::NONE;
    bool m_selection_transform_drag_active = false;
    bool m_selection_transform_drag_dirty = false;
    bool m_selection_transform_constraints_removed = false;
    UUID m_selection_transform_group;
    UUID m_selection_transform_workplane;
    std::vector<UUID> m_selection_transform_entities;
    std::map<UUID, std::unique_ptr<Entity>> m_selection_transform_entities_before;
    std::set<SelectableRef> m_selection_transform_selection_before;
    std::set<SelectableRef> m_selection_transform_overlay_selection_cache;
    glm::dvec2 m_selection_transform_bbox_min = {0, 0};
    glm::dvec2 m_selection_transform_bbox_max = {0, 0};
    glm::dvec2 m_selection_transform_center = {0, 0};
    glm::dvec2 m_selection_transform_rotate_handle = {0, 0};
    std::array<glm::dvec2, 4> m_selection_transform_scale_handles = {};
    glm::dvec2 m_selection_transform_base_bbox_min = {0, 0};
    glm::dvec2 m_selection_transform_base_bbox_max = {0, 0};
    glm::dvec2 m_selection_transform_base_rotate_handle = {0, 0};
    std::array<glm::dvec2, 4> m_selection_transform_base_scale_handles = {};
    double m_selection_transform_start_angle = 0;
    double m_selection_transform_drag_angle = 0;
    double m_selection_transform_drag_scale = 1;
    glm::dvec2 m_selection_transform_scale_base_vector = {1, 0};
    double m_selection_transform_scale_start_factor = 1;
    unsigned int m_selection_transform_scale_handle_index = 0;
    double m_selection_transform_frame_angle = 0;
    bool m_selection_transform_overlay_valid = false;
    bool m_primary_button_pressed = false;
    ToolID m_sticky_draw_tool = ToolID::NONE;
    bool m_restarting_sticky_tool = false;
    sigc::connection m_sticky_tool_restart_connection;
    void update_action_bar_buttons_sensitivity();
    void update_action_bar_visibility();
    bool force_end_tool();

    Glib::RefPtr<Gio::Menu> m_view_options_menu;
    Glib::RefPtr<Gio::SimpleAction> m_perspective_action;
    void set_perspective_projection(bool persp);
    Glib::RefPtr<Gio::SimpleAction> m_previous_construction_entities_action;
    void set_show_previous_construction_entities(bool show);
    Glib::RefPtr<Gio::SimpleAction> m_hide_irrelevant_workplanes_action;
    void set_hide_irrelevant_workplanes(bool hide);
    void add_tool_action(ActionToolID id, const std::string &action);
    Gtk::Scale *m_curvature_comb_scale = nullptr;

    void update_view_hints();

    std::map<ActionToolID, ActionConnection> m_action_connections;

    ActionConnection &connect_action(ToolID tool_id);
    ActionConnection &connect_action(ActionToolID id, std::function<void(const ActionConnection &)> cb);
    ActionConnection &connect_action_with_source(ActionToolID id,
                                                 std::function<void(const ActionConnection &, ActionSource)> cb);

    KeySequence m_keys_current;
    KeyMatchResult keys_match(const KeySequence &keys) const;
    bool handle_action_key(Glib::RefPtr<Gtk::EventControllerKey> controller, unsigned int keyval, unsigned int keycode,
                           Gdk::ModifierType state);
    void handle_tool_action(const ActionConnection &conn);

    bool trigger_action(ActionToolID action, ActionSource source = ActionSource::UNKNOWN);
    bool get_action_sensitive(ActionID action) const;

    void update_action_sensitivity();
    void update_action_sensitivity(const std::set<SelectableRef> &sel);

    std::map<ActionID, bool> m_action_sensitivity;

    void tool_bar_append_action(InToolActionID action1, InToolActionID action2, InToolActionID action3,
                                const std::string &s);
    void tool_bar_clear_actions();
    std::vector<ActionLabelInfo> m_in_tool_action_label_infos;

    sigc::signal<void()> m_signal_action_sensitive;
    Preferences &m_preferences;
    InToolKeySequencesPreferences m_in_tool_key_sequeces_preferences;


    bool m_no_canvas_update = false;
    bool m_solid_model_edge_select_mode = false;

    ToolPopover *m_tool_popover = nullptr;

    WorkspaceBrowser *m_workspace_browser = nullptr;
    Gtk::Revealer *m_workspace_browser_revealer = nullptr;
    Gtk::Popover *m_sidebar_popover = nullptr;
    void set_sidebar_visible(bool visible);
    void toggle_sidebar_visibility();

    void update_workplane_label();
    void update_selection_mode_label();

    Gtk::Notebook *m_properties_notebook = nullptr;
    ConstraintsBox *m_constraints_box = nullptr;
    Gtk::Box *m_group_editor_box = nullptr;
    Gtk::Revealer *m_group_commit_pending_revealer = nullptr;
    GroupEditor *m_group_editor = nullptr;
    void update_group_editor();
    sigc::connection m_delayed_commit_connection;
    void commit_from_editor();
    void handle_commit_from_editor(CommitMode mode);


    SelectionEditor *m_selection_editor = nullptr;
    void update_selection_editor();
    Gtk::Revealer *m_selection_commit_pending_revealer = nullptr;
    sigc::connection m_delayed_selection_commit_connection;
    void commit_from_selection_editor();

    Dialogs m_dialogs;
    Dialogs &get_dialogs() override
    {
        return m_dialogs;
    }

    void set_constraint_icons(glm::vec3 p, glm::vec3 v, const std::vector<ConstraintType> &constraints) override;

    std::map<UUID, WorkspaceView> m_workspace_views;
    UUID m_current_workspace_view;
    WorkspaceView &get_current_workspace_view();
    std::map<UUID, DocumentView> &get_current_document_views();

    UUID create_workspace_view();
    UUID create_workspace_view_from_current();
    void set_current_workspace_view(const UUID &uu);
    void update_workspace_view_names();
    void update_can_close_workspace_view_pages();

    bool m_workspace_view_loading = false;
    void save_workspace_view(const UUID &doc_uu);
    void append_workspace_view_page(const std::string &name, const UUID &uu);
    void close_workspace_view(const UUID &uu);
    void auto_close_workspace_views();
    void rename_workspace_view(const UUID &uu);
    UUID duplicate_workspace_view(const UUID &uu);
    static std::filesystem::path get_workspace_filename_from_document_filename(const std::filesystem::path &path);

    void load_linked_documents(const UUID &uu_doc);

    DocumentView &get_current_document_view() override;

    void handle_tool_change();

    void reset_key_hint_label();

    void show_save_dialog(const std::string &doc_name, std::function<void()> save_cb, std::function<void()> no_save_cb);
    std::function<void()> m_after_save_cb;
    void close_document(const UUID &uu, std::function<void()> save_cb, std::function<void()> no_save_cb);
    void do_close_document(const UUID &uu);

    std::optional<ActionToolID> get_doubleclick_action(const SelectableRef &sr);
    ToolID get_tool_for_drag_move(bool ctrl, const std::set<SelectableRef> &sel);
    ToolID m_drag_tool;
    std::set<SelectableRef> m_selection_for_drag;
    glm::dvec2 m_cursor_pos_win_drag_begin;

    std::unique_ptr<ClippingPlaneWindow> m_clipping_plane_window;

    void update_version_info();

    Dune3DAppWindow &m_win;

    Core m_core;
    SelectionMode m_last_selection_mode;

    std::vector<ConstraintType> m_constraint_tip_icons;
    glm::vec3 m_constraint_tip_pos;
    glm::vec3 m_constraint_tip_vec;

    std::unique_ptr<SelectionFilterWindow> m_selection_filter_window;
    std::unique_ptr<ImageTraceDialog> m_image_trace_dialog;

    SelectionMenuCreator m_selection_menu_creator;

    void update_error_overlay();

    void update_title();
    UUID m_update_groups_after;
    std::map<UUID, std::filesystem::path> m_group_export_paths;
    bool m_sketcher_opening_folder_batch = false;
    std::optional<std::filesystem::path> m_sketcher_folder_path;
    bool m_activate_selection_after_import_picture = false;
};
} // namespace dune3d

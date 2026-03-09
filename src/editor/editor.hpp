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
#include "document/group/group_sketch.hpp"
#include "selection_menu_creator.hpp"
#include "idocument_view_provider.hpp"
#include "core/tool_id.hpp"
#include <array>
#include <future>
#include <optional>
#include <map>
#include <vector>

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
class PictureData;
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
    bool get_selection_snap_enabled() const override;
    std::optional<SelectionSnapTemplateInfo> get_selection_snap_template_info() const override;
    void open_trace_image_dialog(const std::shared_ptr<const PictureData> &picture) override;
    void set_selection_snap_overlay_lines(const std::vector<std::pair<glm::dvec3, glm::dvec3>> &lines_world) override;

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
    struct BoxesPreviewPolyline {
        std::vector<glm::dvec2> points;
        int layer = 0;
    };

    struct BoxesPreviewAsyncResult {
        std::vector<BoxesPreviewPolyline> polylines;
        glm::dvec2 bbox_min = {0, 0};
        glm::dvec2 bbox_max = {0, 0};
        std::string error;
        int request_id = 0;
    };

    struct BoxesImportSegment {
        bool bezier = false;
        glm::dvec2 p1 = {0, 0};
        glm::dvec2 c1 = {0, 0};
        glm::dvec2 c2 = {0, 0};
        glm::dvec2 p2 = {0, 0};
    };

    struct BoxesImportAsyncResult {
        std::vector<BoxesImportSegment> segments;
        glm::dvec2 bbox_min = {0, 0};
        glm::dvec2 bbox_max = {0, 0};
        std::string error;
    };

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
    bool sanitize_canvas_selection_if_needed();
    bool expand_selection_to_closed_loops_if_needed();
    std::optional<UUID> get_single_selected_text_entity();
    void sync_draw_text_popover_from_font_desc();
    void sync_draw_text_popover_from_selection(bool show_popover);
    void apply_draw_text_popover_change(bool apply_to_selected_text);
    void init_layers_popover();
    void rebuild_layers_popover();
    void ensure_current_group_layers_initialized();
    void select_active_layer_for_current_group(const UUID &layer_uu);
    UUID get_active_layer_for_current_group() const;
    void move_selection_to_layer(const UUID &layer_uu);
    void capture_layer_entities_before_tool();
    void apply_active_layer_to_new_entities_after_commit();
    void init_cup_template_popover();
    void draw_cup_template_overlay();
    void init_gears_popover();
    void update_gears_quick_popover();
    void open_gears_generator_window();
    void update_gears_generator_ui();
    void update_gears_generator_preview();
    void draw_gears_generator_preview(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h);
    bool build_gears_generator_polylines(std::vector<std::vector<glm::dvec2>> &polylines) const;
    bool import_gears_generator_to_document();
    void init_joints_popover();
    void update_joints_quick_popover(bool request_popup = false);
    void sync_joints_popover_controls();
    void update_joints_summary();
    void rebuild_joints_family_dropdown();
    void rebuild_joints_settings_ui();
    void rebuild_joints_role_dropdowns();
    void init_boxes_popover();
    void open_boxes_generator_window();
    void queue_boxes_preview_refresh(bool reset_view = false);
    void update_boxes_preview(bool reset_view = false);
    void draw_boxes_preview(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h);
    void rebuild_boxes_catalog_lists();
    void rebuild_boxes_template_list();
    void rebuild_boxes_gallery();
    void sync_boxes_catalog_selection(bool switch_to_category_page);
    void update_boxes_settings_visibility();
    void ensure_boxes_importing_window();
    void finish_boxes_geometry_import();
    void show_boxes_sample_preview(int template_index);
    void open_boxes_gallery_window();
    bool commit_generator_polyline_groups(const std::vector<std::vector<std::vector<glm::dvec2>>> &polyline_groups,
                                          bool closed, const std::string &rebuild_reason, bool clusterize_each_group = false);
    bool commit_generator_polylines(const std::vector<std::vector<glm::dvec2>> &polylines, bool closed,
                                    const std::string &rebuild_reason, bool clusterize = false);
    void generate_gears_geometry();
    bool generate_gears_from_selected_profile();
    void generate_joints_geometry();
    bool generate_joints_from_selected_lines();
    void generate_boxes_geometry();
    void open_layer_edit_popover(const UUID &layer_uu);
    void refresh_layer_edit_popover();
    void draw_layer_glow_overlay();
    void draw_selection_transform_overlay(const std::set<SelectableRef> &selection);
    bool begin_selection_transform_drag(const SelectableRef &handle);
    void update_selection_transform_drag();
    void end_selection_transform_drag();
    void update_sketcher_toolbar_button_states();
    bool is_sticky_draw_tool(ToolID id) const;
    bool is_middle_toggle_draw_tool(ToolID id) const;
    void remember_last_draw_tool(ToolID id);
    void toggle_selection_mode_or_last_draw_tool();
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
    std::set<UUID> m_symmetry_pre_tool_constraints;
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
    Gtk::Button *m_layers_mode_button = nullptr;
    Gtk::Popover *m_layers_popover = nullptr;
    Gtk::Box *m_layers_list_box = nullptr;
    Gtk::Button *m_cup_template_button = nullptr;
    Gtk::Popover *m_cup_template_popover = nullptr;
    Gtk::SpinButton *m_cup_template_height_spin = nullptr;
    Gtk::SpinButton *m_cup_template_circumference_spin = nullptr;
    Gtk::SpinButton *m_cup_template_diameter_spin = nullptr;
    Gtk::SpinButton *m_cup_template_segments_spin = nullptr;
    Gtk::Button *m_gears_button = nullptr;
    Gtk::Popover *m_gears_popover = nullptr;
    Gtk::Popover *m_gears_quick_popover = nullptr;
    Gtk::SpinButton *m_gears_module_spin = nullptr;
    Gtk::SpinButton *m_gears_teeth_spin = nullptr;
    Gtk::SpinButton *m_gears_pressure_angle_spin = nullptr;
    Gtk::SpinButton *m_gears_backlash_spin = nullptr;
    Gtk::SpinButton *m_gears_segments_spin = nullptr;
    Gtk::SpinButton *m_gears_bore_spin = nullptr;
    Gtk::SpinButton *m_gears_material_thickness_spin = nullptr;
    Gtk::Button *m_gears_hole_mode_prev_button = nullptr;
    Gtk::Button *m_gears_hole_mode_next_button = nullptr;
    Gtk::Label *m_gears_hole_mode_label = nullptr;
    Gtk::Switch *m_gears_inward_switch = nullptr;
    Gtk::Switch *m_gears_quick_inward_switch = nullptr;
    Gtk::Button *m_gears_quick_apply_button = nullptr;
    bool m_gears_mode_enabled = false;
    bool m_updating_gears_inward_switches = false;
    enum class GearHoleMode {
        CIRCLE = 0,
        CROSS = 1,
        SLOT = 2,
    };
    GearHoleMode m_gears_hole_mode = GearHoleMode::CIRCLE;
    Gtk::Window *m_gears_generator_window = nullptr;
    Gtk::DrawingArea *m_gears_generator_preview_area = nullptr;
    Gtk::Scale *m_gears_generator_rotation_slider = nullptr;
    Gtk::SpinButton *m_gears_generator_module_spin = nullptr;
    Gtk::SpinButton *m_gears_generator_teeth_spin = nullptr;
    Gtk::SpinButton *m_gears_generator_pressure_spin = nullptr;
    Gtk::SpinButton *m_gears_generator_backlash_spin = nullptr;
    Gtk::SpinButton *m_gears_generator_segments_spin = nullptr;
    Gtk::SpinButton *m_gears_generator_bore_spin = nullptr;
    Gtk::SpinButton *m_gears_generator_material_thickness_spin = nullptr;
    Gtk::Button *m_gears_generator_hole_mode_prev_button = nullptr;
    Gtk::Button *m_gears_generator_hole_mode_next_button = nullptr;
    Gtk::Label *m_gears_generator_hole_mode_label = nullptr;
    Gtk::Switch *m_gears_generator_pair_switch = nullptr;
    Gtk::Switch *m_gears_generator_ratio_degrees_switch = nullptr;
    Gtk::Box *m_gears_generator_pair_box = nullptr;
    Gtk::Label *m_gears_generator_ratio_label = nullptr;
    Gtk::SpinButton *m_gears_generator_diameter1_spin = nullptr;
    Gtk::SpinButton *m_gears_generator_ratio_spin = nullptr;
    Gtk::Label *m_gears_generator_summary_label = nullptr;
    Gtk::Button *m_gears_generator_import_button = nullptr;
    mutable std::vector<std::vector<glm::dvec2>> m_gears_generator_preview_polylines;
    bool m_updating_gears_generator_ui = false;
    Gtk::Button *m_joints_button = nullptr;
    Gtk::Popover *m_joints_popover = nullptr;
    Gtk::Popover *m_joints_quick_popover = nullptr;
    Gtk::DropDown *m_joints_category_dropdown = nullptr;
    Gtk::Button *m_joints_category_prev_button = nullptr;
    Gtk::Label *m_joints_category_label = nullptr;
    Gtk::Button *m_joints_category_next_button = nullptr;
    Gtk::DropDown *m_joints_family_dropdown = nullptr;
    Gtk::Button *m_joints_family_prev_button = nullptr;
    Gtk::Label *m_joints_family_label = nullptr;
    Gtk::Button *m_joints_family_next_button = nullptr;
    Gtk::DropDown *m_joints_variant_dropdown = nullptr;
    Gtk::DropDown *m_joints_role_dropdown = nullptr;
    Gtk::Button *m_joints_role_prev_button = nullptr;
    Gtk::Label *m_joints_role_label = nullptr;
    Gtk::Button *m_joints_role_next_button = nullptr;
    Gtk::Label *m_joints_family_description_label = nullptr;
    Gtk::Label *m_joints_selection_hint_label = nullptr;
    Gtk::Label *m_joints_summary_label = nullptr;
    Gtk::Expander *m_joints_advanced_expander = nullptr;
    Gtk::Button *m_joints_apply_button = nullptr;
    Gtk::Switch *m_joints_auto_size_switch = nullptr;
    Gtk::Box *m_joints_settings_box = nullptr;
    Gtk::Button *m_joints_quick_role_prev_button = nullptr;
    Gtk::Label *m_joints_quick_role_label = nullptr;
    Gtk::Button *m_joints_quick_role_next_button = nullptr;
    Gtk::Label *m_joints_quick_hint_label = nullptr;
    Gtk::Button *m_joints_quick_side_prev_button = nullptr;
    Gtk::Label *m_joints_quick_side_label = nullptr;
    Gtk::Button *m_joints_quick_side_next_button = nullptr;
    Gtk::Switch *m_joints_quick_flip_direction_switch = nullptr;
    Gtk::Button *m_joints_quick_swap_roles_button = nullptr;
    Gtk::Button *m_joints_quick_apply_button = nullptr;
    Gtk::SpinButton *m_joints_finger_spin = nullptr;
    Gtk::SpinButton *m_joints_space_spin = nullptr;
    Gtk::SpinButton *m_joints_width_spin = nullptr;
    Gtk::SpinButton *m_joints_edge_width_spin = nullptr;
    Gtk::SpinButton *m_joints_surroundingspaces_spin = nullptr;
    Gtk::SpinButton *m_joints_play_spin = nullptr;
    Gtk::SpinButton *m_joints_extra_length_spin = nullptr;
    Gtk::DropDown *m_joints_side_dropdown = nullptr;
    Gtk::Button *m_joints_side_prev_button = nullptr;
    Gtk::Label *m_joints_side_label = nullptr;
    Gtk::Button *m_joints_side_next_button = nullptr;
    Gtk::Switch *m_joints_flip_direction_switch = nullptr;
    Gtk::Button *m_joints_swap_roles_button = nullptr;
    Gtk::SpinButton *m_joints_thickness_spin = nullptr;
    Gtk::SpinButton *m_joints_burn_spin = nullptr;
    std::vector<unsigned int> m_joints_visible_family_indices;
    std::map<std::string, std::string> m_joints_setting_values;
    std::map<std::string, Gtk::Widget *> m_joints_setting_rows;
    std::map<std::string, Gtk::SpinButton *> m_joints_spin_settings;
    std::map<std::string, Gtk::Entry *> m_joints_entry_settings;
    std::map<std::string, Gtk::DropDown *> m_joints_dropdown_settings;
    std::map<std::string, Gtk::Switch *> m_joints_switch_settings;
    bool m_joints_mode_enabled = false;
    bool m_updating_joints_ui = false;
    bool m_joints_rebuilding_settings = false;
    bool m_updating_joints_role_controls = false;
    bool m_updating_joints_side_controls = false;
    Gtk::Button *m_boxes_button = nullptr;
    Gtk::Window *m_boxes_generator_window = nullptr;
    Gtk::Window *m_boxes_sample_window = nullptr;
    Gtk::Window *m_boxes_loading_window = nullptr;
    Gtk::Window *m_boxes_gallery_window = nullptr;
    Gtk::Window *m_boxes_importing_window = nullptr;
    Gtk::ListBox *m_boxes_category_list = nullptr;
    Gtk::ListBox *m_boxes_template_list = nullptr;
    Gtk::ListBox *m_boxes_gallery_category_list = nullptr;
    Gtk::FlowBox *m_boxes_gallery_flowbox = nullptr;
    Gtk::Revealer *m_boxes_category_revealer = nullptr;
    Gtk::Box *m_boxes_settings_box = nullptr;
    Gtk::DrawingArea *m_boxes_preview_area = nullptr;
    Gtk::Picture *m_boxes_sample_picture = nullptr;
    Gtk::Label *m_boxes_preview_status_label = nullptr;
    Gtk::Label *m_boxes_importing_label = nullptr;
    Gtk::ProgressBar *m_boxes_importing_progress = nullptr;
    Gtk::Button *m_boxes_import_button = nullptr;
    Gtk::Button *m_boxes_gallery_button = nullptr;
    int m_boxes_template_index = 0;
    std::map<Gtk::ListBox *, std::vector<int>> m_boxes_template_indices_by_list;
    std::set<int> m_boxes_favorite_template_indices;
    std::map<std::string, std::string> m_boxes_setting_values;
    std::map<std::string, Gtk::Widget *> m_boxes_setting_rows;
    std::map<std::string, Gtk::SpinButton *> m_boxes_spin_settings;
    std::map<std::string, Gtk::Entry *> m_boxes_entry_settings;
    std::map<std::string, Gtk::DropDown *> m_boxes_dropdown_settings;
    std::map<std::string, Gtk::Switch *> m_boxes_switch_settings;
    std::vector<BoxesPreviewPolyline> m_boxes_preview_polylines;
    glm::dvec2 m_boxes_preview_bbox_min = {0, 0};
    glm::dvec2 m_boxes_preview_bbox_max = {0, 0};
    double m_boxes_preview_zoom = 1.0;
    double m_boxes_preview_pan_x = 0.0;
    double m_boxes_preview_pan_y = 0.0;
    double m_boxes_preview_drag_pan_x = 0.0;
    double m_boxes_preview_drag_pan_y = 0.0;
    double m_boxes_preview_cursor_x = 0.0;
    double m_boxes_preview_cursor_y = 0.0;
    sigc::connection m_boxes_preview_debounce_connection;
    sigc::connection m_boxes_preview_poll_connection;
    sigc::connection m_boxes_catalog_poll_connection;
    sigc::connection m_boxes_import_poll_connection;
    bool m_boxes_syncing_catalog = false;
    bool m_boxes_catalog_loading = false;
    bool m_boxes_rebuilding_settings = false;
    bool m_boxes_suspend_joints_active = false;
    bool m_boxes_preview_generation_running = false;
    bool m_boxes_import_generation_running = false;
    int m_boxes_preview_request_serial = 0;
    std::string m_boxes_current_category_id = "__favorites__";
    std::future<std::pair<bool, std::string>> m_boxes_catalog_future;
    std::future<BoxesPreviewAsyncResult> m_boxes_preview_future;
    std::future<BoxesImportAsyncResult> m_boxes_import_future;
    std::optional<int> m_boxes_pending_preview_request_id;
    bool m_boxes_pending_preview_reset_view = false;
    BoxesPreviewAsyncResult m_boxes_ready_preview_result;
    BoxesImportAsyncResult m_boxes_ready_import_result;
    UUID m_boxes_import_group;
    UUID m_boxes_import_workplane;
    UUID m_boxes_import_layer;
    glm::dvec2 m_boxes_import_target_center = {0, 0};
    std::string m_boxes_import_template_label;
    Gtk::Popover *m_layer_edit_popover = nullptr;
    Gtk::Entry *m_layer_edit_name_entry = nullptr;
    Gtk::Switch *m_layer_edit_icon_switch = nullptr;
    Gtk::Label *m_layer_edit_process_label = nullptr;
    Gtk::Box *m_layer_edit_process_box = nullptr;
    std::map<GroupSketch::SketchLayerProcess, Gtk::Button *> m_layer_edit_process_buttons;
    std::map<int, Gtk::Button *> m_layer_edit_color_buttons;
    UUID m_layer_editing_uuid;
    Gtk::Switch *m_selection_transform_switch = nullptr;
    Gtk::Switch *m_selection_markers_switch = nullptr;
    Gtk::Switch *m_selection_snap_switch = nullptr;
    Gtk::Switch *m_selection_closed_loop_switch = nullptr;
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
    bool m_selection_snap_enabled = false;
    bool m_selection_closed_loop_enabled = false;
    bool m_applying_closed_loop_selection = false;
    bool m_sanitizing_selection = false;
    std::set<SelectableRef> m_closed_loop_previous_selection;
    std::vector<std::pair<glm::dvec3, glm::dvec3>> m_selection_snap_overlay_lines_world;
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
    ToolID m_last_middle_toggle_draw_tool = ToolID::NONE;
    bool m_middle_click_toggle_candidate = false;
    glm::dvec2 m_middle_click_press_pos = {0, 0};
    bool m_layers_mode_enabled = false;
    bool m_cup_template_enabled = false;
    bool m_updating_cup_template_popover = false;
    double m_cup_template_height_mm = 110;
    double m_cup_template_circumference_mm = 260;
    int m_cup_template_segments = 4;
    bool m_updating_layer_edit_popover = false;
    std::map<UUID, UUID> m_active_layer_by_group;
    std::set<UUID> m_layers_pre_tool_entities;
    bool m_layers_pre_tool_entities_captured = false;
    UUID m_layer_capture_group;
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

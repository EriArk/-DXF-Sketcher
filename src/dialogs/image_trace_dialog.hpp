#pragma once

#include <filesystem>
#include <gtkmm.h>
#include <sigc++/sigc++.h>
#include <memory>
#include <vector>
#include <cairomm/surface.h>
#include "util/image_trace.hpp"
#include "util/json_util.hpp"

namespace dune3d {

class PictureData;

class ImageTraceDialog : public Gtk::Window {
public:
    ImageTraceDialog();

    bool load_image(const std::filesystem::path &path, std::string &error_message);

    using type_signal_apply = sigc::signal<void(const std::string &)>;
    type_signal_apply &signal_apply();

private:
    struct TracePreset {
        std::string name;
        ImageTraceSettings settings;
    };

    struct PreviewState {
        double zoom = 1.0;
        double pan_x = 0.0;
        double pan_y = 0.0;
        double cursor_x = 0.0;
        double cursor_y = 0.0;
        double drag_pan_x = 0.0;
        double drag_pan_y = 0.0;
    };

    struct PreviewTransform {
        double scale = 1.0;
        double ox = 0.0;
        double oy = 0.0;
    };

    void run_trace();
    void queue_previews_draw();
    void update_scale_value_labels();
    void reset_viewports();
    std::pair<unsigned int, unsigned int> get_preview_source_size(bool traced) const;
    PreviewTransform get_preview_transform(const PreviewState &state, int area_w, int area_h, unsigned int source_w,
                                           unsigned int source_h) const;
    void setup_preview_interaction(Gtk::DrawingArea &area, PreviewState &state, bool traced);
    void draw_original_preview(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h);
    void draw_traced_preview(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h);
    void queue_trace_update();
    void connect_scale_auto_trace(Gtk::Scale &scale);
    ImageTraceSettings read_settings_from_ui() const;
    void apply_settings_to_ui(const ImageTraceSettings &settings);
    void append_preset(const TracePreset &preset);
    void rebuild_preset_list();
    void load_default_presets();
    void select_preset_row(int index);
    void import_presets();
    void export_presets();
    void save_current_as_preset();
    void request_delete_preset(Gtk::Widget &anchor, Gtk::ListBoxRow &row);
    void delete_preset_at(int index);
    json serialize_presets() const;
    void deserialize_presets(const json &j);

    Gtk::DrawingArea *m_original_preview = nullptr;
    Gtk::DrawingArea *m_traced_preview = nullptr;
    Gtk::AspectFrame *m_original_aspect_frame = nullptr;
    Gtk::AspectFrame *m_traced_aspect_frame = nullptr;
    Gtk::ScrolledWindow *m_controls_scroll = nullptr;
    Gtk::Scale *m_threshold_scale = nullptr;
    Gtk::Scale *m_smoothness_scale = nullptr;
    Gtk::Scale *m_noise_scale = nullptr;
    Gtk::Scale *m_blur_scale = nullptr;
    Gtk::Scale *m_curve_strength_scale = nullptr;
    Gtk::Scale *m_anti_stair_strength_scale = nullptr;
    Gtk::Scale *m_detail_preserve_scale = nullptr;
    Gtk::Label *m_threshold_value_label = nullptr;
    Gtk::Label *m_smoothness_value_label = nullptr;
    Gtk::Label *m_noise_value_label = nullptr;
    Gtk::Label *m_blur_value_label = nullptr;
    Gtk::Label *m_curve_strength_value_label = nullptr;
    Gtk::Label *m_anti_stair_strength_value_label = nullptr;
    Gtk::Label *m_detail_preserve_value_label = nullptr;
    Gtk::Label *m_outline_offset_value_label = nullptr;
    Gtk::Switch *m_outline_switch = nullptr;
    Gtk::Switch *m_outline_with_trace_switch = nullptr;
    Gtk::Switch *m_anti_stair_switch = nullptr;
    Gtk::Switch *m_curve_fit_switch = nullptr;
    Gtk::Button *m_apply_button = nullptr;
    Gtk::Label *m_status_label = nullptr;
    Gtk::ListBox *m_presets_list = nullptr;
    Gtk::Button *m_preset_save_button = nullptr;
    Gtk::Button *m_preset_import_button = nullptr;
    Gtk::Button *m_preset_export_button = nullptr;
    Gtk::Button *m_outline_offset_dec_button = nullptr;
    Gtk::Button *m_outline_offset_inc_button = nullptr;

    std::shared_ptr<const PictureData> m_picture_data;
    std::vector<unsigned char> m_original_surface_data;
    Cairo::RefPtr<Cairo::ImageSurface> m_original_surface;
    ImageTraceResult m_last_result;
    bool m_last_curve_fit = true;
    double m_last_curve_strength = 0.85;
    PreviewState m_original_view;
    PreviewState m_traced_view;
    std::vector<TracePreset> m_presets;
    int m_custom_preset_counter = 1;
    bool m_updating_preset_list = false;
    bool m_suppress_auto_trace_updates = false;
    double m_outline_offset_mm = 0.0;
    sigc::connection m_trace_debounce_conn;

    type_signal_apply m_signal_apply;
};

} // namespace dune3d

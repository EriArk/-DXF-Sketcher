#pragma once

#include <filesystem>
#include <gtkmm.h>
#include <sigc++/sigc++.h>
#include <memory>
#include <vector>
#include <cairomm/surface.h>

namespace dune3d {

class PictureData;

struct ImageImportPreparedData {
    std::shared_ptr<const PictureData> picture;
    bool use_target_size = false;
    double target_width = 0;
    double target_height = 0;
};

class ImageImportDialog : public Gtk::Window {
public:
    enum class FilterMode : unsigned int { COLOR = 0, GRAYSCALE, BLACK_WHITE, JITTER, STUCCO };

    ImageImportDialog();

    bool load_image(const std::filesystem::path &path, std::string &error_message);

    using type_signal_result = sigc::signal<void(const ImageImportPreparedData &)>;
    type_signal_result &signal_apply();
    type_signal_result &signal_trace();

private:
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

    struct CropRect {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    void run_processing();
    void queue_processing();
    void queue_previews_draw();
    void update_value_labels();
    void update_controls_state();
    void reset_viewports();
    void reset_scale_fields();
    void set_scale_width(double width);
    void set_scale_height(double height);
    std::pair<unsigned int, unsigned int> get_preview_source_size(bool processed) const;
    PreviewTransform get_preview_transform(const PreviewState &state, int area_w, int area_h, unsigned int source_w,
                                           unsigned int source_h) const;
    void setup_preview_interaction(Gtk::DrawingArea &area, PreviewState &state, bool processed);
    void draw_original_preview(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h);
    void draw_result_preview(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h);
    CropRect get_crop_rect_from_view() const;
    FilterMode get_filter_mode() const;

    Gtk::DrawingArea *m_original_preview = nullptr;
    Gtk::DrawingArea *m_result_preview = nullptr;
    Gtk::AspectFrame *m_original_aspect_frame = nullptr;
    Gtk::AspectFrame *m_result_aspect_frame = nullptr;

    Gtk::DropDown *m_filter_dropdown = nullptr;
    Gtk::Scale *m_contrast_scale = nullptr;
    Gtk::Scale *m_brightness_scale = nullptr;
    Gtk::Scale *m_grayscale_scale = nullptr;
    Gtk::Scale *m_sharp_blur_scale = nullptr;
    Gtk::Label *m_contrast_value_label = nullptr;
    Gtk::Label *m_brightness_value_label = nullptr;
    Gtk::Label *m_grayscale_value_label = nullptr;
    Gtk::Label *m_sharp_blur_value_label = nullptr;

    Gtk::Switch *m_outline_switch = nullptr;
    Gtk::Button *m_outline_offset_dec_button = nullptr;
    Gtk::Button *m_outline_offset_inc_button = nullptr;
    Gtk::Label *m_outline_offset_value_label = nullptr;
    Gtk::Switch *m_outline_bg_remove_switch = nullptr;

    Gtk::Switch *m_crop_switch = nullptr;
    Gtk::Switch *m_upscale_switch = nullptr;
    Gtk::Button *m_upscale_factor_dec_button = nullptr;
    Gtk::Button *m_upscale_factor_inc_button = nullptr;
    Gtk::Label *m_upscale_factor_value_label = nullptr;

    Gtk::Switch *m_scale_switch = nullptr;
    Gtk::SpinButton *m_scale_width_spin = nullptr;
    Gtk::SpinButton *m_scale_height_spin = nullptr;

    Gtk::Switch *m_invert_switch = nullptr;
    Gtk::Button *m_apply_button = nullptr;
    Gtk::Button *m_trace_button = nullptr;
    Gtk::Label *m_status_label = nullptr;

    std::shared_ptr<const PictureData> m_picture_data;
    std::shared_ptr<const PictureData> m_processed_picture;
    std::vector<unsigned char> m_original_surface_data;
    std::vector<unsigned char> m_result_surface_data;
    Cairo::RefPtr<Cairo::ImageSurface> m_original_surface;
    Cairo::RefPtr<Cairo::ImageSurface> m_result_surface;

    PreviewState m_original_view;
    PreviewState m_result_view;
    sigc::connection m_processing_debounce_conn;

    bool m_updating_scale_controls = false;
    bool m_last_scale_changed_was_width = true;
    double m_scale_aspect_ratio = 1.0;
    double m_outline_offset_px = 0.0;
    int m_upscale_factor = 2;

    type_signal_result m_signal_apply;
    type_signal_result m_signal_trace;
};

} // namespace dune3d

#include "image_trace_dialog.hpp"
#include "util/fs_util.hpp"
#include "util/picture_data.hpp"
#include "util/picture_util.hpp"
#include "util/util.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace dune3d {
namespace {
void add_rounded_clip(const Cairo::RefPtr<Cairo::Context> &cr, double x, double y, double w, double h, double r)
{
    const double rr = std::max(0.0, std::min(r, std::min(w, h) * 0.5));
    cr->begin_new_sub_path();
    cr->arc(x + w - rr, y + rr, rr, -M_PI_2, 0);
    cr->arc(x + w - rr, y + h - rr, rr, 0, M_PI_2);
    cr->arc(x + rr, y + h - rr, rr, M_PI_2, M_PI);
    cr->arc(x + rr, y + rr, rr, M_PI, 3 * M_PI_2);
    cr->close_path();
    cr->clip();
}

std::string format_value(double v, int digits)
{
    if (digits <= 0)
        return std::to_string(static_cast<int>(std::lround(v)));
    return std::format("{:.{}f}", v, digits);
}

void copy_surface_data_from_picture(const PictureData &picture, std::vector<unsigned char> &surface_data,
                                    Cairo::RefPtr<Cairo::ImageSurface> &surface)
{
    const auto w = static_cast<int>(picture.m_width);
    const auto h = static_cast<int>(picture.m_height);
    if (w <= 0 || h <= 0) {
        surface_data.clear();
        surface.reset();
        return;
    }

    const int stride = w * 4;
    surface_data.resize(static_cast<std::size_t>(stride) * static_cast<std::size_t>(h));

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const auto src = picture.m_data.at(static_cast<std::size_t>(y) * picture.m_width + static_cast<std::size_t>(x));
            const auto b = static_cast<unsigned int>(src & 0xffu);
            const auto g = static_cast<unsigned int>((src >> 8) & 0xffu);
            const auto r = static_cast<unsigned int>((src >> 16) & 0xffu);
            const auto a = static_cast<unsigned int>((src >> 24) & 0xffu);

            const auto rp = static_cast<unsigned char>((r * a + 127u) / 255u);
            const auto gp = static_cast<unsigned char>((g * a + 127u) / 255u);
            const auto bp = static_cast<unsigned char>((b * a + 127u) / 255u);
            const auto ap = static_cast<unsigned char>(a);

            auto *dst = surface_data.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride)
                    + static_cast<std::size_t>(x) * 4;
            dst[0] = bp;
            dst[1] = gp;
            dst[2] = rp;
            dst[3] = ap;
        }
    }

    surface = Cairo::ImageSurface::create(surface_data.data(), Cairo::Surface::Format::ARGB32, w, h, stride);
}
} // namespace

ImageTraceDialog::ImageTraceDialog()
{
    set_title("Trace Image");
    set_default_size(1080, 660);
    set_modal(true);
    set_hide_on_close(true);

    auto header = Gtk::make_managed<Gtk::HeaderBar>();
    header->set_show_title_buttons(true);
    auto title_label = Gtk::make_managed<Gtk::Label>("Trace Image");
    header->set_title_widget(*title_label);
    m_apply_button = Gtk::make_managed<Gtk::Button>("Apply");
    m_apply_button->add_css_class("suggested-action");
    m_apply_button->set_sensitive(false);
    header->pack_end(*m_apply_button);
    set_titlebar(*header);

    auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    root->set_margin_start(12);
    root->set_margin_end(12);
    root->set_margin_top(12);
    root->set_margin_bottom(12);

    auto controls_frame = Gtk::make_managed<Gtk::Frame>("Trace Settings");
    controls_frame->set_size_request(200, -1);
    controls_frame->set_vexpand(true);
    auto controls = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    controls->set_margin_start(10);
    controls->set_margin_end(10);
    controls->set_margin_top(10);
    controls->set_margin_bottom(10);

    auto controls_hint = Gtk::make_managed<Gtk::Label>("Minimal controls. Preview updates automatically.");
    controls_hint->set_xalign(0);
    controls_hint->set_wrap(true);
    controls_hint->add_css_class("dim-label");
    controls->append(*controls_hint);

    auto add_slider_row = [controls](const Glib::ustring &title, Gtk::Scale *&scale, Gtk::Label *&value_label,
                                     double min, double max, double step, int digits) {
        auto col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        auto header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto label = Gtk::make_managed<Gtk::Label>(title);
        label->set_xalign(0);
        label->set_hexpand(true);
        value_label = Gtk::make_managed<Gtk::Label>();
        value_label->set_xalign(1);
        value_label->add_css_class("dim-label");
        header->append(*label);
        header->append(*value_label);

        scale = Gtk::make_managed<Gtk::Scale>(Gtk::Orientation::HORIZONTAL);
        scale->set_range(min, max);
        scale->set_increments(step, step * 5);
        scale->set_draw_value(false);
        scale->set_hexpand(true);
        scale->set_round_digits(digits);

        col->append(*header);
        col->append(*scale);
        controls->append(*col);
    };

    add_slider_row("Threshold", m_threshold_scale, m_threshold_value_label, 0, 255, 1, 0);
    add_slider_row("Smoothness", m_smoothness_scale, m_smoothness_value_label, 0.0, 6.0, 0.1, 1);
    add_slider_row("Noise ignore (px)", m_noise_scale, m_noise_value_label, 0, 500, 1, 0);
    add_slider_row("Opt tolerance", m_curve_strength_scale, m_curve_strength_value_label, 0.0, 1.0, 0.01, 2);

    auto outline_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto outline_label = Gtk::make_managed<Gtk::Label>("Outline mode");
    outline_label->set_xalign(0);
    outline_label->set_hexpand(true);
    m_outline_switch = Gtk::make_managed<Gtk::Switch>();
    m_outline_switch->set_active(false);
    outline_row->append(*outline_label);
    outline_row->append(*m_outline_switch);
    controls->append(*outline_row);

    auto outline_with_trace_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto outline_with_trace_label = Gtk::make_managed<Gtk::Label>("Trace + outline");
    outline_with_trace_label->set_xalign(0);
    outline_with_trace_label->set_hexpand(true);
    m_outline_with_trace_switch = Gtk::make_managed<Gtk::Switch>();
    m_outline_with_trace_switch->set_active(false);
    outline_with_trace_row->append(*outline_with_trace_label);
    outline_with_trace_row->append(*m_outline_with_trace_switch);
    controls->append(*outline_with_trace_row);

    auto outline_offset_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto outline_offset_label = Gtk::make_managed<Gtk::Label>("Outline offset (mm)");
    outline_offset_label->set_xalign(0);
    outline_offset_label->set_hexpand(true);
    m_outline_offset_dec_button = Gtk::make_managed<Gtk::Button>();
    m_outline_offset_dec_button->set_icon_name("go-previous-symbolic");
    m_outline_offset_dec_button->set_has_frame(true);
    m_outline_offset_value_label = Gtk::make_managed<Gtk::Label>("0.0");
    m_outline_offset_value_label->set_width_chars(5);
    m_outline_offset_value_label->set_xalign(0.5);
    m_outline_offset_inc_button = Gtk::make_managed<Gtk::Button>();
    m_outline_offset_inc_button->set_icon_name("go-next-symbolic");
    m_outline_offset_inc_button->set_has_frame(true);
    outline_offset_row->append(*outline_offset_label);
    outline_offset_row->append(*m_outline_offset_dec_button);
    outline_offset_row->append(*m_outline_offset_value_label);
    outline_offset_row->append(*m_outline_offset_inc_button);
    controls->append(*outline_offset_row);

    auto preview_hint = Gtk::make_managed<Gtk::Label>("Mouse wheel: zoom, drag: pan, double click: fit");
    preview_hint->set_xalign(0);
    preview_hint->set_wrap(true);
    preview_hint->add_css_class("dim-label");
    controls->append(*preview_hint);

    auto presets_frame = Gtk::make_managed<Gtk::Frame>("Presets");
    presets_frame->set_vexpand(true);
    auto presets_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    presets_box->set_margin_start(8);
    presets_box->set_margin_end(8);
    presets_box->set_margin_top(8);
    presets_box->set_margin_bottom(8);

    m_presets_list = Gtk::make_managed<Gtk::ListBox>();
    m_presets_list->set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_presets_list->set_vexpand(true);
    m_presets_list->add_css_class("boxed-list");
    presets_box->append(*m_presets_list);

    auto preset_buttons = Gtk::make_managed<Gtk::Grid>();
    preset_buttons->set_column_spacing(6);
    preset_buttons->set_row_spacing(6);
    m_preset_save_button = Gtk::make_managed<Gtk::Button>("Save Current");
    m_preset_import_button = Gtk::make_managed<Gtk::Button>("Import");
    m_preset_export_button = Gtk::make_managed<Gtk::Button>("Export");
    m_preset_save_button->set_hexpand(true);
    m_preset_import_button->set_hexpand(true);
    m_preset_export_button->set_hexpand(true);
    preset_buttons->attach(*m_preset_save_button, 0, 0, 2, 1);
    preset_buttons->attach(*m_preset_import_button, 0, 1, 1, 1);
    preset_buttons->attach(*m_preset_export_button, 1, 1, 1, 1);
    presets_box->append(*preset_buttons);

    presets_frame->set_child(*presets_box);
    controls->append(*presets_frame);

    m_status_label = Gtk::make_managed<Gtk::Label>();
    m_status_label->set_xalign(0);
    m_status_label->set_wrap(true);
    m_status_label->add_css_class("dim-label");
    controls->append(*m_status_label);

    m_controls_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    m_controls_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_controls_scroll->set_min_content_height(100);
    m_controls_scroll->set_child(*controls);
    controls_frame->set_child(*m_controls_scroll);
    root->append(*controls_frame);

    auto previews = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    previews->set_hexpand(true);
    previews->set_vexpand(true);

    auto original_frame = Gtk::make_managed<Gtk::Frame>("Original");
    original_frame->set_hexpand(true);
    original_frame->set_vexpand(true);
    m_original_aspect_frame = Gtk::make_managed<Gtk::AspectFrame>();
    m_original_aspect_frame->set_obey_child(false);
    m_original_aspect_frame->set_ratio(1.0f);
    m_original_aspect_frame->set_xalign(0.5f);
    m_original_aspect_frame->set_yalign(0.5f);
    m_original_aspect_frame->set_hexpand(true);
    m_original_aspect_frame->set_vexpand(true);
    m_original_preview = Gtk::make_managed<Gtk::DrawingArea>();
    m_original_preview->set_hexpand(true);
    m_original_preview->set_vexpand(true);
    m_original_preview->set_content_width(1);
    m_original_preview->set_content_height(1);
    m_original_preview->set_draw_func(sigc::mem_fun(*this, &ImageTraceDialog::draw_original_preview));
    m_original_aspect_frame->set_child(*m_original_preview);
    original_frame->set_child(*m_original_aspect_frame);
    previews->append(*original_frame);

    auto traced_frame = Gtk::make_managed<Gtk::Frame>("Traced");
    traced_frame->set_hexpand(true);
    traced_frame->set_vexpand(true);
    m_traced_aspect_frame = Gtk::make_managed<Gtk::AspectFrame>();
    m_traced_aspect_frame->set_obey_child(false);
    m_traced_aspect_frame->set_ratio(1.0f);
    m_traced_aspect_frame->set_xalign(0.5f);
    m_traced_aspect_frame->set_yalign(0.5f);
    m_traced_aspect_frame->set_hexpand(true);
    m_traced_aspect_frame->set_vexpand(true);
    m_traced_preview = Gtk::make_managed<Gtk::DrawingArea>();
    m_traced_preview->set_hexpand(true);
    m_traced_preview->set_vexpand(true);
    m_traced_preview->set_content_width(1);
    m_traced_preview->set_content_height(1);
    m_traced_preview->set_draw_func(sigc::mem_fun(*this, &ImageTraceDialog::draw_traced_preview));
    m_traced_aspect_frame->set_child(*m_traced_preview);
    traced_frame->set_child(*m_traced_aspect_frame);
    previews->append(*traced_frame);

    root->append(*previews);
    set_child(*root);

    setup_preview_interaction(*m_original_preview, m_original_view, false);
    setup_preview_interaction(*m_traced_preview, m_traced_view, true);

    m_threshold_scale->set_value(160);
    m_smoothness_scale->set_value(1.2);
    m_noise_scale->set_value(16);
    m_curve_strength_scale->set_value(0.38);
    m_outline_offset_mm = 0.0;

    auto on_value_changed = [this] { update_scale_value_labels(); };
    m_threshold_scale->signal_value_changed().connect(on_value_changed);
    m_smoothness_scale->signal_value_changed().connect(on_value_changed);
    m_noise_scale->signal_value_changed().connect(on_value_changed);
    m_curve_strength_scale->signal_value_changed().connect(on_value_changed);

    m_apply_button->signal_clicked().connect([this] {
        if (!m_last_result.ok || m_last_result.svg.empty())
            return;
        m_signal_apply.emit(m_last_result.svg);
    });
    connect_scale_auto_trace(*m_threshold_scale);
    connect_scale_auto_trace(*m_smoothness_scale);
    connect_scale_auto_trace(*m_noise_scale);
    connect_scale_auto_trace(*m_curve_strength_scale);
    if (m_outline_switch) {
        m_outline_switch->property_active().signal_changed().connect([this] {
            if (m_outline_offset_dec_button)
                m_outline_offset_dec_button->set_sensitive(m_outline_switch->get_active());
            if (m_outline_offset_inc_button)
                m_outline_offset_inc_button->set_sensitive(m_outline_switch->get_active());
            if (m_outline_with_trace_switch)
                m_outline_with_trace_switch->set_sensitive(m_outline_switch->get_active());
            if (m_outline_offset_value_label)
                m_outline_offset_value_label->set_sensitive(m_outline_switch->get_active());
            queue_trace_update();
        });
    }
    if (m_outline_with_trace_switch) {
        m_outline_with_trace_switch->property_active().signal_changed().connect([this] { queue_trace_update(); });
    }
    if (m_outline_offset_dec_button) {
        m_outline_offset_dec_button->signal_clicked().connect([this] {
            m_outline_offset_mm = std::max(0.0, m_outline_offset_mm - 0.1);
            update_scale_value_labels();
            queue_trace_update();
        });
    }
    if (m_outline_offset_inc_button) {
        m_outline_offset_inc_button->signal_clicked().connect([this] {
            m_outline_offset_mm = std::min(100.0, m_outline_offset_mm + 0.1);
            update_scale_value_labels();
            queue_trace_update();
        });
    }
    m_presets_list->signal_row_selected().connect([this](Gtk::ListBoxRow *row) {
        if (!row || m_updating_preset_list)
            return;
        const int idx = row->get_index();
        if (idx < 0 || static_cast<std::size_t>(idx) >= m_presets.size())
            return;
        apply_settings_to_ui(m_presets.at(static_cast<std::size_t>(idx)).settings);
        queue_trace_update();
    });
    m_preset_save_button->signal_clicked().connect(sigc::mem_fun(*this, &ImageTraceDialog::save_current_as_preset));
    m_preset_import_button->signal_clicked().connect(sigc::mem_fun(*this, &ImageTraceDialog::import_presets));
    m_preset_export_button->signal_clicked().connect(sigc::mem_fun(*this, &ImageTraceDialog::export_presets));

    load_default_presets();
    update_scale_value_labels();
    m_outline_offset_dec_button->set_sensitive(m_outline_switch->get_active());
    m_outline_offset_inc_button->set_sensitive(m_outline_switch->get_active());
    m_outline_with_trace_switch->set_sensitive(m_outline_switch->get_active());
    m_outline_offset_value_label->set_sensitive(m_outline_switch->get_active());
    select_preset_row(0);
}

bool ImageTraceDialog::load_image(const std::filesystem::path &path, std::string &error_message)
{
    error_message.clear();
    try {
        m_picture_data = picture_data_from_file(path);
    }
    catch (const Glib::Error &err) {
        error_message = err.what();
        return false;
    }
    catch (const std::exception &err) {
        error_message = err.what();
        return false;
    }

    if (!m_picture_data) {
        error_message = "Failed to load image";
        return false;
    }

    if (m_original_aspect_frame && m_traced_aspect_frame && m_picture_data->m_width > 0 && m_picture_data->m_height > 0) {
        const auto ratio = static_cast<float>(m_picture_data->m_width) / static_cast<float>(m_picture_data->m_height);
        m_original_aspect_frame->set_ratio(ratio);
        m_traced_aspect_frame->set_ratio(ratio);
    }

    copy_surface_data_from_picture(*m_picture_data, m_original_surface_data, m_original_surface);
    reset_viewports();
    run_trace();
    return true;
}

ImageTraceDialog::type_signal_apply &ImageTraceDialog::signal_apply()
{
    return m_signal_apply;
}

void ImageTraceDialog::run_trace()
{
    if (!m_picture_data)
        return;

    auto settings = read_settings_from_ui();
    m_last_curve_fit = settings.curve_fit;
    m_last_curve_strength = settings.curve_strength;

    m_last_result = trace_picture_to_svg(*m_picture_data, settings);
    if (m_last_result.ok) {
        if (settings.outline_mode)
            m_status_label->set_text("Outline contour ready");
        else
            m_status_label->set_text("Contours: " + std::to_string(m_last_result.contours.size()));
        m_apply_button->set_sensitive(true);
    }
    else {
        m_status_label->set_text(m_last_result.error);
        m_apply_button->set_sensitive(false);
    }

    queue_previews_draw();
}

void ImageTraceDialog::queue_trace_update()
{
    if (m_trace_debounce_conn.connected())
        m_trace_debounce_conn.disconnect();
    m_trace_debounce_conn = Glib::signal_timeout().connect(
            [this] {
                run_trace();
                return false;
            },
            120);
}

void ImageTraceDialog::connect_scale_auto_trace(Gtk::Scale &scale)
{
    auto wheel = Gtk::EventControllerScroll::create();
    wheel->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    wheel->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    wheel->signal_scroll().connect(
            [this](double, double dy) {
                if (!m_controls_scroll)
                    return false;
                auto adj = m_controls_scroll->get_vadjustment();
                if (!adj)
                    return false;

                const double page = std::max(40.0, adj->get_page_increment());
                const double v_min = adj->get_lower();
                const double v_max = std::max(v_min, adj->get_upper() - adj->get_page_size());
                const double next = std::clamp(adj->get_value() + dy * page * 0.5, v_min, v_max);
                adj->set_value(next);
                return true;
            },
            false);
    scale.add_controller(wheel);

    auto click = Gtk::GestureClick::create();
    click->set_button(1);
    click->signal_released().connect([this](int, double, double) { queue_trace_update(); });
    scale.add_controller(click);
}

void ImageTraceDialog::queue_previews_draw()
{
    if (m_original_preview)
        m_original_preview->queue_draw();
    if (m_traced_preview)
        m_traced_preview->queue_draw();
}

void ImageTraceDialog::update_scale_value_labels()
{
    if (m_threshold_value_label)
        m_threshold_value_label->set_text(format_value(m_threshold_scale->get_value(), 0));
    if (m_smoothness_value_label)
        m_smoothness_value_label->set_text(format_value(m_smoothness_scale->get_value(), 1));
    if (m_noise_value_label)
        m_noise_value_label->set_text(format_value(m_noise_scale->get_value(), 0));
    if (m_curve_strength_value_label)
        m_curve_strength_value_label->set_text(format_value(m_curve_strength_scale->get_value(), 2));
    if (m_outline_offset_value_label)
        m_outline_offset_value_label->set_text(format_value(m_outline_offset_mm, 1));
}

void ImageTraceDialog::reset_viewports()
{
    m_original_view = PreviewState{};
    m_traced_view = PreviewState{};
    queue_previews_draw();
}

ImageTraceSettings ImageTraceDialog::read_settings_from_ui() const
{
    ImageTraceSettings settings;
    settings.threshold = static_cast<int>(std::round(m_threshold_scale->get_value()));
    settings.smoothness = m_smoothness_scale->get_value();
    settings.noise_ignore = static_cast<int>(std::round(m_noise_scale->get_value()));
    settings.opt_tolerance = std::clamp(m_curve_strength_scale->get_value(), 0.0, 1.0);
    settings.blur = std::clamp(static_cast<int>(std::lround(settings.smoothness * 0.55)), 0, 4);
    settings.anti_stair = true;
    settings.anti_stair_strength = std::clamp(0.20 + settings.opt_tolerance * 0.80, 0.0, 1.0);
    settings.detail_preserve = std::clamp(0.90 - settings.opt_tolerance * 0.70, 0.0, 1.0);
    settings.outline_mode = m_outline_switch && m_outline_switch->get_active();
    settings.outline_with_trace = m_outline_with_trace_switch && m_outline_with_trace_switch->get_active();
    settings.outline_offset_mm = std::clamp(m_outline_offset_mm, 0.0, 100.0);
    settings.curve_fit = true;
    const double smooth_norm = std::clamp(settings.smoothness / 6.0, 0.0, 1.0);
    settings.curve_strength = std::clamp(0.82 + smooth_norm * 0.12, 0.75, 1.05);
    return settings;
}

void ImageTraceDialog::apply_settings_to_ui(const ImageTraceSettings &settings)
{
    m_threshold_scale->set_value(std::clamp(settings.threshold, 0, 255));
    m_smoothness_scale->set_value(std::clamp(settings.smoothness, 0.0, 6.0));
    m_noise_scale->set_value(std::clamp(settings.noise_ignore, 0, 500));
    double opt = settings.opt_tolerance;
    if (opt <= 1e-9) {
        opt = std::clamp((settings.anti_stair_strength - 0.20) / 0.80, 0.0, 1.0);
    }
    m_curve_strength_scale->set_value(std::clamp(opt, 0.0, 1.0));
    m_outline_offset_mm = std::clamp(settings.outline_offset_mm, 0.0, 100.0);
    if (m_outline_switch)
        m_outline_switch->set_active(settings.outline_mode);
    if (m_outline_with_trace_switch)
        m_outline_with_trace_switch->set_active(settings.outline_with_trace);
    if (m_outline_offset_dec_button)
        m_outline_offset_dec_button->set_sensitive(settings.outline_mode);
    if (m_outline_offset_inc_button)
        m_outline_offset_inc_button->set_sensitive(settings.outline_mode);
    if (m_outline_with_trace_switch)
        m_outline_with_trace_switch->set_sensitive(settings.outline_mode);
    if (m_outline_offset_value_label)
        m_outline_offset_value_label->set_sensitive(settings.outline_mode);
    update_scale_value_labels();
}

void ImageTraceDialog::append_preset(const TracePreset &preset)
{
    if (preset.name.empty())
        return;
    m_presets.push_back(preset);
}

void ImageTraceDialog::rebuild_preset_list()
{
    if (!m_presets_list)
        return;

    m_updating_preset_list = true;
    while (auto *child = m_presets_list->get_first_child())
        m_presets_list->remove(*child);

    for (const auto &preset : m_presets) {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto row_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row_box->set_margin_start(8);
        row_box->set_margin_end(8);
        row_box->set_margin_top(6);
        row_box->set_margin_bottom(6);
        auto label = Gtk::make_managed<Gtk::Label>(preset.name);
        label->set_xalign(0);
        label->set_hexpand(true);
        row_box->append(*label);

        auto remove_button = Gtk::make_managed<Gtk::Button>();
        remove_button->set_icon_name("list-remove-symbolic");
        remove_button->set_has_frame(false);
        remove_button->set_tooltip_text("Delete preset");
        remove_button->add_css_class("flat");
        remove_button->add_css_class("circular");
        remove_button->set_valign(Gtk::Align::CENTER);
        row_box->append(*remove_button);
        remove_button->signal_clicked().connect(
                [this, row, remove_button] { request_delete_preset(*remove_button, *row); });

        row->set_child(*row_box);
        m_presets_list->append(*row);
    }
    m_updating_preset_list = false;
}

void ImageTraceDialog::load_default_presets()
{
    m_presets.clear();

    TracePreset balanced;
    balanced.name = "Balanced (Default)";
    balanced.settings.threshold = 160;
    balanced.settings.smoothness = 1.2;
    balanced.settings.noise_ignore = 16;
    balanced.settings.opt_tolerance = 0.38;
    balanced.settings.blur = 1;
    balanced.settings.anti_stair = true;
    balanced.settings.anti_stair_strength = 0.50;
    balanced.settings.detail_preserve = 0.63;
    balanced.settings.outline_mode = false;
    balanced.settings.outline_with_trace = false;
    balanced.settings.outline_offset_mm = 0.0;
    balanced.settings.curve_fit = true;
    balanced.settings.curve_strength = 0.85;
    append_preset(balanced);

    rebuild_preset_list();
}

void ImageTraceDialog::select_preset_row(int index)
{
    if (!m_presets_list)
        return;
    if (index < 0 || static_cast<std::size_t>(index) >= m_presets.size())
        return;
    if (auto *row = m_presets_list->get_row_at_index(index))
        m_presets_list->select_row(*row);
}

void ImageTraceDialog::save_current_as_preset()
{
    auto preset = TracePreset{};
    preset.settings = read_settings_from_ui();
    while (true) {
        preset.name = "Custom " + std::to_string(m_custom_preset_counter++);
        const bool exists = std::any_of(m_presets.begin(), m_presets.end(),
                                        [&](const auto &p) { return p.name == preset.name; });
        if (!exists)
            break;
    }
    append_preset(preset);
    rebuild_preset_list();
    select_preset_row(static_cast<int>(m_presets.size()) - 1);
    if (m_status_label)
        m_status_label->set_text("Saved preset: " + preset.name);
}

void ImageTraceDialog::request_delete_preset(Gtk::Widget &anchor, Gtk::ListBoxRow &row)
{
    const int idx = row.get_index();
    if (idx < 0 || static_cast<std::size_t>(idx) >= m_presets.size())
        return;

    auto popover = Gtk::make_managed<Gtk::Popover>();
    popover->set_parent(anchor);
    popover->set_has_arrow(true);
    popover->set_autohide(true);
    popover->set_position(Gtk::PositionType::LEFT);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin_start(10);
    box->set_margin_end(10);
    box->set_margin_top(10);
    box->set_margin_bottom(10);

    auto label = Gtk::make_managed<Gtk::Label>("Delete preset \"" + m_presets.at(static_cast<std::size_t>(idx)).name + "\"?");
    label->set_wrap(true);
    label->set_xalign(0);
    label->set_max_width_chars(28);
    box->append(*label);

    auto actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    actions->set_halign(Gtk::Align::END);
    auto cancel_button = Gtk::make_managed<Gtk::Button>("Cancel");
    auto delete_button = Gtk::make_managed<Gtk::Button>("Delete");
    delete_button->add_css_class("destructive-action");
    actions->append(*cancel_button);
    actions->append(*delete_button);
    box->append(*actions);

    cancel_button->signal_clicked().connect([popover] { popover->popdown(); });
    delete_button->signal_clicked().connect([this, popover, &row] {
        delete_preset_at(row.get_index());
        popover->popdown();
    });

    popover->set_child(*box);
    popover->popup();
}

void ImageTraceDialog::delete_preset_at(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_presets.size())
        return;

    const auto name = m_presets.at(static_cast<std::size_t>(index)).name;
    m_presets.erase(m_presets.begin() + index);
    rebuild_preset_list();

    if (!m_presets.empty()) {
        select_preset_row(std::clamp(index, 0, static_cast<int>(m_presets.size()) - 1));
    }

    if (m_status_label)
        m_status_label->set_text("Deleted preset: " + name);
}

json ImageTraceDialog::serialize_presets() const
{
    json j;
    j["type"] = "dxfsketcher_trace_presets";
    j["version"] = 1;
    auto &arr = j["presets"];
    arr = json::array();
    for (const auto &preset : m_presets) {
        json p;
        p["name"] = preset.name;
        p["threshold"] = preset.settings.threshold;
        p["smoothness"] = preset.settings.smoothness;
        p["noise_ignore"] = preset.settings.noise_ignore;
        p["opt_tolerance"] = preset.settings.opt_tolerance;
        p["blur"] = preset.settings.blur;
        p["curve_fit"] = preset.settings.curve_fit;
        p["curve_strength"] = preset.settings.curve_strength;
        p["anti_stair"] = preset.settings.anti_stair;
        p["anti_stair_strength"] = preset.settings.anti_stair_strength;
        p["detail_preserve"] = preset.settings.detail_preserve;
        p["outline_mode"] = preset.settings.outline_mode;
        p["outline_with_trace"] = preset.settings.outline_with_trace;
        p["outline_offset_mm"] = preset.settings.outline_offset_mm;
        arr.push_back(p);
    }
    return j;
}

void ImageTraceDialog::deserialize_presets(const json &j)
{
    if (!j.is_object() || !j.contains("presets") || !j.at("presets").is_array())
        throw std::runtime_error("Invalid presets file");

    std::size_t imported = 0;
    for (const auto &p : j.at("presets")) {
        if (!p.is_object())
            continue;
        const auto name = p.value("name", std::string{});
        if (name.empty())
            continue;
        TracePreset preset;
        preset.name = name;
        preset.settings.threshold = std::clamp(p.value("threshold", 160), 0, 255);
        preset.settings.smoothness = std::clamp(p.value("smoothness", 1.2), 0.0, 6.0);
        preset.settings.noise_ignore = std::clamp(p.value("noise_ignore", 16), 0, 500);
        preset.settings.opt_tolerance = std::clamp(p.value("opt_tolerance", 0.38), 0.0, 1.0);
        preset.settings.blur = std::clamp(p.value("blur", 1), 0, 4);
        preset.settings.curve_fit = p.value("curve_fit", true);
        preset.settings.curve_strength = p.value("curve_strength", 0.85);
        preset.settings.anti_stair = p.value("anti_stair", true);
        preset.settings.anti_stair_strength = std::clamp(p.value("anti_stair_strength", 0.65), 0.0, 1.0);
        preset.settings.detail_preserve = std::clamp(p.value("detail_preserve", 0.65), 0.0, 1.0);
        preset.settings.outline_mode = p.value("outline_mode", false);
        preset.settings.outline_with_trace = p.value("outline_with_trace", false);
        preset.settings.outline_offset_mm = std::clamp(p.value("outline_offset_mm", 0.0), 0.0, 100.0);

        append_preset(preset);
        imported++;
    }

    rebuild_preset_list();
    if (imported > 0) {
        select_preset_row(static_cast<int>(m_presets.size()) - 1);
        if (m_status_label)
            m_status_label->set_text("Imported presets: " + std::to_string(imported));
    }
    else if (m_status_label) {
        m_status_label->set_text("No valid presets in file");
    }
}

void ImageTraceDialog::import_presets()
{
    auto dialog = Gtk::FileDialog::create();
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto filter_json = Gtk::FileFilter::create();
    filter_json->set_name("JSON");
    filter_json->add_pattern("*.json");
    filters->append(filter_json);
    dialog->set_filters(filters);
    dialog->open(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->open_finish(result);
            const auto path = path_from_string(file->get_path());
            deserialize_presets(load_json_from_file(path));
        }
        catch (const Gtk::DialogError &) {
        }
        catch (const std::exception &err) {
            if (m_status_label)
                m_status_label->set_text(std::string("Import failed: ") + err.what());
        }
    });
}

void ImageTraceDialog::export_presets()
{
    auto dialog = Gtk::FileDialog::create();
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto filter_json = Gtk::FileFilter::create();
    filter_json->set_name("JSON");
    filter_json->add_pattern("*.json");
    filters->append(filter_json);
    dialog->set_filters(filters);
    dialog->set_initial_name("trace-presets.json");
    dialog->save(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->save_finish(result);
            const auto path = path_from_string(file->get_path());
            save_json_to_file(path, serialize_presets());
            if (m_status_label)
                m_status_label->set_text("Presets exported");
        }
        catch (const Gtk::DialogError &) {
        }
        catch (const std::exception &err) {
            if (m_status_label)
                m_status_label->set_text(std::string("Export failed: ") + err.what());
        }
    });
}

std::pair<unsigned int, unsigned int> ImageTraceDialog::get_preview_source_size(bool traced) const
{
    if (traced && m_last_result.width > 0 && m_last_result.height > 0)
        return {m_last_result.width, m_last_result.height};
    if (m_picture_data)
        return {m_picture_data->m_width, m_picture_data->m_height};
    return {0, 0};
}

ImageTraceDialog::PreviewTransform ImageTraceDialog::get_preview_transform(const PreviewState &state, int area_w,
                                                                           int area_h, unsigned int source_w,
                                                                           unsigned int source_h) const
{
    PreviewTransform tf;
    if (area_w <= 0 || area_h <= 0 || source_w == 0 || source_h == 0)
        return tf;

    constexpr double pad = 12.0;
    const double avail_w = std::max(1.0, static_cast<double>(area_w) - 2.0 * pad);
    const double avail_h = std::max(1.0, static_cast<double>(area_h) - 2.0 * pad);
    const double fit_scale = std::max(1e-6, std::min(avail_w / static_cast<double>(source_w),
                                                      avail_h / static_cast<double>(source_h)));

    tf.scale = fit_scale * state.zoom;
    tf.ox = (static_cast<double>(area_w) - static_cast<double>(source_w) * tf.scale) * 0.5 + state.pan_x;
    tf.oy = (static_cast<double>(area_h) - static_cast<double>(source_h) * tf.scale) * 0.5 + state.pan_y;
    return tf;
}

void ImageTraceDialog::setup_preview_interaction(Gtk::DrawingArea &area, PreviewState &state, bool traced)
{
    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([&state](double x, double y) {
        state.cursor_x = x;
        state.cursor_y = y;
    });
    area.add_controller(motion);

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll->signal_scroll().connect([this, &area, &state, traced](double, double dy) {
        const auto [source_w, source_h] = get_preview_source_size(traced);
        if (source_w == 0 || source_h == 0)
            return false;

        const int area_w = area.get_width();
        const int area_h = area.get_height();
        if (area_w <= 0 || area_h <= 0)
            return false;

        const auto before = get_preview_transform(state, area_w, area_h, source_w, source_h);
        if (before.scale <= 1e-9)
            return false;

        const double world_x = (state.cursor_x - before.ox) / before.scale;
        const double world_y = (state.cursor_y - before.oy) / before.scale;

        const double factor = (dy < 0) ? 1.15 : (1.0 / 1.15);
        state.zoom = std::clamp(state.zoom * factor, 0.1, 64.0);

        const auto after = get_preview_transform(state, area_w, area_h, source_w, source_h);
        state.pan_x = state.cursor_x
                - ((static_cast<double>(area_w) - static_cast<double>(source_w) * after.scale) * 0.5
                   + world_x * after.scale);
        state.pan_y = state.cursor_y
                - ((static_cast<double>(area_h) - static_cast<double>(source_h) * after.scale) * 0.5
                   + world_y * after.scale);

        area.queue_draw();
        return true;
    }, false);
    area.add_controller(scroll);

    auto drag = Gtk::GestureDrag::create();
    drag->set_button(1);
    drag->signal_drag_begin().connect([&state](double, double) {
        state.drag_pan_x = state.pan_x;
        state.drag_pan_y = state.pan_y;
    });
    drag->signal_drag_update().connect([&area, &state](double offset_x, double offset_y) {
        state.pan_x = state.drag_pan_x + offset_x;
        state.pan_y = state.drag_pan_y + offset_y;
        area.queue_draw();
    });
    area.add_controller(drag);

    auto click = Gtk::GestureClick::create();
    click->set_button(1);
    click->signal_pressed().connect([&area, &state](int n_press, double, double) {
        if (n_press == 2) {
            state.zoom = 1.0;
            state.pan_x = 0.0;
            state.pan_y = 0.0;
            area.queue_draw();
        }
    });
    area.add_controller(click);
}

void ImageTraceDialog::draw_original_preview(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h)
{
    cr->set_source_rgb(0.10, 0.10, 0.10);
    cr->paint();

    constexpr double border = 1.0;
    constexpr double radius = 10.0;
    const double x = border;
    const double y = border;
    const double ww = std::max(1.0, static_cast<double>(w) - 2.0 * border);
    const double hh = std::max(1.0, static_cast<double>(h) - 2.0 * border);

    cr->save();
    add_rounded_clip(cr, x, y, ww, hh, radius);
    cr->set_source_rgb(0.95, 0.95, 0.95);
    cr->paint();

    const auto [source_w, source_h] = get_preview_source_size(false);
    if (source_w > 0 && source_h > 0 && m_original_surface) {
        const auto tf = get_preview_transform(m_original_view, w, h, source_w, source_h);
        cr->translate(tf.ox, tf.oy);
        cr->scale(tf.scale, tf.scale);
        cr->set_source(m_original_surface, 0, 0);
        cr->paint();
    }
    cr->restore();

    cr->set_source_rgba(1, 1, 1, 0.15);
    cr->set_line_width(1.0);
    cr->begin_new_sub_path();
    cr->arc(x + ww - radius, y + radius, radius, -M_PI_2, 0);
    cr->arc(x + ww - radius, y + hh - radius, radius, 0, M_PI_2);
    cr->arc(x + radius, y + hh - radius, radius, M_PI_2, M_PI);
    cr->arc(x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
    cr->close_path();
    cr->stroke();
}

void ImageTraceDialog::draw_traced_preview(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h)
{
    cr->set_source_rgb(0.10, 0.10, 0.10);
    cr->paint();

    constexpr double border = 1.0;
    constexpr double radius = 10.0;
    const double x = border;
    const double y = border;
    const double ww = std::max(1.0, static_cast<double>(w) - 2.0 * border);
    const double hh = std::max(1.0, static_cast<double>(h) - 2.0 * border);

    cr->save();
    add_rounded_clip(cr, x, y, ww, hh, radius);
    cr->set_source_rgb(0.95, 0.95, 0.95);
    cr->paint();

    const auto [source_w, source_h] = get_preview_source_size(true);
    if (source_w > 0 && source_h > 0 && m_last_result.ok) {
        const auto tf = get_preview_transform(m_traced_view, w, h, source_w, source_h);
        cr->translate(tf.ox, tf.oy);
        cr->scale(tf.scale, tf.scale);
        cr->set_source_rgb(0.12, 0.13, 0.15);
        cr->set_line_width(std::max(0.7, 1.1 / tf.scale));
        const double k = std::clamp(m_last_curve_strength, 0.0, 1.5) / 6.0;
        for (const auto &poly : m_last_result.contours) {
            if (poly.size() < 2)
                continue;
            cr->move_to(poly.front().x, poly.front().y);
            if (!m_last_curve_fit || poly.size() < 4) {
                for (std::size_t i = 1; i < poly.size(); i++)
                    cr->line_to(poly.at(i).x, poly.at(i).y);
            }
            else {
                for (std::size_t i = 0; i < poly.size(); i++) {
                    const auto &p_prev = poly.at((i + poly.size() - 1) % poly.size());
                    const auto &p0 = poly.at(i);
                    const auto &p1 = poly.at((i + 1) % poly.size());
                    const auto &p2 = poly.at((i + 2) % poly.size());

                    const auto c1 = p0 + (p1 - p_prev) * k;
                    const auto c2 = p1 - (p2 - p0) * k;
                    cr->curve_to(c1.x, c1.y, c2.x, c2.y, p1.x, p1.y);
                }
            }
            cr->close_path();
        }
        cr->stroke();
    }
    cr->restore();

    cr->set_source_rgba(1, 1, 1, 0.15);
    cr->set_line_width(1.0);
    cr->begin_new_sub_path();
    cr->arc(x + ww - radius, y + radius, radius, -M_PI_2, 0);
    cr->arc(x + ww - radius, y + hh - radius, radius, 0, M_PI_2);
    cr->arc(x + radius, y + hh - radius, radius, M_PI_2, M_PI);
    cr->arc(x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
    cr->close_path();
    cr->stroke();
}

} // namespace dune3d

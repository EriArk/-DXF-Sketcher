#include "image_import_dialog.hpp"
#include "util/picture_data.hpp"
#include "util/picture_util.hpp"
#include <algorithm>
#include <cmath>

namespace dune3d {
namespace {
struct ImageBuffer {
    unsigned int w = 0;
    unsigned int h = 0;
    std::vector<uint32_t> px;
};

constexpr uint8_t clamp_u8(int v)
{
    return static_cast<uint8_t>(std::clamp(v, 0, 255));
}

uint8_t px_r(uint32_t px)
{
    return static_cast<uint8_t>((px >> 16u) & 0xffu);
}

uint8_t px_g(uint32_t px)
{
    return static_cast<uint8_t>((px >> 8u) & 0xffu);
}

uint8_t px_b(uint32_t px)
{
    return static_cast<uint8_t>(px & 0xffu);
}

uint8_t px_a(uint32_t px)
{
    return static_cast<uint8_t>((px >> 24u) & 0xffu);
}

uint32_t make_px(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return static_cast<uint32_t>(b) | (static_cast<uint32_t>(g) << 8u) | (static_cast<uint32_t>(r) << 16u)
            | (static_cast<uint32_t>(a) << 24u);
}

double luma(uint32_t px)
{
    return 0.299 * static_cast<double>(px_r(px)) + 0.587 * static_cast<double>(px_g(px))
            + 0.114 * static_cast<double>(px_b(px));
}

ImageBuffer to_image_buffer(const std::shared_ptr<const PictureData> &picture)
{
    ImageBuffer out;
    if (!picture)
        return out;
    out.w = picture->m_width;
    out.h = picture->m_height;
    out.px = picture->m_data;
    return out;
}

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

ImageBuffer crop_image(const ImageBuffer &src, int x, int y, int w, int h)
{
    if (src.w == 0 || src.h == 0 || src.px.empty())
        return {};
    const int sx0 = std::clamp(x, 0, static_cast<int>(src.w));
    const int sy0 = std::clamp(y, 0, static_cast<int>(src.h));
    const int sx1 = std::clamp(x + w, 0, static_cast<int>(src.w));
    const int sy1 = std::clamp(y + h, 0, static_cast<int>(src.h));
    if (sx1 <= sx0 || sy1 <= sy0)
        return src;

    ImageBuffer out;
    out.w = static_cast<unsigned int>(sx1 - sx0);
    out.h = static_cast<unsigned int>(sy1 - sy0);
    out.px.resize(static_cast<std::size_t>(out.w) * out.h);
    for (unsigned int yy = 0; yy < out.h; yy++) {
        const auto *src_row = src.px.data() + (static_cast<std::size_t>(sy0) + yy) * src.w + static_cast<std::size_t>(sx0);
        auto *dst_row = out.px.data() + yy * out.w;
        std::copy(src_row, src_row + out.w, dst_row);
    }
    return out;
}

ImageBuffer resize_bilinear(const ImageBuffer &src, unsigned int dst_w, unsigned int dst_h)
{
    if (src.w == 0 || src.h == 0 || src.px.empty() || dst_w == 0 || dst_h == 0)
        return src;
    if (src.w == dst_w && src.h == dst_h)
        return src;

    ImageBuffer out;
    out.w = dst_w;
    out.h = dst_h;
    out.px.resize(static_cast<std::size_t>(dst_w) * dst_h);

    for (unsigned int y = 0; y < dst_h; y++) {
        const double fy = ((static_cast<double>(y) + 0.5) * static_cast<double>(src.h) / static_cast<double>(dst_h)) - 0.5;
        const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, static_cast<int>(src.h) - 1);
        const int y1 = std::clamp(y0 + 1, 0, static_cast<int>(src.h) - 1);
        const double ty = std::clamp(fy - std::floor(fy), 0.0, 1.0);
        for (unsigned int x = 0; x < dst_w; x++) {
            const double fx =
                    ((static_cast<double>(x) + 0.5) * static_cast<double>(src.w) / static_cast<double>(dst_w)) - 0.5;
            const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, static_cast<int>(src.w) - 1);
            const int x1 = std::clamp(x0 + 1, 0, static_cast<int>(src.w) - 1);
            const double tx = std::clamp(fx - std::floor(fx), 0.0, 1.0);

            const auto p00 = src.px.at(static_cast<std::size_t>(y0) * src.w + static_cast<std::size_t>(x0));
            const auto p10 = src.px.at(static_cast<std::size_t>(y0) * src.w + static_cast<std::size_t>(x1));
            const auto p01 = src.px.at(static_cast<std::size_t>(y1) * src.w + static_cast<std::size_t>(x0));
            const auto p11 = src.px.at(static_cast<std::size_t>(y1) * src.w + static_cast<std::size_t>(x1));

            auto sample = [tx, ty](uint8_t c00, uint8_t c10, uint8_t c01, uint8_t c11) {
                const auto top = static_cast<double>(c00) * (1.0 - tx) + static_cast<double>(c10) * tx;
                const auto bot = static_cast<double>(c01) * (1.0 - tx) + static_cast<double>(c11) * tx;
                return clamp_u8(static_cast<int>(std::lround(top * (1.0 - ty) + bot * ty)));
            };

            out.px.at(static_cast<std::size_t>(y) * dst_w + static_cast<std::size_t>(x)) =
                    make_px(sample(px_r(p00), px_r(p10), px_r(p01), px_r(p11)),
                            sample(px_g(p00), px_g(p10), px_g(p01), px_g(p11)),
                            sample(px_b(p00), px_b(p10), px_b(p01), px_b(p11)),
                            sample(px_a(p00), px_a(p10), px_a(p01), px_a(p11)));
        }
    }

    return out;
}

std::vector<uint32_t> box_blur(const std::vector<uint32_t> &src, unsigned int w, unsigned int h, int radius)
{
    if (radius <= 0 || w == 0 || h == 0)
        return src;
    std::vector<uint32_t> out(src.size(), 0);
    for (unsigned int y = 0; y < h; y++) {
        for (unsigned int x = 0; x < w; x++) {
            int sr = 0;
            int sg = 0;
            int sb = 0;
            int sa = 0;
            int count = 0;
            for (int dy = -radius; dy <= radius; dy++) {
                const int yy = static_cast<int>(y) + dy;
                if (yy < 0 || yy >= static_cast<int>(h))
                    continue;
                for (int dx = -radius; dx <= radius; dx++) {
                    const int xx = static_cast<int>(x) + dx;
                    if (xx < 0 || xx >= static_cast<int>(w))
                        continue;
                    const auto px = src.at(static_cast<std::size_t>(yy) * w + static_cast<std::size_t>(xx));
                    sr += px_r(px);
                    sg += px_g(px);
                    sb += px_b(px);
                    sa += px_a(px);
                    count++;
                }
            }
            if (count <= 0)
                count = 1;
            out.at(static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) =
                    make_px(static_cast<uint8_t>(sr / count), static_cast<uint8_t>(sg / count),
                            static_cast<uint8_t>(sb / count), static_cast<uint8_t>(sa / count));
        }
    }
    return out;
}

void apply_contrast_brightness(std::vector<uint32_t> &px, double contrast_value, double brightness_value)
{
    if (std::abs(contrast_value) < 1e-6 && std::abs(brightness_value) < 1e-6)
        return;

    const double c = std::clamp(contrast_value, -100.0, 100.0) * 2.55;
    const double factor = (259.0 * (c + 255.0)) / (255.0 * (259.0 - c));
    const int bright_shift = static_cast<int>(std::lround(std::clamp(brightness_value, -100.0, 100.0) * 2.55));

    for (auto &it : px) {
        const auto a = px_a(it);
        const auto apply_one = [factor, bright_shift](uint8_t v) {
            const auto cv = static_cast<int>(std::lround(factor * (static_cast<double>(v) - 128.0) + 128.0)) + bright_shift;
            return clamp_u8(cv);
        };
        it = make_px(apply_one(px_r(it)), apply_one(px_g(it)), apply_one(px_b(it)), a);
    }
}

void apply_grayscale_mix(std::vector<uint32_t> &px, double grayscale_value)
{
    if (std::abs(grayscale_value) < 1e-6)
        return;
    const double v = std::clamp(grayscale_value, -100.0, 100.0);
    for (auto &it : px) {
        const auto a = px_a(it);
        const double lum = luma(it);
        double r = px_r(it);
        double g = px_g(it);
        double b = px_b(it);
        if (v >= 0) {
            const double t = v / 100.0;
            r = r * (1.0 - t) + lum * t;
            g = g * (1.0 - t) + lum * t;
            b = b * (1.0 - t) + lum * t;
        }
        else {
            const double sat = 1.0 + (-v / 100.0);
            r = lum + (r - lum) * sat;
            g = lum + (g - lum) * sat;
            b = lum + (b - lum) * sat;
        }
        it = make_px(clamp_u8(static_cast<int>(std::lround(r))), clamp_u8(static_cast<int>(std::lround(g))),
                     clamp_u8(static_cast<int>(std::lround(b))), a);
    }
}

void apply_filter_mode(std::vector<uint32_t> &px, unsigned int w, unsigned int h, ImageImportDialog::FilterMode mode)
{
    if (mode == ImageImportDialog::FilterMode::COLOR || w == 0 || h == 0)
        return;

    if (mode == ImageImportDialog::FilterMode::GRAYSCALE || mode == ImageImportDialog::FilterMode::BLACK_WHITE) {
        for (auto &it : px) {
            const auto a = px_a(it);
            const auto gv = clamp_u8(static_cast<int>(std::lround(luma(it))));
            const auto out = mode == ImageImportDialog::FilterMode::BLACK_WHITE ? (gv >= 128 ? 255 : 0) : gv;
            it = make_px(out, out, out, a);
        }
        return;
    }

    if (mode == ImageImportDialog::FilterMode::JITTER) {
        std::vector<double> g(px.size(), 0.0);
        for (std::size_t i = 0; i < px.size(); i++)
            g.at(i) = luma(px.at(i));

        const auto add_error = [w, h, &g](int x, int y, double e, double k) {
            if (x < 0 || y < 0 || x >= static_cast<int>(w) || y >= static_cast<int>(h))
                return;
            g.at(static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) += e * k;
        };

        for (unsigned int y = 0; y < h; y++) {
            for (unsigned int x = 0; x < w; x++) {
                const auto idx = static_cast<std::size_t>(y) * w + x;
                const auto oldv = std::clamp(g.at(idx), 0.0, 255.0);
                const double newv = oldv >= 128.0 ? 255.0 : 0.0;
                const double err = oldv - newv;
                g.at(idx) = newv;
                add_error(static_cast<int>(x) + 1, static_cast<int>(y), err, 7.0 / 16.0);
                add_error(static_cast<int>(x) - 1, static_cast<int>(y) + 1, err, 3.0 / 16.0);
                add_error(static_cast<int>(x), static_cast<int>(y) + 1, err, 5.0 / 16.0);
                add_error(static_cast<int>(x) + 1, static_cast<int>(y) + 1, err, 1.0 / 16.0);
            }
        }

        for (std::size_t i = 0; i < px.size(); i++) {
            const auto a = px_a(px.at(i));
            const auto v = clamp_u8(static_cast<int>(std::lround(std::clamp(g.at(i), 0.0, 255.0))));
            px.at(i) = make_px(v, v, v, a);
        }
        return;
    }

    if (mode == ImageImportDialog::FilterMode::STUCCO) {
        auto src = px;
        for (unsigned int y = 0; y < h; y++) {
            for (unsigned int x = 0; x < w; x++) {
                const auto idx = static_cast<std::size_t>(y) * w + x;
                const auto sx = std::max(0, static_cast<int>(x) - 1);
                const auto sy = std::max(0, static_cast<int>(y) - 1);
                const auto prev = src.at(static_cast<std::size_t>(sy) * w + static_cast<std::size_t>(sx));
                const double d = (luma(src.at(idx)) - luma(prev)) * 2.2;
                const auto v = clamp_u8(static_cast<int>(std::lround(128.0 + d)));
                px.at(idx) = make_px(v, v, v, px_a(src.at(idx)));
            }
        }
    }
}

void apply_sharp_blur(std::vector<uint32_t> &px, unsigned int w, unsigned int h, double sharp_blur_value)
{
    if (w == 0 || h == 0 || std::abs(sharp_blur_value) < 1e-6)
        return;

    const double v = std::clamp(sharp_blur_value, -100.0, 100.0);
    if (v < 0) {
        const double t = -v / 100.0;
        const int radius = 1 + static_cast<int>(std::floor(t * 2.5));
        auto blurred = box_blur(px, w, h, radius);
        for (std::size_t i = 0; i < px.size(); i++) {
            const auto a = px_a(px.at(i));
            auto mix_chan = [t](uint8_t a0, uint8_t b0) {
                return clamp_u8(static_cast<int>(std::lround(static_cast<double>(a0) * (1.0 - t) + b0 * t)));
            };
            px.at(i) = make_px(mix_chan(px_r(px.at(i)), px_r(blurred.at(i))), mix_chan(px_g(px.at(i)), px_g(blurred.at(i))),
                               mix_chan(px_b(px.at(i)), px_b(blurred.at(i))), a);
        }
    }
    else {
        const double amount = (v / 100.0) * 1.8;
        auto blurred = box_blur(px, w, h, 1);
        for (std::size_t i = 0; i < px.size(); i++) {
            const auto a = px_a(px.at(i));
            const auto apply = [amount](uint8_t src, uint8_t blur) {
                const double out = static_cast<double>(src) + (static_cast<double>(src) - static_cast<double>(blur)) * amount;
                return clamp_u8(static_cast<int>(std::lround(out)));
            };
            px.at(i) = make_px(apply(px_r(px.at(i)), px_r(blurred.at(i))), apply(px_g(px.at(i)), px_g(blurred.at(i))),
                               apply(px_b(px.at(i)), px_b(blurred.at(i))), a);
        }
    }
}

std::vector<uint8_t> build_binary_mask(const std::vector<uint32_t> &px)
{
    std::vector<uint8_t> mask(px.size(), 0);
    std::size_t alpha_count = 0;
    for (const auto c : px) {
        if (px_a(c) > 15)
            alpha_count++;
    }

    const bool mostly_opaque = alpha_count > (px.size() * 9) / 10;
    for (std::size_t i = 0; i < px.size(); i++) {
        const auto c = px.at(i);
        if (!mostly_opaque) {
            mask.at(i) = px_a(c) > 15 ? 1 : 0;
        }
        else {
            mask.at(i) = (px_r(c) < 245 || px_g(c) < 245 || px_b(c) < 245) ? 1 : 0;
        }
    }
    return mask;
}

std::vector<uint8_t> morph_mask(const std::vector<uint8_t> &mask, unsigned int w, unsigned int h, int radius, bool dilate)
{
    if (radius <= 0 || w == 0 || h == 0)
        return mask;
    std::vector<uint8_t> out(mask.size(), 0);
    for (unsigned int y = 0; y < h; y++) {
        for (unsigned int x = 0; x < w; x++) {
            bool value = dilate ? false : true;
            for (int dy = -radius; dy <= radius; dy++) {
                for (int dx = -radius; dx <= radius; dx++) {
                    const int xx = static_cast<int>(x) + dx;
                    const int yy = static_cast<int>(y) + dy;
                    const bool inside = xx >= 0 && yy >= 0 && xx < static_cast<int>(w) && yy < static_cast<int>(h);
                    const bool m = inside && mask.at(static_cast<std::size_t>(yy) * w + static_cast<std::size_t>(xx));
                    if (dilate) {
                        if (m) {
                            value = true;
                            dy = radius + 1;
                            break;
                        }
                    }
                    else if (!m) {
                        value = false;
                        dy = radius + 1;
                        break;
                    }
                }
            }
            out.at(static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) = value ? 1 : 0;
        }
    }
    return out;
}

ImageBuffer crop_to_nontransparent(const ImageBuffer &src)
{
    if (src.w == 0 || src.h == 0 || src.px.empty())
        return src;
    int min_x = static_cast<int>(src.w);
    int min_y = static_cast<int>(src.h);
    int max_x = -1;
    int max_y = -1;
    for (unsigned int y = 0; y < src.h; y++) {
        for (unsigned int x = 0; x < src.w; x++) {
            const auto a = px_a(src.px.at(static_cast<std::size_t>(y) * src.w + x));
            if (a == 0)
                continue;
            min_x = std::min(min_x, static_cast<int>(x));
            min_y = std::min(min_y, static_cast<int>(y));
            max_x = std::max(max_x, static_cast<int>(x));
            max_y = std::max(max_y, static_cast<int>(y));
        }
    }
    if (max_x < min_x || max_y < min_y)
        return src;
    return crop_image(src, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

void apply_outline_mode(ImageBuffer &img, double offset_px, bool background_remove)
{
    if (img.w == 0 || img.h == 0 || img.px.empty())
        return;

    auto mask = build_binary_mask(img.px);
    const int radius = std::clamp(static_cast<int>(std::lround(std::abs(offset_px))), 0, 24);
    if (offset_px > 0 && radius > 0) {
        mask = morph_mask(mask, img.w, img.h, radius, true);
    }
    else if (offset_px < 0 && radius > 0) {
        mask = morph_mask(mask, img.w, img.h, radius, false);
    }

    const auto eroded = morph_mask(mask, img.w, img.h, 1, false);
    std::vector<uint32_t> outlined(img.px.size(), make_px(0, 0, 0, 0));
    for (std::size_t i = 0; i < mask.size(); i++) {
        const bool edge = mask.at(i) && !eroded.at(i);
        if (edge)
            outlined.at(i) = make_px(0, 0, 0, 255);
    }
    img.px = std::move(outlined);
    if (background_remove)
        img = crop_to_nontransparent(img);
}

void invert_colors(std::vector<uint32_t> &px)
{
    for (auto &it : px) {
        const auto a = px_a(it);
        it = make_px(static_cast<uint8_t>(255 - px_r(it)), static_cast<uint8_t>(255 - px_g(it)),
                     static_cast<uint8_t>(255 - px_b(it)), a);
    }
}

std::string format_signed(double v)
{
    const auto iv = static_cast<int>(std::lround(v));
    if (iv > 0)
        return "+" + std::to_string(iv);
    return std::to_string(iv);
}
} // namespace

ImageImportDialog::ImageImportDialog()
{
    set_title("Import Raster");
    set_default_size(1160, 720);
    set_modal(true);
    set_hide_on_close(true);

    auto header = Gtk::make_managed<Gtk::HeaderBar>();
    header->set_show_title_buttons(true);
    auto title_label = Gtk::make_managed<Gtk::Label>("Import Raster");
    header->set_title_widget(*title_label);
    m_apply_button = Gtk::make_managed<Gtk::Button>("Apply");
    m_apply_button->add_css_class("suggested-action");
    m_apply_button->set_sensitive(false);
    m_trace_button = Gtk::make_managed<Gtk::Button>("Trace");
    m_trace_button->set_sensitive(false);
    header->pack_end(*m_apply_button);
    header->pack_end(*m_trace_button);
    set_titlebar(*header);

    auto root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    root->set_margin_start(12);
    root->set_margin_end(12);
    root->set_margin_top(12);
    root->set_margin_bottom(12);

    auto controls_frame = Gtk::make_managed<Gtk::Frame>("Image Settings");
    controls_frame->set_size_request(320, -1);
    controls_frame->set_vexpand(true);

    auto controls_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    controls_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    controls_scroll->set_min_content_height(100);
    auto controls = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    controls->set_margin_start(10);
    controls->set_margin_end(10);
    controls->set_margin_top(10);
    controls->set_margin_bottom(10);

    auto filter_label = Gtk::make_managed<Gtk::Label>("Filter");
    filter_label->set_xalign(0);
    controls->append(*filter_label);
    auto filter_model = Gtk::StringList::create();
    filter_model->append("Color");
    filter_model->append("Grayscale");
    filter_model->append("Black and White");
    filter_model->append("Jitter");
    filter_model->append("Stucco");
    m_filter_dropdown = Gtk::make_managed<Gtk::DropDown>(filter_model, nullptr);
    m_filter_dropdown->set_selected(static_cast<unsigned int>(FilterMode::COLOR));
    controls->append(*m_filter_dropdown);

    auto add_slider_row = [controls](const Glib::ustring &title, Gtk::Scale *&scale, Gtk::Label *&value_label) {
        auto col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto label = Gtk::make_managed<Gtk::Label>(title);
        label->set_xalign(0);
        label->set_hexpand(true);
        value_label = Gtk::make_managed<Gtk::Label>("0");
        value_label->add_css_class("dim-label");
        value_label->set_xalign(1);
        row->append(*label);
        row->append(*value_label);
        scale = Gtk::make_managed<Gtk::Scale>(Gtk::Orientation::HORIZONTAL);
        scale->set_range(-100.0, 100.0);
        scale->set_value(0.0);
        scale->set_increments(1.0, 10.0);
        scale->set_draw_value(false);
        col->append(*row);
        col->append(*scale);
        controls->append(*col);
    };
    add_slider_row("Contrast", m_contrast_scale, m_contrast_value_label);
    add_slider_row("Brightness", m_brightness_scale, m_brightness_value_label);
    add_slider_row("Grayscale", m_grayscale_scale, m_grayscale_value_label);
    add_slider_row("Sharp / Blur", m_sharp_blur_scale, m_sharp_blur_value_label);

    auto outline_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto outline_label = Gtk::make_managed<Gtk::Label>("Outline Mode");
    outline_label->set_xalign(0);
    outline_label->set_hexpand(true);
    m_outline_switch = Gtk::make_managed<Gtk::Switch>();
    outline_row->append(*outline_label);
    outline_row->append(*m_outline_switch);
    controls->append(*outline_row);

    auto outline_offset_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto outline_offset_label = Gtk::make_managed<Gtk::Label>("Outline offset");
    outline_offset_label->set_xalign(0);
    outline_offset_label->set_hexpand(true);
    m_outline_offset_dec_button = Gtk::make_managed<Gtk::Button>();
    m_outline_offset_dec_button->set_icon_name("go-previous-symbolic");
    m_outline_offset_dec_button->set_has_frame(true);
    m_outline_offset_value_label = Gtk::make_managed<Gtk::Label>("0 px");
    m_outline_offset_value_label->set_width_chars(6);
    m_outline_offset_value_label->set_xalign(0.5);
    m_outline_offset_inc_button = Gtk::make_managed<Gtk::Button>();
    m_outline_offset_inc_button->set_icon_name("go-next-symbolic");
    m_outline_offset_inc_button->set_has_frame(true);
    outline_offset_row->append(*outline_offset_label);
    outline_offset_row->append(*m_outline_offset_dec_button);
    outline_offset_row->append(*m_outline_offset_value_label);
    outline_offset_row->append(*m_outline_offset_inc_button);
    controls->append(*outline_offset_row);

    auto bg_remove_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto bg_remove_label = Gtk::make_managed<Gtk::Label>("Background Remove");
    bg_remove_label->set_xalign(0);
    bg_remove_label->set_hexpand(true);
    m_outline_bg_remove_switch = Gtk::make_managed<Gtk::Switch>();
    bg_remove_row->append(*bg_remove_label);
    bg_remove_row->append(*m_outline_bg_remove_switch);
    controls->append(*bg_remove_row);

    auto upscale_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto upscale_label = Gtk::make_managed<Gtk::Label>("Upscale");
    upscale_label->set_xalign(0);
    upscale_label->set_hexpand(true);
    m_upscale_switch = Gtk::make_managed<Gtk::Switch>();
    upscale_row->append(*upscale_label);
    upscale_row->append(*m_upscale_switch);
    controls->append(*upscale_row);

    auto upscale_factor_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto upscale_factor_label = Gtk::make_managed<Gtk::Label>("Upscale factor");
    upscale_factor_label->set_xalign(0);
    upscale_factor_label->set_hexpand(true);
    m_upscale_factor_dec_button = Gtk::make_managed<Gtk::Button>();
    m_upscale_factor_dec_button->set_icon_name("go-previous-symbolic");
    m_upscale_factor_dec_button->set_has_frame(true);
    m_upscale_factor_value_label = Gtk::make_managed<Gtk::Label>("x2");
    m_upscale_factor_value_label->set_width_chars(4);
    m_upscale_factor_value_label->set_xalign(0.5);
    m_upscale_factor_inc_button = Gtk::make_managed<Gtk::Button>();
    m_upscale_factor_inc_button->set_icon_name("go-next-symbolic");
    m_upscale_factor_inc_button->set_has_frame(true);
    upscale_factor_row->append(*upscale_factor_label);
    upscale_factor_row->append(*m_upscale_factor_dec_button);
    upscale_factor_row->append(*m_upscale_factor_value_label);
    upscale_factor_row->append(*m_upscale_factor_inc_button);
    controls->append(*upscale_factor_row);

    auto scale_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto scale_label = Gtk::make_managed<Gtk::Label>("Scale");
    scale_label->set_xalign(0);
    scale_label->set_hexpand(true);
    m_scale_switch = Gtk::make_managed<Gtk::Switch>();
    scale_row->append(*scale_label);
    scale_row->append(*m_scale_switch);
    controls->append(*scale_row);

    auto size_grid = Gtk::make_managed<Gtk::Grid>();
    size_grid->set_column_spacing(8);
    size_grid->set_row_spacing(6);
    auto width_label = Gtk::make_managed<Gtk::Label>("Width");
    width_label->set_xalign(0);
    auto height_label = Gtk::make_managed<Gtk::Label>("Height");
    height_label->set_xalign(0);
    m_scale_width_spin = Gtk::make_managed<Gtk::SpinButton>();
    m_scale_width_spin->set_range(0.1, 5000.0);
    m_scale_width_spin->set_digits(2);
    m_scale_width_spin->set_increments(0.5, 10.0);
    m_scale_height_spin = Gtk::make_managed<Gtk::SpinButton>();
    m_scale_height_spin->set_range(0.1, 5000.0);
    m_scale_height_spin->set_digits(2);
    m_scale_height_spin->set_increments(0.5, 10.0);
    size_grid->attach(*width_label, 0, 0, 1, 1);
    size_grid->attach(*m_scale_width_spin, 1, 0, 1, 1);
    size_grid->attach(*height_label, 0, 1, 1, 1);
    size_grid->attach(*m_scale_height_spin, 1, 1, 1, 1);
    controls->append(*size_grid);

    auto invert_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto invert_label = Gtk::make_managed<Gtk::Label>("Invert Color");
    invert_label->set_xalign(0);
    invert_label->set_hexpand(true);
    m_invert_switch = Gtk::make_managed<Gtk::Switch>();
    invert_row->append(*invert_label);
    invert_row->append(*m_invert_switch);
    controls->append(*invert_row);

    m_status_label = Gtk::make_managed<Gtk::Label>();
    m_status_label->set_xalign(0);
    m_status_label->set_wrap(true);
    m_status_label->add_css_class("dim-label");
    controls->append(*m_status_label);

    controls_scroll->set_child(*controls);
    controls_frame->set_child(*controls_scroll);
    root->append(*controls_frame);

    auto right = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    right->set_hexpand(true);
    right->set_vexpand(true);

    auto crop_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto crop_label = Gtk::make_managed<Gtk::Label>("Image Cropping");
    crop_label->set_xalign(0);
    crop_label->set_hexpand(true);
    m_crop_switch = Gtk::make_managed<Gtk::Switch>();
    crop_row->append(*crop_label);
    crop_row->append(*m_crop_switch);
    right->append(*crop_row);

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
    m_original_preview->set_draw_func(sigc::mem_fun(*this, &ImageImportDialog::draw_original_preview));
    m_original_aspect_frame->set_child(*m_original_preview);
    original_frame->set_child(*m_original_aspect_frame);
    right->append(*original_frame);

    auto result_frame = Gtk::make_managed<Gtk::Frame>("Result");
    result_frame->set_hexpand(true);
    result_frame->set_vexpand(true);
    m_result_aspect_frame = Gtk::make_managed<Gtk::AspectFrame>();
    m_result_aspect_frame->set_obey_child(false);
    m_result_aspect_frame->set_ratio(1.0f);
    m_result_aspect_frame->set_xalign(0.5f);
    m_result_aspect_frame->set_yalign(0.5f);
    m_result_aspect_frame->set_hexpand(true);
    m_result_aspect_frame->set_vexpand(true);
    m_result_preview = Gtk::make_managed<Gtk::DrawingArea>();
    m_result_preview->set_hexpand(true);
    m_result_preview->set_vexpand(true);
    m_result_preview->set_content_width(1);
    m_result_preview->set_content_height(1);
    m_result_preview->set_draw_func(sigc::mem_fun(*this, &ImageImportDialog::draw_result_preview));
    m_result_aspect_frame->set_child(*m_result_preview);
    result_frame->set_child(*m_result_aspect_frame);
    right->append(*result_frame);

    root->append(*right);
    set_child(*root);

    setup_preview_interaction(*m_original_preview, m_original_view, false);
    setup_preview_interaction(*m_result_preview, m_result_view, true);

    auto queue_cb = [this] { queue_processing(); };
    m_filter_dropdown->property_selected().signal_changed().connect(queue_cb);
    m_contrast_scale->signal_value_changed().connect([this] {
        update_value_labels();
        queue_processing();
    });
    m_brightness_scale->signal_value_changed().connect([this] {
        update_value_labels();
        queue_processing();
    });
    m_grayscale_scale->signal_value_changed().connect([this] {
        update_value_labels();
        queue_processing();
    });
    m_sharp_blur_scale->signal_value_changed().connect([this] {
        update_value_labels();
        queue_processing();
    });
    m_outline_switch->property_active().signal_changed().connect([this] {
        update_controls_state();
        queue_processing();
    });
    m_outline_bg_remove_switch->property_active().signal_changed().connect(queue_cb);
    m_outline_offset_dec_button->signal_clicked().connect([this] {
        m_outline_offset_px = std::max(-24.0, m_outline_offset_px - 1.0);
        update_value_labels();
        queue_processing();
    });
    m_outline_offset_inc_button->signal_clicked().connect([this] {
        m_outline_offset_px = std::min(24.0, m_outline_offset_px + 1.0);
        update_value_labels();
        queue_processing();
    });
    m_crop_switch->property_active().signal_changed().connect([this] {
        update_controls_state();
        queue_processing();
    });
    m_upscale_switch->property_active().signal_changed().connect([this] {
        update_controls_state();
        queue_processing();
    });
    m_upscale_factor_dec_button->signal_clicked().connect([this] {
        m_upscale_factor = std::max(2, m_upscale_factor - 1);
        update_value_labels();
        queue_processing();
    });
    m_upscale_factor_inc_button->signal_clicked().connect([this] {
        m_upscale_factor = std::min(4, m_upscale_factor + 1);
        update_value_labels();
        queue_processing();
    });
    m_scale_switch->property_active().signal_changed().connect([this] { update_controls_state(); });
    m_scale_width_spin->signal_value_changed().connect([this] {
        if (m_updating_scale_controls)
            return;
        m_last_scale_changed_was_width = true;
        set_scale_width(m_scale_width_spin->get_value());
    });
    m_scale_height_spin->signal_value_changed().connect([this] {
        if (m_updating_scale_controls)
            return;
        m_last_scale_changed_was_width = false;
        set_scale_height(m_scale_height_spin->get_value());
    });
    m_invert_switch->property_active().signal_changed().connect(queue_cb);

    m_apply_button->signal_clicked().connect([this] {
        if (!m_processed_picture)
            return;
        ImageImportPreparedData data;
        data.picture = m_processed_picture;
        data.use_target_size = m_scale_switch && m_scale_switch->get_active();
        if (data.use_target_size) {
            data.target_width = std::max(0.1, m_scale_width_spin->get_value());
            data.target_height = std::max(0.1, m_scale_height_spin->get_value());
        }
        m_signal_apply.emit(data);
    });
    m_trace_button->signal_clicked().connect([this] {
        if (!m_processed_picture)
            return;
        ImageImportPreparedData data;
        data.picture = m_processed_picture;
        data.use_target_size = false;
        m_signal_trace.emit(data);
    });

    update_value_labels();
    update_controls_state();
}

bool ImageImportDialog::load_image(const std::filesystem::path &path, std::string &error_message)
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

    if (m_original_aspect_frame && m_picture_data->m_width > 0 && m_picture_data->m_height > 0) {
        const auto ratio = static_cast<float>(m_picture_data->m_width) / static_cast<float>(m_picture_data->m_height);
        m_original_aspect_frame->set_ratio(ratio);
        if (m_result_aspect_frame)
            m_result_aspect_frame->set_ratio(ratio);
    }

    copy_surface_data_from_picture(*m_picture_data, m_original_surface_data, m_original_surface);
    reset_viewports();
    reset_scale_fields();
    run_processing();
    return true;
}

ImageImportDialog::type_signal_result &ImageImportDialog::signal_apply()
{
    return m_signal_apply;
}

ImageImportDialog::type_signal_result &ImageImportDialog::signal_trace()
{
    return m_signal_trace;
}

void ImageImportDialog::run_processing()
{
    if (!m_picture_data)
        return;

    auto img = to_image_buffer(m_picture_data);
    if (img.w == 0 || img.h == 0 || img.px.empty())
        return;

    if (m_crop_switch && m_crop_switch->get_active()) {
        const auto crop = get_crop_rect_from_view();
        if (crop.w > 0 && crop.h > 0)
            img = crop_image(img, crop.x, crop.y, crop.w, crop.h);
    }

    if (m_upscale_switch && m_upscale_switch->get_active()) {
        img = resize_bilinear(img, std::max(1u, img.w * static_cast<unsigned int>(m_upscale_factor)),
                              std::max(1u, img.h * static_cast<unsigned int>(m_upscale_factor)));
    }

    apply_filter_mode(img.px, img.w, img.h, get_filter_mode());
    apply_contrast_brightness(img.px, m_contrast_scale->get_value(), m_brightness_scale->get_value());
    apply_grayscale_mix(img.px, m_grayscale_scale->get_value());
    apply_sharp_blur(img.px, img.w, img.h, m_sharp_blur_scale->get_value());

    if (m_outline_switch && m_outline_switch->get_active()) {
        apply_outline_mode(img, m_outline_offset_px,
                           m_outline_bg_remove_switch && m_outline_bg_remove_switch->get_active());
    }

    if (m_invert_switch && m_invert_switch->get_active())
        invert_colors(img.px);

    m_processed_picture = std::make_shared<PictureData>(UUID::random(), img.w, img.h, std::move(img.px));
    copy_surface_data_from_picture(*m_processed_picture, m_result_surface_data, m_result_surface);
    if (m_result_aspect_frame && m_processed_picture->m_width > 0 && m_processed_picture->m_height > 0) {
        const auto ratio =
                static_cast<float>(m_processed_picture->m_width) / static_cast<float>(m_processed_picture->m_height);
        m_result_aspect_frame->set_ratio(ratio);
    }

    m_apply_button->set_sensitive(m_processed_picture != nullptr);
    m_trace_button->set_sensitive(m_processed_picture != nullptr);
    m_status_label->set_text("Result: " + std::to_string(m_processed_picture->m_width) + " x "
                             + std::to_string(m_processed_picture->m_height) + " px");
    queue_previews_draw();
}

void ImageImportDialog::queue_processing()
{
    if (m_processing_debounce_conn.connected())
        m_processing_debounce_conn.disconnect();

    constexpr unsigned int kProcessingDebounceMs = 60;
    m_processing_debounce_conn = Glib::signal_timeout().connect(
            [this] {
                run_processing();
                return false;
            },
            kProcessingDebounceMs);
}

void ImageImportDialog::queue_previews_draw()
{
    if (m_original_preview)
        m_original_preview->queue_draw();
    if (m_result_preview)
        m_result_preview->queue_draw();
}

void ImageImportDialog::update_value_labels()
{
    if (m_contrast_value_label)
        m_contrast_value_label->set_text(format_signed(m_contrast_scale->get_value()));
    if (m_brightness_value_label)
        m_brightness_value_label->set_text(format_signed(m_brightness_scale->get_value()));
    if (m_grayscale_value_label)
        m_grayscale_value_label->set_text(format_signed(m_grayscale_scale->get_value()));
    if (m_sharp_blur_value_label)
        m_sharp_blur_value_label->set_text(format_signed(m_sharp_blur_scale->get_value()));
    if (m_outline_offset_value_label)
        m_outline_offset_value_label->set_text(std::to_string(static_cast<int>(std::lround(m_outline_offset_px))) + " px");
    if (m_upscale_factor_value_label)
        m_upscale_factor_value_label->set_text("x" + std::to_string(m_upscale_factor));
}

void ImageImportDialog::update_controls_state()
{
    const bool outline = m_outline_switch && m_outline_switch->get_active();
    if (m_outline_offset_dec_button)
        m_outline_offset_dec_button->set_sensitive(outline);
    if (m_outline_offset_inc_button)
        m_outline_offset_inc_button->set_sensitive(outline);
    if (m_outline_offset_value_label)
        m_outline_offset_value_label->set_sensitive(outline);
    if (m_outline_bg_remove_switch)
        m_outline_bg_remove_switch->set_sensitive(outline);

    const bool upscale = m_upscale_switch && m_upscale_switch->get_active();
    if (m_upscale_factor_dec_button)
        m_upscale_factor_dec_button->set_sensitive(upscale);
    if (m_upscale_factor_inc_button)
        m_upscale_factor_inc_button->set_sensitive(upscale);
    if (m_upscale_factor_value_label)
        m_upscale_factor_value_label->set_sensitive(upscale);

    const bool scale = m_scale_switch && m_scale_switch->get_active();
    if (m_scale_width_spin)
        m_scale_width_spin->set_sensitive(scale);
    if (m_scale_height_spin)
        m_scale_height_spin->set_sensitive(scale);
}

void ImageImportDialog::reset_viewports()
{
    m_original_view = PreviewState{};
    m_result_view = PreviewState{};
    queue_previews_draw();
}

void ImageImportDialog::reset_scale_fields()
{
    if (!m_picture_data || !m_scale_width_spin || !m_scale_height_spin)
        return;
    const auto w = std::max(1u, m_picture_data->m_width);
    const auto h = std::max(1u, m_picture_data->m_height);
    m_scale_aspect_ratio = static_cast<double>(w) / static_cast<double>(h);
    const double max_size = 10.0;
    double tw = max_size;
    double th = max_size;
    if (m_scale_aspect_ratio >= 1.0) {
        th = max_size / m_scale_aspect_ratio;
    }
    else {
        tw = max_size * m_scale_aspect_ratio;
    }
    m_updating_scale_controls = true;
    m_scale_width_spin->set_value(std::max(0.1, tw));
    m_scale_height_spin->set_value(std::max(0.1, th));
    m_updating_scale_controls = false;
}

void ImageImportDialog::set_scale_width(double width)
{
    if (!m_scale_height_spin || m_scale_aspect_ratio <= 1e-9)
        return;
    m_updating_scale_controls = true;
    m_scale_height_spin->set_value(std::max(0.1, width / m_scale_aspect_ratio));
    m_updating_scale_controls = false;
}

void ImageImportDialog::set_scale_height(double height)
{
    if (!m_scale_width_spin || m_scale_aspect_ratio <= 1e-9)
        return;
    m_updating_scale_controls = true;
    m_scale_width_spin->set_value(std::max(0.1, height * m_scale_aspect_ratio));
    m_updating_scale_controls = false;
}

std::pair<unsigned int, unsigned int> ImageImportDialog::get_preview_source_size(bool processed) const
{
    if (processed && m_processed_picture)
        return {m_processed_picture->m_width, m_processed_picture->m_height};
    if (m_picture_data)
        return {m_picture_data->m_width, m_picture_data->m_height};
    return {0, 0};
}

ImageImportDialog::PreviewTransform ImageImportDialog::get_preview_transform(const PreviewState &state, int area_w, int area_h,
                                                                             unsigned int source_w,
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

void ImageImportDialog::setup_preview_interaction(Gtk::DrawingArea &area, PreviewState &state, bool processed)
{
    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([&state](double x, double y) {
        state.cursor_x = x;
        state.cursor_y = y;
    });
    area.add_controller(motion);

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll->signal_scroll().connect([this, &area, &state, processed](double, double dy) {
        const auto [source_w, source_h] = get_preview_source_size(processed);
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
        if (!processed && m_crop_switch && m_crop_switch->get_active())
            queue_processing();
        return true;
    }, false);
    area.add_controller(scroll);

    auto drag = Gtk::GestureDrag::create();
    drag->set_button(1);
    drag->signal_drag_begin().connect([&state](double, double) {
        state.drag_pan_x = state.pan_x;
        state.drag_pan_y = state.pan_y;
    });
    drag->signal_drag_update().connect([this, &area, &state, processed](double offset_x, double offset_y) {
        state.pan_x = state.drag_pan_x + offset_x;
        state.pan_y = state.drag_pan_y + offset_y;
        area.queue_draw();
        if (!processed && m_crop_switch && m_crop_switch->get_active())
            queue_processing();
    });
    area.add_controller(drag);

    auto click = Gtk::GestureClick::create();
    click->set_button(1);
    click->signal_pressed().connect([this, &area, &state, processed](int n_press, double, double) {
        if (n_press != 2)
            return;
        state.zoom = 1.0;
        state.pan_x = 0.0;
        state.pan_y = 0.0;
        area.queue_draw();
        if (!processed && m_crop_switch && m_crop_switch->get_active())
            queue_processing();
    });
    area.add_controller(click);
}

void ImageImportDialog::draw_original_preview(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h)
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

void ImageImportDialog::draw_result_preview(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h)
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
    if (source_w > 0 && source_h > 0 && m_result_surface) {
        const auto tf = get_preview_transform(m_result_view, w, h, source_w, source_h);
        cr->translate(tf.ox, tf.oy);
        cr->scale(tf.scale, tf.scale);
        cr->set_source(m_result_surface, 0, 0);
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

ImageImportDialog::CropRect ImageImportDialog::get_crop_rect_from_view() const
{
    CropRect rect;
    if (!m_picture_data || !m_original_preview) {
        return rect;
    }
    rect.w = static_cast<int>(m_picture_data->m_width);
    rect.h = static_cast<int>(m_picture_data->m_height);

    const int area_w = m_original_preview->get_width();
    const int area_h = m_original_preview->get_height();
    if (area_w <= 0 || area_h <= 0)
        return rect;

    const auto tf = get_preview_transform(m_original_view, area_w, area_h, m_picture_data->m_width, m_picture_data->m_height);
    if (tf.scale <= 1e-9)
        return rect;

    const int x0 = static_cast<int>(std::floor((0.0 - tf.ox) / tf.scale));
    const int y0 = static_cast<int>(std::floor((0.0 - tf.oy) / tf.scale));
    const int x1 = static_cast<int>(std::ceil((static_cast<double>(area_w) - tf.ox) / tf.scale));
    const int y1 = static_cast<int>(std::ceil((static_cast<double>(area_h) - tf.oy) / tf.scale));

    const int cx0 = std::clamp(x0, 0, static_cast<int>(m_picture_data->m_width));
    const int cy0 = std::clamp(y0, 0, static_cast<int>(m_picture_data->m_height));
    const int cx1 = std::clamp(x1, 0, static_cast<int>(m_picture_data->m_width));
    const int cy1 = std::clamp(y1, 0, static_cast<int>(m_picture_data->m_height));
    if (cx1 <= cx0 || cy1 <= cy0)
        return rect;

    rect.x = cx0;
    rect.y = cy0;
    rect.w = cx1 - cx0;
    rect.h = cy1 - cy0;
    return rect;
}

ImageImportDialog::FilterMode ImageImportDialog::get_filter_mode() const
{
    if (!m_filter_dropdown)
        return FilterMode::COLOR;
    switch (m_filter_dropdown->get_selected()) {
    case 1:
        return FilterMode::GRAYSCALE;
    case 2:
        return FilterMode::BLACK_WHITE;
    case 3:
        return FilterMode::JITTER;
    case 4:
        return FilterMode::STUCCO;
    default:
        return FilterMode::COLOR;
    }
}
} // namespace dune3d

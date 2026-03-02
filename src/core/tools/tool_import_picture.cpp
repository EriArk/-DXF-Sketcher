#include "tool_import_picture.hpp"
#include "document/document.hpp"
#include "document/entity/entity_picture.hpp"
#include "document/entity/entity_workplane.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/dialogs.hpp"
#include <gtkmm.h>
#include "util/fs_util.hpp"
#include "util/picture_util.hpp"
#include "util/picture_data.hpp"
#include "tool_common_impl.hpp"
#include "core/tool_data_path.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include "util/action_label.hpp"
#include "core/tool_id.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_bezier2d.hpp"
#include "canvas/selection_mode.hpp"
#include "logger/logger.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <regex>
#include <unordered_map>
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

namespace dune3d {

namespace {
class ToolDataTexture : public ToolData {
public:
    ToolDataTexture(Glib::RefPtr<Gdk::Texture> tex) : texture(tex)
    {
    }
    Glib::RefPtr<Gdk::Texture> texture;
};

bool has_svg_extension(const std::filesystem::path &path)
{
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == ".svg";
}

bool nearly_equal(const glm::dvec2 &a, const glm::dvec2 &b, double eps = 1e-8)
{
    return glm::length(a - b) <= eps;
}

glm::dvec2 map_svg_point(double x, double y, double svg_height)
{
    return {x, svg_height - y};
}

std::string read_file_to_string(const std::filesystem::path &path)
{
    std::ifstream ifs(path_to_string(path), std::ios::in | std::ios::binary);
    if (!ifs.good())
        return {};
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

std::string get_attr_value(const std::string &attrs, const std::string &name)
{
    const std::regex rx(name + "\\s*=\\s*\"([^\"]*)\"", std::regex::icase);
    std::smatch m;
    if (std::regex_search(attrs, m, rx))
        return m[1].str();
    return {};
}

std::string expand_svg_use_tags(const std::string &svg)
{
    if (svg.find("<use") == std::string::npos)
        return svg;

    std::unordered_map<std::string, std::string> groups;
    const std::regex group_rx(R"(<g\b([^>]*)>([\s\S]*?)</g>)", std::regex::icase);
    for (std::sregex_iterator it(svg.begin(), svg.end(), group_rx), end; it != end; ++it) {
        const auto attrs = (*it)[1].str();
        const auto content = (*it)[2].str();
        const auto id = get_attr_value(attrs, "id");
        if (!id.empty())
            groups[id] = content;
    }

    if (groups.empty())
        return svg;

    std::string out;
    out.reserve(svg.size() + 2048);
    const std::regex use_rx(R"(<use\b([^>]*)/?>)", std::regex::icase);
    std::size_t pos = 0;
    std::smatch m;
    auto begin = svg.cbegin();
    auto end = svg.cend();
    while (std::regex_search(begin + static_cast<std::ptrdiff_t>(pos), end, m, use_rx)) {
        const auto hit_pos = pos + static_cast<std::size_t>(m.position(0));
        const auto hit_len = static_cast<std::size_t>(m.length(0));
        out.append(svg, pos, hit_pos - pos);
        const auto attrs = m[1].str();
        auto href = get_attr_value(attrs, "xlink:href");
        if (href.empty())
            href = get_attr_value(attrs, "href");
        std::string repl = m[0].str();
        if (!href.empty() && href.front() == '#') {
            const auto id = href.substr(1);
            if (groups.contains(id)) {
                const auto transform = get_attr_value(attrs, "transform");
                repl = "<g";
                if (!transform.empty())
                    repl += " transform=\"" + transform + "\"";
                repl += ">";
                repl += groups.at(id);
                repl += "</g>";
            }
        }
        out += repl;
        pos = hit_pos + hit_len;
    }
    out.append(svg, pos, std::string::npos);
    return out;
}

int count_nsvg_segments(const NSVGimage *image)
{
    int segs = 0;
    for (auto *shape = image ? image->shapes : nullptr; shape; shape = shape->next) {
        for (auto *path = shape->paths; path; path = path->next) {
            for (int i = 0; i < path->npts - 1; i += 3)
                segs++;
        }
    }
    return segs;
}

NSVGimage *parse_svg_with_fallback(const std::filesystem::path &path)
{
    auto *image = nsvgParseFromFile(path_to_string(path).c_str(), "mm", 96.0f);
    if (image && count_nsvg_segments(image) > 0)
        return image;

    if (image)
        nsvgDelete(image);

    const auto raw = read_file_to_string(path);
    if (raw.empty())
        return nullptr;
    const auto expanded = expand_svg_use_tags(raw);
    std::vector<char> buf(expanded.begin(), expanded.end());
    buf.push_back('\0');
    return nsvgParse(buf.data(), "mm", 96.0f);
}
} // namespace

ToolResponse ToolImportPicture::begin(const ToolArgs &args)
{
    m_wrkpl = get_workplane();
    update_tip();

    if (m_tool_id == ToolID::IMPORT_PICTURE) {
        auto dialog = Gtk::FileDialog::create();
        {
            auto dir = m_core.get_current_document_directory();
            if (!dir.empty())
                dialog->set_initial_folder(Gio::File::create_for_path(path_to_string(dir)));
        }

        // Add filters, so that only certain file types can be selected:
        auto filters = Gio::ListStore<Gtk::FileFilter>::create();

        auto filter_any = Gtk::FileFilter::create();
        filter_any->add_pixbuf_formats();
        filter_any->set_name("Pictures");
        filters->append(filter_any);

        dialog->set_filters(filters);

        // Show the dialog and wait for a user response:
        dialog->open(m_intf.get_dialogs().get_parent(), [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
            try {
                auto file = dialog->open_finish(result);
                // Notice that this is a std::string, not a Glib::ustring.
                auto filename = file->get_path();
                m_intf.tool_update_data(std::make_unique<ToolDataPath>(path_from_string(filename)));
            }
            catch (const Gtk::DialogError &err) {
                // Can be thrown by dialog->open_finish(result).
                m_intf.tool_update_data(std::make_unique<ToolDataPath>());
            }
            catch (const Glib::Error &err) {
                m_intf.tool_update_data(std::make_unique<ToolDataPath>());
            }
        });
    }
    else {
        auto clipboard = Gdk::Display::get_default()->get_clipboard();
        m_intf.tool_bar_set_tool_tip("waiting for paste data");
        clipboard->read_texture_async([this, clipboard](const Glib::RefPtr<Gio::AsyncResult> &result) {
            try {
                m_intf.tool_update_data(std::make_unique<ToolDataTexture>(clipboard->read_texture_finish(result)));
            }
            catch (const Glib::Error &err) {
                m_intf.tool_update_data(std::make_unique<ToolDataTexture>(nullptr));
            }
        });
    }


    return ToolResponse();
}

ToolBase::CanBegin ToolImportPicture::can_begin()
{
    return get_workplane_uuid() != UUID{};
}

void ToolImportPicture::add_picture(std::shared_ptr<const PictureData> pic_data)
{
    m_pic = &add_entity<EntityPicture>();
    m_pic->m_wrkpl = get_workplane_uuid();
    m_pic->m_origin = m_wrkpl->project(get_cursor_pos_for_workplane(*m_wrkpl));
    m_pic->m_data = pic_data;
    m_pic->m_data_uuid = pic_data->m_uuid;
    m_pic->m_width = pic_data->m_width;
    m_pic->m_height = pic_data->m_height;
    {
        const auto m = std::max(m_pic->m_width, m_pic->m_height);
        const double scale = 10. / m;
        m_pic->m_scale_x = scale;
        m_pic->m_scale_y = scale;
    }
    m_pic->m_lock_aspect_ratio = true;
    m_pic->m_selection_invisible = true;
    m_pic->update_builtin_anchors();
    m_intf.enable_hover_selection();
}

ToolResponse ToolImportPicture::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataPath *>(args.data.get())) {
            if (data->path != std::filesystem::path{}) {
                if (has_svg_extension(data->path)) {
                    auto image = parse_svg_with_fallback(data->path);
                    if (!image) {
                        m_intf.tool_bar_flash("Couldn't parse SVG");
                        return ToolResponse::end();
                    }
                    const auto svg_height = static_cast<double>(image->height);
                    std::size_t imported_count = 0;
                    std::vector<UUID> imported_entities;
                    imported_entities.reserve(1024);
                    glm::dvec2 bbox_min{std::numeric_limits<double>::infinity(),
                                        std::numeric_limits<double>::infinity()};
                    glm::dvec2 bbox_max{-std::numeric_limits<double>::infinity(),
                                        -std::numeric_limits<double>::infinity()};
                    auto expand_bbox = [&](const glm::dvec2 &pt) {
                        bbox_min.x = std::min(bbox_min.x, pt.x);
                        bbox_min.y = std::min(bbox_min.y, pt.y);
                        bbox_max.x = std::max(bbox_max.x, pt.x);
                        bbox_max.y = std::max(bbox_max.y, pt.y);
                    };
                    m_selection.clear();
                    for (auto *shape = image->shapes; shape; shape = shape->next) {
                        for (auto *path = shape->paths; path; path = path->next) {
                            if (path->npts < 4)
                                continue;
                            for (int i = 0; i < path->npts - 1; i += 3) {
                                const auto *p = &path->pts[i * 2];
                                const auto p1 = map_svg_point(p[0], p[1], svg_height);
                                const auto c1 = map_svg_point(p[2], p[3], svg_height);
                                const auto c2 = map_svg_point(p[4], p[5], svg_height);
                                const auto p2 = map_svg_point(p[6], p[7], svg_height);

                                if (nearly_equal(p1, p2))
                                    continue;

                                if (nearly_equal(p1, c1) && nearly_equal(p2, c2)) {
                                    auto &line = add_entity<EntityLine2D>();
                                    line.m_wrkpl = get_workplane_uuid();
                                    line.m_p1 = p1;
                                    line.m_p2 = p2;
                                    m_selection.emplace(SelectableRef::Type::ENTITY, line.m_uuid, 0);
                                    imported_entities.push_back(line.m_uuid);
                                    expand_bbox(p1);
                                    expand_bbox(p2);
                                }
                                else {
                                    auto &bez = add_entity<EntityBezier2D>();
                                    bez.m_wrkpl = get_workplane_uuid();
                                    bez.m_p1 = p1;
                                    bez.m_c1 = c1;
                                    bez.m_c2 = c2;
                                    bez.m_p2 = p2;
                                    m_selection.emplace(SelectableRef::Type::ENTITY, bez.m_uuid, 0);
                                    imported_entities.push_back(bez.m_uuid);
                                    expand_bbox(p1);
                                    expand_bbox(c1);
                                    expand_bbox(c2);
                                    expand_bbox(p2);
                                }
                                imported_count++;
                            }
                        }
                    }
                    nsvgDelete(image);

                    if (imported_count == 0) {
                        m_intf.tool_bar_flash("SVG has no importable paths");
                        return ToolResponse::end();
                    }

                    if (std::isfinite(bbox_min.x) && std::isfinite(bbox_min.y) && std::isfinite(bbox_max.x)
                        && std::isfinite(bbox_max.y)) {
                        const auto bbox_center = (bbox_min + bbox_max) * 0.5;
                        const auto target_center = m_wrkpl->project(get_cursor_pos_for_workplane(*m_wrkpl));
                        const auto delta = target_center - bbox_center;
                        for (const auto &uu : imported_entities) {
                            if (auto *line = dynamic_cast<EntityLine2D *>(get_doc().get_entity_ptr(uu))) {
                                line->m_p1 += delta;
                                line->m_p2 += delta;
                            }
                            else if (auto *bez = dynamic_cast<EntityBezier2D *>(get_doc().get_entity_ptr(uu))) {
                                bez->m_p1 += delta;
                                bez->m_c1 += delta;
                                bez->m_c2 += delta;
                                bez->m_p2 += delta;
                            }
                        }
                    }

                    set_current_group_generate_pending();
                    m_intf.set_canvas_selection_mode(SelectionMode::NORMAL);
                    return ToolResponse::commit();
                }

                auto pic_data = picture_data_from_file(data->path);
                add_picture(pic_data);
            }
            else {
                return ToolResponse::end();
            }
        }
        else if (auto data = dynamic_cast<const ToolDataTexture *>(args.data.get())) {
            if (data->texture) {
                auto pic_data = picture_data_from_texture(data->texture);
                add_picture(pic_data);
            }
            else {
                m_intf.tool_bar_flash("no picture in clipboard");
                return ToolResponse::end();
            }
        }
    }
    else if (args.type == ToolEventType::MOVE && m_pic) {
        m_pic->m_origin = m_wrkpl->project(get_cursor_pos_for_workplane(*m_wrkpl));
        set_first_update_group_current();
    }
    else if (args.type == ToolEventType::ACTION && m_pic) {
        switch (args.action) {
        case InToolActionID::LMB:
            m_pic->m_selection_invisible = false;
            if (m_constrain) {
                const EntityAndPoint origin{m_pic->m_uuid, 1};
                constrain_point(get_workplane_uuid(), origin);
            }
            return ToolResponse::commit();

        case InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT:
            m_constrain = !m_constrain;
            break;

        case InToolActionID::RMB:
        case InToolActionID::CANCEL:
            return ToolResponse::revert();

        default:;
        }
    }
    update_tip();
    return ToolResponse();
}

void ToolImportPicture::update_tip()
{
    std::vector<ActionLabelInfo> actions;

    actions.emplace_back(InToolActionID::LMB, "place");
    actions.emplace_back(InToolActionID::RMB, "cancel");

    if (m_constrain)
        actions.emplace_back(InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT, "constraint off");
    else
        actions.emplace_back(InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT, "constraint on");


    std::vector<ConstraintType> constraint_icons;
    glm::vec3 v = {NAN, NAN, NAN};

    m_intf.tool_bar_set_tool_tip("");
    if (m_constrain) {
        set_constrain_tip("origin");
        update_constraint_icons(constraint_icons);
    }

    m_intf.set_constraint_icons(get_cursor_pos_for_workplane(*m_wrkpl), v, constraint_icons);

    m_intf.tool_bar_set_actions(actions);
}

} // namespace dune3d

#include "group_sketch.hpp"
#include "nlohmann/json.hpp"
#include "document/solid_model/solid_model.hpp"
#include "igroup_solid_model_json.hpp"
#include "util/json_util.hpp"
#include <algorithm>
#include <set>
#include <sstream>

namespace dune3d {

namespace {
GroupSketch::SketchLayerProcess layer_process_from_json(const json &j, GroupSketch::SketchLayerProcess def)
{
    try {
        return j.get<GroupSketch::SketchLayerProcess>();
    }
    catch (...) {
        return def;
    }
}

int clamp_layer_color_aci(int aci)
{
    if (aci < 1 || aci > 255)
        return 7;
    return aci;
}
} // namespace

NLOHMANN_JSON_SERIALIZE_ENUM(GroupSketch::SketchLayerProcess, {
                                                                {GroupSketch::SketchLayerProcess::LINE_ENGRAVING, "line_engraving"},
                                                                {GroupSketch::SketchLayerProcess::FILL_ENGRAVING, "fill_engraving"},
                                                                {GroupSketch::SketchLayerProcess::LINE_CUTTING, "line_cutting"},
                                                                {GroupSketch::SketchLayerProcess::IMAGE_ENGRAVING, "image_engraving"},
                                                        })

GroupSketch::GroupSketch(const UUID &uu) : Group(uu)
{
    ensure_default_layers();
}

GroupSketch::GroupSketch(const UUID &uu, const json &j)
    : Group(uu, j), m_operation(j.value("operation", Operation::UNION))
{
    if (j.contains("layers") && j.at("layers").is_array()) {
        for (const auto &jl : j.at("layers")) {
            if (!jl.is_object())
                continue;
            SketchLayer layer;
            if (jl.contains("uuid"))
                jl.at("uuid").get_to(layer.m_uuid);
            layer.m_name = jl.value("name", "");
            if (jl.contains("process"))
                layer.m_process = layer_process_from_json(jl.at("process"), SketchLayerProcess::LINE_ENGRAVING);
            layer.m_show_process_icon = jl.value("show_process_icon", true);
            layer.m_color = clamp_layer_color_aci(jl.value("color", 7));
            m_layers.push_back(layer);
        }
    }
    ensure_default_layers();
}

json GroupSketch::serialize() const
{
    auto j = Group::serialize();
    j["operation"] = m_operation;
    auto j_layers = json::array();
    for (const auto &layer : m_layers) {
        j_layers.push_back(json{
                {"uuid", layer.m_uuid},
                {"name", layer.m_name},
                {"process", layer.m_process},
                {"show_process_icon", layer.m_show_process_icon},
                {"color", clamp_layer_color_aci(layer.m_color)},
        });
    }
    j["layers"] = j_layers;
    return j;
}

std::unique_ptr<Group> GroupSketch::clone() const
{
    return std::make_unique<GroupSketch>(*this);
}

const SolidModel *GroupSketch::get_solid_model() const
{
    return m_solid_model.get();
}

void GroupSketch::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

UUID GroupSketch::add_layer()
{
    SketchLayer layer;
    layer.m_uuid = UUID::random();
    layer.m_name = find_next_layer_name();
    layer.m_process = SketchLayerProcess::LINE_ENGRAVING;
    layer.m_show_process_icon = true;
    layer.m_color = 7;
    m_layers.push_back(layer);
    return layer.m_uuid;
}

bool GroupSketch::remove_layer(const UUID &layer_uu, UUID &fallback_layer_uu)
{
    ensure_default_layers();
    if (m_layers.size() <= 1)
        return false;
    const auto it = std::find_if(m_layers.begin(), m_layers.end(),
                                 [&](const auto &layer) { return layer.m_uuid == layer_uu; });
    if (it == m_layers.end())
        return false;
    const auto idx = static_cast<size_t>(std::distance(m_layers.begin(), it));
    size_t fallback_idx = 0;
    if (idx > 0)
        fallback_idx = idx - 1;
    else if (idx + 1 < m_layers.size())
        fallback_idx = idx + 1;
    fallback_layer_uu = m_layers.at(fallback_idx).m_uuid;
    m_layers.erase(it);
    ensure_default_layers();
    return true;
}

bool GroupSketch::move_layer(const UUID &layer_uu, int delta)
{
    if (delta == 0)
        return false;
    const auto it = std::find_if(m_layers.begin(), m_layers.end(),
                                 [&](const auto &layer) { return layer.m_uuid == layer_uu; });
    if (it == m_layers.end())
        return false;
    const auto idx = static_cast<int>(std::distance(m_layers.begin(), it));
    const auto new_idx = idx + delta;
    if (new_idx < 0 || new_idx >= static_cast<int>(m_layers.size()))
        return false;
    std::swap(m_layers.at(static_cast<size_t>(idx)), m_layers.at(static_cast<size_t>(new_idx)));
    return true;
}

GroupSketch::SketchLayer *GroupSketch::get_layer_ptr(const UUID &layer_uu)
{
    const auto it = std::find_if(m_layers.begin(), m_layers.end(),
                                 [&](const auto &layer) { return layer.m_uuid == layer_uu; });
    if (it == m_layers.end())
        return nullptr;
    return &*it;
}

const GroupSketch::SketchLayer *GroupSketch::get_layer_ptr(const UUID &layer_uu) const
{
    const auto it = std::find_if(m_layers.begin(), m_layers.end(),
                                 [&](const auto &layer) { return layer.m_uuid == layer_uu; });
    if (it == m_layers.end())
        return nullptr;
    return &*it;
}

UUID GroupSketch::get_default_layer_uuid() const
{
    if (m_layers.empty())
        return {};
    return m_layers.front().m_uuid;
}

void GroupSketch::ensure_default_layers()
{
    if (m_layers.empty()) {
        m_layers.push_back(SketchLayer{UUID::random(), "Line", SketchLayerProcess::LINE_ENGRAVING, true, 7});
        m_layers.push_back(SketchLayer{UUID::random(), "Fill", SketchLayerProcess::FILL_ENGRAVING, true, 3});
        m_layers.push_back(SketchLayer{UUID::random(), "Cut", SketchLayerProcess::LINE_CUTTING, true, 1});
    }
    std::set<UUID> seen;
    size_t layer_number = 1;
    for (auto &layer : m_layers) {
        if (!layer.m_uuid || seen.contains(layer.m_uuid))
            layer.m_uuid = UUID::random();
        seen.insert(layer.m_uuid);
        if (layer.m_name.empty()) {
            std::ostringstream ss;
            ss << "Layer " << layer_number;
            layer.m_name = ss.str();
        }
        layer.m_color = clamp_layer_color_aci(layer.m_color);
        layer_number++;
    }
}

std::string GroupSketch::find_next_layer_name() const
{
    std::set<int> used_numbers;
    for (const auto &layer : m_layers) {
        constexpr const char *prefix = "Layer ";
        if (layer.m_name.rfind(prefix, 0) != 0)
            continue;
        try {
            const auto value = std::stoi(layer.m_name.substr(std::char_traits<char>::length(prefix)));
            if (value > 0)
                used_numbers.insert(value);
        }
        catch (...) {
        }
    }
    int candidate = 1;
    while (used_numbers.contains(candidate))
        candidate++;
    std::ostringstream ss;
    ss << "Layer " << candidate;
    return ss.str();
}

} // namespace dune3d

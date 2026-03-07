#pragma once
#include "group.hpp"
#include "igroup_solid_model.hpp"
#include <vector>

namespace dune3d {
class GroupSketch : public Group, public IGroupSolidModel {
public:
    enum class SketchLayerProcess { LINE_ENGRAVING, FILL_ENGRAVING, LINE_CUTTING, IMAGE_ENGRAVING };

    struct SketchLayer {
        UUID m_uuid;
        std::string m_name;
        SketchLayerProcess m_process = SketchLayerProcess::LINE_ENGRAVING;
        bool m_show_process_icon = true;
        int m_color = 7;
    };

    explicit GroupSketch(const UUID &uu);
    explicit GroupSketch(const UUID &uu, const json &j);
    static constexpr Type s_type = Type::SKETCH;
    Type get_type() const override
    {
        return s_type;
    }
    json serialize() const override;
    std::unique_ptr<Group> clone() const override;


    std::shared_ptr<const SolidModel> m_solid_model;

    Operation m_operation = Operation::UNION;
    Operation get_operation() const override
    {
        return m_operation;
    }
    void set_operation(Operation op) override
    {
        m_operation = op;
    }

    const SolidModel *get_solid_model() const override;
    void update_solid_model(const Document &doc) override;

    UUID add_layer();
    bool remove_layer(const UUID &layer_uu, UUID &fallback_layer_uu);
    bool move_layer(const UUID &layer_uu, int delta);
    SketchLayer *get_layer_ptr(const UUID &layer_uu);
    const SketchLayer *get_layer_ptr(const UUID &layer_uu) const;
    UUID get_default_layer_uuid() const;
    void ensure_default_layers();
    std::string find_next_layer_name() const;

    std::vector<SketchLayer> m_layers;
};

} // namespace dune3d

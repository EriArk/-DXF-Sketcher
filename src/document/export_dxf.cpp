#include "export_dxf.hpp"
#include "document.hpp"
#include "group/group.hpp"
#include "entity/ientity_in_workplane.hpp"
#include "entity/entity_circle2d.hpp"
#include "entity/entity_line2d.hpp"
#include "entity/entity_arc2d.hpp"
#include "entity/entity_bezier2d.hpp"
#include "entity/entity_cluster.hpp"
#include "entity/entity_text.hpp"
#include "util/fs_util.hpp"
#include "dxflib/dl_dxf.h"

namespace dune3d {

namespace {

class DxfWrapper {
public:
    DxfWrapper(const std::filesystem::path &path) : m_writer(std::ofstream{path})
    {
        if (m_writer.openFailed())
            throw std::runtime_error("couldn't open file for writing");

        m_dxf.writeHeader(m_writer);
        m_writer.sectionEnd();

        m_writer.sectionEntities();
    }

    template <typename Td, typename... Args>
    void write(void (DL_Dxf ::*f)(DL_WriterA &, const Td &, const DL_Attributes &), Args &&...args)
    {
        std::invoke(f, m_dxf, m_writer, Td(std::forward<Args>(args)...), DL_Attributes{});
    }
    template <typename Td, typename... Args> void write(void (DL_Dxf ::*f)(DL_WriterA &, const Td &), Args &&...args)
    {
        std::invoke(f, m_dxf, m_writer, Td(std::forward<Args>(args)...));
    }

    ~DxfWrapper()
    {
        m_writer.sectionEnd();
        m_dxf.writeObjects(m_writer);
        m_dxf.writeObjectsEnd(m_writer);
        m_writer.dxfEOF();
        m_writer.close();
    }

private:
    DL_Dxf m_dxf;
    DL_WriterA m_writer;
};

using Transform2D = std::function<glm::dvec2(const glm::dvec2 &)>;

static double point_line_distance_2d(const glm::dvec2 &p, const glm::dvec2 &a, const glm::dvec2 &b)
{
    const auto ab = b - a;
    const auto len = glm::length(ab);
    if (len < 1e-12)
        return glm::length(p - a);
    const auto area = std::abs((p.x - a.x) * ab.y - (p.y - a.y) * ab.x);
    return area / len;
}

static unsigned int estimate_bezier_export_steps(const glm::dvec2 &p1, const glm::dvec2 &c1, const glm::dvec2 &c2,
                                                 const glm::dvec2 &p2)
{
    const auto ctrl_len = glm::length(c1 - p1) + glm::length(c2 - c1) + glm::length(p2 - c2);
    const auto chord = glm::length(p2 - p1);
    const auto curvature = std::max(point_line_distance_2d(c1, p1, p2), point_line_distance_2d(c2, p1, p2));
    const auto complexity = std::max(0.0, ctrl_len - chord) + curvature * 2.0;
    auto steps = static_cast<unsigned int>(std::lround(4.0 + complexity * 1.2));
    steps = std::clamp(steps, 4u, 48u);
    return steps;
}

static bool entity_is_valid_for_export(const Entity &en, const Transform2D &tr)
{
    constexpr double eps = 1e-6;
    if (auto line = dynamic_cast<const EntityLine2D *>(&en))
        return glm::length(tr(line->m_p1) - tr(line->m_p2)) >= eps;
    if (auto circle = dynamic_cast<const EntityCircle2D *>(&en))
        return circle->get_radius() >= eps;
    if (auto arc = dynamic_cast<const EntityArc2D *>(&en)) {
        const auto p1 = tr(arc->m_from);
        const auto p2 = tr(arc->m_to);
        const auto center = tr(arc->m_center);
        return glm::length(p1 - center) >= eps && glm::length(p2 - center) >= eps && glm::length(p1 - p2) >= eps;
    }
    return true;
}

static bool write_entity_to_dxf(DxfWrapper &dxf, const Entity &en, const Transform2D &tr);

static bool write_cluster_content_to_dxf(DxfWrapper &dxf, const ClusterContent &content, const Transform2D &tr)
{
    bool wrote_any = false;
    for (const auto &[uu, en] : content.m_entities) {
        if (en->m_construction)
            continue;
        wrote_any = write_entity_to_dxf(dxf, *en, tr) || wrote_any;
    }
    return wrote_any;
}

static bool write_entity_to_dxf(DxfWrapper &dxf, const Entity &en, const Transform2D &tr)
{
    if (!entity_is_valid_for_export(en, tr))
        return false;

    if (auto line = dynamic_cast<const EntityLine2D *>(&en)) {
        const auto p1 = tr(line->m_p1);
        const auto p2 = tr(line->m_p2);
        dxf.write(&DL_Dxf::writeLine, p1.x, p1.y, 0, p2.x, p2.y, 0);
        return true;
    }
    if (auto arc = dynamic_cast<const EntityArc2D *>(&en)) {
        const auto center = tr(arc->m_center);
        const auto p1 = tr(arc->m_from);
        const auto p2 = tr(arc->m_to);
        const auto v1 = p1 - center;
        const auto v2 = p2 - center;
        const auto a1 = glm::degrees(atan2(v1.y, v1.x));
        const auto a2 = glm::degrees(atan2(v2.y, v2.x));
        dxf.write(&DL_Dxf::writeArc, center.x, center.y, 0, glm::length(v1), a1, a2);
        return true;
    }
    if (auto bez = dynamic_cast<const EntityBezier2D *>(&en)) {
        const auto p1 = tr(bez->m_p1);
        const auto c1 = tr(bez->m_c1);
        const auto c2 = tr(bez->m_c2);
        const auto p2 = tr(bez->m_p2);
        const auto steps = estimate_bezier_export_steps(p1, c1, c2, p2);
        glm::dvec2 last = p1;
        for (unsigned int i = 1; i <= steps; i++) {
            const auto t = static_cast<double>(i) / static_cast<double>(steps);
            const auto p = tr(bez->get_interpolated(t));
            dxf.write(&DL_Dxf::writeLine, last.x, last.y, 0, p.x, p.y, 0);
            last = p;
        }
        return true;
    }
    if (auto circle = dynamic_cast<const EntityCircle2D *>(&en)) {
        const auto center = tr(circle->m_center);
        dxf.write(&DL_Dxf::writeCircle, center.x, center.y, 0, circle->get_radius());
        return true;
    }
    if (auto cluster = dynamic_cast<const EntityCluster *>(&en)) {
        auto tr_cluster = [tr, cluster](const glm::dvec2 &v) { return tr(cluster->transform(v)); };
        return write_cluster_content_to_dxf(dxf, *cluster->m_content, tr_cluster);
    }
    if (auto text = dynamic_cast<const EntityText *>(&en)) {
        auto tr_text = [tr, text](const glm::dvec2 &v) { return tr(text->transform(v)); };
        return write_cluster_content_to_dxf(dxf, *text->m_content, tr_text);
    }
    return false;
}
} // namespace

void export_dxf(const std::filesystem::path &filename, const Document &doc, const UUID &group_uu)
{
    auto &group = doc.get_group(group_uu);
    if (!group.m_active_wrkpl)
        throw std::runtime_error("needs workplane");

    DxfWrapper dxf(filename);
    const auto tr_identity = [](const glm::dvec2 &v) { return v; };
    for (const auto &[uu, en] : doc.m_entities) {
        if (en->m_group != group.m_uuid)
            continue;
        if (en->m_construction)
            continue;
        auto en_wrkpl = dynamic_cast<const IEntityInWorkplane *>(en.get());
        if (!en_wrkpl)
            continue;
        if (en_wrkpl->get_workplane() != group.m_active_wrkpl)
            continue;
        write_entity_to_dxf(dxf, *en, tr_identity);
    }
}
} // namespace dune3d

#include "document/solid_model/solid_model.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/group/igroup_solid_model.hpp"
#include "import_step/import.hpp"
#include "util/step_exporter.hpp"

#include <stdexcept>

namespace dune3d {

struct STEPExporter::Impl {
};

SolidModel::~SolidModel() = default;

std::shared_ptr<const SolidModel> SolidModel::create(const Document &, GroupExtrude &)
{
    return nullptr;
}

std::shared_ptr<const SolidModel> SolidModel::create(const Document &, GroupFillet &)
{
    return nullptr;
}

std::shared_ptr<const SolidModel> SolidModel::create(const Document &, GroupChamfer &)
{
    return nullptr;
}

std::shared_ptr<const SolidModel> SolidModel::create(const Document &, GroupLathe &)
{
    return nullptr;
}

std::shared_ptr<const SolidModel> SolidModel::create(const Document &, GroupRevolve &)
{
    return nullptr;
}

std::shared_ptr<const SolidModel> SolidModel::create(const Document &, GroupLinearArray &)
{
    return nullptr;
}

std::shared_ptr<const SolidModel> SolidModel::create(const Document &, GroupPolarArray &)
{
    return nullptr;
}

std::shared_ptr<const SolidModel> SolidModel::create(const Document &, GroupMirrorHV &)
{
    return nullptr;
}

std::shared_ptr<const SolidModel> SolidModel::create(const Document &, GroupLoft &)
{
    return nullptr;
}

std::shared_ptr<const SolidModel> SolidModel::create(const Document &, GroupSketch &)
{
    return nullptr;
}

std::shared_ptr<const SolidModel> SolidModel::create(const Document &, GroupSolidModelOperation &)
{
    return nullptr;
}

std::shared_ptr<const SolidModel> SolidModel::create(const Document &, GroupPipe &)
{
    return nullptr;
}

void SolidModel::export_projections(const std::filesystem::path &, std::vector<const SolidModel *>, const glm::dvec3 &,
                                    const glm::dquat &)
{
    throw std::runtime_error("Projection export is not available in sketcher-only builds");
}

const IGroupSolidModel *SolidModel::get_last_solid_model_group(const Document &doc, const Group &group,
                                                               IncludeGroup include_group)
{
    const IGroupSolidModel *last_solid_model_group = nullptr;
    auto this_body = &group.find_body(doc).body;

    for (auto gr : doc.get_groups_sorted()) {
        if (include_group == IncludeGroup::NO && gr->m_uuid == group.m_uuid)
            break;

        if (auto gr_solid = dynamic_cast<const IGroupSolidModel *>(gr)) {
            auto body = &gr->find_body(doc).body;
            if (body == this_body && gr_solid->get_solid_model())
                last_solid_model_group = gr_solid;
        }

        if (include_group == IncludeGroup::YES && gr->m_uuid == group.m_uuid)
            break;
    }

    return last_solid_model_group;
}

const SolidModel *SolidModel::get_last_solid_model(const Document &doc, const Group &group, IncludeGroup include_group)
{
    auto gr = get_last_solid_model_group(doc, group, include_group);
    if (gr)
        return gr->get_solid_model();
    return nullptr;
}

STEPExporter::STEPExporter(const char *) : m_impl(nullptr)
{
}

STEPExporter::~STEPExporter() = default;

void STEPExporter::add_model(const char *, const TopoDS_Shape &, const Color &)
{
    throw std::runtime_error("STEP export is not available in sketcher-only builds");
}

void STEPExporter::write(const std::filesystem::path &) const
{
    throw std::runtime_error("STEP export is not available in sketcher-only builds");
}

namespace STEPImporter {

Result::~Result() = default;

Result import(const std::filesystem::path &)
{
    return {};
}

std::shared_ptr<const Shapes> import_shapes(const std::filesystem::path &)
{
    return {};
}

} // namespace STEPImporter

} // namespace dune3d

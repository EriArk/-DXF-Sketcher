#include "file_version.hpp"
#include "nlohmann/json.hpp"
#include "logger/logger.hpp"
#include "util/uuid.hpp"

namespace dune3d {

FileVersion::FileVersion(unsigned int a) : m_app(a), m_file(a)
{
}

FileVersion::FileVersion(unsigned int a, unsigned int f) : m_app(a), m_file(f)
{
}

FileVersion::FileVersion(unsigned int a, const json &j) : m_app(a), m_file(j.value("version", 0))
{
}

void FileVersion::serialize(json &j) const
{
    if (m_app)
        j["version"] = m_app;
}

void FileVersion::update_file_from_app()
{
    m_file = m_app;
}

std::string FileVersion::get_message() const
{
    if (m_app > m_file) {
        return "This document uses an older file-format version. Saving will update it to the current DXF Sketcher "
               "format and may make it incompatible with older DXF Sketcher builds.";
    }
    else if (m_file > m_app) {
        return "This document uses a newer file-format version. Some content may not display correctly, so it has "
               "been opened read-only to avoid accidental data loss.";
    }
    else {
        return "";
    }
}
} // namespace dune3d

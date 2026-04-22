#include "about_dialog.hpp"
#include "util/version.hpp"
#include <gdkmm/texture.h>

namespace dune3d {

AboutDialog::AboutDialog() : Gtk::AboutDialog()
{
    std::string version = Version::get_string() + " " + Version::name;
    if (strlen(Version::commit)) {
        version += "\nCommit " + std::string(Version::commit);
    }
    set_version(version);
    set_program_name("DXF Sketcher");
    std::vector<Glib::ustring> authors;
    authors.push_back("Lazar Stafeev <lazarstafeev@gmail.com>");
    authors.push_back("DXF Sketcher contributors");
    authors.push_back("Built on work from dune3d by Lukas K. and contributors");
    set_authors(authors);
    set_license_type(Gtk::License::GPL_3_0);
    set_copyright("Copyright © 2026 DXF Sketcher contributors");
    set_website("https://github.com/EriArk/-DXF-Sketcher");
    set_website_label("github.com/EriArk/-DXF-Sketcher");
    set_comments("DXF Sketcher is a practical 2D editor focused on quick DXF cleanup, sketching, and fabrication prep.\n\n"
                 "It is tuned for workshop work: open drawings or folders, edit directly on canvas, use built-in helpers "
                 "when needed, and save back to DXF or SVG.\n\n"
                 "Built on top of dune3d with a DXF-first workflow.");

    if (auto logo = Gdk::Texture::create_from_resource("/org/dune3d/dune3d/icons/scalable/apps/logo.png")) {
        set_logo(logo);
    }
}

} // namespace dune3d

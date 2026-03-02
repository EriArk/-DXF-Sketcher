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
    authors.push_back("Based on upstream Dune3D by Lukas K. and contributors");
    set_authors(authors);
    set_license_type(Gtk::License::GPL_3_0);
    set_copyright("Copyright © 2026 DXF Sketcher contributors");
    set_website("https://github.com/EriArk/-DXF-Sketcher");
    set_website_label("github.com/EriArk/-DXF-Sketcher");
    set_comments("DXF Sketcher is a practical 2D editor focused on fast DXF workflows.\n\n"
                 "This fork removes most 3D-oriented complexity and keeps day-to-day sketching tasks simple:\n"
                 "open files/folders, edit sketches, and save directly back to DXF/SVG.\n\n"
                 "Built as a customized fork of Dune3D.");

    if (auto logo = Gdk::Texture::create_from_resource("/org/dune3d/dune3d/icons/scalable/apps/logo.png")) {
        set_logo(logo);
    }
}

} // namespace dune3d

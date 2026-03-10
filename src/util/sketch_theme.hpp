#pragma once

#include <array>
#include <gtkmm/widget.h>

namespace dune3d {

inline void sync_sketch_theme_classes(Gtk::Widget &source, Gtk::Widget &target)
{
    constexpr std::array<const char *, 8> classes = {
            "sketch-light",
            "sketch-heaven",
            "sketch-dark-blue",
            "sketch-mix",
            "sketch-accent-blue",
            "sketch-accent-orange",
            "sketch-accent-teal",
            "sketch-accent-pink",
    };

    for (const auto *css_class : classes) {
        if (source.has_css_class(css_class))
            target.add_css_class(css_class);
        else
            target.remove_css_class(css_class);
    }

    if (source.has_css_class("sketch-accent-lime"))
        target.add_css_class("sketch-accent-lime");
    else
        target.remove_css_class("sketch-accent-lime");
}

} // namespace dune3d

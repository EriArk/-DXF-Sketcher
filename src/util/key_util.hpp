#pragma once
#include <gtkmm.h>

namespace dune3d {
void remap_keys(guint &keyval, guint keycode, Gdk::ModifierType &state);
} // namespace dune3d

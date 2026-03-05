#include "key_util.hpp"
#include <gdk/gdk.h>

namespace dune3d {

namespace {
bool is_ascii_latin_letter(guint keyval)
{
    const auto lower = gdk_keyval_to_lower(keyval);
    return lower >= GDK_KEY_a && lower <= GDK_KEY_z;
}

guint map_letter_keycode_to_group0(guint keycode, guint fallback_keyval)
{
    if (keycode == 0)
        return fallback_keyval;

    auto *display = gdk_display_get_default();
    if (!display)
        return fallback_keyval;

    GdkKeymapKey *keys = nullptr;
    guint *keyvals = nullptr;
    int n_entries = 0;
    if (!gdk_display_map_keycode(display, keycode, &keys, &keyvals, &n_entries) || n_entries <= 0 || !keyvals) {
        return fallback_keyval;
    }

    guint best = fallback_keyval;
    bool have_group0 = false;
    for (int i = 0; i < n_entries; i++) {
        const auto kv = gdk_keyval_to_lower(keyvals[i]);
        if (!is_ascii_latin_letter(kv))
            continue;

        if (keys && keys[i].group == 0 && keys[i].level == 0) {
            best = kv;
            have_group0 = true;
            break;
        }
        if (keys && keys[i].group == 0 && !have_group0) {
            best = kv;
            have_group0 = true;
        }
        else if (!have_group0 && best == fallback_keyval) {
            best = kv;
        }
    }

    g_free(keys);
    g_free(keyvals);
    return best;
}
} // namespace

void remap_keys(guint &keyval, guint keycode, Gdk::ModifierType &state)
{
    // Keep shortcuts layout-independent for letter keys:
    // if input is a non-latin letter, map the physical key to group 0 (latin layout).
    const auto current_lower = gdk_keyval_to_lower(keyval);
    const auto current_unicode = gdk_keyval_to_unicode(current_lower);
    if (current_unicode != 0 && g_unichar_isalpha(current_unicode) && !is_ascii_latin_letter(current_lower)) {
        keyval = map_letter_keycode_to_group0(keycode, keyval);
    }
    keyval = gdk_keyval_to_lower(keyval);

#ifdef __APPLE__
    state &= (Gdk::ModifierType)GDK_MODIFIER_MASK;
    if ((int)(state & Gdk::ModifierType::CONTROL_MASK)) {
        state &= ~Gdk::ModifierType::CONTROL_MASK;
        state |= Gdk::ModifierType::ALT_MASK;
    }
    if ((int)(state & Gdk::ModifierType::META_MASK)) {
        state &= ~Gdk::ModifierType::META_MASK;
        state |= Gdk::ModifierType::CONTROL_MASK;
    }
    if (keyval == GDK_KEY_BackSpace)
        keyval = GDK_KEY_Delete;
    else if (keyval == GDK_KEY_Delete)
        keyval = GDK_KEY_BackSpace;
#endif
}

} // namespace dune3d

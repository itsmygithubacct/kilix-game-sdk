#include "kilix_ui.h"

#include <type_traits>

static_assert(KILIX_UI_VERSION_MAJOR == 0, "unexpected major version");
static_assert(std::is_standard_layout<kilix_ui_focus>::value,
              "focus must remain a C-compatible record");
static_assert(std::is_standard_layout<kilix_ui_style>::value,
              "style must remain a C-compatible record");

int kilix_ui_cpp_header_smoke(kilix_ui_focus *focus)
{
    kilix_ui_focus_init(focus, 1u, 1u);
    return kilix_ui_focus_accepts(focus, KILIX_UI_ACTION_ACCEPT, nullptr)
        ? 0 : 1;
}

/*
 * Single translation unit that expands the vendored raygui implementation.
 * Kept separate from our own sources so the ~4000-line third-party header is
 * compiled once and without our strict -W flags. See vendor/raygui.h and
 * docs/adr/0007-gui-presentation-layer.md.
 */
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

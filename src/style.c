#include "style.h"

#include <gtk/gtk.h>

static const char *ggame_style_css =
    ".board { background-color: #2a160b; border: 2px solid #000; }"
    ".board-light { background-color: #e6d1a8; border-radius: 0; }"
    "button.board-dark {"
    "  background-color: #774433;"
    "  background-image: none;"
    "}"
    ".board-square { padding: 0; border-radius: 0; }"
    ".piece-label {"
    "  font-size: 30px;"
    "  color: #fff;"
    "  background-color: rgba(0, 0, 0, 0.6);"
    "  border-radius: 6px;"
    "  padding: 2px 6px;"
    "}"
    ".square-index {"
    "  font-size: 9px;"
    "  font-weight: 600;"
    "  padding: 1px 3px;"
    "  border-radius: 6px;"
    "  background-color: rgba(0, 0, 0, 0.6);"
    "  color: #fff;"
    "}"
    ".board-halo {"
    "  background-image:"
    "    radial-gradient(circle,"
    "      rgba(247, 215, 77, 0.85) 0,"
    "      rgba(247, 215, 77, 0.4) 50%,"
    "      rgba(247, 215, 77, 0.0) 72%);"
    "}"
    ".board-halo-selected {"
    "  background-image:"
    "    radial-gradient(circle,"
    "      rgba(96, 214, 120, 0.85) 0,"
    "      rgba(96, 214, 120, 0.4) 50%,"
    "      rgba(96, 214, 120, 0.0) 72%);"
    "}"
    "button.board-halo {"
    "  background-image:"
    "    radial-gradient(circle,"
    "      rgba(247, 215, 77, 0.85) 0,"
    "      rgba(247, 215, 77, 0.4) 50%,"
    "      rgba(247, 215, 77, 0.0) 72%);"
    "}"
    "button.board-halo-selected {"
    "  background-image:"
    "    radial-gradient(circle,"
    "      rgba(96, 214, 120, 0.85) 0,"
    "      rgba(96, 214, 120, 0.4) 50%,"
    "      rgba(96, 214, 120, 0.0) 72%);"
    "}"
    ".sgf-panel { background-color: #f5f5f5; border: 1px solid #ccc; }"
    ".sgf-disc {"
    "  background-image: none;"
    "  border: 1px solid #222;"
    "  border-radius: 999px;"
    "  padding: 0;"
    "  min-width: 34px;"
    "  min-height: 34px;"
    "}"
    ".sgf-disc-black { background-color: #222; color: #fff; }"
    ".sgf-disc-white { background-color: #fff; color: #111; border: 1px solid #222; }"
    ".sgf-disc-root {"
    "  background-color: transparent;"
    "  border: none;"
    "  color: #666;"
    "  font-size: 18px;"
    "}"
    ".sgf-disc-selected { box-shadow: 0 0 0 3px #5cc7ff; }"
    ".analysis-graph {"
    "  border: 1px solid #c9c9c9;"
    "  background-color: #fafafa;"
    "}"
    ".analysis-status {"
    "  color: #555;"
    "  margin: 6px 0 2px 0;"
    "}"
    ".puzzle-picker-button {"
    "  min-width: 52px;"
    "  min-height: 52px;"
    "  padding: 4px;"
    "  border-radius: 0;"
    "  background-image: none;"
    "}"
    ".puzzle-picker-button.puzzle-picker-untried {"
    "  background-color: #ffffff;"
    "  color: #111111;"
    "  border: 1px solid #8a8a8a;"
    "}"
    ".puzzle-picker-button.puzzle-picker-untried:active {"
    "  background-color: #e7e7e7;"
    "  border: 1px solid #6f6f6f;"
    "}"
    ".puzzle-picker-button.puzzle-picker-solved {"
    "  background-color: #d5f0cf;"
    "  color: #1a4d14;"
    "  border: 1px solid #4b8b43;"
    "}"
    ".puzzle-picker-button.puzzle-picker-solved:active {"
    "  background-color: #bddcb6;"
    "  border: 1px solid #3f7a38;"
    "}"
    ".puzzle-picker-button.puzzle-picker-failed {"
    "  background-color: #f2d2d2;"
    "  color: #7a1515;"
    "  border: 1px solid #a53a3a;"
    "}"
    ".puzzle-picker-button.puzzle-picker-failed:active {"
    "  background-color: #e0bbbb;"
    "  border: 1px solid #8d2f2f;"
    "}"
    ".puzzle-picker-number {"
    "  font-weight: 700;"
    "}"
    ".puzzle-picker-icon {"
    "  -gtk-icon-size: 14px;"
    "}"
    "scale { padding: 0; }";

void ggame_style_init(void) {
  static gsize initialized = 0;

  if (!g_once_init_enter(&initialized)) {
    return;
  }

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider, ggame_style_css);

  GdkDisplay *display = gdk_display_get_default();
  if (!display) {
    g_debug("Failed to fetch default display for CSS styling\n");
    g_object_unref(provider);
    g_once_init_leave(&initialized, 1);
    return;
  }

  gtk_style_context_add_provider_for_display(display,
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
  g_once_init_leave(&initialized, 1);
}

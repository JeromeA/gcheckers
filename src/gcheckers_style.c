#include "gcheckers_style.h"

#include <gtk/gtk.h>

static const char *gcheckers_style_css =
    ".board { background-color: #2a160b; border: 2px solid #000; }"
    ".board-light { background-color: #e6d1a8; border-radius: 0; }"
    ".board-dark {"
    "  background-color: #3b2412;"
    "  background-image: none;"
    "  border-radius: 0;"
    "}"
    "button.board-dark {"
    "  background-color: #3b2412;"
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
    "  font-size: 6px;"
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
    ".sgf-disc-selected { box-shadow: 0 0 0 3px #5cc7ff; }";

void gcheckers_style_init(void) {
  static gsize initialized = 0;

  if (!g_once_init_enter(&initialized)) {
    return;
  }

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider, gcheckers_style_css);

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

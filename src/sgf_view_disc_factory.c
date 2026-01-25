#include "sgf_view_disc_factory.h"

struct _SgfViewDiscFactory {
  GObject parent_instance;
};

G_DEFINE_TYPE(SgfViewDiscFactory, sgf_view_disc_factory, G_TYPE_OBJECT)

enum { SIGNAL_NODE_CLICKED, SIGNAL_LAST };

static guint sgf_view_disc_factory_signals[SIGNAL_LAST] = {0};

static void sgf_view_disc_factory_on_clicked(GtkButton *button, gpointer user_data) {
  SgfViewDiscFactory *self = SGF_VIEW_DISC_FACTORY(user_data);

  g_return_if_fail(SGF_IS_VIEW_DISC_FACTORY(self));
  g_return_if_fail(GTK_IS_BUTTON(button));

  const SgfNode *node = g_object_get_data(G_OBJECT(button), "sgf-node");
  if (!node) {
    g_debug("Missing SGF node for clicked disc\n");
    return;
  }

  g_signal_emit(self, sgf_view_disc_factory_signals[SIGNAL_NODE_CLICKED], 0, node);
}

static void sgf_view_disc_factory_class_init(SgfViewDiscFactoryClass *klass) {
  sgf_view_disc_factory_signals[SIGNAL_NODE_CLICKED] = g_signal_new("node-clicked",
                                                                    G_TYPE_FROM_CLASS(klass),
                                                                    G_SIGNAL_RUN_LAST,
                                                                    0,
                                                                    NULL,
                                                                    NULL,
                                                                    NULL,
                                                                    G_TYPE_NONE,
                                                                    1,
                                                                    G_TYPE_POINTER);
}

static void sgf_view_disc_factory_init(SgfViewDiscFactory *self) {
  (void)self;
}

SgfViewDiscFactory *sgf_view_disc_factory_new(void) {
  return g_object_new(SGF_TYPE_VIEW_DISC_FACTORY, NULL);
}

GtkWidget *sgf_view_disc_factory_build(SgfViewDiscFactory *self,
                                       const SgfNode *node,
                                       const SgfNode *selected,
                                       GHashTable *node_widgets,
                                       int disc_stride) {
  g_return_val_if_fail(SGF_IS_VIEW_DISC_FACTORY(self), NULL);
  g_return_val_if_fail(node != NULL, NULL);

  char label[16];
  guint move_number = sgf_node_get_move_number(node);
  if (move_number == 0) {
    g_snprintf(label, sizeof(label), "\u2022");
  } else {
    g_snprintf(label, sizeof(label), "%u", move_number);
  }

  GtkWidget *button = gtk_button_new_with_label(label);
  gtk_widget_add_css_class(button, "sgf-disc");
  gtk_widget_set_size_request(button, disc_stride, disc_stride);
  gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);

  SgfColor color = sgf_node_get_color(node);
  if (move_number == 0) {
    gtk_widget_add_css_class(button, "sgf-disc-root");
  } else if (color == SGF_COLOR_BLACK) {
    gtk_widget_add_css_class(button, "sgf-disc-black");
  } else if (color == SGF_COLOR_WHITE) {
    gtk_widget_add_css_class(button, "sgf-disc-white");
  }

  if (selected == node) {
    gtk_widget_add_css_class(button, "sgf-disc-selected");
  }

  g_object_set_data(G_OBJECT(button), "sgf-node", (gpointer)node);
  g_signal_connect(button, "clicked", G_CALLBACK(sgf_view_disc_factory_on_clicked), self);

  if (node_widgets) {
    g_hash_table_insert(node_widgets, (gpointer)node, button);
  }

  return button;
}

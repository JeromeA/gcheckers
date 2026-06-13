#include "sgf_view_disc_factory.h"

struct _SgfViewDiscFactory {
  GObject parent_instance;
};

G_DEFINE_TYPE(SgfViewDiscFactory, sgf_view_disc_factory, G_TYPE_OBJECT)

enum {
  SIGNAL_NODE_CLICKED,
  SIGNAL_NODE_CONTEXT_MENU,
  SIGNAL_LAST
};

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

static void sgf_view_disc_factory_on_context_pressed(GtkGestureClick *gesture,
                                                     int n_press,
                                                     double x,
                                                     double y,
                                                     gpointer user_data) {
  SgfViewDiscFactory *self = SGF_VIEW_DISC_FACTORY(user_data);

  g_return_if_fail(SGF_IS_VIEW_DISC_FACTORY(self));
  g_return_if_fail(GTK_IS_GESTURE_CLICK(gesture));

  if (n_press != 1) {
    return;
  }

  GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  if (!GTK_IS_WIDGET(widget)) {
    g_debug("Missing SGF disc widget for context menu");
    return;
  }

  const SgfNode *node = g_object_get_data(G_OBJECT(widget), "sgf-node");
  if (!node) {
    g_debug("Missing SGF node for context menu");
    return;
  }

  gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
  g_signal_emit(self,
                sgf_view_disc_factory_signals[SIGNAL_NODE_CONTEXT_MENU],
                0,
                node,
                widget,
                x,
                y);
}

static void sgf_view_disc_factory_unref_closure(gpointer data, GClosure * /*closure*/) {
  g_return_if_fail(SGF_IS_VIEW_DISC_FACTORY(data));

  g_object_unref(data);
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
  sgf_view_disc_factory_signals[SIGNAL_NODE_CONTEXT_MENU] = g_signal_new("node-context-menu",
                                                                         G_TYPE_FROM_CLASS(klass),
                                                                         G_SIGNAL_RUN_LAST,
                                                                         0,
                                                                         NULL,
                                                                         NULL,
                                                                         NULL,
                                                                         G_TYPE_NONE,
                                                                         4,
                                                                         G_TYPE_POINTER,
                                                                         GTK_TYPE_WIDGET,
                                                                         G_TYPE_DOUBLE,
                                                                         G_TYPE_DOUBLE);
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
  g_signal_connect_data(button,
                        "clicked",
                        G_CALLBACK(sgf_view_disc_factory_on_clicked),
                        g_object_ref(self),
                        sgf_view_disc_factory_unref_closure,
                        0);

  GtkGesture *context_gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(context_gesture), GDK_BUTTON_SECONDARY);
  g_signal_connect_data(context_gesture,
                        "pressed",
                        G_CALLBACK(sgf_view_disc_factory_on_context_pressed),
                        g_object_ref(self),
                        sgf_view_disc_factory_unref_closure,
                        0);
  gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(context_gesture));

  if (node_widgets) {
    g_hash_table_insert(node_widgets, (gpointer)node, button);
  }

  return button;
}

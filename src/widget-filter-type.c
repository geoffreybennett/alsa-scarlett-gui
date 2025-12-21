// SPDX-FileCopyrightText: 2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "widget-filter-type.h"

#define FILTER_ICON_WIDTH 24
#define FILTER_ICON_HEIGHT 16

struct filter_type_dropdown {
  GtkWidget                 *button;
  GtkWidget                 *btn_icon;
  GtkWidget                 *btn_label;
  GtkWidget                 *popover;
  GtkWidget                 *listview;
  GtkSingleSelection        *selection;
  BiquadFilterType           current_type;
  FilterTypeChangedCallback  changed_callback;
  gpointer                   changed_user_data;
};

// Draw a filter type icon representing the frequency response shape
static void draw_filter_type_icon(
  cairo_t         *cr,
  BiquadFilterType type,
  int              width,
  int              height
) {
  double w = width;
  double h = height;
  double margin = 2;
  double x0 = margin;
  double x1 = w - margin;
  double y_top = margin + 2;
  double y_mid = h / 2;
  double y_bot = h - margin - 2;

  cairo_set_line_width(cr, 1.5);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

  switch (type) {
    case BIQUAD_TYPE_PEAKING:
      // Bell curve
      cairo_move_to(cr, x0, y_mid);
      cairo_curve_to(cr, w * 0.3, y_mid, w * 0.35, y_top, w * 0.5, y_top);
      cairo_curve_to(cr, w * 0.65, y_top, w * 0.7, y_mid, x1, y_mid);
      break;

    case BIQUAD_TYPE_LOW_SHELF:
      // High on left, step down to right
      cairo_move_to(cr, x0, y_top + 2);
      cairo_line_to(cr, w * 0.35, y_top + 2);
      cairo_curve_to(cr, w * 0.5, y_top + 2, w * 0.5, y_bot - 2, w * 0.65, y_bot - 2);
      cairo_line_to(cr, x1, y_bot - 2);
      break;

    case BIQUAD_TYPE_HIGH_SHELF:
      // Low on left, step up to right
      cairo_move_to(cr, x0, y_bot - 2);
      cairo_line_to(cr, w * 0.35, y_bot - 2);
      cairo_curve_to(cr, w * 0.5, y_bot - 2, w * 0.5, y_top + 2, w * 0.65, y_top + 2);
      cairo_line_to(cr, x1, y_top + 2);
      break;

    case BIQUAD_TYPE_LOWPASS:
      // Flat on left, slope down to right
      cairo_move_to(cr, x0, y_top + 2);
      cairo_line_to(cr, w * 0.45, y_top + 2);
      cairo_curve_to(cr, w * 0.6, y_top + 2, w * 0.7, y_mid, x1, y_bot);
      break;

    case BIQUAD_TYPE_HIGHPASS:
      // Slope up from left, flat on right
      cairo_move_to(cr, x0, y_bot);
      cairo_curve_to(cr, w * 0.3, y_mid, w * 0.4, y_top + 2, w * 0.55, y_top + 2);
      cairo_line_to(cr, x1, y_top + 2);
      break;

    case BIQUAD_TYPE_BANDPASS:
      // Narrower peak shape
      cairo_move_to(cr, x0, y_bot);
      cairo_curve_to(cr, w * 0.3, y_bot, w * 0.4, y_top, w * 0.5, y_top);
      cairo_curve_to(cr, w * 0.6, y_top, w * 0.7, y_bot, x1, y_bot);
      break;

    case BIQUAD_TYPE_NOTCH:
      // Flat with V-shaped dip
      cairo_move_to(cr, x0, y_top + 2);
      cairo_line_to(cr, w * 0.35, y_top + 2);
      cairo_line_to(cr, w * 0.5, y_bot - 2);
      cairo_line_to(cr, w * 0.65, y_top + 2);
      cairo_line_to(cr, x1, y_top + 2);
      break;

    case BIQUAD_TYPE_GAIN:
      // Flat line (frequency-independent gain)
      cairo_move_to(cr, x0, y_mid);
      cairo_line_to(cr, x1, y_mid);
      break;

    case BIQUAD_TYPE_LOWPASS_1:
      // Gentler slope (6 dB/oct) - flatter curve than 2nd order
      cairo_move_to(cr, x0, y_top + 2);
      cairo_line_to(cr, w * 0.4, y_top + 2);
      cairo_curve_to(cr, w * 0.55, y_top + 3, w * 0.7, y_mid, x1, y_mid + 3);
      break;

    case BIQUAD_TYPE_HIGHPASS_1:
      // Gentler slope (6 dB/oct)
      cairo_move_to(cr, x0, y_mid + 3);
      cairo_curve_to(cr, w * 0.3, y_mid, w * 0.45, y_top + 3, w * 0.6, y_top + 2);
      cairo_line_to(cr, x1, y_top + 2);
      break;

    case BIQUAD_TYPE_LOW_SHELF_1:
      // Gentler shelf transition
      cairo_move_to(cr, x0, y_top + 2);
      cairo_line_to(cr, w * 0.25, y_top + 2);
      cairo_curve_to(cr, w * 0.5, y_top + 2, w * 0.5, y_bot - 2, w * 0.75, y_bot - 2);
      cairo_line_to(cr, x1, y_bot - 2);
      break;

    case BIQUAD_TYPE_HIGH_SHELF_1:
      // Gentler shelf transition
      cairo_move_to(cr, x0, y_bot - 2);
      cairo_line_to(cr, w * 0.25, y_bot - 2);
      cairo_curve_to(cr, w * 0.5, y_bot - 2, w * 0.5, y_top + 2, w * 0.75, y_top + 2);
      cairo_line_to(cr, x1, y_top + 2);
      break;

    default:
      // Flat line for unknown types
      cairo_move_to(cr, x0, y_mid);
      cairo_line_to(cr, x1, y_mid);
      break;
  }

  cairo_stroke(cr);
}

// Draw callback for icons in the popup list
static void list_icon_draw(
  GtkDrawingArea *area,
  cairo_t        *cr,
  int             width,
  int             height,
  gpointer        user_data
) {
  BiquadFilterType type = GPOINTER_TO_INT(user_data);

  GtkWidget *widget = GTK_WIDGET(area);
  GdkRGBA color;
  gtk_widget_get_color(widget, &color);
  gdk_cairo_set_source_rgba(cr, &color);

  draw_filter_type_icon(cr, type, width, height);
}

// Draw callback for the button's icon
static void button_icon_draw(
  GtkDrawingArea *area,
  cairo_t        *cr,
  int             width,
  int             height,
  gpointer        user_data
) {
  struct filter_type_dropdown *data = user_data;

  GtkWidget *widget = GTK_WIDGET(area);
  GdkRGBA color;
  gtk_widget_get_color(widget, &color);
  gdk_cairo_set_source_rgba(cr, &color);

  draw_filter_type_icon(cr, data->current_type, width, height);
}

static void list_item_setup(
  GtkListItemFactory *factory,
  GtkListItem        *list_item,
  gpointer            user_data
) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

  GtkWidget *icon = gtk_drawing_area_new();
  gtk_widget_set_size_request(icon, FILTER_ICON_WIDTH, FILTER_ICON_HEIGHT);
  gtk_box_append(GTK_BOX(box), icon);

  GtkWidget *label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0);
  gtk_box_append(GTK_BOX(box), label);

  gtk_list_item_set_child(list_item, box);
}

static void list_item_bind(
  GtkListItemFactory *factory,
  GtkListItem        *list_item,
  gpointer            user_data
) {
  GtkWidget *box = gtk_list_item_get_child(list_item);
  GtkWidget *icon = gtk_widget_get_first_child(box);
  GtkWidget *label = gtk_widget_get_next_sibling(icon);

  int index = gtk_list_item_get_position(list_item);
  BiquadFilterType type = (BiquadFilterType)index;

  gtk_drawing_area_set_draw_func(
    GTK_DRAWING_AREA(icon), list_icon_draw, GINT_TO_POINTER(type), NULL
  );

  gtk_label_set_text(GTK_LABEL(label), biquad_type_name(type));
}

static void list_activated(
  GtkListView                 *listview,
  guint                        index,
  struct filter_type_dropdown *data
) {
  if (index < BIQUAD_TYPE_COUNT) {
    data->current_type = (BiquadFilterType)index;
    gtk_single_selection_set_selected(data->selection, index);
    gtk_label_set_text(GTK_LABEL(data->btn_label), biquad_type_name(index));
    gtk_widget_queue_draw(data->btn_icon);

    if (data->changed_callback)
      data->changed_callback(data->button, data->current_type, data->changed_user_data);
  }

  gtk_popover_popdown(GTK_POPOVER(data->popover));
}

static void button_clicked(
  GtkWidget                   *widget,
  struct filter_type_dropdown *data
) {
  gtk_popover_popup(GTK_POPOVER(data->popover));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->button), FALSE);

  // Scroll to and focus the currently selected item
  guint selected = gtk_single_selection_get_selected(data->selection);
  gtk_list_view_scroll_to(
    GTK_LIST_VIEW(data->listview), selected, GTK_LIST_SCROLL_FOCUS, NULL
  );
}

static void dropdown_data_destroy(struct filter_type_dropdown *data) {
  g_free(data);
}

GtkWidget *make_filter_type_dropdown(BiquadFilterType initial_type) {
  struct filter_type_dropdown *data = g_malloc0(
    sizeof(struct filter_type_dropdown)
  );
  data->current_type = initial_type;

  // Create button with icon and label
  data->button = gtk_toggle_button_new();
  gtk_widget_add_css_class(data->button, "filter-type-dropdown");

  GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_button_set_child(GTK_BUTTON(data->button), btn_box);

  data->btn_icon = gtk_drawing_area_new();
  gtk_widget_set_size_request(data->btn_icon, FILTER_ICON_WIDTH, FILTER_ICON_HEIGHT);
  gtk_drawing_area_set_draw_func(
    GTK_DRAWING_AREA(data->btn_icon), button_icon_draw, data, NULL
  );
  gtk_box_append(GTK_BOX(btn_box), data->btn_icon);

  data->btn_label = gtk_label_new(biquad_type_name(initial_type));
  gtk_box_append(GTK_BOX(btn_box), data->btn_label);

  // Create popover
  data->popover = gtk_popover_new();
  gtk_popover_set_has_arrow(GTK_POPOVER(data->popover), FALSE);
  gtk_widget_set_parent(data->popover, btn_box);

  // Build list model
  GtkStringList *model = gtk_string_list_new(NULL);
  for (int i = 0; i < BIQUAD_TYPE_COUNT; i++)
    gtk_string_list_append(model, biquad_type_name(i));

  // Factory for list items
  GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "setup", G_CALLBACK(list_item_setup), data);
  g_signal_connect(factory, "bind", G_CALLBACK(list_item_bind), data);

  data->selection = gtk_single_selection_new(G_LIST_MODEL(model));
  gtk_single_selection_set_selected(data->selection, initial_type);

  data->listview = gtk_list_view_new(
    GTK_SELECTION_MODEL(data->selection), factory
  );
  gtk_list_view_set_single_click_activate(GTK_LIST_VIEW(data->listview), TRUE);
  gtk_widget_add_css_class(data->listview, "filter-type-list");
  gtk_popover_set_child(GTK_POPOVER(data->popover), data->listview);

  // Connect signals
  g_signal_connect(
    data->button, "clicked", G_CALLBACK(button_clicked), data
  );
  g_signal_connect(
    data->listview, "activate", G_CALLBACK(list_activated), data
  );

  // Store data pointer for API functions
  g_object_set_data(G_OBJECT(data->button), "dropdown-data", data);

  g_object_weak_ref(
    G_OBJECT(data->button), (GWeakNotify)dropdown_data_destroy, data
  );

  return data->button;
}

BiquadFilterType filter_type_dropdown_get_selected(GtkWidget *widget) {
  struct filter_type_dropdown *data = g_object_get_data(
    G_OBJECT(widget), "dropdown-data"
  );
  return data ? data->current_type : BIQUAD_TYPE_PEAKING;
}

void filter_type_dropdown_set_selected(GtkWidget *widget, BiquadFilterType type) {
  struct filter_type_dropdown *data = g_object_get_data(
    G_OBJECT(widget), "dropdown-data"
  );
  if (!data || type >= BIQUAD_TYPE_COUNT)
    return;

  data->current_type = type;
  gtk_single_selection_set_selected(data->selection, type);
  gtk_label_set_text(GTK_LABEL(data->btn_label), biquad_type_name(type));
  gtk_widget_queue_draw(data->btn_icon);
}

void filter_type_dropdown_queue_redraw(GtkWidget *widget) {
  struct filter_type_dropdown *data = g_object_get_data(
    G_OBJECT(widget), "dropdown-data"
  );
  if (data)
    gtk_widget_queue_draw(data->btn_icon);
}

void filter_type_dropdown_connect_changed(
  GtkWidget                  *widget,
  FilterTypeChangedCallback   callback,
  gpointer                    user_data
) {
  struct filter_type_dropdown *data = g_object_get_data(
    G_OBJECT(widget), "dropdown-data"
  );
  if (data) {
    data->changed_callback = callback;
    data->changed_user_data = user_data;
  }
}

// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <graphene.h>
#include <gtk/gtk.h>
#include <math.h>

#include "alsa.h"
#include "custom-names.h"
#include "glow.h"
#include "gtkhelper.h"
#include "hw-io-availability.h"
#include "port-enable.h"
#include "stereo-link.h"
#include "stringhelper.h"
#include "widget-gain.h"
#include "window-mixer.h"

// data structure for rotated label info
struct rotated_label_info {
  char            *text;
  int              text_w;
  int              text_h;
  gboolean         hover;
  cairo_surface_t *glow_cache;
  int              glow_cache_w;
  int              glow_cache_h;
};

static void rotated_label_info_free(gpointer data) {
  struct rotated_label_info *info = data;
  if (info->glow_cache)
    cairo_surface_destroy(info->glow_cache);
  g_free(info->text);
  g_free(info);
}

// rotation angle for mixer input labels
#define LABEL_ANGLE (G_PI / 6.0)  // 30°
#define LABEL_SIN 0.5                       // sin(30°)
#define LABEL_COS 0.86602540378443864676    // cos(30°)

// vertical padding for rotated label anchor
#define LABEL_VERTICAL_PAD 5

// fraction of cell width to inset rotated labels from cell edge
#define LABEL_INSET_FRACTION 0.2

// mixer glow bar sizing
#define MIXER_GLOW_MAX_WIDTH 35.0
#define MIXER_GLOW_WIDTH_SCALE(intensity) \
  (0.75 + 0.75 * (intensity))
// total glow extent including round cap overshoot
#define MIXER_GLOW_FULL_EXTENT \
  (MIXER_GLOW_MAX_WIDTH + \
   GLOW_MAX_WIDTH * MIXER_GLOW_WIDTH_SCALE(1) /* max */ / 2)
#define MIXER_LABEL_MIN_WIDTH \
  ((int)(MIXER_GLOW_FULL_EXTENT * 2))

// draw a horizontal glow bar at a specific position
static void draw_glow_at(
  cairo_t *cr,
  double   cx,
  double   cy,
  double   max_extent,
  double   level_db
) {
  double intensity = get_glow_intensity(level_db);
  if (intensity <= 0)
    return;

  double r, g, b;
  level_to_colour(level_db, &r, &g, &b);

  double extent = max_extent * intensity;
  if (extent < 5.0)
    extent = 5.0;

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  for (int layer = GLOW_LAYERS - 1; layer >= 0; layer--) {
    double width, alpha;
    get_glow_layer_params(layer, intensity, &width, &alpha);

    width *= MIXER_GLOW_WIDTH_SCALE(intensity);

    cairo_set_source_rgba(cr, r, g, b, alpha);
    cairo_set_line_width(cr, width);
    cairo_move_to(cr, cx - extent, cy);
    cairo_line_to(cr, cx + extent, cy);
    cairo_stroke(cr);
  }
}

// get label centre coordinates relative to parent
static gboolean get_label_centre(
  GtkWidget *label,
  GtkWidget *parent,
  double    *cx,
  double    *cy
) {
  if (!label || !gtk_widget_get_visible(label))
    return FALSE;

  graphene_point_t src = GRAPHENE_POINT_INIT(
    gtk_widget_get_width(label) / 2.0,
    gtk_widget_get_height(label) / 2.0
  );
  graphene_point_t dest;
  if (!gtk_widget_compute_point(label, parent, &src, &dest))
    return FALSE;

  *cx = dest.x;
  *cy = dest.y;
  return TRUE;
}

// result of computing the anchor point for a rotated label
struct label_anchor {
  double anchor_x, anchor_y;
  graphene_point_t tl, br;
};

// compute the cell corners and anchor point for a rotated label.
// text_h is needed to offset the anchor from the cell edge.
static gboolean compute_label_anchor(
  GtkWidget            *label_widget,
  GtkWidget            *overlay_widget,
  gboolean              is_bottom,
  int                   text_h,
  struct label_anchor  *out
) {
  // get cell corners in overlay coordinates
  graphene_point_t src_tl = GRAPHENE_POINT_INIT(0, 0);
  graphene_point_t src_br = GRAPHENE_POINT_INIT(
    gtk_widget_get_width(label_widget),
    gtk_widget_get_height(label_widget)
  );
  if (!gtk_widget_compute_point(
        label_widget, overlay_widget, &src_tl, &out->tl) ||
      !gtk_widget_compute_point(
        label_widget, overlay_widget, &src_br, &out->br))
    return FALSE;

  double cell_w = out->br.x - out->tl.x;
  double inset = cell_w * LABEL_INSET_FRACTION;

  if (is_bottom) {
    // text top-right at cell top-right; body extends right by
    // text_h * sin, so shift anchor left to stay within cell;
    // additional inset moves text further left within cell
    out->anchor_x =
      out->br.x - text_h * LABEL_SIN - inset;
    out->anchor_y = out->tl.y + LABEL_VERTICAL_PAD;
  } else {
    // text bottom-left at cell bottom-left; body extends down
    // by text_h * cos, so shift anchor up to stay within cell;
    // additional inset moves text further right within cell
    out->anchor_x = out->tl.x + inset;
    out->anchor_y = out->br.y - text_h * LABEL_COS;
  }

  return TRUE;
}

// get the centre of a rotated mixer input label in overlay
// coordinates, for positioning glow bars behind the text
static gboolean get_rotated_label_centre(
  GtkWidget *label_widget,
  GtkWidget *overlay_widget,
  gboolean   is_bottom,
  double    *cx,
  double    *cy
) {
  if (!label_widget || !gtk_widget_get_visible(label_widget))
    return FALSE;

  struct rotated_label_info *info = g_object_get_data(
    G_OBJECT(label_widget), "label_info"
  );
  if (!info || !info->text || !*info->text)
    return FALSE;

  struct label_anchor anchor;
  if (!compute_label_anchor(
        label_widget, overlay_widget,
        is_bottom, info->text_h, &anchor))
    return FALSE;

  // text centre in text space, accounting for the extra
  // -text_w shift applied to bottom labels
  double tx = is_bottom
    ? -info->text_w / 2.0 : info->text_w / 2.0;
  double ty = info->text_h / 2.0;

  // rotate by -LABEL_ANGLE and translate to anchor
  *cx = anchor.anchor_x + tx * LABEL_COS + ty * LABEL_SIN;
  *cy = anchor.anchor_y - tx * LABEL_SIN + ty * LABEL_COS;
  return TRUE;
}

// draw a single mono glow bar centred on a label
static void draw_label_glow(
  cairo_t   *cr,
  GtkWidget *label,
  GtkWidget *parent,
  double     level_db
) {
  double cx, cy;
  if (!get_label_centre(label, parent, &cx, &cy))
    return;

  draw_glow_at(cr, cx, cy, MIXER_GLOW_MAX_WIDTH, level_db);
}

// draw one side of a stereo glow bar growing outward from centre;
// round cap on outer end only, flat on inner end
static void draw_glow_bar_outward(
  cairo_t *cr,
  double   inner_x,
  double   cy,
  double   max_extent,
  double   level_db,
  int      direction  // -1 = left, +1 = right
) {
  double intensity = get_glow_intensity(level_db);
  if (intensity <= 0)
    return;

  double r, g, b;
  level_to_colour(level_db, &r, &g, &b);

  double extent = max_extent * intensity;
  if (extent < 5.0)
    extent = 5.0;

  double outer_x = inner_x + direction * extent;

  for (int layer = GLOW_LAYERS - 1; layer >= 0; layer--) {
    double width, alpha;
    get_glow_layer_params(layer, intensity, &width, &alpha);

    width *= MIXER_GLOW_WIDTH_SCALE(intensity);

    double half = width / 2.0;

    cairo_set_source_rgba(cr, r, g, b, alpha);
    cairo_set_line_width(cr, width);

    // flat inner end, extend to outer edge of round cap
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    cairo_move_to(cr, inner_x, cy);
    cairo_line_to(cr, outer_x, cy);
    cairo_stroke(cr);

    // round cap as semicircle on outer end
    double a_start = direction > 0 ? -G_PI / 2 : G_PI / 2;
    cairo_arc(
      cr, outer_x, cy, half, a_start, a_start + G_PI
    );
    cairo_fill(cr);
  }
}

// draw split L/R glow bars growing outward from label centre
static void draw_stereo_label_glow(
  cairo_t   *cr,
  GtkWidget *label,
  GtkWidget *parent,
  double     level_db_l,
  double     level_db_r
) {
  double cx, cy;
  if (!get_label_centre(label, parent, &cx, &cy))
    return;

  draw_glow_bar_outward(cr, cx, cy, MIXER_GLOW_MAX_WIDTH, level_db_l, -1);
  draw_glow_bar_outward(cr, cx, cy, MIXER_GLOW_MAX_WIDTH, level_db_r, +1);
}

// draw a mono glow bar centred on a rotated mixer input label
static void draw_rotated_label_glow(
  cairo_t   *cr,
  GtkWidget *label,
  GtkWidget *parent,
  gboolean   is_bottom,
  double     level_db
) {
  double cx, cy;
  if (!get_rotated_label_centre(label, parent, is_bottom, &cx, &cy))
    return;

  cairo_save(cr);
  cairo_translate(cr, cx, cy);
  cairo_rotate(cr, -LABEL_ANGLE);
  draw_glow_at(cr, 0, 0, MIXER_GLOW_MAX_WIDTH, level_db);
  cairo_restore(cr);
}

// draw split L/R glow bars centred on a rotated mixer input label
static void draw_rotated_stereo_label_glow(
  cairo_t   *cr,
  GtkWidget *label,
  GtkWidget *parent,
  gboolean   is_bottom,
  double     level_db_l,
  double     level_db_r
) {
  double cx, cy;
  if (!get_rotated_label_centre(label, parent, is_bottom, &cx, &cy))
    return;

  cairo_save(cr);
  cairo_translate(cr, cx, cy);
  cairo_rotate(cr, -LABEL_ANGLE);
  draw_glow_bar_outward(cr, 0, 0, MIXER_GLOW_MAX_WIDTH, level_db_l, -1);
  draw_glow_bar_outward(cr, 0, 0, MIXER_GLOW_MAX_WIDTH, level_db_r, +1);
  cairo_restore(cr);
}

// draw function for mixer label glow overlay
static void draw_mixer_glow(
  GtkDrawingArea *drawing_area,
  cairo_t        *cr,
  int             width,
  int             height,
  void           *user_data
) {
  struct alsa_card *card = user_data;
  GtkWidget *parent = card->mixer_glow;

  if (!card->routing_srcs)
    return;

  // draw glow behind mixer output labels (Mix A, Mix B, etc.)
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (r_src->port_category != PC_MIX)
      continue;

    if (!is_routing_src_enabled(r_src) || !should_display_src(r_src))
      continue;

    double level_l = get_routing_src_level_db(card, r_src);

    if (is_src_linked(r_src)) {
      struct routing_src *r_src_r = get_src_partner(r_src);
      double level_r = r_src_r ?
        get_routing_src_level_db(card, r_src_r) : level_l;

      draw_stereo_label_glow(
        cr, r_src->mixer_label_left, parent, level_l, level_r
      );
      draw_stereo_label_glow(
        cr, r_src->mixer_label_right, parent, level_l, level_r
      );
    } else {
      draw_label_glow(
        cr, r_src->mixer_label_left, parent, level_l
      );
      draw_label_glow(
        cr, r_src->mixer_label_right, parent, level_l
      );
    }
  }

  // draw glow behind mixer input labels (top/bottom)
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!r_snk->elem || r_snk->elem->port_category != PC_MIX)
      continue;

    if (!is_routing_snk_enabled(r_snk) || !should_display_snk(r_snk))
      continue;

    // get the source connected to this mixer input
    int r_src_idx = alsa_get_elem_value(r_snk->elem);
    if (!r_src_idx)
      continue;

    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, r_src_idx
    );

    double level_l = get_routing_src_level_db(card, r_src);

    if (is_snk_linked(r_snk)) {
      struct routing_snk *r_snk_r = get_snk_partner(r_snk);
      double level_r = level_l;

      if (r_snk_r && r_snk_r->elem) {
        int r_src_idx_r = alsa_get_elem_value(r_snk_r->elem);
        if (r_src_idx_r) {
          struct routing_src *r_src_r = &g_array_index(
            card->routing_srcs, struct routing_src, r_src_idx_r
          );
          level_r = get_routing_src_level_db(card, r_src_r);
        }
      }

      draw_rotated_stereo_label_glow(
        cr, r_snk->mixer_label_top, parent,
        FALSE, level_l, level_r
      );
      draw_rotated_stereo_label_glow(
        cr, r_snk->mixer_label_bottom, parent,
        TRUE, level_l, level_r
      );
    } else {
      draw_rotated_label_glow(
        cr, r_snk->mixer_label_top, parent,
        FALSE, level_l
      );
      draw_rotated_label_glow(
        cr, r_snk->mixer_label_bottom, parent,
        TRUE, level_l
      );
    }
  }
}

// mixer_gain_widgets is stored in card->mixer_gain_widgets
// struct mixer_gain_widget is declared in window-mixer.h

// apply a single-pass box blur to the alpha channel of an ARGB32
// image surface. operates on raw pixel data in-place. uses a
// temporary buffer to avoid read-after-write hazards.
static void box_blur_alpha(
  unsigned char *data,
  int            width,
  int            height,
  int            stride,
  int            radius
) {
  // horizontal pass — blur each row into a temp buffer,
  // then copy back
  unsigned char *tmp = g_malloc(
    MAX(width, height) * sizeof(unsigned char)
  );

  for (int y = 0; y < height; y++) {
    unsigned char *row = data + y * stride;

    // build initial window for x=0: [0, min(radius, width-1)]
    int right = MIN(radius, width - 1);
    int sum = 0;
    for (int x = 0; x <= right; x++)
      sum += row[x * 4 + 3];

    for (int x = 0; x < width; x++) {
      int count = MIN(x, radius) + MIN(width - 1 - x, radius) + 1;
      tmp[x] = sum / count;

      // slide window: remove departing left, add arriving right
      int rem = x - radius;
      int add = x + radius + 1;
      if (rem >= 0)
        sum -= row[rem * 4 + 3];
      if (add < width)
        sum += row[add * 4 + 3];
    }

    for (int x = 0; x < width; x++)
      row[x * 4 + 3] = tmp[x];
  }

  // vertical pass — blur each column into a temp buffer,
  // then copy back
  for (int x = 0; x < width; x++) {
    int off = x * 4 + 3;

    // build initial window for y=0: [0, min(radius, height-1)]
    int bottom = MIN(radius, height - 1);
    int sum = 0;
    for (int y = 0; y <= bottom; y++)
      sum += data[y * stride + off];

    for (int y = 0; y < height; y++) {
      int count =
        MIN(y, radius) + MIN(height - 1 - y, radius) + 1;
      tmp[y] = sum / count;

      // slide window: remove departing top, add arriving bottom
      int rem = y - radius;
      int add = y + radius + 1;
      if (rem >= 0)
        sum -= data[rem * stride + off];
      if (add < height)
        sum += data[add * stride + off];
    }

    for (int y = 0; y < height; y++)
      data[y * stride + off] = tmp[y];
  }

  g_free(tmp);
}

// build the blurred green text-shadow glow surface for a label.
// matches CSS: text-shadow: 0 0 5px #00c000, 0 0 15px #00c000.
// CSS blur-radius ≈ 2σ; 3 box-blur passes at radius r give
// σ ≈ 0.87r, so r = CSS_radius / (2 × 0.87) ≈ CSS_radius / 1.73.
static cairo_surface_t *build_glow_surface(
  PangoLayout *layout,
  int          text_w,
  int          text_h,
  int          pad
) {
  int surf_w = text_w + pad * 2;
  int surf_h = text_h + pad * 2;

  static const int radii[] = { 3, 9 };

  // render text mask once, reuse for each blur radius
  unsigned char *text_mask = g_malloc(surf_w * surf_h);

  {
    cairo_surface_t *mask_surf = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, surf_w, surf_h
    );
    cairo_t *gc = cairo_create(mask_surf);
    cairo_set_source_rgba(gc, 1, 1, 1, 1);
    cairo_move_to(gc, pad, pad);
    pango_cairo_show_layout(gc, layout);
    cairo_destroy(gc);

    cairo_surface_flush(mask_surf);
    unsigned char *mdata =
      cairo_image_surface_get_data(mask_surf);
    int mstride =
      cairo_image_surface_get_stride(mask_surf);
    for (int y = 0; y < surf_h; y++)
      for (int x = 0; x < surf_w; x++)
        text_mask[y * surf_w + x] =
          mdata[y * mstride + x * 4 + 3];
    cairo_surface_destroy(mask_surf);
  }

  // composite surface: accumulate both blur passes
  cairo_surface_t *result = cairo_image_surface_create(
    CAIRO_FORMAT_ARGB32, surf_w, surf_h
  );
  cairo_t *gc = cairo_create(result);

  for (int r = 0; r < 2; r++) {
    cairo_surface_t *surf = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, surf_w, surf_h
    );
    cairo_surface_flush(surf);
    unsigned char *data =
      cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);

    // seed alpha from text mask
    for (int y = 0; y < surf_h; y++)
      for (int x = 0; x < surf_w; x++)
        data[y * stride + x * 4 + 3] =
          text_mask[y * surf_w + x];

    // 3 box-blur passes ≈ Gaussian
    for (int pass = 0; pass < 3; pass++)
      box_blur_alpha(
        data, surf_w, surf_h, stride, radii[r]
      );

    // recolour to premultiplied #00c000. restore full
    // opacity where the original text was so the glow is
    // bright green right at the text edge (matching CSS
    // text-shadow which paints the colour at full opacity
    // then blurs outward).
    for (int y = 0; y < surf_h; y++) {
      unsigned char *row = data + y * stride;
      for (int x = 0; x < surf_w; x++) {
        int a = row[x * 4 + 3];
        int orig = text_mask[y * surf_w + x];
        if (orig > a)
          a = orig;
        row[x * 4 + 0] = 0;               // B
        row[x * 4 + 1] = 0xC0 * a / 255;  // G
        row[x * 4 + 2] = 0;               // R
        row[x * 4 + 3] = a;
      }
    }
    cairo_surface_mark_dirty(surf);

    cairo_set_source_surface(gc, surf, 0, 0);
    cairo_paint(gc);
    cairo_surface_destroy(surf);
  }

  cairo_destroy(gc);
  g_free(text_mask);

  return result;
}

// draw cached blurred green text-shadow glow behind a label.
// caller must set cairo position to text origin.
static void draw_text_shadow_glow(
  cairo_t                  *cr,
  struct rotated_label_info *info,
  PangoLayout              *layout,
  int                       text_w,
  int                       text_h
) {
  if (text_w <= 0 || text_h <= 0)
    return;

  static const int max_radius = 9;
  int pad = max_radius * 3 + 2;

  // rebuild cache if dimensions changed
  if (!info->glow_cache ||
      info->glow_cache_w != text_w ||
      info->glow_cache_h != text_h) {
    if (info->glow_cache)
      cairo_surface_destroy(info->glow_cache);
    info->glow_cache =
      build_glow_surface(layout, text_w, text_h, pad);
    info->glow_cache_w = text_w;
    info->glow_cache_h = text_h;
  }

  cairo_set_source_surface(
    cr, info->glow_cache, -pad, -pad
  );
  cairo_paint(cr);
}

// draw a single rotated mixer input label.
// for top labels: text bottom-left touches cell bottom-left corner.
// for bottom labels: text bottom-right touches cell top-right corner.
//
// with rotation angle θ, the text bottom edge (y = text_h in text
// space) maps to (+text_h × sinθ, +text_h × cosθ) in screen space
// relative to the text origin. the anchor is therefore offset from
// the cell corner by (-text_h × sinθ, -text_h × cosθ).
static void draw_snk_label(
  cairo_t  *cr,
  GtkWidget *label_widget,
  GtkWidget *overlay_widget,
  gboolean   is_bottom
) {
  struct rotated_label_info *info = g_object_get_data(
    G_OBJECT(label_widget), "label_info"
  );
  if (!info || !info->text || !*info->text)
    return;

  // use the widget's own Pango context to get the system theme
  // font; bold on hover
  PangoContext *pango_ctx =
    gtk_widget_get_pango_context(overlay_widget);
  PangoLayout *layout = pango_layout_new(pango_ctx);

  if (info->hover) {
    PangoFontDescription *bold = pango_font_description_copy(
      pango_context_get_font_description(pango_ctx)
    );
    pango_font_description_set_weight(bold, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, bold);
    pango_font_description_free(bold);
  }

  pango_layout_set_text(layout, info->text, -1);

  int text_w, text_h;
  pango_layout_get_pixel_size(layout, &text_w, &text_h);

  struct label_anchor anchor;
  if (!compute_label_anchor(
        label_widget, overlay_widget,
        is_bottom, text_h, &anchor)) {
    g_object_unref(layout);
    return;
  }

  cairo_save(cr);
  cairo_translate(cr, anchor.anchor_x, anchor.anchor_y);
  cairo_rotate(cr, -LABEL_ANGLE);

  // shift text left so it ends (right edge) at the anchor column
  if (is_bottom)
    cairo_translate(cr, -text_w, 0);

  if (info->hover) {
    draw_text_shadow_glow(cr, info, layout, text_w, text_h);
    cairo_set_source_rgb(cr, 1, 1, 1);
  } else {
    GdkRGBA color;
    gtk_widget_get_color(overlay_widget, &color);
    gdk_cairo_set_source_rgba(cr, &color);
  }

  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
  g_object_unref(layout);
}

static void draw_mixer_labels(
  GtkDrawingArea *drawing_area,
  cairo_t        *cr,
  int             width,
  int             height,
  void           *user_data
) {
  struct alsa_card *card = user_data;

  if (!card->routing_snks)
    return;

  GtkWidget *overlay = GTK_WIDGET(drawing_area);

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!r_snk->elem || r_snk->elem->port_category != PC_MIX)
      continue;

    if (!is_routing_snk_enabled(r_snk) || !should_display_snk(r_snk))
      continue;

    if (r_snk->mixer_label_top)
      draw_snk_label(cr, r_snk->mixer_label_top, overlay, FALSE);

    if (r_snk->mixer_label_bottom)
      draw_snk_label(cr, r_snk->mixer_label_bottom, overlay, TRUE);
  }
}

static void set_rotated_label_text(
  GtkWidget  *widget,
  const char *text
) {
  struct rotated_label_info *info = g_object_get_data(
    G_OBJECT(widget), "label_info"
  );
  if (!info) {
    info = g_malloc0(sizeof(struct rotated_label_info));
    g_object_set_data_full(
      G_OBJECT(widget), "label_info",
      info, rotated_label_info_free
    );
  }

  g_free(info->text);
  info->text = g_strdup(text);

  // invalidate glow cache (dimensions may have changed)
  if (info->glow_cache) {
    cairo_surface_destroy(info->glow_cache);
    info->glow_cache = NULL;
  }

  // trigger redraw of label overlay
  struct alsa_card *card = g_object_get_data(G_OBJECT(widget), "card");
  if (card && card->mixer_label_overlay)
    gtk_widget_queue_draw(card->mixer_label_overlay);

  // measure text using the overlay's Pango context (system theme font)
  // to calculate the required box height for this rotation angle
  if (!card || !card->mixer_label_overlay)
    return;

  PangoContext *pango_ctx =
    gtk_widget_get_pango_context(card->mixer_label_overlay);
  PangoLayout *layout = pango_layout_new(pango_ctx);

  // measure with bold weight so the reserved height covers
  // the hover state where text becomes bold
  PangoFontDescription *bold = pango_font_description_copy(
    pango_context_get_font_description(pango_ctx)
  );
  pango_font_description_set_weight(bold, PANGO_WEIGHT_BOLD);
  pango_layout_set_font_description(layout, bold);
  pango_font_description_free(bold);

  pango_layout_set_text(layout, text, -1);

  pango_layout_get_pixel_size(layout, &info->text_w, &info->text_h);
  g_object_unref(layout);

  // use the wider of text width and glow bar extent for height;
  // width uses text only (glow overflows horizontally by design)
  double glow_w = MIXER_GLOW_FULL_EXTENT;
  double effective_w = MAX(info->text_w, glow_w);

  int required_h = (int)ceil(
    effective_w * LABEL_SIN +
    info->text_h * LABEL_COS
  );
  gtk_widget_set_size_request(widget, 1, required_h);
}

static void set_rotated_label_hover(
  GtkWidget *widget,
  gboolean   hover
) {
  struct rotated_label_info *info = g_object_get_data(
    G_OBJECT(widget), "label_info"
  );
  if (info) {
    info->hover = hover;

    // trigger redraw of label overlay
    struct alsa_card *card = g_object_get_data(G_OBJECT(widget), "card");
    if (card && card->mixer_label_overlay)
      gtk_widget_queue_draw(card->mixer_label_overlay);
  }
}

static GtkWidget *create_rotated_label(
  const char       *text,
  struct alsa_card *card
) {
  // create box for positioning; size set by set_rotated_label_text
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  g_object_set_data(G_OBJECT(box), "card", card);
  set_rotated_label_text(box, text);

  return box;
}

static void mixer_gain_enter(
  GtkEventControllerMotion *controller,
  double x, double y,
  gpointer user_data
) {
  GtkWidget *widget = GTK_WIDGET(user_data);
  GtkWidget *mix_left = g_object_get_data(G_OBJECT(widget), "mix_label_left");
  GtkWidget *mix_right = g_object_get_data(G_OBJECT(widget), "mix_label_right");
  GtkWidget *source_top = g_object_get_data(G_OBJECT(widget), "source_label_top");
  GtkWidget *source_bottom = g_object_get_data(G_OBJECT(widget), "source_label_bottom");

  if (mix_left)
    gtk_widget_add_css_class(mix_left, "mixer-label-hover");
  if (mix_right)
    gtk_widget_add_css_class(mix_right, "mixer-label-hover");
  if (source_top)
    set_rotated_label_hover(source_top, TRUE);
  if (source_bottom)
    set_rotated_label_hover(source_bottom, TRUE);
}

static void mixer_gain_leave(
  GtkEventControllerMotion *controller,
  gpointer user_data
) {
  GtkWidget *widget = GTK_WIDGET(user_data);
  GtkWidget *mix_left = g_object_get_data(G_OBJECT(widget), "mix_label_left");
  GtkWidget *mix_right = g_object_get_data(G_OBJECT(widget), "mix_label_right");
  GtkWidget *source_top = g_object_get_data(G_OBJECT(widget), "source_label_top");
  GtkWidget *source_bottom = g_object_get_data(G_OBJECT(widget), "source_label_bottom");

  if (mix_left)
    gtk_widget_remove_css_class(mix_left, "mixer-label-hover");
  if (mix_right)
    gtk_widget_remove_css_class(mix_right, "mixer-label-hover");
  if (source_top)
    set_rotated_label_hover(source_top, FALSE);
  if (source_bottom)
    set_rotated_label_hover(source_bottom, FALSE);
}

static void add_mixer_hover_controller(GtkWidget *widget) {
  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "enter", G_CALLBACK(mixer_gain_enter), widget);
  g_signal_connect(motion, "leave", G_CALLBACK(mixer_gain_leave), widget);
  gtk_widget_add_controller(widget, motion);
}

static struct routing_snk *get_mixer_r_snk(
  struct alsa_card *card,
  int               input_num
) {
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    struct alsa_elem *elem = r_snk->elem;

    if (elem->port_category != PC_MIX)
      continue;

    if (elem->lr_num == input_num)
      return r_snk;
  }
  return NULL;
}

// Get mixer routing source by mix number (0-based: A=0, B=1, etc.)
static struct routing_src *get_mixer_r_src(
  struct alsa_card *card,
  int               mix_num
) {
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (r_src->port_category == PC_MIX && r_src->port_num == mix_num)
      return r_src;
  }
  return NULL;
}

static void populate_mixer_gain_widgets(struct alsa_card *card) {
  int num_mixes = card->routing_in_count[PC_MIX];
  int num_inputs = card->routing_out_count[PC_MIX];

  for (int mix_num = 0; mix_num < num_mixes; mix_num++) {
    struct routing_src *mix_src = get_mixer_r_src(card, mix_num);
    if (!mix_src)
      continue;

    if (!should_display_src(mix_src))
      continue;

    int output_linked = is_src_linked(mix_src);
    struct routing_src *mix_src_r =
      output_linked ? get_src_partner(mix_src) : NULL;
    int mix_num_r = mix_src_r ? mix_src_r->port_num : -1;

    for (int input_num = 0; input_num < num_inputs; input_num++) {
      struct routing_snk *r_snk = get_mixer_r_snk(card, input_num + 1);
      if (!r_snk)
        continue;

      if (!should_display_snk(r_snk))
        continue;

      int input_linked = is_snk_linked(r_snk);
      struct routing_snk *r_snk_r =
        input_linked ? get_snk_partner(r_snk) : NULL;
      int input_num_r = r_snk_r && r_snk_r->elem ?
                        r_snk_r->elem->lr_num - 1 : -1;

      struct alsa_elem *elem_arr[4];
      int elem_count = 0;

      if (!input_linked && !output_linked) {
        struct alsa_elem *e = card->mixer_gains[mix_num][input_num];
        if (e)
          elem_arr[elem_count++] = e;
      } else if (input_linked && !output_linked) {
        struct alsa_elem *e1 = card->mixer_gains[mix_num][input_num];
        struct alsa_elem *e2 = card->mixer_gains[mix_num][input_num_r];
        if (e1) elem_arr[elem_count++] = e1;
        if (e2) elem_arr[elem_count++] = e2;
      } else if (!input_linked && output_linked) {
        struct alsa_elem *e1 = card->mixer_gains[mix_num][input_num];
        struct alsa_elem *e2 = card->mixer_gains[mix_num_r][input_num];
        if (e1) elem_arr[elem_count++] = e1;
        if (e2) elem_arr[elem_count++] = e2;
      } else {
        struct alsa_elem *e1 = card->mixer_gains[mix_num][input_num];
        struct alsa_elem *e2 = card->mixer_gains[mix_num_r][input_num_r];
        if (e1) elem_arr[elem_count++] = e1;
        if (e2) elem_arr[elem_count++] = e2;
      }

      if (elem_count == 0)
        continue;

      // Create the gain widget
      GtkWidget *w;
      if (elem_count == 1) {
        w = make_gain_alsa_elem(
          elem_arr[0], 1, WIDGET_GAIN_TAPER_LOG, 0, TRUE
        );
      } else {
        w = make_stereo_gain_alsa_elem(
          elem_arr, elem_count, 1,
          WIDGET_GAIN_TAPER_LOG, 0, TRUE
        );
      }

      if (!w)
        continue;

      // keep alive when removed from grid
      g_object_ref(w);

      struct mixer_gain_widget *mg =
        g_malloc0(sizeof(struct mixer_gain_widget));
      mg->widget = w;
      mg->mix_num = mix_num;
      mg->input_num = input_num;
      mg->r_snk = r_snk;
      mg->elem = elem_arr[0];

      mg->elem_count = elem_count;
      for (int i = 0; i < elem_count; i++)
        mg->elems[i] = elem_arr[i];

      mg->r_snks[0] = r_snk;
      mg->r_snk_count = 1;
      if (input_linked && r_snk_r) {
        mg->r_snks[1] = r_snk_r;
        mg->r_snk_count = 2;
      }

      card->mixer_gain_widgets =
        g_list_prepend(card->mixer_gain_widgets, mg);

      // Store label references for hover effect
      g_object_set_data(
        G_OBJECT(w), "mix_label_left",
        mix_src->mixer_label_left
      );
      g_object_set_data(
        G_OBJECT(w), "mix_label_right",
        mix_src->mixer_label_right
      );
      g_object_set_data(
        G_OBJECT(w), "source_label_top",
        r_snk->mixer_label_top
      );
      g_object_set_data(
        G_OBJECT(w), "source_label_bottom",
        r_snk->mixer_label_bottom
      );

      add_mixer_hover_controller(w);
    }
  }

  card->mixer_gain_widgets =
    g_list_reverse(card->mixer_gain_widgets);
}

GtkWidget *create_mixer_controls(struct alsa_card *card) {
  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");

  // clear any existing mixer gain widgets from previous window
  for (GList *l = card->mixer_gain_widgets; l != NULL; l = l->next) {
    struct mixer_gain_widget *mg = l->data;
    if (mg->widget) {
      cleanup_gain_widget(mg->widget);
      g_object_unref(mg->widget);
    }
    g_free(mg);
  }
  g_list_free(card->mixer_gain_widgets);
  card->mixer_gain_widgets = NULL;

  // create overlay to hold the grid and glow layer
  GtkWidget *mixer_overlay = card->mixer_overlay = gtk_overlay_new();
  gtk_widget_add_css_class(mixer_overlay, "window-content");
  gtk_widget_add_css_class(mixer_overlay, "top-level-content");
  gtk_widget_add_css_class(mixer_overlay, "window-mixer");
  gtk_frame_set_child(GTK_FRAME(top), mixer_overlay);

  // create grid as base child (determines size)
  GtkWidget *mixer_top = gtk_grid_new();
  gtk_overlay_set_child(GTK_OVERLAY(mixer_overlay), mixer_top);

  // create unavailable overlay label (hidden by default)
  GtkWidget *unavail = card->mixer_unavailable_label = gtk_label_new(
    "Mixer unavailable at\ncurrent sample rate"
  );
  gtk_widget_add_css_class(unavail, "mixer-unavailable");
  gtk_widget_set_halign(unavail, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(unavail, GTK_ALIGN_CENTER);
  gtk_widget_set_can_target(unavail, FALSE);
  gtk_widget_set_visible(unavail, FALSE);
  gtk_overlay_add_overlay(GTK_OVERLAY(mixer_overlay), unavail);

  gtk_widget_set_halign(mixer_top, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(mixer_top, GTK_ALIGN_CENTER);

  // store the grid for later access
  card->mixer_grid = mixer_top;

  // create drawing area for glow effects as underlay
  // use measure callback to not affect sizing
  card->mixer_glow = gtk_drawing_area_new();
  gtk_widget_set_can_target(card->mixer_glow, FALSE);
  gtk_drawing_area_set_draw_func(
    GTK_DRAWING_AREA(card->mixer_glow), draw_mixer_glow, card, NULL
  );

  // insert at position 0 to be behind the grid
  // but GtkOverlay doesn't support ordering, so we need another approach
  gtk_overlay_add_overlay(GTK_OVERLAY(mixer_overlay), card->mixer_glow);

  // lower the glow below the grid by reordering
  gtk_widget_insert_before(card->mixer_glow, mixer_overlay, mixer_top);

  // create drawing area for rotated label overlay
  card->mixer_label_overlay = gtk_drawing_area_new();
  gtk_widget_set_can_target(card->mixer_label_overlay, FALSE);
  gtk_drawing_area_set_draw_func(
    GTK_DRAWING_AREA(card->mixer_label_overlay), draw_mixer_labels, card, NULL
  );
  gtk_overlay_add_overlay(GTK_OVERLAY(mixer_overlay), card->mixer_label_overlay);

  // create the Mix X labels on the left and right of the grid
  for (int i = 0; i < card->routing_in_count[PC_MIX]; i++) {
    struct routing_src *r_src = get_mixer_r_src(card, i);

    char *name = r_src ?
      get_mixer_output_label_for_mixer_window(r_src) :
      g_strdup_printf("Mix %c", i + 'A');

    GtkWidget *l_left = gtk_label_new(name);
    gtk_label_set_ellipsize(GTK_LABEL(l_left), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(l_left), 12);
    gtk_widget_set_size_request(l_left, MIXER_LABEL_MIN_WIDTH, -1);
    gtk_widget_set_tooltip_text(l_left, name);
    g_object_ref(l_left);
    gtk_grid_attach(
      GTK_GRID(mixer_top), l_left, 0, i + 1, 1, 1
    );

    GtkWidget *l_right = gtk_label_new(name);
    gtk_label_set_ellipsize(GTK_LABEL(l_right), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(l_right), 12);
    gtk_widget_set_size_request(l_right, MIXER_LABEL_MIN_WIDTH, -1);
    gtk_widget_set_tooltip_text(l_right, name);
    g_object_ref(l_right);
    gtk_grid_attach(
      GTK_GRID(mixer_top), l_right,
      card->routing_out_count[PC_MIX] + 1, i + 1, 1, 1
    );

    g_free(name);

    if (r_src) {
      r_src->mixer_label_left = l_left;
      r_src->mixer_label_right = l_right;
    }
  }

  // Create corner label for mixer inputs/outputs legend
  card->mixer_corner_label = gtk_label_new(NULL);
  gtk_label_set_markup(
    GTK_LABEL(card->mixer_corner_label),
    "<span line_height=\"1.8\">Inputs →</span>\nOutputs ↓"
  );
  gtk_label_set_justify(GTK_LABEL(card->mixer_corner_label), GTK_JUSTIFY_CENTER);
  gtk_widget_set_valign(card->mixer_corner_label, GTK_ALIGN_END);
  gtk_widget_add_css_class(card->mixer_corner_label, "mixer-corner-label");
  g_object_ref(card->mixer_corner_label);

  // Create all mixer input labels upfront (top and bottom)
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!r_snk->elem || r_snk->elem->port_category != PC_MIX)
      continue;

    // Create the labels if they don't exist
    if (!r_snk->mixer_label_top) {
      r_snk->mixer_label_top = create_rotated_label("", card);
      r_snk->mixer_label_bottom = create_rotated_label("", card);
      g_object_ref(r_snk->mixer_label_top);
      g_object_ref(r_snk->mixer_label_bottom);

      // Attach to grid initially (repositioned during rebuild)
      int input_num = r_snk->elem->lr_num - 1;
      gtk_grid_attach(
        GTK_GRID(mixer_top), r_snk->mixer_label_top,
        input_num + 1, 0, 1, 1
      );
      gtk_grid_attach(
        GTK_GRID(mixer_top), r_snk->mixer_label_bottom,
        input_num + 1, card->routing_in_count[PC_MIX] + 1, 1, 1
      );
    }
  }

  populate_mixer_gain_widgets(card);

  update_mixer_labels(card);

  // rebuild grid layout based on port enable states
  rebuild_mixer_grid(card);

  // set initial availability state
  int available = get_sample_rate_category(card->current_sample_rate) != SR_HIGH;
  update_mixer_availability(card, available);

  return top;
}

void update_mixer_labels(struct alsa_card *card) {
  // Update mixer output labels (Mix A, Mix B, etc.) for stereo linking
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (r_src->port_category != PC_MIX)
      continue;

    // Skip R channel of linked pair
    if (!should_display_src(r_src))
      continue;

    char *name;
    if (is_src_linked(r_src)) {
      name = get_src_pair_display_name(r_src);
    } else {
      name = get_mixer_output_label_for_mixer_window(r_src);
    }

    if (r_src->mixer_label_left) {
      gtk_label_set_text(GTK_LABEL(r_src->mixer_label_left), name);
      gtk_widget_set_tooltip_text(r_src->mixer_label_left, name);
    }
    if (r_src->mixer_label_right) {
      gtk_label_set_text(GTK_LABEL(r_src->mixer_label_right), name);
      gtk_widget_set_tooltip_text(r_src->mixer_label_right, name);
    }

    g_free(name);
  }

  // Update mixer input labels (source names)
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    struct alsa_elem *elem = r_snk->elem;

    if (elem->port_category != PC_MIX)
      continue;

    // Skip R channel of linked pair (label handled by L channel)
    if (!should_display_snk(r_snk))
      continue;

    int routing_src_idx = alsa_get_elem_value(elem);

    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, routing_src_idx
    );

    if (r_snk->mixer_label_top) {
      char *display_name;

      // If this mixer input is linked and the connected source is also linked,
      // show the stereo pair name
      if (is_snk_linked(r_snk) && is_src_linked(r_src)) {
        display_name = get_src_pair_display_name(r_src);
      } else {
        display_name = g_strdup(get_routing_src_display_name(r_src));
      }

      set_rotated_label_text(r_snk->mixer_label_top, display_name);
      set_rotated_label_text(r_snk->mixer_label_bottom, display_name);
      g_free(display_name);
    }
  }
}

// Rebuild the mixer grid layout based on current port enable states
void rebuild_mixer_grid(struct alsa_card *card) {
  if (!card || !card->mixer_grid)
    return;

  GtkGrid *grid = GTK_GRID(card->mixer_grid);

  // Remove all widgets from the grid (but keep references)
  GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(grid));
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_grid_remove(grid, child);
    child = next;
  }

  // Build list of visible mixer outputs (sources)
  int visible_mix_count = 0;
  int mix_num_to_row[MAX_MIX_OUT];  // map mix_num to row

  // initialize all to -1 (not visible)
  for (int i = 0; i < MAX_MIX_OUT; i++)
    mix_num_to_row[i] = -1;

  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (src->port_category != PC_MIX)
      continue;

    // bounds check
    if (src->port_num < 0 || src->port_num >= MAX_MIX_OUT)
      continue;

    // Visible if enabled AND should be displayed (not R of linked pair)
    if (is_routing_src_enabled(src) && should_display_src(src)) {
      mix_num_to_row[src->port_num] = visible_mix_count;
      visible_mix_count++;
    } else {
      mix_num_to_row[src->port_num] = -1;  // not visible
    }
  }

  // Build list of visible mixer inputs (sinks)
  int visible_input_count = 0;
  int max_mixer_inputs = card->routing_out_count[PC_MIX];  // actual number of mixer inputs
  int input_num_to_col[max_mixer_inputs];  // map input_num to column

  // initialize all to -1 (not visible)
  for (int i = 0; i < max_mixer_inputs; i++)
    input_num_to_col[i] = -1;

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!snk->elem || snk->elem->port_category != PC_MIX)
      continue;

    int input_num = snk->elem->lr_num - 1;  // convert to 0-based

    // bounds check
    if (input_num < 0 || input_num >= max_mixer_inputs)
      continue;

    // Visible if enabled AND should be displayed (not R of linked pair)
    int visible = is_routing_snk_enabled(snk) && should_display_snk(snk);

    if (visible) {
      input_num_to_col[input_num] = visible_input_count;
      visible_input_count++;
    } else {
      input_num_to_col[input_num] = -1;  // not visible
    }
  }

  // Re-attach mixer output labels (left and right)
  int row_offset = 1;  // row 0 is for rotated input labels
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (src->port_category != PC_MIX)
      continue;

    // bounds check
    if (src->port_num < 0 || src->port_num >= MAX_MIX_OUT)
      continue;

    int row = mix_num_to_row[src->port_num];
    if (row < 0)  // not visible
      continue;

    if (src->mixer_label_left) {
      gtk_grid_attach(
        grid, src->mixer_label_left,
        0, row + row_offset, 1, 1
      );
    }

    if (src->mixer_label_right) {
      gtk_grid_attach(
        grid, src->mixer_label_right,
        visible_input_count + 1, row + row_offset, 1, 1
      );
    }
  }

  // Attach corner label
  if (card->mixer_corner_label) {
    gtk_grid_attach(grid, card->mixer_corner_label, 0, 0, 1, 1);
  }

  // Re-attach mixer input labels (top and bottom)
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!snk->elem || snk->elem->port_category != PC_MIX)
      continue;

    int input_num = snk->elem->lr_num - 1;

    // bounds check
    if (input_num < 0 || input_num >= max_mixer_inputs)
      continue;

    int col = input_num_to_col[input_num];
    if (col < 0)  // not visible
      continue;

    if (snk->mixer_label_top) {
      gtk_grid_attach(
        grid, snk->mixer_label_top,
        col + 1, 0, 1, 1
      );
    }

    if (snk->mixer_label_bottom) {
      gtk_grid_attach(
        grid, snk->mixer_label_bottom,
        col + 1, visible_mix_count + row_offset, 1, 1
      );
    }
  }

  // Re-attach gain widgets
  for (GList *l = card->mixer_gain_widgets; l != NULL; l = l->next) {
    struct mixer_gain_widget *mg = l->data;

    // bounds check before accessing arrays
    if (mg->mix_num < 0 || mg->mix_num >= MAX_MIX_OUT ||
        mg->input_num < 0 || mg->input_num >= max_mixer_inputs)
      continue;

    int row = mix_num_to_row[mg->mix_num];
    int col = input_num_to_col[mg->input_num];

    // Only attach if both mixer output and input are visible
    if (row >= 0 && col >= 0) {
      gtk_grid_attach(
        grid, mg->widget,
        col + 1, row + row_offset, 1, 1
      );
    }
  }
}

// Update mixer window availability indication
void update_mixer_availability(struct alsa_card *card, int available) {
  if (!card->mixer_grid)
    return;

  gtk_widget_set_opacity(card->mixer_grid, available ? 1.0 : 0.3);

  if (card->mixer_unavailable_label)
    gtk_widget_set_visible(card->mixer_unavailable_label, !available);
}

// Recreate mixer widgets when stereo state changes
// This destroys existing widgets and creates new ones based on current stereo
void recreate_mixer_widgets(struct alsa_card *card) {
  if (!card || !card->mixer_grid)
    return;

  GtkGrid *grid = GTK_GRID(card->mixer_grid);

  // Clear existing mixer gain widgets
  for (GList *l = card->mixer_gain_widgets; l != NULL; l = l->next) {
    struct mixer_gain_widget *mg = l->data;
    if (mg->widget) {
      if (gtk_widget_get_parent(mg->widget) == GTK_WIDGET(grid))
        gtk_grid_remove(grid, mg->widget);
      cleanup_gain_widget(mg->widget);
      g_object_unref(mg->widget);
    }
    g_free(mg);
  }
  g_list_free(card->mixer_gain_widgets);
  card->mixer_gain_widgets = NULL;

  populate_mixer_gain_widgets(card);

  update_mixer_labels(card);
  rebuild_mixer_grid(card);
}

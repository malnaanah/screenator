/*
 * Screenator for Windows -- paper document, pages and PDF backgrounds.
 * GPL-2.0-or-later.  See app.h.
 *
 * The paper model keeps one full-screen annotation surface per page.  The
 * visible page is always g.backbuf; switching pages copies the live buffer
 * into the page and the target page into g.backbuf.  Page insert/delete have
 * their own one-level (actually four-level) undo/redo history, separate from
 * the raster drawing undo used by tools.c.
 *
 * PDF page backgrounds are rendered with PDFium to an ARGB32 image at the
 * current screen size, scaled uniformly and centred.  For saving, PDFium
 * imports the original PDF page as a form layer and a Cairo-generated
 * transparent annotation page is composed over it; the source page keeps its
 * original vector paths, fonts, text and images.  Plain/ruled/graph pages are
 * exported as real Cairo vector graphics.
 */

#include "app.h"
#include <shlwapi.h>
#include <fpdfview.h>
#include <fpdf_edit.h>
#include <fpdf_ppo.h>
#include <fpdf_transformpage.h>
#include <fpdf_save.h>
#include <fpdf_text.h>
#include <cairo/cairo-pdf.h>
#include <wchar.h>
#include <stdint.h>

#define PAPER_UNDO_MAX 4
#define PDF_CACHE_MAX  4

typedef struct
{
  Pt *pts;
  int n, cap;
  double width;
} EraseStroke;

typedef struct
{
  int base_w, base_h;
  double scale;
} PaperResizeState;

typedef struct
{
  Paper bg;                    /* PAPER_PLAIN/RULED/GRAPH/PDF */
  int pdf_page;                /* 0-based PDF page, or -1 */
  cairo_surface_t *ann;        /* annotation layer at screen size */
  cairo_surface_t *vec;        /* recording surface for vector export */
  FrameShape *texts;           /* committed text frames, for PDF export */
  int ntexts, textcap;
  int text_w, text_h;          /* coordinate space the text frames use */
  PaperResizeState resize;     /* keeps repeated window resizes reversible */
  int erased;                  /* eraser used: apply an eraser mask on export */
  int vec_dirty;               /* vector layer has at least one operation */
  EraseStroke *erases;         /* eraser brush strokes for the export mask */
  int nerases, erasecap;
} PaperPage;

typedef struct
{
  int kind;                    /* 1 = insert, 2 = delete */
  int index;                   /* page index the action applies to */
  int prev_index, new_index;   /* current page before/after the action */
  PaperPage page;              /* owned off-document page image, if any */
  int has_page;
} PaperAction;

typedef struct
{
  int index;
  cairo_surface_t *surf;
} PdfCacheEntry;

typedef struct
{
  wchar_t path[MAX_PATH];
  int page;                    /* 0-based */
} PdfHistoryEntry;

/* One saved paper document per paper type.  Switching type moves the live
 * pages into the store and restores the target type's pages, so Plain, Ruled,
 * Graph and PDF each keep their own pages and annotations. */
typedef struct
{
  int ready;
  int count;
  int index;
  int changes_pending;
  PaperPage pages[PAPER_MAX_PAGES];
  wchar_t pdf_path[MAX_PATH];
} PaperDocStore;

static PaperDocStore doc_store[PAPER_PDF + 1];
static Paper current_type = PAPER_PLAIN;

static PaperPage pages[PAPER_MAX_PAGES];
static int page_count;
static int page_index;
static int doc_ready;
static int paper_changes_pending;
static cairo_surface_t *screen_ann;
static int paper_scroll_y;
static ULONGLONG paper_scroll_paint_at;

static FrameShape *text_undo[MAX_UNDO];
static int text_undo_n[MAX_UNDO];
static int text_undo_w[MAX_UNDO], text_undo_h[MAX_UNDO];
static FrameShape *text_redo[MAX_UNDO];
static int text_redo_n[MAX_UNDO];
static int text_redo_w[MAX_UNDO], text_redo_h[MAX_UNDO];
static int vec_undo_erased[MAX_UNDO], vec_redo_erased[MAX_UNDO];
static int vec_undo_dirty[MAX_UNDO], vec_redo_dirty[MAX_UNDO];
static PaperResizeState vec_undo_resize[MAX_UNDO];
static PaperResizeState vec_redo_resize[MAX_UNDO];
static EraseStroke *er_undo[MAX_UNDO];
static int er_undo_n[MAX_UNDO];
static int er_undo_w[MAX_UNDO], er_undo_h[MAX_UNDO];
static EraseStroke *er_redo[MAX_UNDO];
static int er_redo_n[MAX_UNDO];
static int er_redo_w[MAX_UNDO], er_redo_h[MAX_UNDO];

#define PAPER_CONT_GAP 18

static PaperAction undo_stack[PAPER_UNDO_MAX];
static PaperAction redo_stack[PAPER_UNDO_MAX];
static int undo_depth, redo_depth;
static int page_change_last;

/* PDF document and cached rendered page images. */
static FPDF_DOCUMENT pdf_doc;
static wchar_t pdf_path[MAX_PATH];
static int pdf_npages;
/* Height/width ratio of the loaded PDF's first page.  PDF pages use their own
 * paper size and ignore the paper-size setting. */
static double pdf_page_aspect;
static PdfCacheEntry pdf_cache[PDF_CACHE_MAX];
static int pdf_cache_next;

/* Metadata restored from the config file; used when Paper is first entered. */
static Paper restored_bg;
static Paper restored_types[PAPER_MAX_PAGES];
static int restored_ntypes;
static int restored_count;
static int restored_index;
static wchar_t restored_pdf_path[MAX_PATH];
static int restored_ready;

/* Last page for each recently opened PDF. */
static PdfHistoryEntry pdf_history[32];
static int pdf_history_count;
static int pdf_history_loaded;
static int paper_test_mode;

/* ------------------------------------------------------------------ */
/* small helpers                                                      */
/* ------------------------------------------------------------------ */

static wchar_t *paper_wide_from_utf8(const char *s)
{
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  wchar_t *w = (wchar_t *) malloc((size_t) n * sizeof(wchar_t));
  if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}

static char *paper_utf8_from_wide(const wchar_t *w)
{
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  char *s = (char *) malloc((size_t) n);
  if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
  return s;
}

static int paper_surface_w(void)
{
  int w = app_view_w();
  return w > 0 ? w : 1920;
}

static int paper_surface_h(void)
{
  int vw = app_view_w();
  int vh = app_view_h();
  int h;

  if (current_type == PAPER_PDF && pdf_doc && pdf_page_aspect > 0.01)
    {
      /* An opened PDF keeps its own page proportions; the paper-size setting
       * does not apply to it.  The width still fills the screen. */
      h = (int) ((double) vw * pdf_page_aspect + 0.5);
    }
  else if (g.paper_size_kind == PAPER_SIZE_SCREEN)
    h = vh;
  else
    {
      double aspect;
      if (g.paper_size_kind == PAPER_SIZE_A4_PORTRAIT)
        aspect = 297.0 / 210.0;
      else if (g.paper_size_kind == PAPER_SIZE_A4_LANDSCAPE)
        aspect = 210.0 / 297.0;
      else if (g.paper_custom_w_mm > 0 && g.paper_custom_h_mm > 0)
        aspect = (double) g.paper_custom_h_mm /
                 (double) g.paper_custom_w_mm;
      else
        aspect = (double) vh / (double) vw;
      h = (int) ((double) vw * aspect + 0.5);
    }
  if (h < 1) h = 1;
  if (h > 8192) h = 8192;
  return h;
}

int paper_page_width(void)
{
  return paper_surface_w();
}

int paper_page_height(void)
{
  return paper_surface_h();
}

static void paper_resize_state_init(PaperPage *p)
{
  int w = 0, h = 0;
  if (!p || (p->resize.base_w > 0 && p->resize.base_h > 0)) return;
  if (p->ann && cairo_surface_get_type(p->ann) == CAIRO_SURFACE_TYPE_IMAGE)
    {
      w = cairo_image_surface_get_width(p->ann);
      h = cairo_image_surface_get_height(p->ann);
    }
  if ((w <= 0 || h <= 0) && p->text_w > 0 && p->text_h > 0)
    { w = p->text_w; h = p->text_h; }
  if (w <= 0 || h <= 0)
    { w = paper_surface_w(); h = paper_surface_h(); }
  p->resize.base_w = w;
  p->resize.base_h = h;
  p->resize.scale = 1.0;
}

static double paper_resize_state_step(PaperResizeState *state, int w, int h)
{
  double target, sx, sy, current;
  if (!state || w <= 0 || h <= 0) return 1.0;
  if (state->base_w <= 0 || state->base_h <= 0)
    {
      state->base_w = w;
      state->base_h = h;
      state->scale = 1.0;
    }
  sx = (double) w / (double) state->base_w;
  sy = (double) h / (double) state->base_h;
  target = sx < sy ? sx : sy;
  if (target <= 0.0) target = 1.0;
  current = state->scale > 0.0 ? state->scale : 1.0;
  state->scale = target;
  return target / current;
}

static void paper_clear_surface(cairo_surface_t *s);
static void paper_copy_surface(cairo_surface_t *dst, cairo_surface_t *src);
static cairo_surface_t *pdf_render_page(int index, int w, int h);
static void pdf_history_remember(const wchar_t *path, int page);
static void paper_remember_pdf_page(void);
static FrameShape *paper_copy_texts(const FrameShape *src, int n, int *nout);
static void paper_free_texts(FrameShape **list, int *count, int *cap);
static EraseStroke *paper_copy_erases(const EraseStroke *src, int n,
                                      int *nout);
static void paper_free_erases(EraseStroke **list, int *count);
static void paper_clear_store(Paper type);
static void paper_store_active(void);
static int paper_restore_doc(Paper type);
static void pdf_close(void);
static int paper_load_pdf_document(const wchar_t *path, char *error,
                                   int error_size);
static void paper_clear_active_pages(void);
static void paper_resize_restored(void);

static int paper_stride(void)
{
  return paper_surface_h() + PAPER_CONT_GAP;
}

static int paper_page_top(int index)
{
  /* No gap before the first page: the strip starts at the page's top edge. */
  return index * paper_stride();
}

static int paper_doc_height(void)
{
  if (page_count <= 0) return paper_surface_h();
  /* The inter-page gap is part of the stride, so there is no extra space
   * before the first page or after the last one. */
  return paper_page_top(page_count - 1) + paper_surface_h();
}

static void paper_scroll_clamp(void)
{
  int view = app_paper_viewport_h();
  if (view < 1) view = 1080;
  int max = paper_doc_height() - view;
  if (max < 0) max = 0;
  if (paper_scroll_y > max) paper_scroll_y = max;
  if (paper_scroll_y < 0) paper_scroll_y = 0;
}

void paper_scroll_reclamp(void)
{
  paper_scroll_clamp();
}

static int paper_page_at_view_y(int y)
{
  int raw = y + paper_scroll_y;
  int stride = paper_stride();
  int idx, within;

  if (raw <= 0) return 0;
  idx = raw / stride;
  within = raw % stride;
  if (within >= paper_surface_h()) idx++;
  if (idx < 0) idx = 0;
  if (idx >= page_count) idx = page_count - 1;
  return idx;
}

int paper_continuous_active(void)
{
  int tall = g.paper_size_kind != PAPER_SIZE_SCREEN;
  if (doc_ready && current_type == PAPER_PDF && pdf_doc &&
      pdf_page_aspect > 0.01)
    tall = paper_surface_h() > app_paper_viewport_h();
  return g.paper != PAPER_NONE && doc_ready && page_count > 0 &&
         (g.paper_continuous_scroll || tall);
}

int paper_current_page_view_top(void)
{
  if (!paper_continuous_active()) return 0;
  return paper_page_top(page_index) - paper_scroll_y;
}

/* Page number to show for the current viewport.  In the continuous strip
 * this follows scrolling instead of waiting for a click. */
int paper_view_page_index(void)
{
  if (!paper_continuous_active()) return page_index;
  int view_h = app_paper_viewport_h();
  int view_top = paper_scroll_y;
  int view_bottom = view_top + view_h;
  int center_page = paper_page_at_view_y(view_h / 2);
  int best = center_page;
  int best_visible = -1;

  if (view_h <= 0) return best;
  for (int i = 0; i < page_count; i++)
    {
      int page_top = paper_page_top(i);
      int page_bottom = page_top + paper_surface_h();
      int visible_top = page_top > view_top ? page_top : view_top;
      int visible_bottom = page_bottom < view_bottom ? page_bottom : view_bottom;
      int visible = visible_bottom - visible_top;
      if (visible > best_visible ||
          (visible == best_visible && i == center_page))
        {
          best = i;
          best_visible = visible;
        }
    }
  return best;
}

/* Make the page occupying the largest part of the viewport the active page
 * before a page-wide action such as Clear Screen.  Keep the scroll position
 * unchanged when moving the live annotation buffer to that page. */
void paper_select_view_page(void)
{
  int index;
  if (!doc_ready || !paper_continuous_active()) return;
  index = paper_view_page_index();
  if (index < 0 || index >= page_count || index == page_index) return;

  paper_sync_current();
  page_index = index;
  paper_reload_current();
  if (pages[page_index].bg == PAPER_PDF && pdf_path[0])
    pdf_history_remember(pdf_path, page_index);
  app_render_all();
  ui_refresh_tray();
}

void paper_map_point(Pt *p)
{
  if (!paper_continuous_active() || !p) return;
  p->y -= paper_current_page_view_top();
}

void paper_prepare_point(Pt *p)
{
  int idx;

  if (!paper_continuous_active() || !p) return;
  idx = paper_page_at_view_y((int) p->y - g.vy);
  if (idx != page_index)
    {
      tools_commit(0);
      paper_sync_current();
      page_index = idx;
      paper_reload_current();
      if (pages[page_index].bg == PAPER_PDF && pdf_path[0])
        pdf_history_remember(pdf_path, page_index);
      app_render_all();
      ui_refresh_tray();
    }
  paper_map_point(p);
}

/* Vertical distance between two consecutive page origins in the strip. */
int paper_page_stride(void)
{
  return paper_stride();
}

/* The page index under a viewport point, without changing anything. */
int paper_page_at_view(Pt p)
{
  if (!paper_continuous_active()) return page_index;
  return paper_page_at_view_y((int) p.y - g.vy);
}

/* Switch the current page without scrolling the strip.  Used when a stroke
 * crosses onto the adjacent page so the rest lands on the correct page. */
void paper_switch_page_index(int idx)
{
  if (!doc_ready || idx < 0 || idx >= page_count || idx == page_index) return;
  paper_sync_current();
  page_index = idx;
  paper_reload_current();
  if (pages[page_index].bg == PAPER_PDF && pdf_path[0])
    pdf_history_remember(pdf_path, page_index);
  ui_refresh_tray();
}

int paper_scroll_step(int direction)
{
  int before, step;

  if (!paper_continuous_active()) return 0;
  before = paper_scroll_y;
  step = paper_surface_h() / 6;
  if (step < 48) step = 48;
  paper_scroll_y += direction * step;
  paper_scroll_clamp();
  if (paper_scroll_y == before) return 0;
  app_render_all();
  toolbar_refresh();
  return 1;
}

int paper_scroll_pixels(int delta)
{
  int before;
  ULONGLONG now;

  if (!paper_continuous_active()) return 0;
  before = paper_scroll_y;
  paper_scroll_y += delta;
  paper_scroll_clamp();
  if (paper_scroll_y == before) return 0;
  toolbar_refresh();
  /* Paint directly while dragging.  WM_TIMER is starved while pen pointer
   * messages stream in, so a deferred repaint would only appear after the
   * button is released.  Limit the repaint rate instead of dropping it. */
  now = GetTickCount64();
  if (!paper_scroll_paint_at || now - paper_scroll_paint_at >= 16)
    {
      paper_scroll_paint_at = now;
      app_render_all();
    }
  return 1;
}

void paper_set_continuous(int enabled)
{
  enabled = enabled ? 1 : 0;
  if (g.paper_continuous_scroll == enabled) return;
  tools_commit(0);
  paper_sync_current();
  g.paper_continuous_scroll = enabled;
  if (enabled)
    paper_scroll_y = paper_page_top(page_index);
  else
    paper_scroll_y = 0;
  paper_scroll_clamp();
  app_render_all();
  ui_refresh_tray();
}

void paper_draw_continuous(cairo_t *cr, int w, int h)
{
  int i;

  cairo_save(cr);
  cairo_set_source_rgb(cr, 0.90, 0.90, 0.90);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);
  cairo_restore(cr);

  if (!doc_ready || page_count <= 0) return;

  for (i = 0; i < page_count; i++)
    {
      int top = paper_page_top(i) - paper_scroll_y;
      Paper bg;
      cairo_surface_t *ann;

      if (top >= h || top + paper_surface_h() <= 0) continue;

      bg = pages[i].bg;
      cairo_save(cr);
      cairo_rectangle(cr, 0, top, w, paper_surface_h());
      cairo_clip(cr);

      if (bg == PAPER_PDF)
        {
          cairo_surface_t *pdf =
            pdf_render_page(pages[i].pdf_page, w, paper_surface_h());
          if (pdf)
            {
              cairo_set_source_surface(cr, pdf, 0, top);
              cairo_paint(cr);
            }
          else
            {
              cairo_set_source_rgb(cr, 1, 1, 1);
              cairo_rectangle(cr, 0, top, w, paper_surface_h());
              cairo_fill(cr);
            }
        }
      else
        {
          cairo_save(cr);
          cairo_translate(cr, 0, top);
          draw_paper(cr, bg, w, paper_surface_h());
          cairo_restore(cr);
        }

      ann = (i == page_index && g.backbuf) ? g.backbuf : pages[i].ann;
      if (ann && !g.hide_drawings)
        {
          cairo_set_source_surface(cr, ann, 0, top);
          cairo_paint(cr);
        }
      cairo_restore(cr);

      /* Page edge shading.  The separator line and the "Page x / y" label
       * between pages were removed. */
      cairo_save(cr);
      cairo_set_source_rgba(cr, 0.55, 0.55, 0.55, 0.35);
      cairo_set_line_width(cr, 1.0);
      cairo_rectangle(cr, 0.5, top + 0.5, w - 1.0,
                      paper_surface_h() - 1.0);
      cairo_stroke(cr);
      cairo_restore(cr);
    }
}

static cairo_surface_t *paper_new_surface(void)
{
  return cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                    paper_surface_w(), paper_surface_h());
}

/* A recording surface holds the same drawing operations as the raster
 * annotation layer, but keeps strokes as paths and text as text so the
 * exported PDF stays vector. */
static cairo_surface_t *paper_new_vector(void)
{
  cairo_rectangle_t ext;
  ext.x = 0;
  ext.y = 0;
  ext.width = paper_surface_w();
  ext.height = paper_surface_h();
  return cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, &ext);
}

static cairo_surface_t *paper_copy_vector(cairo_surface_t *src)
{
  cairo_rectangle_t ext;
  cairo_surface_t *dst;
  cairo_t *cr;

  if (!src || cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) return NULL;
  if (!cairo_recording_surface_get_extents(src, &ext)) return NULL;
  dst = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, &ext);
  if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy(dst);
      return NULL;
    }
  cr = cairo_create(dst);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  return dst;
}

static cairo_surface_t *paper_scale_vector_factor(cairo_surface_t *src,
                                                  int dw, int dh,
                                                  double factor)
{
  cairo_rectangle_t ext, dst_ext;
  cairo_surface_t *dst;
  cairo_t *cr;
  if (!src || dw <= 0 || dh <= 0 || factor <= 0.0 ||
      !cairo_recording_surface_get_extents(src, &ext))
    return NULL;
  dst_ext.x = 0;
  dst_ext.y = 0;
  dst_ext.width = dw;
  dst_ext.height = dh;
  dst = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, &dst_ext);
  if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy(dst);
      return NULL;
    }
  cr = cairo_create(dst);
  cairo_scale(cr, factor, factor);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  return dst;
}

/* Context for recording the next annotation operation on the current paper
 * page.  Returns NULL outside Paper mode. */
cairo_t *paper_vector_context(void)
{
  PaperPage *p;
  cairo_t *cr;

  if (!doc_ready || g.paper == PAPER_NONE || page_index < 0 ||
      page_index >= page_count)
    return NULL;
  p = &pages[page_index];
  if (!p->vec) p->vec = paper_new_vector();
  if (!p->vec) return NULL;
  paper_changes_pending = 1;
  p->vec_dirty = 1;
  if (p->text_w <= 0 || p->text_h <= 0)
    {
      p->text_w = paper_surface_w();
      p->text_h = paper_surface_h();
    }
  cr = cairo_create(p->vec);
  cairo_rectangle(cr, 0, 0, paper_surface_w(), paper_surface_h());
  cairo_clip(cr);
  return cr;
}

void paper_vector_undo_push(int slot)
{
  if (!doc_ready || g.paper == PAPER_NONE || slot < 0 || slot >= MAX_UNDO)
    return;
  if (g.vec_undo[slot])
    {
      cairo_surface_destroy(g.vec_undo[slot]);
      g.vec_undo[slot] = NULL;
    }
  if (page_index >= 0 && page_index < page_count)
    {
      PaperPage *p = &pages[page_index];
      paper_resize_state_init(p);
      vec_undo_resize[slot] = p->resize;
      g.vec_undo[slot] = paper_copy_vector(p->vec);
      paper_free_texts(&text_undo[slot], &text_undo_n[slot], NULL);
      text_undo[slot] = paper_copy_texts(p->texts, p->ntexts,
                                         &text_undo_n[slot]);
      text_undo_w[slot] = p->text_w;
      text_undo_h[slot] = p->text_h;
      vec_undo_erased[slot] = p->erased;
      vec_undo_dirty[slot] = p->vec_dirty;
      paper_free_erases(&er_undo[slot], &er_undo_n[slot]);
      er_undo[slot] = paper_copy_erases(p->erases, p->nerases,
                                        &er_undo_n[slot]);
      er_undo_w[slot] = p->text_w > 0 ? p->text_w : paper_surface_w();
      er_undo_h[slot] = p->text_h > 0 ? p->text_h : paper_surface_h();
    }
}

void paper_vector_capture_redo(int slot)
{
  if (!doc_ready || g.paper == PAPER_NONE || slot < 0 || slot >= MAX_UNDO)
    return;
  if (g.vec_redo[slot])
    {
      cairo_surface_destroy(g.vec_redo[slot]);
      g.vec_redo[slot] = NULL;
    }
  if (page_index >= 0 && page_index < page_count)
    {
      PaperPage *p = &pages[page_index];
      paper_resize_state_init(p);
      vec_redo_resize[slot] = p->resize;
      g.vec_redo[slot] = paper_copy_vector(p->vec);
      paper_free_texts(&text_redo[slot], &text_redo_n[slot], NULL);
      text_redo[slot] = paper_copy_texts(p->texts, p->ntexts,
                                         &text_redo_n[slot]);
      text_redo_w[slot] = p->text_w;
      text_redo_h[slot] = p->text_h;
      vec_redo_erased[slot] = p->erased;
      vec_redo_dirty[slot] = p->vec_dirty;
      paper_free_erases(&er_redo[slot], &er_redo_n[slot]);
      er_redo[slot] = paper_copy_erases(p->erases, p->nerases,
                                        &er_redo_n[slot]);
      er_redo_w[slot] = p->text_w > 0 ? p->text_w : paper_surface_w();
      er_redo_h[slot] = p->text_h > 0 ? p->text_h : paper_surface_h();
    }
}

void paper_vector_swap_undo(int slot)
{
  cairo_surface_t *tmp;
  FrameShape *ttmp;
  int tn, tw, th;

  if (!doc_ready || g.paper == PAPER_NONE || slot < 0 || slot >= MAX_UNDO)
    return;
  if (page_index < 0 || page_index >= page_count) return;
  tmp = pages[page_index].vec;
  pages[page_index].vec = g.vec_undo[slot];
  g.vec_undo[slot] = tmp;

  ttmp = pages[page_index].texts;
  tn = pages[page_index].ntexts;
  tw = pages[page_index].text_w;
  th = pages[page_index].text_h;
  pages[page_index].texts = text_undo[slot];
  pages[page_index].ntexts = text_undo_n[slot];
  pages[page_index].textcap = text_undo_n[slot];
  pages[page_index].text_w = text_undo_w[slot];
  pages[page_index].text_h = text_undo_h[slot];
  text_undo[slot] = ttmp;
  text_undo_n[slot] = tn;
  text_undo_w[slot] = tw;
  text_undo_h[slot] = th;
  {
    int e = pages[page_index].erased;
    int d = pages[page_index].vec_dirty;
    pages[page_index].erased = vec_undo_erased[slot];
    pages[page_index].vec_dirty = vec_undo_dirty[slot];
    vec_undo_erased[slot] = e;
    vec_undo_dirty[slot] = d;
  }
  {
    EraseStroke *ep = pages[page_index].erases;
    int en = pages[page_index].nerases;
    pages[page_index].erases = er_undo[slot];
    pages[page_index].nerases = er_undo_n[slot];
    pages[page_index].erasecap = er_undo_n[slot];
    er_undo[slot] = ep;
    er_undo_n[slot] = en;
  }
}

void paper_vector_swap_redo(int slot)
{
  cairo_surface_t *tmp;
  FrameShape *ttmp;
  int tn, tw, th;

  if (!doc_ready || g.paper == PAPER_NONE || slot < 0 || slot >= MAX_UNDO)
    return;
  if (page_index < 0 || page_index >= page_count) return;
  tmp = pages[page_index].vec;
  pages[page_index].vec = g.vec_redo[slot];
  g.vec_redo[slot] = tmp;

  ttmp = pages[page_index].texts;
  tn = pages[page_index].ntexts;
  tw = pages[page_index].text_w;
  th = pages[page_index].text_h;
  pages[page_index].texts = text_redo[slot];
  pages[page_index].ntexts = text_redo_n[slot];
  pages[page_index].textcap = text_redo_n[slot];
  pages[page_index].text_w = text_redo_w[slot];
  pages[page_index].text_h = text_redo_h[slot];
  text_redo[slot] = ttmp;
  text_redo_n[slot] = tn;
  text_redo_w[slot] = tw;
  text_redo_h[slot] = th;
  {
    int e = pages[page_index].erased;
    int d = pages[page_index].vec_dirty;
    pages[page_index].erased = vec_redo_erased[slot];
    pages[page_index].vec_dirty = vec_redo_dirty[slot];
    vec_redo_erased[slot] = e;
    vec_redo_dirty[slot] = d;
  }
  {
    EraseStroke *ep = pages[page_index].erases;
    int en = pages[page_index].nerases;
    pages[page_index].erases = er_redo[slot];
    pages[page_index].nerases = er_redo_n[slot];
    pages[page_index].erasecap = er_redo_n[slot];
    er_redo[slot] = ep;
    er_redo_n[slot] = en;
  }
}

void paper_vector_undo_clear(void)
{
  for (int i = 0; i < MAX_UNDO; i++)
    {
      if (g.vec_undo[i])
        {
          cairo_surface_destroy(g.vec_undo[i]);
          g.vec_undo[i] = NULL;
        }
      if (g.vec_redo[i])
        {
          cairo_surface_destroy(g.vec_redo[i]);
          g.vec_redo[i] = NULL;
        }
      paper_free_texts(&text_undo[i], &text_undo_n[i], NULL);
      paper_free_texts(&text_redo[i], &text_redo_n[i], NULL);
      paper_free_erases(&er_undo[i], &er_undo_n[i]);
      paper_free_erases(&er_redo[i], &er_redo_n[i]);
      text_undo_w[i] = text_undo_h[i] = 0;
      text_redo_w[i] = text_redo_h[i] = 0;
      er_undo_w[i] = er_undo_h[i] = 0;
      er_redo_w[i] = er_redo_h[i] = 0;
      memset(&vec_undo_resize[i], 0, sizeof(vec_undo_resize[i]));
      memset(&vec_redo_resize[i], 0, sizeof(vec_redo_resize[i]));
    }
}

/* Text frames are not replayed from a recording surface: Cairo's recording
 * surface drops text when it is painted into a PDF surface on this backend.
 * Keeping the frames themselves lets the export redraw them directly, which
 * preserves real PDF text. */

static void paper_free_texts(FrameShape **list, int *count, int *cap)
{
  if (!list || !*list) { if (count) *count = 0; if (cap) *cap = 0; return; }
  for (int i = 0; i < (count ? *count : 0); i++)
    {
      free((*list)[i].text);
      free((*list)[i].font);
    }
  free(*list);
  *list = NULL;
  if (count) *count = 0;
  if (cap) *cap = 0;
}

static FrameShape *paper_copy_texts(const FrameShape *src, int n, int *nout)
{
  FrameShape *dst;
  if (nout) *nout = 0;
  if (!src || n <= 0) return NULL;
  dst = (FrameShape *) calloc((size_t) n, sizeof(FrameShape));
  if (!dst) return NULL;
  for (int i = 0; i < n; i++)
    {
      dst[i] = src[i];
      dst[i].text = src[i].text ? _strdup(src[i].text) : NULL;
      dst[i].font = src[i].font ? _strdup(src[i].font) : NULL;
    }
  if (nout) *nout = n;
  return dst;
}

/* Eraser brushes are kept as vector polylines and rendered into an alpha
 * mask at export time, so annotations stay vector/text. */
static int erase_open_idx = -1;
static int erase_open_page = -1;

static EraseStroke *paper_erase_new_stroke(PaperPage *p, double width)
{
  EraseStroke *s;
  if (p->nerases >= p->erasecap)
    {
      int cap = p->erasecap ? p->erasecap * 2 : 8;
      EraseStroke *n = (EraseStroke *) realloc(
        p->erases, (size_t) cap * sizeof(EraseStroke));
      if (!n) return NULL;
      p->erases = n;
      p->erasecap = cap;
    }
  s = &p->erases[p->nerases];
  memset(s, 0, sizeof(*s));
  s->width = width > 0 ? width : 1;
  p->nerases++;
  return s;
}

static void paper_erase_add(EraseStroke *s, Pt p)
{
  if (s->n >= s->cap)
    {
      int cap = s->cap ? s->cap * 2 : 32;
      Pt *n = (Pt *) realloc(s->pts, (size_t) cap * sizeof(Pt));
      if (!n) return;
      s->pts = n;
      s->cap = cap;
    }
  s->pts[s->n++] = p;
}

void paper_erase_add_point(Pt p, double width)
{
  PaperPage *pg;
  EraseStroke *s;

  if (!doc_ready || g.paper == PAPER_NONE || page_index < 0 ||
      page_index >= page_count)
    return;
  pg = &pages[page_index];
  if (pg->text_w <= 0 || pg->text_h <= 0)
    {
      pg->text_w = paper_surface_w();
      pg->text_h = paper_surface_h();
    }
  if (erase_open_idx < 0 || erase_open_page != page_index ||
      erase_open_idx >= pg->nerases ||
      pg->erases[erase_open_idx].width != width)
    {
      s = paper_erase_new_stroke(pg, width);
      if (!s) return;
      erase_open_idx = pg->nerases - 1;
      erase_open_page = page_index;
    }
  else
    s = &pg->erases[erase_open_idx];
  paper_erase_add(s, p);
  pg->erased = 1;
}

void paper_erase_add_span(int x1, int y1, int x2, int y2, double width)
{
  PaperPage *pg;
  EraseStroke *s;

  if (!doc_ready || g.paper == PAPER_NONE || page_index < 0 ||
      page_index >= page_count)
    return;
  pg = &pages[page_index];
  if (pg->text_w <= 0 || pg->text_h <= 0)
    {
      pg->text_w = paper_surface_w();
      pg->text_h = paper_surface_h();
    }
  s = paper_erase_new_stroke(pg, width);
  if (!s) return;
  paper_erase_add(s, (Pt) { (double) x1, (double) y1 });
  paper_erase_add(s, (Pt) { (double) x2, (double) y2 });
  pg->erased = 1;
}

void paper_erase_end(void)
{
  erase_open_idx = -1;
  erase_open_page = -1;
}

void paper_add_text(const FrameShape *shape){
  PaperPage *p;

  if (!doc_ready || g.paper == PAPER_NONE || !shape || !shape->text ||
      !shape->text[0] || page_index < 0 || page_index >= page_count)
    return;
  p = &pages[page_index];
  if (p->ntexts >= p->textcap)
    {
      int cap = p->textcap ? p->textcap * 2 : 8;
      FrameShape *n = (FrameShape *) realloc(p->texts,
                                             (size_t) cap * sizeof(FrameShape));
      if (!n) return;
      p->texts = n;
      p->textcap = cap;
    }
  if (p->text_w <= 0 || p->text_h <= 0)
    {
      p->text_w = paper_surface_w();
      p->text_h = paper_surface_h();
    }
  p->texts[p->ntexts] = *shape;
  p->texts[p->ntexts].text = _strdup(shape->text);
  p->texts[p->ntexts].font = shape->font ? _strdup(shape->font) : NULL;
  p->ntexts++;
  paper_changes_pending = 1;
  p->vec_dirty = 1;
}

static void paper_draw_page_texts(cairo_t *cr, int index)
{
  PaperPage *p;
  if (!doc_ready || index < 0 || index >= page_count) return;
  p = &pages[index];
  for (int i = 0; i < p->ntexts; i++)
    draw_frame_shape(cr, &p->texts[i]);
}

static void paper_scale_texts_factor(FrameShape *list, int n, double factor)
{
  if (!list || n <= 0 || factor <= 0.0) return;
  for (int i = 0; i < n; i++)
    {
      list[i].frame.cx *= factor;
      list[i].frame.cy *= factor;
      list[i].frame.w *= factor;
      list[i].frame.h *= factor;
      list[i].font_size *= factor;
    }
}

static EraseStroke *paper_copy_erases(const EraseStroke *src, int n,
                                      int *nout)
{
  EraseStroke *dst;

  if (nout) *nout = 0;
  if (!src || n <= 0) return NULL;
  dst = (EraseStroke *) calloc((size_t) n, sizeof(EraseStroke));
  if (!dst) return NULL;
  for (int i = 0; i < n; i++)
    {
      dst[i].width = src[i].width;
      dst[i].n = src[i].n;
      dst[i].cap = src[i].n;
      if (src[i].n > 0)
        {
          dst[i].pts = (Pt *) malloc(sizeof(Pt) * (size_t) src[i].n);
          if (!dst[i].pts)
            {
              for (int j = 0; j < i; j++) free(dst[j].pts);
              free(dst);
              return NULL;
            }
          memcpy(dst[i].pts, src[i].pts,
                 sizeof(Pt) * (size_t) src[i].n);
        }
    }
  if (nout) *nout = n;
  return dst;
}

static void paper_free_erases(EraseStroke **list, int *count)
{
  if (!list || !*list) { if (count) *count = 0; return; }
  for (int i = 0; i < (count ? *count : 0); i++)
    free((*list)[i].pts);
  free(*list);
  *list = NULL;
  if (count) *count = 0;
}

static void paper_scale_erases_factor(EraseStroke *list, int n,
                                      double factor)
{
  if (!list || n <= 0 || factor <= 0.0) return;
  for (int i = 0; i < n; i++)
    {
      for (int j = 0; j < list[i].n; j++)
        {
          list[i].pts[j].x *= factor;
          list[i].pts[j].y *= factor;
        }
      list[i].width *= factor;
      if (list[i].width < 1.0) list[i].width = 1.0;
    }
}

/* Rebuild the screen-sized annotation cache from its vector source after a
 * page resize.  Scaling the old image repeatedly softens every stroke. */
static cairo_surface_t *paper_render_annotations(PaperPage *p, int w, int h)
{
  cairo_surface_t *surface;
  cairo_t *cr;

  if (!p || (!p->vec && p->ntexts <= 0) || p->nerases > 0 || w <= 0 || h <= 0)
    return NULL;
  surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy(surface);
      return NULL;
    }
  cr = cairo_create(surface);
  if (p->vec)
    {
      cairo_set_source_surface(cr, p->vec, 0, 0);
      cairo_paint(cr);
    }
  for (int i = 0; i < p->ntexts; i++)
    draw_frame_shape(cr, &p->texts[i]);
  cairo_destroy(cr);
  cairo_surface_mark_dirty(surface);
  return surface;
}

static void paper_save_screen_layer(void)
{
  if (!g.backbuf) return;
  if (!screen_ann) screen_ann = paper_new_surface();
  if (screen_ann) paper_copy_surface(screen_ann, g.backbuf);
}

static void paper_restore_screen_layer(void)
{
  if (!g.backbuf || !screen_ann) return;
  paper_clear_surface(g.backbuf);
  paper_copy_surface(g.backbuf, screen_ann);
  cairo_surface_mark_dirty(g.backbuf);
}

/* The screen annotation layer, independent of the paper page currently shown.
 * Used when saving annotations on exit while Paper mode is active. */
cairo_surface_t *paper_screen_annotations(void)
{
  if (g.paper != PAPER_NONE && screen_ann) return screen_ann;
  return g.backbuf;
}

static void paper_free_page(PaperPage *p)
{
  if (!p) return;
  if (p->ann)
    {
      cairo_surface_destroy(p->ann);
      p->ann = NULL;
    }
  if (p->vec)
    {
      cairo_surface_destroy(p->vec);
      p->vec = NULL;
    }
  if (p->texts)
    {
      for (int i = 0; i < p->ntexts; i++)
        {
          free(p->texts[i].text);
          free(p->texts[i].font);
        }
      free(p->texts);
      p->texts = NULL;
    }
  p->ntexts = p->textcap = 0;
  p->text_w = p->text_h = 0;
  memset(&p->resize, 0, sizeof(p->resize));
  p->erased = 0;
  p->vec_dirty = 0;
  if (p->erases)
    {
      for (int i = 0; i < p->nerases; i++) free(p->erases[i].pts);
      free(p->erases);
      p->erases = NULL;
    }
  p->nerases = p->erasecap = 0;
  p->bg = PAPER_PLAIN;
  p->pdf_page = -1;
}

static void paper_ensure_page(PaperPage *p)
{
  if (!p->ann) p->ann = paper_new_surface();
}

static void paper_clear_store(Paper type)
{
  PaperDocStore *s;

  if (type < PAPER_PLAIN || type > PAPER_PDF) return;
  s = &doc_store[type];
  for (int i = 0; i < s->count; i++) paper_free_page(&s->pages[i]);
  memset(s->pages, 0, sizeof(s->pages));
  s->count = 0;
  s->index = 0;
  s->ready = 0;
  s->changes_pending = 0;
  s->pdf_path[0] = 0;
}

static void paper_clear_active_pages(void)
{
  for (int i = 0; i < page_count; i++) paper_free_page(&pages[i]);
  memset(pages, 0, sizeof(pages));
  page_count = 0;
  page_index = 0;
  doc_ready = 0;
  paper_changes_pending = 0;
}

/* Move the live pages into the store for their type. */
static void paper_store_active(void)
{
  Paper t = current_type;
  PaperDocStore *s;

  if (!doc_ready || page_count <= 0) return;
  if (t < PAPER_PLAIN || t > PAPER_PDF) t = PAPER_PLAIN;
  int changes_pending = paper_changes_pending;
  paper_clear_store(t);
  s = &doc_store[t];
  memcpy(s->pages, pages, sizeof(PaperPage) * (size_t) page_count);
  s->count = page_count;
  s->index = page_index;
  s->ready = 1;
  s->changes_pending = changes_pending;
  if (t == PAPER_PDF)
    {
      wcsncpy(s->pdf_path, pdf_path, MAX_PATH - 1);
      s->pdf_path[MAX_PATH - 1] = 0;
    }
  memset(pages, 0, sizeof(PaperPage) * (size_t) page_count);
  page_count = 0;
  page_index = 0;
  doc_ready = 0;
}

static void paper_resize_restored(void)
{
  int need = 0;
  int w = paper_surface_w();
  int h = paper_surface_h();

  for (int i = 0; i < page_count && !need; i++)
    {
      if (pages[i].ann &&
          (cairo_image_surface_get_width(pages[i].ann) != w ||
           cairo_image_surface_get_height(pages[i].ann) != h))
        need = 1;
      else if (pages[i].text_w > 0 &&
               (pages[i].text_w != w || pages[i].text_h != h))
        need = 1;
    }
  if (need) paper_resize_all(w, h);
}

/* Bring a stored document back into the live page array. */
static int paper_restore_doc(Paper type)
{
  PaperDocStore *s;

  if (type < PAPER_PLAIN || type > PAPER_PDF) return 0;
  s = &doc_store[type];
  if (!s->ready || s->count <= 0) return 0;

  memcpy(pages, s->pages, sizeof(PaperPage) * (size_t) s->count);
  memset(s->pages, 0, sizeof(PaperPage) * (size_t) s->count);
  page_count = s->count;
  page_index = s->index;
  if (page_index < 0 || page_index >= page_count) page_index = 0;
  doc_ready = 1;
  current_type = type;
  paper_changes_pending = s->changes_pending;
  s->ready = 0;
  s->count = 0;
  s->index = 0;
  s->changes_pending = 0;

  if (type == PAPER_PDF)
    {
      int reload = !pdf_doc || _wcsicmp(pdf_path, s->pdf_path) != 0;
      if (reload && s->pdf_path[0])
        {
          char err[384] = "";
          if (!paper_load_pdf_document(s->pdf_path, err, sizeof(err)))
            pdf_close();
        }
      else
        {
          wcsncpy(pdf_path, s->pdf_path, MAX_PATH - 1);
          pdf_path[MAX_PATH - 1] = 0;
        }
      s->pdf_path[0] = 0;
    }

  paper_resize_restored();
  return 1;
}

/* TRUE when a PDF document can be selected (loaded, stored, or remembered
 * in the settings). */
int paper_pdf_available(void)
{
  if (pdf_doc) return 1;
  if (doc_store[PAPER_PDF].ready) return 1;
  if (doc_ready && current_type == PAPER_PDF) return 1;
  if (restored_ready && restored_bg == PAPER_PDF && restored_pdf_path[0])
    return 1;
  return 0;
}

int paper_select_type(Paper type)
{
  if (type < PAPER_PLAIN || type > PAPER_PDF) return 0;
  if (type == PAPER_PDF && !(doc_ready && current_type == PAPER_PDF) &&
      !doc_store[PAPER_PDF].ready && !pdf_doc)
    return 0;
  if (doc_ready && current_type == type) return 1;

  paper_remember_pdf_page();

  if (doc_ready)
    {
      paper_sync_current();
      paper_store_active();
    }

  if (paper_restore_doc(type))
    {
      paper_reload_current();
      if (paper_continuous_active())
        {
          paper_scroll_y = paper_page_top(page_index);
          paper_scroll_clamp();
        }
      app_render_all();
      ui_refresh_tray();
      /* Keep the active tool armed after a document switch. */
      if (g.tool != TOOL_NONE)
        {
          app_activate_overlay_input();
          app_update_cursor();
        }
    }
  else
    paper_new_document(type);
  return 1;
}

static void paper_clear_surface(cairo_surface_t *s)
{
  if (!s) return;
  cairo_t *cr = cairo_create(s);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_mark_dirty(s);
}

static void paper_copy_surface(cairo_surface_t *dst, cairo_surface_t *src)
{
  if (!dst || !src) return;
  cairo_t *cr = cairo_create(dst);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_mark_dirty(dst);
}

static cairo_surface_t *paper_scale_center(cairo_surface_t *src, int dw,
                                           int dh)
{
  if (!src || dw <= 0 || dh <= 0) return NULL;
  int sw = cairo_image_surface_get_width(src);
  int sh = cairo_image_surface_get_height(src);
  if (sw <= 0 || sh <= 0) return NULL;

  cairo_surface_t *dst =
    cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
  double s = (double) dw / (double) sw;
  double sy = (double) dh / (double) sh;
  if (sy < s) s = sy;
  double ox = (dw - sw * s) / 2.0;
  double oy = (dh - sh * s) / 2.0;
  if (g.paper_windowed && g.paper_size_kind == PAPER_SIZE_SCREEN)
    ox = oy = 0.0;

  cairo_t *cr = cairo_create(dst);
  cairo_translate(cr, ox, oy);
  cairo_scale(cr, s, s);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  return dst;
}

static cairo_surface_t *paper_scale_surface_factor(cairo_surface_t *src,
                                                   int dw, int dh,
                                                   double factor)
{
  cairo_surface_t *dst;
  cairo_t *cr;
  if (!src || dw <= 0 || dh <= 0 || factor <= 0.0) return NULL;
  dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
  if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy(dst);
      return NULL;
    }
  cr = cairo_create(dst);
  cairo_scale(cr, factor, factor);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  return dst;
}

static int paper_clamp_index(int i)
{
  if (i < 0) return 0;
  if (i >= page_count) return page_count > 0 ? page_count - 1 : 0;
  return i;
}

/* The persistent annotations file is loaded into g.backbuf before Paper is
 * opened.  Keep those annotations on the first visible page instead of
 * clearing them when the page model is created. */
static void paper_seed_current_from_backbuf(void)
{
  if (!doc_ready || !g.backbuf || page_index < 0 || page_index >= page_count)
    return;
  paper_ensure_page(&pages[page_index]);
  if (pages[page_index].ann)
    paper_copy_surface(pages[page_index].ann, g.backbuf);
}

/* ------------------------------------------------------------------ */
/* PDF last-page history                                              */
/* ------------------------------------------------------------------ */

static void pdf_history_path(wchar_t *buf, int cap)
{
  buf[0] = 0;
  if (!g.config_dir[0]) return;
  _snwprintf(buf, (size_t) cap, L"%s\\paper-pdf-pages.txt", g.config_dir);
}

static int pdf_history_find(const wchar_t *path)
{
  if (!path || !path[0]) return -1;
  for (int i = 0; i < pdf_history_count; i++)
    if (_wcsicmp(pdf_history[i].path, path) == 0) return i;
  return -1;
}

static void pdf_history_load(void)
{
  if (pdf_history_loaded) return;
  pdf_history_loaded = 1;
  pdf_history_count = 0;

  wchar_t wpath[MAX_PATH];
  pdf_history_path(wpath, MAX_PATH);
  if (!wpath[0]) return;
  char *path = paper_utf8_from_wide(wpath);
  if (!path) return;

  FILE *f = fopen(path, "r");
  free(path);
  if (!f) return;

  char line[4096];
  while (fgets(line, sizeof(line), f) && pdf_history_count < 32)
    {
      char *tab = strchr(line, '\t');
      if (!tab) continue;
      *tab = 0;
      int page = atoi(line);
      char *p = tab + 1;
      size_t n = strlen(p);
      while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == '\n')) p[--n] = 0;
      if (!p[0]) continue;
      wchar_t *wp = paper_wide_from_utf8(p);
      if (!wp) continue;
      wcsncpy(pdf_history[pdf_history_count].path, wp, MAX_PATH - 1);
      pdf_history[pdf_history_count].path[MAX_PATH - 1] = 0;
      pdf_history[pdf_history_count].page = page;
      pdf_history_count++;
      free(wp);
    }
  fclose(f);
}

static void pdf_history_save(void)
{
  if (!pdf_history_loaded || paper_test_mode || g.smoke) return;
  wchar_t wpath[MAX_PATH];
  pdf_history_path(wpath, MAX_PATH);
  if (!wpath[0]) return;
  char *path = paper_utf8_from_wide(wpath);
  if (!path) return;

  FILE *f = fopen(path, "w");
  free(path);
  if (!f) return;
  for (int i = 0; i < pdf_history_count; i++)
    {
      char *p = paper_utf8_from_wide(pdf_history[i].path);
      if (!p) continue;
      fprintf(f, "%d\t%s\n", pdf_history[i].page, p);
      free(p);
    }
  fclose(f);
}

static void pdf_history_remember(const wchar_t *path, int page)
{
  if (!path || !path[0]) return;
  pdf_history_load();
  int idx = pdf_history_find(path);
  if (idx < 0)
    {
      if (pdf_history_count >= 32)
        {
          memmove(&pdf_history[0], &pdf_history[1],
                  sizeof(pdf_history[0]) * 31);
          pdf_history_count = 31;
        }
      idx = pdf_history_count++;
    }
  wcsncpy(pdf_history[idx].path, path, MAX_PATH - 1);
  pdf_history[idx].path[MAX_PATH - 1] = 0;
  pdf_history[idx].page = page;
  pdf_history_save();
}

static int pdf_history_page(const wchar_t *path)
{
  pdf_history_load();
  int idx = pdf_history_find(path);
  return idx >= 0 ? pdf_history[idx].page : -1;
}

/* Remember the page the PDF document is showing.  In the continuous view the
 * visible page (not the last clicked one) is remembered, so exiting after
 * scrolling reopens the file at that page. */
static void paper_remember_pdf_page(void)
{
  if (!doc_ready || page_count <= 0) return;
  if (current_type != PAPER_PDF || !pdf_path[0]) return;
  pdf_history_remember(pdf_path, paper_view_page_index());
}

/* ------------------------------------------------------------------ */
/* PDF document and page rendering                                    */
/* ------------------------------------------------------------------ */

static void pdf_cache_clear(void)
{
  for (int i = 0; i < PDF_CACHE_MAX; i++)
    {
      if (pdf_cache[i].surf)
        {
          cairo_surface_destroy(pdf_cache[i].surf);
          pdf_cache[i].surf = NULL;
        }
      pdf_cache[i].index = -1;
    }
  pdf_cache_next = 0;
}

static void pdf_close(void)
{
  pdf_cache_clear();
  if (pdf_doc)
    {
      FPDF_CloseDocument(pdf_doc);
      pdf_doc = NULL;
    }
  pdf_npages = 0;
  pdf_page_aspect = 0.0;
  pdf_path[0] = 0;
}

static const char *pdf_error_text(unsigned long code)
{
  switch (code)
    {
    case FPDF_ERR_FILE:
      return "The PDF file could not be found or opened.";
    case FPDF_ERR_FORMAT:
      return "The file is not a valid PDF document or is corrupted.";
    case FPDF_ERR_PASSWORD:
      return "This PDF is encrypted or password protected. "
             "Screenator cannot open it without a password.";
    case FPDF_ERR_SECURITY:
      return "This PDF uses an unsupported security scheme.";
    case FPDF_ERR_PAGE:
      return "The PDF has no readable pages.";
    default:
      return "The PDF could not be loaded.";
    }
}

static void pdf_set_error(char *error, int size, const char *text,
                          unsigned long code)
{
  if (!error || size <= 0) return;
  if (code != FPDF_ERR_SUCCESS && code != 0)
    snprintf(error, (size_t) size, "%s (PDFium error %lu)", text, code);
  else
    snprintf(error, (size_t) size, "%s", text);
}

/* Render one PDF page into a cached full-paper ARGB32 image. */
static cairo_surface_t *pdf_render_page(int index, int w, int h)
{
  if (!pdf_doc || index < 0 || index >= pdf_npages || w <= 0 || h <= 0)
    return NULL;

  for (int i = 0; i < PDF_CACHE_MAX; i++)
    if (pdf_cache[i].surf && pdf_cache[i].index == index)
      return pdf_cache[i].surf;

  FPDF_PAGE page = FPDF_LoadPage(pdf_doc, index);
  if (!page) return NULL;

  double pw = FPDF_GetPageWidthF(page);
  double ph = FPDF_GetPageHeightF(page);
  if (pw <= 0 || ph <= 0)
    {
      FPDF_ClosePage(page);
      return NULL;
    }

  cairo_surface_t *surf =
    cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy(surf);
      FPDF_ClosePage(page);
      return NULL;
    }

  /* White paper behind the page. */
  cairo_t *cr = cairo_create(surf);
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_flush(surf);

  int stride = cairo_image_surface_get_stride(surf);
  unsigned char *data = cairo_image_surface_get_data(surf);
  FPDF_BITMAP bmp = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA, data, stride);
  if (bmp)
    {
      double margin = 12.0;
      double availw = (double) w - 2.0 * margin;
      double availh = (double) h - 2.0 * margin;
      if (availw < 1) availw = 1;
      if (availh < 1) availh = 1;
      double scale = availw / pw;
      double sy = availh / ph;
      if (sy < scale) scale = sy;

      int dw = (int) (pw * scale + 0.5);
      int dh = (int) (ph * scale + 0.5);
      if (dw < 1) dw = 1;
      if (dh < 1) dh = 1;
      int dx = (w - dw) / 2;
      int dy = (h - dh) / 2;

      FPDFBitmap_FillRect(bmp, 0, 0, w, h, 0xFFFFFFFF);
      FPDF_RenderPageBitmap(bmp, page, dx, dy, dw, dh, 0,
                            FPDF_ANNOT | FPDF_LCD_TEXT);
      FPDFBitmap_Destroy(bmp);
    }

  FPDF_ClosePage(page);
  cairo_surface_mark_dirty(surf);

  /* Replace the round-robin cache entry. */
  int slot = pdf_cache_next;
  pdf_cache_next = (pdf_cache_next + 1) % PDF_CACHE_MAX;
  if (pdf_cache[slot].surf)
    cairo_surface_destroy(pdf_cache[slot].surf);
  pdf_cache[slot].index = index;
  pdf_cache[slot].surf = surf;
  return surf;
}

static void pdf_build_pages(void)
{
  page_count = pdf_npages < PAPER_MAX_PAGES ? pdf_npages : PAPER_MAX_PAGES;
  if (page_count < 1) page_count = 1;
  for (int i = 0; i < page_count; i++)
    {
      pages[i].bg = PAPER_PDF;
      pages[i].pdf_page = i;
      pages[i].ann = NULL;
      pages[i].vec = NULL;
      pages[i].texts = NULL;
      pages[i].ntexts = pages[i].textcap = 0;
      pages[i].text_w = pages[i].text_h = 0;
      memset(&pages[i].resize, 0, sizeof(pages[i].resize));
      pages[i].erased = 0;
      pages[i].vec_dirty = 0;
      pages[i].erases = NULL;
      pages[i].nerases = pages[i].erasecap = 0;
    }
  page_index = 0;
  doc_ready = 1;
}

static int paper_load_pdf_document(const wchar_t *path, char *error,
                                   int error_size)
{
  if (error && error_size > 0) error[0] = 0;
  if (!path || !path[0])
    {
      pdf_set_error(error, error_size, "No PDF file was selected.",
                    FPDF_ERR_SUCCESS);
      return 0;
    }

  pdfium_init();
  char *utf8 = paper_utf8_from_wide(path);
  if (!utf8)
    {
      pdf_set_error(error, error_size, "Out of memory.", FPDF_ERR_SUCCESS);
      return 0;
    }

  FPDF_DOCUMENT nd = FPDF_LoadDocument(utf8, NULL);
  free(utf8);
  if (!nd)
    {
      unsigned long code = FPDF_GetLastError();
      pdf_set_error(error, error_size, pdf_error_text(code), code);
      return 0;
    }

  int npages = FPDF_GetPageCount(nd);
  if (npages <= 0)
    {
      FPDF_CloseDocument(nd);
      pdf_set_error(error, error_size, "The PDF has no pages.",
                    FPDF_ERR_PAGE);
      return 0;
    }

  pdf_close();
  pdf_doc = nd;
  pdf_npages = npages;
  pdf_page_aspect = 0.0;
  {
    FPDF_PAGE first = FPDF_LoadPage(nd, 0);
    if (first)
      {
        double pw = FPDF_GetPageWidthF(first);
        double ph = FPDF_GetPageHeightF(first);
        if (pw > 0.0 && ph > 0.0) pdf_page_aspect = ph / pw;
        FPDF_ClosePage(first);
      }
  }
  wcsncpy(pdf_path, path, MAX_PATH - 1);
  pdf_path[MAX_PATH - 1] = 0;
  return 1;
}

static int pdf_open_path(const wchar_t *path, char *error, int error_size)
{
  if (error && error_size > 0) error[0] = 0;
  if (!path || !path[0])
    {
      if (error && error_size > 0)
        snprintf(error, (size_t) error_size, "No PDF file was selected.");
      return 0;
    }

  /* Save the live document before the PDF replaces the active pages. */
  paper_remember_pdf_page();
  if (doc_ready)
    {
      paper_sync_current();
      if (current_type == PAPER_PDF)
        paper_clear_active_pages();
      else
        paper_store_active();
    }
  paper_clear_store(PAPER_PDF);

  if (!paper_load_pdf_document(path, error, error_size))
    return 0;

  int last = pdf_history_page(path);
  pdf_build_pages();
  current_type = PAPER_PDF;
  if (last >= 0 && last < page_count) page_index = last;
  else if (restored_pdf_path[0] &&
           _wcsicmp(restored_pdf_path, path) == 0 &&
           restored_index >= 0 && restored_index < page_count)
    page_index = restored_index;

  pdf_history_remember(path, page_index);
  paper_changes_pending = 0;
  return 1;
}

void paper_draw_pdf_page(cairo_t *cr, int w, int h)
{
  cairo_surface_t *surf = NULL;

  if (doc_ready && pdf_doc && page_index >= 0 && page_index < page_count &&
      pages[page_index].bg == PAPER_PDF && pages[page_index].pdf_page >= 0)
    surf = pdf_render_page(pages[page_index].pdf_page, w, h);

  if (!surf)
    {
      cairo_save(cr);
      cairo_set_source_rgb(cr, 1, 1, 1);
      cairo_rectangle(cr, 0, 0, w, h);
      cairo_fill(cr);
      cairo_restore(cr);
      return;
    }

  cairo_save(cr);
  int sw = cairo_image_surface_get_width(surf);
  int sh = cairo_image_surface_get_height(surf);
  if (sw > 0 && sh > 0 && (sw != w || sh != h))
    {
      cairo_scale(cr, (double) w / (double) sw, (double) h / (double) sh);
    }
  cairo_set_source_surface(cr, surf, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);
}

/* ------------------------------------------------------------------ */
/* page model                                                         */
/* ------------------------------------------------------------------ */

static void paper_clear_history(void)
{
  for (int i = 0; i < PAPER_UNDO_MAX; i++)
    {
      if (undo_stack[i].has_page)
        {
          paper_free_page(&undo_stack[i].page);
          undo_stack[i].has_page = 0;
        }
      if (redo_stack[i].has_page)
        {
          paper_free_page(&redo_stack[i].page);
          redo_stack[i].has_page = 0;
        }
    }
  undo_depth = redo_depth = 0;
  page_change_last = 0;
}

static void paper_clear_document(void)
{
  paper_clear_active_pages();
  for (int t = PAPER_PLAIN; t <= PAPER_PDF; t++)
    paper_clear_store((Paper) t);
  paper_clear_history();
  pdf_close();
}

void paper_new_document(Paper bg)
{
  if (bg != PAPER_PLAIN && bg != PAPER_RULED && bg != PAPER_GRAPH &&
      bg != PAPER_PDF)
    bg = PAPER_PLAIN;

  /* Drop the live pages and any saved copy of this type; the other paper
   * types keep their own documents. */
  paper_clear_active_pages();
  paper_clear_store(bg);
  paper_clear_history();
  pdf_close();

  tools_clear_undo();
  page_count = 1;
  page_index = 0;
  memset(&pages[0], 0, sizeof(pages[0]));
  pages[0].bg = bg;
  pages[0].pdf_page = -1;
  pages[0].ann = paper_new_surface();
  pages[0].vec = NULL;
  current_type = bg;
  doc_ready = 1;
  paper_changes_pending = 0;
  paper_reload_current();
  if (paper_continuous_active())
    {
      paper_scroll_y = paper_page_top(0);
      paper_scroll_clamp();
    }
}

void paper_sync_current(void)
{
  /* g.backbuf is only the page's annotation layer while Paper mode is
   * showing it; in Screen mode it holds the screen annotations, which must
   * never be stored into a paper page. */
  if (g.paper == PAPER_NONE) return;
  if (!doc_ready || !g.backbuf || page_index < 0 || page_index >= page_count)
    return;
  paper_ensure_page(&pages[page_index]);
  if (pages[page_index].ann)
    paper_copy_surface(pages[page_index].ann, g.backbuf);
}

void paper_reload_current(void)
{
  paper_erase_end();
  if (!doc_ready || !g.backbuf || page_index < 0 || page_index >= page_count)
    return;
  PaperPage *p = &pages[page_index];
  paper_clear_surface(g.backbuf);
  if (p->ann) paper_copy_surface(g.backbuf, p->ann);
  cairo_surface_mark_dirty(g.backbuf);
  g.paper = p->bg;
}

void paper_enter(Paper initial)
{
  paper_save_screen_layer();
  if (doc_ready)
    {
      paper_reload_current();
      g.paper = paper_page_background();
      if (paper_continuous_active())
        {
          paper_scroll_y = paper_page_top(page_index);
          paper_scroll_clamp();
        }
      return;
    }

  /* Try to reopen the PDF remembered in the settings. */
  if (restored_ready && restored_bg == PAPER_PDF &&
      restored_pdf_path[0] && restored_count > 0)
    {
      char err[384];
      if (pdf_open_path(restored_pdf_path, err, sizeof(err)))
        {
          page_index = paper_clamp_index(restored_index);
          paper_reload_current();
          g.paper = PAPER_PDF;
          if (paper_continuous_active())
            {
              paper_scroll_y = paper_page_top(page_index);
              paper_scroll_clamp();
            }
          return;
        }
    }

  int have_restored_doc = restored_ready && restored_count > 0;
  int count = have_restored_doc ? restored_count : 1;
  if (count > PAPER_MAX_PAGES) count = PAPER_MAX_PAGES;
  page_count = count;
  for (int i = 0; i < page_count; i++)
    {
      Paper bg = PAPER_PLAIN;
      if (have_restored_doc && i < restored_ntypes)
        bg = restored_types[i];
      else if (have_restored_doc)
        bg = restored_bg;
      else
        bg = initial;
      if (bg != PAPER_PLAIN && bg != PAPER_RULED && bg != PAPER_GRAPH &&
          bg != PAPER_PDF)
        bg = PAPER_PLAIN;
      if (bg == PAPER_PDF) bg = PAPER_PLAIN;
      pages[i].bg = bg;
      pages[i].pdf_page = -1;
      pages[i].ann = NULL;
      pages[i].vec = NULL;
      pages[i].texts = NULL;
      pages[i].ntexts = pages[i].textcap = 0;
      pages[i].text_w = pages[i].text_h = 0;
      memset(&pages[i].resize, 0, sizeof(pages[i].resize));
      pages[i].erased = 0;
      pages[i].vec_dirty = 0;
      pages[i].erases = NULL;
      pages[i].nerases = pages[i].erasecap = 0;
    }
  page_index = paper_clamp_index(have_restored_doc ? restored_index : 0);
  doc_ready = 1;
  current_type = pages[page_index].bg;
  if (current_type < PAPER_PLAIN || current_type > PAPER_PDF)
    current_type = PAPER_PLAIN;
  paper_reload_current();
  g.paper = paper_page_background();
  if (paper_continuous_active())
    {
      paper_scroll_y = paper_page_top(page_index);
      paper_scroll_clamp();
    }
}

void paper_leave(void)
{
  if (!doc_ready) return;
  paper_sync_current();
  paper_remember_pdf_page();
  paper_restore_screen_layer();
}

void paper_set_background(Paper bg)
{
  if (bg != PAPER_PLAIN && bg != PAPER_RULED && bg != PAPER_GRAPH &&
      bg != PAPER_PDF)
    return;
  if (!doc_ready)
    {
      paper_enter(bg);
      return;
    }
  paper_select_type(bg);
}

/* Switch the live buffer to an already-saved page index. */
static void paper_switch_index(int idx, int scroll_to_page)
{
  if (!doc_ready) return;
  page_index = paper_clamp_index(idx);
  paper_reload_current();
  if (paper_continuous_active() && scroll_to_page)
    {
      paper_scroll_y = paper_page_top(page_index);
      paper_scroll_clamp();
    }
  if (pages[page_index].bg == PAPER_PDF && pdf_path[0])
    pdf_history_remember(pdf_path, page_index);
  app_render_all();
  toolbar_refresh();
  ui_refresh_tray();
  if (g.tool != TOOL_NONE)
    {
      app_activate_overlay_input();
      app_update_cursor();
    }
}

int paper_insert_page(int before)
{
  if (!doc_ready) paper_enter(PAPER_PLAIN);
  if (!doc_ready) return 0;
  if (page_count >= PAPER_MAX_PAGES) return 0;

  tools_commit(0);
  paper_sync_current();

  int idx = before ? page_index : page_index + 1;
  if (idx < 0) idx = 0;
  if (idx > page_count) idx = page_count;

  Paper bg = pages[page_index].bg;
  if (bg == PAPER_PDF) bg = PAPER_PLAIN;

  PaperPage np;
  memset(&np, 0, sizeof(np));
  np.bg = bg;
  np.pdf_page = -1;
  np.ann = paper_new_surface();

  for (int i = page_count; i > idx; i--) pages[i] = pages[i - 1];
  pages[idx] = np;
  page_count++;
  paper_changes_pending = 1;

  int old_index = page_index;
  PaperAction act;
  memset(&act, 0, sizeof(act));
  act.kind = 1;
  act.index = idx;
  act.prev_index = old_index;
  act.new_index = idx;

  /* A new action invalidates redo. */
  for (int i = 0; i < PAPER_UNDO_MAX; i++)
    if (redo_stack[i].has_page)
      {
        paper_free_page(&redo_stack[i].page);
        redo_stack[i].has_page = 0;
      }
  redo_depth = 0;
  if (undo_depth >= PAPER_UNDO_MAX)
    {
      if (undo_stack[0].has_page)
        {
          paper_free_page(&undo_stack[0].page);
          undo_stack[0].has_page = 0;
        }
      memmove(&undo_stack[0], &undo_stack[1],
              sizeof(undo_stack[0]) * (PAPER_UNDO_MAX - 1));
      undo_depth = PAPER_UNDO_MAX - 1;
    }
  undo_stack[undo_depth++] = act;
  page_change_last = 1;

  tools_clear_undo();
  paper_switch_index(idx, 1);
  return 1;
}

int paper_delete_page(void)
{
  if (!doc_ready || page_count <= 1) return 0;
  tools_commit(0);
  paper_sync_current();

  int old_index = page_index;
  PaperPage removed = pages[old_index];
  for (int i = old_index; i + 1 < page_count; i++)
    pages[i] = pages[i + 1];
  page_count--;
  paper_changes_pending = 1;
  int new_index = old_index < page_count ? old_index : page_count - 1;
  page_index = new_index;

  PaperAction act;
  memset(&act, 0, sizeof(act));
  act.kind = 2;
  act.index = old_index;
  act.prev_index = old_index;
  act.new_index = new_index;
  act.page = removed;
  act.has_page = 1;

  for (int i = 0; i < PAPER_UNDO_MAX; i++)
    if (redo_stack[i].has_page)
      {
        paper_free_page(&redo_stack[i].page);
        redo_stack[i].has_page = 0;
      }
  redo_depth = 0;
  if (undo_depth >= PAPER_UNDO_MAX)
    {
      if (undo_stack[0].has_page)
        {
          paper_free_page(&undo_stack[0].page);
          undo_stack[0].has_page = 0;
        }
      memmove(&undo_stack[0], &undo_stack[1],
              sizeof(undo_stack[0]) * (PAPER_UNDO_MAX - 1));
      undo_depth = PAPER_UNDO_MAX - 1;
    }
  undo_stack[undo_depth++] = act;
  page_change_last = 1;

  tools_clear_undo();
  paper_switch_index(new_index, 1);
  return 1;
}

int paper_next_page(void)
{
  if (!doc_ready || page_index + 1 >= page_count) return 0;
  tools_commit(0);
  paper_sync_current();
  paper_switch_index(page_index + 1, 1);
  return 1;
}

int paper_prev_page(void)
{
  if (!doc_ready || page_index <= 0) return 0;
  tools_commit(0);
  paper_sync_current();
  paper_switch_index(page_index - 1, 1);
  return 1;
}

int paper_goto_page(int index)
{
  if (!doc_ready || index < 0 || index >= page_count) return 0;
  if (index == page_index)
    {
      if (paper_continuous_active())
        {
          paper_scroll_y = paper_page_top(page_index);
          paper_scroll_clamp();
          app_render_all();
          toolbar_refresh();
        }
      return 1;
    }
  tools_commit(0);
  paper_sync_current();
  paper_switch_index(index, 1);
  return 1;
}

int paper_page_count(void)
{
  return doc_ready ? page_count : 0;
}

int paper_page_index(void)
{
  return doc_ready ? page_index : 0;
}

/* Identifies the active paper document and page for the drawing undo stack.
 * -1 means Screen mode. */
int paper_undo_token(void)
{
  if (g.paper == PAPER_NONE || !doc_ready) return -1;
  return (((int) current_type & 0xff) << 16) | (page_index & 0xffff);
}

/* Switch to the document/page encoded in an undo token, without clearing the
 * drawing history. */
int paper_undo_goto(int token)
{
  Paper type;
  int page;

  if (token < 0) return 0;
  type = (Paper) ((token >> 16) & 0xff);
  page = token & 0xffff;
  if (type < PAPER_PLAIN || type > PAPER_PDF) return 0;

  if (g.paper == PAPER_NONE) app_set_paper(type, 0);
  if (!doc_ready) return 0;
  if (current_type != type && !paper_select_type(type)) return 0;
  if (!doc_ready || page < 0 || page >= page_count) return 0;
  if (page != page_index)
    {
      paper_sync_current();
      page_index = page;
      paper_reload_current();
      if (paper_continuous_active())
        {
          paper_scroll_y = paper_page_top(page_index);
          paper_scroll_clamp();
        }
      app_render_all();
      ui_refresh_tray();
      if (g.tool != TOOL_NONE)
        {
          app_activate_overlay_input();
          app_update_cursor();
        }
    }
  return 1;
}

Paper paper_page_background(void)
{
  if (!doc_ready || page_index < 0 || page_index >= page_count)
    return PAPER_PLAIN;
  return pages[page_index].bg;
}

int paper_page_has_document(void)
{
  return doc_ready;
}

int paper_doc_has_annotations(void)
{
  if (!doc_ready) return 0;
  for (int i = 0; i < page_count; i++)
    {
      if (pages[i].vec || pages[i].ntexts > 0 || pages[i].erased)
        return 1;
      if (pages[i].ann)
        {
          cairo_surface_flush(pages[i].ann);
          unsigned int *px = (unsigned int *)
            cairo_image_surface_get_data(pages[i].ann);
          int stride = cairo_image_surface_get_stride(pages[i].ann) / 4;
          int w = cairo_image_surface_get_width(pages[i].ann);
          int h = cairo_image_surface_get_height(pages[i].ann);
          if (px && w > 0 && h > 0)
            {
              for (int y = 0; y < h; y += 8)
                for (int x = 0; x < w; x += 8)
                  if (px[(size_t) y * stride + x] & 0xFF000000u)
                    return 1;
            }
        }
    }
  return 0;
}

/* ------------------------------------------------------------------ */
/* page-level undo / redo                                             */
/* ------------------------------------------------------------------ */

void paper_note_drawing_change(void)
{
  if (doc_ready) paper_changes_pending = 1;
  page_change_last = 0;
}

void paper_note_erase(void)
{
  if (doc_ready && page_index >= 0 && page_index < page_count)
    {
      pages[page_index].erased = 1;
      paper_changes_pending = 1;
    }
}

void paper_clear_erase(void)
{
  if (doc_ready && page_index >= 0 && page_index < page_count)
    {
      PaperPage *p = &pages[page_index];
      p->erased = 0;
      p->vec_dirty = 0;
      if (p->vec)
        {
          cairo_surface_destroy(p->vec);
          p->vec = NULL;
        }
      if (p->texts)
        {
          for (int i = 0; i < p->ntexts; i++)
            {
              free(p->texts[i].text);
              free(p->texts[i].font);
            }
          free(p->texts);
          p->texts = NULL;
        }
      p->ntexts = 0;
      p->textcap = 0;
    }
}

void paper_reset_current_page(void)
{
  if (!doc_ready || page_index < 0 || page_index >= page_count) return;
  tools_commit(0);
  tools_clear();
  paper_sync_current();
  app_render_all();
  ui_refresh_tray();
}

int paper_page_change_is_last(void)
{
  return page_change_last;
}

int paper_page_can_undo(void)
{
  return doc_ready && undo_depth > 0;
}

int paper_page_can_redo(void)
{
  return doc_ready && redo_depth > 0;
}

static void paper_push_redo(PaperAction *a)
{
  if (redo_depth >= PAPER_UNDO_MAX)
    {
      if (redo_stack[0].has_page)
        {
          paper_free_page(&redo_stack[0].page);
          redo_stack[0].has_page = 0;
        }
      memmove(&redo_stack[0], &redo_stack[1],
              sizeof(redo_stack[0]) * (PAPER_UNDO_MAX - 1));
      redo_depth = PAPER_UNDO_MAX - 1;
    }
  redo_stack[redo_depth++] = *a;
  a->has_page = 0;
}

static void paper_push_undo(PaperAction *a)
{
  if (undo_depth >= PAPER_UNDO_MAX)
    {
      if (undo_stack[0].has_page)
        {
          paper_free_page(&undo_stack[0].page);
          undo_stack[0].has_page = 0;
        }
      memmove(&undo_stack[0], &undo_stack[1],
              sizeof(undo_stack[0]) * (PAPER_UNDO_MAX - 1));
      undo_depth = PAPER_UNDO_MAX - 1;
    }
  undo_stack[undo_depth++] = *a;
  a->has_page = 0;
}

int paper_page_undo(void)
{
  if (!doc_ready || undo_depth <= 0) return 0;
  tools_commit(0);
  paper_sync_current();
  PaperAction a = undo_stack[--undo_depth];

  if (a.kind == 1)
    {
      /* Undo an insertion: remove the page and keep its current content in
       * the action so redo can put it back. */
      if (a.index < 0 || a.index >= page_count)
        {
          if (a.has_page) paper_free_page(&a.page);
          return 0;
        }
      if (a.has_page) paper_free_page(&a.page);
      a.page = pages[a.index];
      a.has_page = 1;
      for (int i = a.index; i + 1 < page_count; i++)
        pages[i] = pages[i + 1];
      page_count--;
      page_index = paper_clamp_index(a.prev_index);
    }
  else
    {
      /* Undo a deletion: put the saved page back. */
      if (a.index < 0 || a.index > page_count || !a.has_page)
        {
          if (a.has_page) paper_free_page(&a.page);
          return 0;
        }
      for (int i = page_count; i > a.index; i--) pages[i] = pages[i - 1];
      pages[a.index] = a.page;
      a.has_page = 0;
      page_count++;
      page_index = paper_clamp_index(a.prev_index);
    }

  paper_push_redo(&a);
  paper_changes_pending = 1;
  paper_reload_current();
  tools_clear_undo();
  page_change_last = 1;
  app_render_all();
  ui_refresh_tray();
  return 1;
}

int paper_page_redo(void)
{
  if (!doc_ready || redo_depth <= 0) return 0;
  tools_commit(0);
  paper_sync_current();
  PaperAction a = redo_stack[--redo_depth];

  if (a.kind == 1)
    {
      /* Redo an insertion. */
      if (a.index < 0 || a.index > page_count)
        {
          if (a.has_page) paper_free_page(&a.page);
          return 0;
        }
      PaperPage np;
      if (a.has_page)
        {
          np = a.page;
          a.has_page = 0;
        }
      else
        {
          memset(&np, 0, sizeof(np));
          np.bg = (page_count > 0 && pages[paper_clamp_index(a.index)].bg != PAPER_PDF)
                    ? pages[paper_clamp_index(a.index)].bg : PAPER_PLAIN;
          np.pdf_page = -1;
          np.ann = paper_new_surface();
        }
      for (int i = page_count; i > a.index; i--) pages[i] = pages[i - 1];
      pages[a.index] = np;
      page_count++;
      page_index = paper_clamp_index(a.new_index);
    }
  else
    {
      /* Redo a deletion. */
      if (a.index < 0 || a.index >= page_count)
        {
          if (a.has_page) paper_free_page(&a.page);
          return 0;
        }
      if (a.has_page) paper_free_page(&a.page);
      a.page = pages[a.index];
      a.has_page = 1;
      for (int i = a.index; i + 1 < page_count; i++)
        pages[i] = pages[i + 1];
      page_count--;
      page_index = paper_clamp_index(a.new_index);
    }

  paper_push_undo(&a);
  paper_changes_pending = 1;
  paper_reload_current();
  tools_clear_undo();
  page_change_last = 1;
  app_render_all();
  ui_refresh_tray();
  return 1;
}

/* ------------------------------------------------------------------ */
/* resizing and page decoration                                       */
/* ------------------------------------------------------------------ */

static void paper_resize_page(PaperPage *p, int w, int h)
{
  double factor;
  cairo_surface_t *scaled;
  if (!p || w <= 0 || h <= 0) return;
  paper_resize_state_init(p);
  factor = paper_resize_state_step(&p->resize, w, h);
  if (p->ann)
    {
      scaled = paper_scale_surface_factor(p->ann, w, h, factor);
      if (scaled)
        {
          cairo_surface_destroy(p->ann);
          p->ann = scaled;
        }
    }
  if (p->vec)
    {
      scaled = paper_scale_vector_factor(p->vec, w, h, factor);
      if (scaled)
        {
          cairo_surface_destroy(p->vec);
          p->vec = scaled;
        }
    }
  paper_scale_texts_factor(p->texts, p->ntexts, factor);
  paper_scale_erases_factor(p->erases, p->nerases, factor);
  /* Keep vector annotations sharp across repeated Paper/window transitions.
   * Erased pages retain the raster path because their current eraser data is
   * represented as a final mask and cannot preserve later-over-earlier order. */
  if ((p->vec || p->ntexts > 0) && p->nerases == 0)
    {
      scaled = paper_render_annotations(p, w, h);
      if (scaled)
        {
          if (p->ann) cairo_surface_destroy(p->ann);
          p->ann = scaled;
        }
    }
  p->text_w = w;
  p->text_h = h;
}

void paper_resize_all(int w, int h)
{
  if (!doc_ready) return;
  /* Rendered PDF page images are cached at the old paper size. */
  pdf_cache_clear();
  for (int i = 0; i < page_count; i++)
    paper_resize_page(&pages[i], w, h);
  if (screen_ann)
    {
      cairo_surface_t *scaled = paper_scale_center(screen_ann, g.vw, g.vh);
      cairo_surface_destroy(screen_ann);
      screen_ann = scaled;
    }
  for (int i = 0; i < PAPER_UNDO_MAX; i++)
    {
      if (undo_stack[i].has_page)
        paper_resize_page(&undo_stack[i].page, w, h);
      if (redo_stack[i].has_page)
        paper_resize_page(&redo_stack[i].page, w, h);
    }
  for (int i = 0; i < MAX_UNDO; i++)
    {
      double undo_factor =
        paper_resize_state_step(&vec_undo_resize[i], w, h);
      double redo_factor =
        paper_resize_state_step(&vec_redo_resize[i], w, h);
      if (g.vec_undo[i])
        {
          cairo_surface_t *s = paper_scale_vector_factor(g.vec_undo[i], w, h,
                                                         undo_factor);
          if (s)
            {
              cairo_surface_destroy(g.vec_undo[i]);
              g.vec_undo[i] = s;
            }
        }
      if (g.vec_redo[i])
        {
          cairo_surface_t *s = paper_scale_vector_factor(g.vec_redo[i], w, h,
                                                         redo_factor);
          if (s)
            {
              cairo_surface_destroy(g.vec_redo[i]);
              g.vec_redo[i] = s;
            }
        }
      if (text_undo[i])
        {
          paper_scale_texts_factor(text_undo[i], text_undo_n[i], undo_factor);
          text_undo_w[i] = w;
          text_undo_h[i] = h;
        }
      if (text_redo[i])
        {
          paper_scale_texts_factor(text_redo[i], text_redo_n[i], redo_factor);
          text_redo_w[i] = w;
          text_redo_h[i] = h;
        }
      if (er_undo[i])
        {
          paper_scale_erases_factor(er_undo[i], er_undo_n[i], undo_factor);
          er_undo_w[i] = w;
          er_undo_h[i] = h;
        }
      if (er_redo[i])
        {
          paper_scale_erases_factor(er_redo[i], er_redo_n[i], redo_factor);
          er_redo_w[i] = w;
          er_redo_h[i] = h;
        }
    }
  if (paper_continuous_active())
    {
      paper_scroll_y = paper_page_top(page_index);
      paper_scroll_clamp();
    }
}

void paper_replace_current_surface(cairo_surface_t *src)
{
  if (!doc_ready || !src || page_index < 0 || page_index >= page_count)
    return;
  PaperPage *p = &pages[page_index];
  if (!p->ann) p->ann = paper_new_surface();
  if (p->ann) paper_copy_surface(p->ann, src);
}

void paper_draw_page_margin(cairo_t *cr, int w, int h)
{
  if (!doc_ready || page_count <= 1) return;

  const double gap = 6.0;
  cairo_save(cr);
  cairo_set_source_rgb(cr, 0.80, 0.80, 0.80);
  cairo_rectangle(cr, 0, 0, w, gap);
  cairo_fill(cr);
  cairo_rectangle(cr, 0, h - gap, w, gap);
  cairo_fill(cr);
  cairo_rectangle(cr, 0, gap, gap, h - 2 * gap);
  cairo_fill(cr);
  cairo_rectangle(cr, w - gap, gap, gap, h - 2 * gap);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 0.25, 0.25, 0.25, 0.45);
  cairo_set_line_width(cr, 1.0);
  cairo_rectangle(cr, gap + 0.5, gap + 0.5,
                  w - 2 * gap - 1.0, h - 2 * gap - 1.0);
  cairo_stroke(cr);
  cairo_restore(cr);
}

/* ------------------------------------------------------------------ */
/* save / export                                                      */
/* ------------------------------------------------------------------ */

static const char *paper_type_name(Paper p)
{
  switch (p)
    {
    case PAPER_RULED: return "ruled";
    case PAPER_GRAPH: return "graph";
    case PAPER_PDF:   return "pdf";
    default:          return "plain";
    }
}

typedef struct
{
  FPDF_FILEWRITE base;
  FILE *f;
} PaperPdfWriter;

static int paper_pdf_write_block(FPDF_FILEWRITE *self, const void *data,
                                 unsigned long size)
{
  PaperPdfWriter *w = (PaperPdfWriter *) self;
  return fwrite(data, 1, (size_t) size, w->f) == (size_t) size;
}

static int paper_pdfium_save(FPDF_DOCUMENT doc, const wchar_t *path)
{
  PaperPdfWriter w;
  w.base.version = 1;
  w.base.WriteBlock = paper_pdf_write_block;
  w.f = _wfopen(path, L"wb");
  if (!w.f) return 0;
  int ok = FPDF_SaveAsCopy(doc, &w.base, 0) ? 1 : 0;
  fclose(w.f);
  return ok;
}

static int paper_pdfium_import(FPDF_DOCUMENT dest, FPDF_DOCUMENT src,
                               int page_one_based)
{
  char range[32];
  int index = FPDF_GetPageCount(dest);
  snprintf(range, sizeof(range), "%d", page_one_based);
  return FPDF_ImportPages(dest, src, range, index) ? 1 : 0;
}

/* One PDF page per paper page containing only the paper background. */
static int paper_write_background_pdf(const wchar_t *path, int w, int h)
{
  char *utf8 = paper_utf8_from_wide(path);
  cairo_surface_t *surface;
  cairo_t *cr;
  cairo_status_t st;

  if (!utf8) return 0;
  surface = cairo_pdf_surface_create(utf8, (double) w * 0.75,
                                     (double) h * 0.75);
  free(utf8);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy(surface);
      return 0;
    }

  cr = cairo_create(surface);
  for (int i = 0; i < page_count; i++)
    {
      if (i > 0) cairo_show_page(cr);
      cairo_save(cr);
      cairo_scale(cr, 0.75, 0.75);
      if (pages[i].bg == PAPER_PDF)
        {
          /* Placeholder; the original page is imported instead. */
          cairo_set_source_rgb(cr, 1, 1, 1);
          cairo_paint(cr);
        }
      else
        draw_paper(cr, pages[i].bg, w, h);
      cairo_restore(cr);
    }
  cairo_destroy(cr);
  cairo_surface_finish(surface);
  st = cairo_surface_status(surface);
  cairo_surface_destroy(surface);
  return st == CAIRO_STATUS_SUCCESS;
}

/* TRUE when the page's annotation raster contains any visible pixel.  This is
 * the reliable test for whether an annotation overlay is needed. */
static int paper_page_has_visible_content(int index)
{
  PaperPage *p;
  unsigned int *px;
  int stride, w, h;

  if (!doc_ready || index < 0 || index >= page_count) return 0;
  p = &pages[index];
  if (p->vec_dirty || p->ntexts > 0) return 1;
  if (!p->ann) return 0;
  cairo_surface_flush(p->ann);
  px = (unsigned int *) cairo_image_surface_get_data(p->ann);
  stride = cairo_image_surface_get_stride(p->ann) / 4;
  w = cairo_image_surface_get_width(p->ann);
  h = cairo_image_surface_get_height(p->ann);
  if (!px || w <= 0 || h <= 0) return 0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      if (px[(size_t) y * stride + x] & 0xFF000000u)
        return 1;
  return 0;
}

int paper_page_has_annotations(void)
{
  return paper_page_has_visible_content(page_index);
}

/* Build the eraser mask: opaque everywhere except under the eraser strokes. */
static cairo_surface_t *paper_erase_mask(int w, int h, PaperPage *p)
{
  cairo_surface_t *mask;
  cairo_t *cr;

  if (!p || p->nerases <= 0 || w <= 0 || h <= 0) return NULL;
  mask = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (cairo_surface_status(mask) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy(mask);
      return NULL;
    }
  cr = cairo_create(mask);
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  for (int i = 0; i < p->nerases; i++)
    {
      EraseStroke *s = &p->erases[i];
      if (s->n <= 0) continue;
      cairo_set_line_width(cr, s->width > 0 ? s->width : 1.0);
      if (s->n == 1)
        {
          cairo_arc(cr, s->pts[0].x, s->pts[0].y, s->width / 2.0,
                    0, 2 * M_PI);
          cairo_fill(cr);
        }
      else
        {
          cairo_move_to(cr, s->pts[0].x, s->pts[0].y);
          for (int j = 1; j < s->n; j++)
            cairo_line_to(cr, s->pts[j].x, s->pts[j].y);
          cairo_stroke(cr);
        }
    }
  cairo_destroy(cr);
  cairo_surface_mark_dirty(mask);
  return mask;
}

/* One transparent PDF page per paper page with the annotation layer. */
static int paper_write_annotation_pdf(const wchar_t *path, int w, int h)
{
  char *utf8 = paper_utf8_from_wide(path);
  cairo_surface_t *surface;
  cairo_t *cr;
  cairo_status_t st;

  if (!utf8) return 0;
  surface = cairo_pdf_surface_create(utf8, (double) w * 0.75,
                                     (double) h * 0.75);
  free(utf8);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy(surface);
      return 0;
    }

  cr = cairo_create(surface);
  for (int i = 0; i < page_count; i++)
    {
      if (i > 0) cairo_show_page(cr);
      cairo_save(cr);

      if (!paper_page_has_visible_content(i))
        {
          /* An empty overlay would be dropped by Cairo (or imported as a
           * transparency mask image).  Use an opaque white page and place it
           * behind the background during composition. */
          cairo_set_source_rgb(cr, 1, 1, 1);
          cairo_paint(cr);
        }
      else
        {
          cairo_scale(cr, 0.75, 0.75);
          cairo_push_group(cr);
          if (pages[i].vec)
            {
              cairo_set_source_surface(cr, pages[i].vec, 0, 0);
              cairo_paint(cr);
            }
          else if (pages[i].ann)
            {
              cairo_set_source_surface(cr, pages[i].ann, 0, 0);
              cairo_paint(cr);
            }
          paper_draw_page_texts(cr, i);
          cairo_pop_group_to_source(cr);
          if (pages[i].nerases > 0)
            {
              cairo_surface_t *mask = paper_erase_mask(w, h, &pages[i]);
              if (mask)
                {
                  cairo_mask_surface(cr, mask, 0, 0);
                  cairo_surface_destroy(mask);
                }
              else
                cairo_paint(cr);
            }
          else
            cairo_paint(cr);
        }
      cairo_restore(cr);
    }
  cairo_destroy(cr);
  cairo_surface_finish(surface);
  st = cairo_surface_status(surface);
  cairo_surface_destroy(surface);
  return st == CAIRO_STATUS_SUCCESS;
}

static void paper_bg_compose_matrix(FPDF_DOCUMENT srcdoc, int srcpage, int w,
                                    int h, double hpt, FS_MATRIX *m)
{
  FPDF_PAGE page = FPDF_LoadPage(srcdoc, srcpage);
  double pw = page ? FPDF_GetPageWidthF(page) : 0;
  double ph = page ? FPDF_GetPageHeightF(page) : 0;
  FS_RECTF box;
  memset(&box, 0, sizeof(box));
  if (page) FPDFPage_GetMediaBox(page, &box.left, &box.bottom, &box.right,
                                 &box.top);
  if (page) FPDF_ClosePage(page);

  m->a = 1; m->b = 0; m->c = 0; m->d = 1; m->e = 0; m->f = 0;
  if (pw <= 0 || ph <= 0) return;

  {
    double margin = 12.0;
    double availw = (double) w - 2.0 * margin;
    double availh = (double) h - 2.0 * margin;
    double scale = availw / pw;
    double dw, dh, dx, dy, s, k = 72.0 / 96.0;

    if (availh / ph < scale) scale = availh / ph;
    if (scale <= 0) return;
    dw = pw * scale;
    dh = ph * scale;
    dx = ((double) w - dw) / 2.0;
    dy = ((double) h - dh) / 2.0;
    s = scale * k;

    m->a = s;
    m->b = 0;
    m->c = 0;
    m->d = s;
    m->e = dx * k - box.left * s;
    m->f = hpt - (dy + dh) * k - box.bottom * s;
  }
}

int paper_export_path(const wchar_t *path, char *error, int error_size)
{
  int w = paper_surface_w();
  int h = paper_surface_h();
  double wpt = (double) w * 72.0 / 96.0;
  double hpt = (double) h * 72.0 / 96.0;
  wchar_t bgpath[MAX_PATH];
  wchar_t annpath[MAX_PATH];
  char *utfbg = NULL;
  char *utfann = NULL;
  FPDF_DOCUMENT bgdoc = NULL;
  FPDF_DOCUMENT anndoc = NULL;
  FPDF_DOCUMENT src = NULL;
  FPDF_DOCUMENT out = NULL;
  int ok = 0;

  if (error && error_size > 0) error[0] = 0;
  if (!doc_ready || page_count <= 0)
    {
      if (error && error_size > 0)
        snprintf(error, (size_t) error_size, "There is no paper to save.");
      return 0;
    }

  paper_sync_current();

  if (!g.config_dir[0])
    {
      if (error && error_size > 0)
        snprintf(error, (size_t) error_size,
                 "The settings folder is not available.");
      return 0;
    }
  _snwprintf(bgpath, MAX_PATH, L"%s\\paper-export-bg.pdf", g.config_dir);
  _snwprintf(annpath, MAX_PATH, L"%s\\paper-export-ann.pdf", g.config_dir);
  DeleteFileW(bgpath);
  DeleteFileW(annpath);

  if (!paper_write_background_pdf(bgpath, w, h) ||
      !paper_write_annotation_pdf(annpath, w, h))
    {
      if (error && error_size > 0)
        snprintf(error, (size_t) error_size,
                 "The PDF annotation layers could not be created.");
      goto cleanup;
    }

  utfbg = paper_utf8_from_wide(bgpath);
  utfann = paper_utf8_from_wide(annpath);
  if (!utfbg || !utfann)
    {
      if (error && error_size > 0)
        snprintf(error, (size_t) error_size, "Out of memory.");
      goto cleanup;
    }

  bgdoc = FPDF_LoadDocument(utfbg, "");
  anndoc = FPDF_LoadDocument(utfann, "");
  if (!bgdoc || !anndoc)
    {
      if (error && error_size > 0)
        snprintf(error, (size_t) error_size,
                 "The temporary PDF layers could not be opened.");
      goto cleanup;
    }

  src = FPDF_CreateNewDocument();
  if (!src)
    {
      if (error && error_size > 0)
        snprintf(error, (size_t) error_size,
                 "The output PDF document could not be created.");
      goto cleanup;
    }

  /* Two source pages per paper page: background then annotation overlay. */
  for (int i = 0; i < page_count; i++)
    {
      int bg_ok;
      if (pages[i].bg == PAPER_PDF && pdf_doc && pages[i].pdf_page >= 0)
        bg_ok = paper_pdfium_import(src, pdf_doc, pages[i].pdf_page + 1);
      else
        bg_ok = paper_pdfium_import(src, bgdoc, i + 1);
      if (!bg_ok)
        {
          if (error && error_size > 0)
            snprintf(error, (size_t) error_size,
                     "PDF background page %d could not be imported.", i + 1);
          goto cleanup;
        }
      if (!paper_pdfium_import(src, anndoc, i + 1))
        {
          if (error && error_size > 0)
            snprintf(error, (size_t) error_size,
                     "PDF annotation page %d could not be imported.", i + 1);
          goto cleanup;
        }
    }

  if (FPDF_GetPageCount(src) != page_count * 2)
    {
      if (error && error_size > 0)
        snprintf(error, (size_t) error_size,
                 "The PDF page layers are incomplete.");
      goto cleanup;
    }

  out = FPDF_ImportNPagesToOne(src, 2.0 * wpt, hpt, 2, 1);
  if (!out || FPDF_GetPageCount(out) != page_count)
    {
      if (error && error_size > 0)
        snprintf(error, (size_t) error_size,
                 "The PDF page composition failed.");
      goto cleanup;
    }

  for (int i = 0; i < page_count; i++)
    {
      FPDF_PAGE page = FPDF_LoadPage(out, i);
      FPDF_PAGEOBJECT bgobj = NULL, annobj = NULL;
      FS_MATRIX mbg, mann;
      int count;

      if (!page)
        {
          if (error && error_size > 0)
            snprintf(error, (size_t) error_size,
                     "A composed PDF page could not be opened.");
          goto cleanup;
        }
      count = FPDFPage_CountObjects(page);
      if (count < 2)
        {
          FPDF_ClosePage(page);
          if (error && error_size > 0)
            snprintf(error, (size_t) error_size,
                     "A composed PDF page has no layer pair.");
          goto cleanup;
        }
      bgobj = FPDFPage_GetObject(page, 0);
      annobj = FPDFPage_GetObject(page, 1);
      memset(&mbg, 0, sizeof(mbg));
      memset(&mann, 0, sizeof(mann));
      FPDFPageObj_GetMatrix(bgobj, &mbg);
      FPDFPageObj_GetMatrix(annobj, &mann);
      if (mann.e < mbg.e)
        {
          FPDF_PAGEOBJECT t = bgobj;
          FS_MATRIX m = mbg;
          bgobj = annobj; mbg = mann;
          annobj = t; mann = m;
        }

      /* For a page without annotations the second form is an opaque white
       * backdrop.  Move it behind the background form. */
      if (!paper_page_has_visible_content(i))
        {
          FPDFPage_RemoveObject(page, annobj);
          FPDFPage_InsertObjectAtIndex(page, annobj, 0);
        }

      memset(&mann, 0, sizeof(mann));
      mann.a = 1; mann.d = 1;
      FPDFPageObj_SetMatrix(annobj, &mann);

      if (pages[i].bg == PAPER_PDF && pdf_doc && pages[i].pdf_page >= 0)
        paper_bg_compose_matrix(pdf_doc, pages[i].pdf_page, w, h, hpt, &mbg);
      else
        {
          memset(&mbg, 0, sizeof(mbg));
          mbg.a = 1; mbg.d = 1;
        }
      FPDFPageObj_SetMatrix(bgobj, &mbg);

      FPDFPage_SetMediaBox(page, 0, 0, wpt, hpt);
      FPDFPage_SetCropBox(page, 0, 0, wpt, hpt);
      FPDFPage_GenerateContent(page);
      FPDF_ClosePage(page);
    }

  if (!paper_pdfium_save(out, path))
    {
      if (error && error_size > 0)
        snprintf(error, (size_t) error_size,
                 "The PDF file could not be written.");
      goto cleanup;
    }
  ok = 1;

cleanup:
  free(utfbg);
  free(utfann);
  if (out) FPDF_CloseDocument(out);
  if (src) FPDF_CloseDocument(src);
  if (bgdoc) FPDF_CloseDocument(bgdoc);
  if (anndoc) FPDF_CloseDocument(anndoc);
  DeleteFileW(bgpath);
  DeleteFileW(annpath);
  return ok;
}

static void paper_save_message(const wchar_t *path, int ok,
                               const char *error)
{
  wchar_t text[2048];
  if (ok)
    {
      _snwprintf(text, 2048, L"Paper saved to:\n%s", path);
      MessageBoxW(g.overlay, text, L"Paper saved",
                  MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
    }
  else
    {
      wchar_t *we = paper_wide_from_utf8(error && error[0] ? error
                                                 : "The paper could not be saved.");
      _snwprintf(text, 2048, L"%s", we ? we : L"The paper could not be saved.");
      free(we);
      MessageBoxW(g.overlay, text, L"Paper save failed",
                  MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
}

static void paper_default_filename(Paper type, wchar_t *buf, size_t cap)
{
  const char *kind = paper_type_name(type);
  wchar_t wkind[16] = L"plain";
  MultiByteToWideChar(CP_UTF8, 0, kind, -1, wkind, 16);
  SYSTEMTIME st;
  GetLocalTime(&st);
  _snwprintf(buf, cap, L"Screenator-%s-%04d%02d%02d-%02d%02d%02d.pdf",
             wkind, st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
}

typedef struct
{
  Paper type;
  wchar_t label[MAX_PATH + 64];
  wchar_t path[MAX_PATH];
  char error[512];
  int saved;
} PendingPaperSave;

static int paper_collect_pending(PendingPaperSave items[4])
{
  int n = 0;
  paper_sync_current();
  for (Paper type = PAPER_PLAIN; type <= PAPER_PDF; type++)
    {
      int active = doc_ready && current_type == type;
      PaperDocStore *store = &doc_store[type];
      int changed = active ? paper_changes_pending
                           : store->ready && store->changes_pending;
      if (!changed) continue;
      PendingPaperSave *item = &items[n++];
      memset(item, 0, sizeof(*item));
      item->type = type;
      int count = active ? page_count : store->count;
      const wchar_t *label = type == PAPER_RULED ? L"Ruled paper" :
                             type == PAPER_GRAPH ? L"Graph paper" : L"Plain paper";
      if (type == PAPER_PDF)
        {
          const wchar_t *path = active ? pdf_path : store->pdf_path;
          _snwprintf(item->label, MAX_PATH + 64, L"PDF: %s (%d pages)",
                     PathFindFileNameW(path), count);
        }
      else
        _snwprintf(item->label, MAX_PATH + 64, L"%s (%d pages)", label, count);
    }
  return n;
}

static void paper_save_directory(wchar_t dir[MAX_PATH])
{
  dir[0] = 0;
  if (FAILED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, dir)))
    dir[0] = 0;
  if (!dir[0])
    {
      DWORD n = GetCurrentDirectoryW(MAX_PATH, dir);
      if (n == 0 || n >= MAX_PATH) wcscpy(dir, L".");
    }
}

static int paper_save_current(const wchar_t *dir, wchar_t path[MAX_PATH],
                              char *error, int error_size)
{
  wchar_t name[128];
  paper_default_filename(current_type, name, 128);
  if (wcslen(dir) + wcslen(name) + 24 >= MAX_PATH)
    {
      snprintf(error, (size_t) error_size, "The output folder path is too long.");
      return 0;
    }
  _snwprintf(path, MAX_PATH, L"%s\\%s", dir, name);
  path[MAX_PATH - 1] = 0;
  /* Preserve previous exports, including two saves within the same second. */
  name[wcslen(name) - 4] = 0;
  for (int suffix = 1; GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; suffix++)
    {
      _snwprintf(path, MAX_PATH, L"%s\\%s-%d.pdf", dir, name, suffix);
      path[MAX_PATH - 1] = 0;
    }
  int ok = paper_export_path(path, error, error_size);
  if (ok) paper_changes_pending = 0;
  return ok;
}

static int paper_save_pending(PendingPaperSave *items, int count,
                              const wchar_t *dir)
{
  Paper mode = g.paper;
  Paper original = current_type;
  int original_page = page_index;
  int original_scroll = paper_scroll_y;
  int ok = 1;
  if (mode == PAPER_NONE) paper_enter(original);
  for (int i = 0; i < count; i++)
    {
      PendingPaperSave *item = &items[i];
      if (!paper_select_type(item->type))
        snprintf(item->error, sizeof(item->error), "The document could not be selected.");
      else
        item->saved = paper_save_current(dir, item->path, item->error,
                                         sizeof(item->error));
      if (!item->saved) ok = 0;
    }
  paper_select_type(original);
  paper_goto_page(original_page);
  paper_scroll_y = original_scroll;
  paper_scroll_clamp();
  if (mode == PAPER_NONE) paper_leave();
  g.paper = mode;
  app_request_render();
  ui_refresh_tray();
  return ok;
}

int paper_confirm_save_all(void)
{
  PendingPaperSave items[4];
  int count = paper_collect_pending(items);
  if (!count) return 1;
  wchar_t text[8192] = L"Unsaved changes in:\n";
  for (int i = 0; i < count; i++)
    {
      wcscat(text, L"\n");
      wcscat(text, items[i].label);
    }
  wcscat(text, L"\n\nSave all of these documents, including all their pages, before closing Screenator?");
  int choice = MessageBoxW(g.overlay, text, L"Unsaved Paper documents",
                           MB_YESNOCANCEL | MB_ICONQUESTION | MB_TOPMOST);
  if (choice != IDYES) return choice == IDNO;
  wchar_t dir[MAX_PATH];
  paper_save_directory(dir);
  int ok = paper_save_pending(items, count, dir);
  wcscpy(text, ok ? L"Saved all modified documents:\n" :
                    L"Screenator will stay open because some documents could not be saved.\n");
  for (int i = 0; i < count; i++)
    {
      wcscat(text, L"\n");
      wcscat(text, items[i].label);
      wcscat(text, items[i].saved ? L"\nSaved to: " : L"\nFailed: ");
      if (items[i].saved) wcscat(text, items[i].path);
      else
        {
          wchar_t *error = paper_wide_from_utf8(items[i].error);
          wcscat(text, error ? error : L"The document could not be saved.");
          free(error);
        }
      wcscat(text, L"\n");
    }
  MessageBoxW(g.overlay, text, ok ? L"Paper documents saved" : L"Paper save failed",
               MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR) | MB_TOPMOST);
  return ok;
}

void paper_save_dialog(void)
{
  ui_prepare_dialog();
  if (!doc_ready)
    {
      MessageBoxW(g.overlay,
                  L"No paper page is active. Select a paper type first.",
                  L"Paper save", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
      return;
    }

  if (!paper_changes_pending)
    {
      MessageBoxW(g.overlay, L"No changes have been made to save.",
                  L"Paper save", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
      return;
    }

  paper_sync_current();

  wchar_t dir[MAX_PATH];
  paper_save_directory(dir);
  wchar_t path[MAX_PATH];
  char error[512];
  int ok = paper_save_current(dir, path, error, sizeof(error));
  dlog("paper save %ls ok=%d err=%s", path, ok, error);
  paper_save_message(path, ok, error);
}

/* ------------------------------------------------------------------ */
/* open PDF                                                           */
/* ------------------------------------------------------------------ */

int paper_open_pdf_path(const wchar_t *path, char *error, int error_size)
{  if (!pdf_open_path(path, error, error_size)) return 0;

  for (int i = 0; i < PAPER_UNDO_MAX; i++)
    {
      if (undo_stack[i].has_page)
        {
          paper_free_page(&undo_stack[i].page);
          undo_stack[i].has_page = 0;
        }
      if (redo_stack[i].has_page)
        {
          paper_free_page(&redo_stack[i].page);
          redo_stack[i].has_page = 0;
        }
    }
  undo_depth = redo_depth = 0;
  page_change_last = 0;

  if (g.paper != PAPER_NONE)
    {
      /* The PDF may have different page proportions than the previous paper,
       * so resize the canvas and reload the current page. */
      ui_resize_canvas_for_doc();
      tools_clear_undo();
      if (paper_continuous_active())
        {
          paper_scroll_y = paper_page_top(page_index);
          paper_scroll_clamp();
        }
      app_render_all();
      ui_refresh_tray();
    }
  return 1;
}

int paper_pdf_page_count(void)
{
  return pdf_npages;
}

int paper_open_pdf_dialog(void)
{
  wchar_t path[MAX_PATH];
  path[0] = 0;
  ui_prepare_dialog();

  /* Replacing an annotated PDF should offer to save it first (Ctrl+O and the
   * contextual Reset/Open PDF action both come through here). */
  if (paper_page_has_document() && paper_page_background() == PAPER_PDF &&
      paper_doc_has_annotations())
    {
      int r = MessageBoxW(g.overlay,
                          L"This PDF has annotations.\n\n"
                          L"Save the annotated PDF before opening another "
                          L"file?",
                          L"Open PDF", MB_YESNOCANCEL | MB_ICONQUESTION |
                          MB_TOPMOST);
      if (r == IDCANCEL) return 0;
      if (r == IDYES) paper_save_dialog();
    }

  OPENFILENAMEW ofn;
  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g.overlay ? g.overlay : g.msgwin;
  ofn.lpstrFilter = L"PDF files (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = L"Open PDF";
  ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
              OFN_HIDEREADONLY;

  if (!GetOpenFileNameW(&ofn)) return 0;

  char error[512];
  if (!paper_open_pdf_path(path, error, sizeof(error)))
    {
      wchar_t *we = paper_wide_from_utf8(error);
      wchar_t text[1024];
      _snwprintf(text, 1024, L"Could not open the PDF:\n%s",
                 we ? we : L"Unknown error.");
      free(we);
      MessageBoxW(g.overlay, text, L"Open PDF",
                  MB_OK | MB_ICONERROR | MB_TOPMOST);
      return 0;
    }

  if (g.paper == PAPER_NONE)
    app_set_paper(PAPER_PDF, 0);
  else
    {
      g.paper = PAPER_PDF;
      paper_reload_current();
      /* Bring the newly chosen PDF to the front of the screen. */
      app_activate_overlay_input();
      app_render_all();
      ui_refresh_tray();
    }

  if (paper_pdf_page_count() > paper_page_count())
    {
      wchar_t text[256];
      _snwprintf(text, 256,
                 L"This PDF has %d pages. Screenator loaded the first %d "
                 L"pages (the Paper limit).",
                 paper_pdf_page_count(), paper_page_count());
      MessageBoxW(g.overlay, text, L"Open PDF",
                  MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
    }
  config_save();
  return 1;
}

/* ------------------------------------------------------------------ */
/* settings                                                           */
/* ------------------------------------------------------------------ */

void paper_config_load(void)
{
  restored_ready = 0;
  restored_ntypes = 0;
  restored_pdf_path[0] = 0;
  if (!g.config_path[0]) return;

  restored_bg = (Paper) GetPrivateProfileIntW(L"screenator", L"paper_type",
                                               PAPER_PLAIN, g.config_path);
  restored_count = (int) GetPrivateProfileIntW(L"screenator",
                                                L"paper_page_count", 1,
                                                g.config_path);
  restored_index = (int) GetPrivateProfileIntW(L"screenator",
                                                L"paper_page_index", 0,
                                                g.config_path);

  /* Only treat the metadata as restored when the config actually contains
   * paper keys.  Older settings files must keep working and use the paper
   * type the user selects. */
  {
    wchar_t probe[16];
    GetPrivateProfileStringW(L"screenator", L"paper_type", L"", probe, 16,
                             g.config_path);
    int has_type = probe[0] != 0;
    GetPrivateProfileStringW(L"screenator", L"paper_page_count", L"", probe,
                             16, g.config_path);
    restored_ready = has_type || probe[0] != 0;
  }

  if (restored_count < 1) restored_count = 1;
  if (restored_count > PAPER_MAX_PAGES) restored_count = PAPER_MAX_PAGES;
  if (restored_index < 0) restored_index = 0;
  if (restored_index >= restored_count) restored_index = restored_count - 1;

  wchar_t buf[MAX_PATH];
  GetPrivateProfileStringW(L"screenator", L"paper_pdf_path", L"", buf,
                           MAX_PATH, g.config_path);
  wcsncpy(restored_pdf_path, buf, MAX_PATH - 1);
  restored_pdf_path[MAX_PATH - 1] = 0;

  wchar_t types[256];
  GetPrivateProfileStringW(L"screenator", L"paper_page_types", L"", types,
                           256, g.config_path);
  if (types[0])
    {
      wchar_t *p = types;
      while (*p && restored_ntypes < PAPER_MAX_PAGES)
        {
          wchar_t *end = NULL;
          long v = wcstol(p, &end, 10);
          if (end == p) break;
          Paper bg = (Paper) v;
          if (bg != PAPER_PLAIN && bg != PAPER_RULED && bg != PAPER_GRAPH &&
              bg != PAPER_PDF)
            bg = PAPER_PLAIN;
          restored_types[restored_ntypes++] = bg;
          p = end;
          while (*p == L',' || *p == L' ') p++;
        }
    }
  pdf_history_load();
  /* Accept legacy session keys, but retain only per-file PDF positions. */
  if (restored_pdf_path[0] && pdf_history_page(restored_pdf_path) < 0)
    pdf_history_remember(restored_pdf_path, restored_index);
  restored_ready = 0;
  restored_count = 0;
  restored_index = 0;
  restored_ntypes = 0;
  restored_bg = PAPER_PLAIN;
  restored_pdf_path[0] = 0;
}

void paper_config_save(void)
{
  if (!g.config_path[0] || g.smoke) return;

  Paper type;
  int count, index;
  if (doc_ready)
    {
      type = current_type;
      count = page_count;
      index = page_index;
    }
  else if (restored_ready)
    {
      type = restored_bg;
      count = restored_count;
      index = restored_index;
    }
  else
    {
      type = PAPER_PLAIN;
      count = 0;
      index = 0;
    }

  wchar_t num[64];
  _snwprintf(num, 64, L"%d", (int) type);
  WritePrivateProfileStringW(L"screenator", L"paper_type", num, g.config_path);
  _snwprintf(num, 64, L"%d", count);
  WritePrivateProfileStringW(L"screenator", L"paper_page_count", num,
                             g.config_path);
  _snwprintf(num, 64, L"%d", index);
  WritePrivateProfileStringW(L"screenator", L"paper_page_index", num,
                             g.config_path);

  wchar_t types[512];
  int used = 0;
  types[0] = 0;
  if (doc_ready)
    {
      for (int i = 0; i < page_count && used < 480; i++)
        used += _snwprintf(types + used, 512 - used, i ? L",%d" : L"%d",
                           (int) pages[i].bg);
    }
  else if (restored_ready)
    {
      for (int i = 0; i < restored_ntypes && used < 480; i++)
        used += _snwprintf(types + used, 512 - used, i ? L",%d" : L"%d",
                           (int) restored_types[i]);
    }
  WritePrivateProfileStringW(L"screenator", L"paper_page_types", types,
                             g.config_path);

  if (doc_ready && current_type == PAPER_PDF && pdf_path[0])
    WritePrivateProfileStringW(L"screenator", L"paper_pdf_path", pdf_path,
                               g.config_path);
  else
    WritePrivateProfileStringW(L"screenator", L"paper_pdf_path", L"",
                               g.config_path);

  paper_remember_pdf_page();

  pdf_history_save();
}

void paper_shutdown(void)
{
  if (doc_ready)
    {
      paper_sync_current();
      paper_remember_pdf_page();
    }
  paper_clear_document();
  pdf_history_save();
}

/* Reset Settings removes %APPDATA%\Screenator.  Drop the in-memory paper
 * document and remembered PDF metadata as well. */
void paper_reset(void)
{
  paper_clear_document();
  if (screen_ann) paper_clear_surface(screen_ann);
  restored_ready = 0;
  restored_count = 0;
  restored_index = 0;
  restored_ntypes = 0;
  restored_bg = PAPER_PLAIN;
  restored_pdf_path[0] = 0;
  pdf_history_count = 0;
  pdf_history_loaded = 1;
  page_change_last = 0;
  g.paper = PAPER_NONE;
}

/* ------------------------------------------------------------------ */
/* self check                                                         */
/* ------------------------------------------------------------------ */

/* Write a tiny one-page PDF with a red rectangle.  This gives the selftest a
 * real PDFium document without shipping a binary test fixture. */
static int paper_write_test_pdf(const wchar_t *path)
{
  const char *content = "1 0 0 RG\n5 w\n50 50 300 200 re\nS\n";
  int clen = (int) strlen(content);
  long off[5];
  memset(off, 0, sizeof(off));

  FILE *f = _wfopen(path, L"wb");
  if (!f) return 0;

  fputs("%PDF-1.4\n", f);

  off[1] = ftell(f);
  fputs("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n", f);
  off[2] = ftell(f);
  fputs("2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n", f);
  off[3] = ftell(f);
  fputs("3 0 obj\n<< /Type /Page /Parent 2 0 R "
        "/MediaBox [0 0 400 300] /Contents 4 0 R "
        "/Resources << >> >>\nendobj\n", f);
  off[4] = ftell(f);
  fprintf(f, "4 0 obj\n<< /Length %d >>\nstream\n", clen);
  fwrite(content, 1, (size_t) clen, f);
  fputs("\nendstream\nendobj\n", f);

  long xref = ftell(f);
  fputs("xref\n0 5\n0000000000 65535 f \n", f);
  for (int i = 1; i <= 4; i++)
    fprintf(f, "%010ld 00000 n \n", off[i]);
  fputs("trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n", f);
  fprintf(f, "%ld\n%%%%EOF\n", xref);

  int ok = ferror(f) == 0;
  fclose(f);
  return ok;
}

static int paper_file_has_image(const wchar_t *path)
{
  FILE *f = _wfopen(path, L"rb");
  long size;
  char *buf;
  int found = 0;

  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0)
    {
      fclose(f);
      return 0;
    }
  buf = (char *) malloc((size_t) size + 1);
  if (buf && fread(buf, 1, (size_t) size, f) == (size_t) size)
    {
      buf[size] = 0;
      if (strstr(buf, "/Subtype /Image") || strstr(buf, "/Subtype/Image"))
        found = 1;
    }
  free(buf);
  fclose(f);
  return found;
}

int paper_selfcheck(char *log, int log_size)
{  int ok = 1;
  size_t pos = 0;
  int old_vw = g.vw, old_vh = g.vh;
  int old_size_kind = g.paper_size_kind;
  int old_custom_w = g.paper_custom_w_mm;
  int old_custom_h = g.paper_custom_h_mm;
  cairo_surface_t *old_back = g.backbuf;
  Paper old_paper = g.paper;

  if (!log || log_size <= 0) return 0;

  g.vw = 640;
  g.vh = 480;
  g.paper_size_kind = PAPER_SIZE_SCREEN;
  g.paper = PAPER_NONE;
  g.backbuf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, g.vw, g.vh);

  /* Legacy session metadata must not reopen pages or replace PDF bookmarks. */
  {
    wchar_t saved_config[MAX_PATH], dir[MAX_PATH], ini[MAX_PATH];
    PdfHistoryEntry saved_history[32];
    int saved_count = pdf_history_count;
    int saved_loaded = pdf_history_loaded;
    int saved_test_mode = paper_test_mode;
    memcpy(saved_config, g.config_path, sizeof(saved_config));
    memcpy(saved_history, pdf_history, sizeof(saved_history));
    app_exe_dir(dir, MAX_PATH);
    if (!GetTempFileNameW(dir, L"scr", 0, ini))
      ok = 0;
    else
      {
        wcscpy(g.config_path, ini);
        WritePrivateProfileStringW(L"screenator", L"paper_type", L"4", ini);
        WritePrivateProfileStringW(L"screenator", L"paper_page_count", L"9", ini);
        WritePrivateProfileStringW(L"screenator", L"paper_page_index", L"4", ini);
        WritePrivateProfileStringW(L"screenator", L"paper_pdf_path",
                                   L"startup-test.pdf", ini);
        pdf_history_loaded = 1;
        pdf_history_count = 1;
        wcscpy(pdf_history[0].path, L"startup-test.pdf");
        pdf_history[0].page = 6;
        paper_test_mode = 1;
        paper_config_load();
        if (restored_ready || restored_count || restored_pdf_path[0] ||
            pdf_history_page(L"startup-test.pdf") != 6)
          {
            ok = 0;
            pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                     "paper: startup reset failed\n");
          }
        DeleteFileW(ini);
      }
    memcpy(g.config_path, saved_config, sizeof(saved_config));
    memcpy(pdf_history, saved_history, sizeof(saved_history));
    pdf_history_count = saved_count;
    pdf_history_loaded = saved_loaded;
    paper_test_mode = saved_test_mode;
  }

  {
    static const Paper types[4] = { PAPER_PLAIN, PAPER_RULED, PAPER_GRAPH,
                                    PAPER_PDF };
    static const wchar_t *prefix[4] = { L"Screenator-plain-",
                                        L"Screenator-ruled-",
                                        L"Screenator-graph-",
                                        L"Screenator-pdf-" };
    for (int i = 0; i < 4; i++)
      {
        wchar_t name[128];
        paper_default_filename(types[i], name, 128);
        size_t n = wcslen(name);
        if (wcsncmp(name, prefix[i], wcslen(prefix[i])) != 0 ||
            n < 24 || wcscmp(name + n - 4, L".pdf") != 0)
          {
            ok = 0;
            pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                     "paper: filename failed for %s\n",
                                     paper_type_name(types[i]));
          }
      }
  }

  paper_new_document(PAPER_RULED);
  if (paper_page_count() != 1 || paper_page_background() != PAPER_RULED)
    {
      ok = 0;
      pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                               "paper: new document failed\n");
    }
  else
    pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                             "paper: 1 ruled page\n");

  if (!paper_insert_page(0)) /* after */
    { ok = 0; pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                       "paper: insert after failed\n"); }
  else if (paper_page_count() != 2 || paper_page_index() != 1)
    { ok = 0; pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                       "paper: insert after state wrong\n"); }
  else
    pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                             "paper: insert after ok\n");

  if (!paper_insert_page(1)) /* before */
    { ok = 0; pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                       "paper: insert before failed\n"); }
  else if (paper_page_count() != 3 || paper_page_index() != 1)
    { ok = 0; pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                       "paper: insert before state wrong\n"); }
  else
    pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                             "paper: insert before ok\n");

  if (!paper_delete_page())
    { ok = 0; pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                       "paper: delete failed\n"); }
  else if (paper_page_count() != 2)
    { ok = 0; pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                       "paper: delete state wrong\n"); }
  else
    pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                             "paper: delete ok\n");

  if (!paper_page_undo())
    { ok = 0; pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                       "paper: page undo failed\n"); }
  else if (paper_page_count() != 3)
    { ok = 0; pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                       "paper: page undo state wrong\n"); }
  else
    pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                             "paper: page undo ok\n");

  if (!paper_page_redo())
    { ok = 0; pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                       "paper: page redo failed\n"); }
  else if (paper_page_count() != 2)
    { ok = 0; pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                       "paper: page redo state wrong\n"); }
  else
    pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                             "paper: page redo ok\n");

  /* Annotation surfaces are per page and survive a page switch. */
  paper_goto_page(0);
  if (g.backbuf)
    {
      cairo_t *cr = cairo_create(g.backbuf);
      cairo_set_source_rgba(cr, 1, 0, 0, 1);
      cairo_rectangle(cr, 10, 10, 20, 20);
      cairo_fill(cr);
      cairo_destroy(cr);
      paper_sync_current();
      paper_goto_page(1);
      paper_goto_page(0);
      cairo_surface_flush(g.backbuf);
      unsigned int *px = (unsigned int *) cairo_image_surface_get_data(g.backbuf);
      int stride = cairo_image_surface_get_stride(g.backbuf) / 4;
      unsigned int got = px[12 * stride + 12];
      if ((got & 0x00FFFFFF) == 0)
        {
          ok = 0;
          pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                   "paper: page annotation lost\n");
        }
      else
        pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                 "paper: page annotations isolated\n");
    }

  /* Plain, Ruled and Graph keep separate documents and annotations. */
  {
    cairo_t *cr;
    unsigned int *px;
    int st;
    int plain_ok = 0, ruled_ok = 0;

    paper_new_document(PAPER_PLAIN);
    g.paper = PAPER_PLAIN;
    cr = cairo_create(g.backbuf);
    cairo_set_source_rgba(cr, 1, 0, 0, 1);
    cairo_rectangle(cr, 10, 10, 20, 20);
    cairo_fill(cr);
    cairo_destroy(cr);
    paper_sync_current();

    if (!paper_select_type(PAPER_RULED) ||
        paper_page_background() != PAPER_RULED)
      { ok = 0; pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                         "paper: ruled document failed\n"); }
    else if (paper_page_has_annotations())
      { ok = 0; pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                         "paper: ruled shares plain "
                                         "annotations\n"); }
    else
      {
        cr = cairo_create(g.backbuf);
        cairo_set_source_rgba(cr, 0, 0, 1, 1);
        cairo_rectangle(cr, 40, 40, 20, 20);
        cairo_fill(cr);
        cairo_destroy(cr);
        paper_sync_current();

        if (!paper_select_type(PAPER_PLAIN))
          { ok = 0; pos += (size_t) snprintf(log + pos,
                                             (size_t) log_size - pos,
                                             "paper: plain document lost\n"); }
        else
          {
            cairo_surface_flush(g.backbuf);
            px = (unsigned int *) cairo_image_surface_get_data(g.backbuf);
            st = cairo_image_surface_get_stride(g.backbuf) / 4;
            plain_ok = (px[12 * st + 12] & 0x00FFFFFF) != 0 &&
                       (px[42 * st + 42] & 0x00FFFFFF) == 0;

            if (!paper_select_type(PAPER_RULED))
              { ok = 0; pos += (size_t) snprintf(log + pos,
                                                 (size_t) log_size - pos,
                                                 "paper: ruled document "
                                                 "lost\n"); }
            else
              {
                cairo_surface_flush(g.backbuf);
                px = (unsigned int *) cairo_image_surface_get_data(g.backbuf);
                st = cairo_image_surface_get_stride(g.backbuf) / 4;
                ruled_ok = (px[42 * st + 42] & 0x00FFFFFF) != 0 &&
                           (px[12 * st + 12] & 0x00FFFFFF) == 0;
                if (!plain_ok || !ruled_ok)
                  { ok = 0; pos += (size_t) snprintf(
                      log + pos, (size_t) log_size - pos,
                      "paper: paper documents not separate (%d/%d)\n",
                      plain_ok, ruled_ok); }
                else
                  pos += (size_t) snprintf(log + pos,
                                           (size_t) log_size - pos,
                                           "paper: separate paper documents "
                                           "ok\n");
              }
          }
      }
    paper_new_document(PAPER_PLAIN);
    g.paper = PAPER_NONE;
  }

  /* Screen-mode strokes must never be written into a paper page. */
  {
    cairo_t *cr;
    unsigned int *px;
    int st;

    paper_new_document(PAPER_PLAIN);
    g.paper = PAPER_PLAIN;
    cr = cairo_create(g.backbuf);
    cairo_set_source_rgba(cr, 1, 0, 0, 1);
    cairo_rectangle(cr, 10, 10, 20, 20);
    cairo_fill(cr);
    cairo_destroy(cr);
    paper_sync_current();

    /* Back in Screen mode: this stroke belongs to the screen layer only. */
    g.paper = PAPER_NONE;
    cr = cairo_create(g.backbuf);
    cairo_set_source_rgba(cr, 0, 0, 1, 1);
    cairo_rectangle(cr, 60, 60, 20, 20);
    cairo_fill(cr);
    cairo_destroy(cr);
    paper_sync_current();

    g.paper = PAPER_PLAIN;
    paper_reload_current();
    cairo_surface_flush(g.backbuf);
    px = (unsigned int *) cairo_image_surface_get_data(g.backbuf);
    st = cairo_image_surface_get_stride(g.backbuf) / 4;
    if ((px[12 * st + 12] & 0x00FFFFFF) == 0 ||
        (px[70 * st + 70] & 0x00FFFFFF) != 0)
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                 "paper: screen strokes leaked into paper\n");
      }
    else
      pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                               "paper: screen and paper layers separate\n");
    g.paper = PAPER_NONE;
  }

  /* Drawing undo spans pages and follows the page each step belongs to. */
  {
    cairo_t *cr;

    paper_new_document(PAPER_PLAIN);
    g.paper = PAPER_PLAIN;
    paper_insert_page(0);
    paper_goto_page(0);

    tools_push_undo();
    cr = cairo_create(g.backbuf);
    cairo_set_source_rgba(cr, 1, 0, 0, 1);
    cairo_rectangle(cr, 10, 10, 20, 20);
    cairo_fill(cr);
    cairo_destroy(cr);
    paper_sync_current();

    paper_goto_page(1);
    tools_push_undo();
    cr = cairo_create(g.backbuf);
    cairo_set_source_rgba(cr, 0, 0, 1, 1);
    cairo_rectangle(cr, 40, 40, 20, 20);
    cairo_fill(cr);
    cairo_destroy(cr);
    paper_sync_current();

    tools_undo();
    if (paper_page_index() != 1 || paper_page_has_annotations())
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                 "paper: undo on page 2 failed\n");
      }
    else
      {
        tools_undo();
        if (paper_page_index() != 0 || paper_page_has_annotations())
          {
            ok = 0;
            pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                     "paper: undo across pages failed\n");
          }
        else
          pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                   "paper: multi-page undo ok\n");
      }
    /* Redo must capture/restore its own page even after manual navigation.
     * Repeat the round trip to check the undo snapshots produced by redo. */
    for (int round = 0; round < 2; round++)
      {
        for (int page = 0; page < 2; page++)
          {
            paper_goto_page(1 - page);
            tools_redo();
            cairo_surface_flush(g.backbuf);
            int x = page ? 45 : 15;
            int stride = cairo_image_surface_get_stride(g.backbuf) / 4;
            uint32_t *pixels = (uint32_t *)
              cairo_image_surface_get_data(g.backbuf);
            uint32_t expected = page ? 0xff0000ffu : 0xffff0000u;
            if (paper_page_index() != page || pixels[x * stride + x] != expected)
              {
                ok = 0;
                pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                         "paper: multi-page redo failed\n");
              }
          }
        for (int page = 1; page >= 0; page--)
          {
            paper_goto_page(1 - page);
            tools_undo();
            if (paper_page_index() != page || paper_page_has_annotations())
              {
                ok = 0;
                pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                         "paper: redo/undo round trip failed\n");
              }
          }
      }
    paper_new_document(PAPER_PLAIN);
    g.paper = PAPER_NONE;
  }

  /* A real PDF through PDFium: load, raster render and export. */
  {
    wchar_t dir[MAX_PATH];
    wchar_t pdfpath[MAX_PATH];
    wchar_t outpath[MAX_PATH];
    app_exe_dir(dir, MAX_PATH);
    _snwprintf(pdfpath, MAX_PATH, L"%s\\paper-selftest.pdf", dir);
    _snwprintf(outpath, MAX_PATH, L"%s\\paper-selftest-out.pdf", dir);

    paper_test_mode = 1;
    if (!paper_write_test_pdf(pdfpath))
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                 "paper: test PDF write failed\n");
      }
    else
      {
        char err[384] = "";
        if (!paper_open_pdf_path(pdfpath, err, sizeof(err)))
          {
            ok = 0;
            pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                     "paper: PDF open failed (%s)\n", err);
          }
        else if (paper_page_count() != 1 ||
                 paper_page_background() != PAPER_PDF)
          {
            ok = 0;
            pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                     "paper: PDF page model wrong\n");
          }
        else
          {
            pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                     "paper: PDF opened (1 page)\n");

            int w = paper_surface_w(), h = paper_surface_h();
            cairo_surface_t *ts =
              cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
            cairo_t *tcr = cairo_create(ts);
            cairo_set_operator(tcr, CAIRO_OPERATOR_CLEAR);
            cairo_paint(tcr);
            cairo_set_operator(tcr, CAIRO_OPERATOR_OVER);
            paper_draw_pdf_page(tcr, w, h);
            cairo_destroy(tcr);
            cairo_surface_flush(ts);

            unsigned char *data = cairo_image_surface_get_data(ts);
            int stride = cairo_image_surface_get_stride(ts);
            int red = 0;
            for (int y = 0; y < h; y++)
              for (int x = 0; x < w; x++)
                {
                  unsigned char *p = data + y * stride + x * 4;
                  if (p[2] > 160 && p[1] < 120 && p[0] < 120) red++;
                }
            unsigned char *corner = data + 2 * stride + 2 * 4;
            int opaque_paper = corner[3] > 0 &&
                               corner[0] > 200 && corner[1] > 200 &&
                               corner[2] > 200;
            cairo_surface_destroy(ts);
            if (red < 20 || !opaque_paper)
              {
                ok = 0;
                pos += (size_t) snprintf(log + pos,
                                         (size_t) log_size - pos,
                                         "paper: PDF render produced no "
                                         "content (%d red pixels, paper=%d)\n",
                                         red, opaque_paper);
              }
            else
              pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                       "paper: PDF render (%d px)\n", red);

            char e2[384] = "";
            /* Add a vector stroke and text to the PDF page before export. */
            {
              Color vc = { 0, 0, 1, 1 };
              LineShape vline;
              FrameShape vtext;
              cairo_t *vcr;

              g.paper = PAPER_PDF;
              vcr = paper_vector_context();
              if (vcr)
                {
                  memset(&vline, 0, sizeof(vline));
                  vline.x0 = 60;
                  vline.y0 = 120;
                  vline.x1 = 420;
                  vline.y1 = 180;
                  draw_line_shape(vcr, &vline, 10, vc);
                  cairo_destroy(vcr);
                }
              memset(&vtext, 0, sizeof(vtext));
              vtext.type = SHAPE_TEXT;
              vtext.text = (char *) "Vector text";
              vtext.font = (char *) "Arial";
              vtext.font_size = 30;
              vtext.frame.cx = 300;
              vtext.frame.cy = 260;
              vtext.frame.w = 220;
              vtext.frame.h = 44;
              vtext.color = (Color) { 0, 0, 0, 1 };
              paper_add_text(&vtext);
            }

            /* Switching to Plain and back keeps this PDF document and its
             * annotations, and leaves the Plain document empty. */
            paper_sync_current();
            if (!paper_select_type(PAPER_PLAIN) ||
                paper_page_background() != PAPER_PLAIN ||
                paper_page_has_annotations())
              {
                ok = 0;
                pos += (size_t) snprintf(log + pos,
                                         (size_t) log_size - pos,
                                         "paper: PDF annotations leak into "
                                         "Plain\n");
              }
            else if (!paper_select_type(PAPER_PDF) ||
                     paper_page_background() != PAPER_PDF ||
                     !paper_page_has_annotations())
              {
                ok = 0;
                pos += (size_t) snprintf(log + pos,
                                         (size_t) log_size - pos,
                                         "paper: PDF annotations lost on "
                                         "switch\n");
              }
            else
              pos += (size_t) snprintf(log + pos,
                                       (size_t) log_size - pos,
                                       "paper: separate PDF document ok\n");

            /* Closing saves every dirty document, including stored paper
             * types and the PDF, and clears only successfully saved flags. */
            {
              wchar_t batchdir[MAX_PATH];
              PendingPaperSave pending[4];
              for (Paper type = PAPER_PLAIN; type <= PAPER_RULED; type++)
                {
                  paper_select_type(type);
                  paper_insert_page(0);
                  paper_note_drawing_change();
                }
              paper_select_type(PAPER_PDF);
              int n = paper_collect_pending(pending);
              if (n < 3 || !GetTempFileNameW(dir, L"sav", 0, batchdir))
                ok = 0;
              else
                {
                  int expected[4];
                  for (int i = 0; i < n; i++)
                    expected[i] = pending[i].type == current_type ? page_count :
                                    doc_store[pending[i].type].count;
                  DeleteFileW(batchdir);
                  paper_leave();
                  g.paper = PAPER_NONE;
                  cairo_surface_flush(g.backbuf);
                  uint32_t screen_pixel = *(uint32_t *)
                    cairo_image_surface_get_data(g.backbuf);
                  /* The missing directory must fail without clearing flags. */
                  int failed = !paper_save_pending(pending, n, batchdir);
                  int remaining = paper_collect_pending(pending);
                  if (!failed || remaining != n || g.paper != PAPER_NONE ||
                      !CreateDirectoryW(batchdir, NULL) ||
                      !paper_save_pending(pending, n, batchdir))
                    ok = 0;
                  else
                    {
                      for (int i = 0; i < n; i++)
                        {
                          char *file = paper_utf8_from_wide(pending[i].path);
                          FPDF_DOCUMENT saved = file ? FPDF_LoadDocument(file, NULL) : NULL;
                          if (!saved || FPDF_GetPageCount(saved) != expected[i]) ok = 0;
                          if (saved) FPDF_CloseDocument(saved);
                          free(file);
                        }
                      PendingPaperSave remaining_items[4];
                      cairo_surface_flush(g.backbuf);
                      if (paper_collect_pending(remaining_items) ||
                          current_type != PAPER_PDF || g.paper != PAPER_NONE ||
                          *(uint32_t *) cairo_image_surface_get_data(g.backbuf) !=
                            screen_pixel) ok = 0;
                    }
                  for (int i = 0; i < n; i++) DeleteFileW(pending[i].path);
                  RemoveDirectoryW(batchdir);
                  paper_enter(PAPER_PDF);
                }
            }

            if (!paper_export_path(outpath, e2, sizeof(e2)))
              {
                ok = 0;
                pos += (size_t) snprintf(log + pos,
                                         (size_t) log_size - pos,
                                         "paper: PDF export failed (%s)\n",
                                         e2);
              }
            else
              {
                WIN32_FILE_ATTRIBUTE_DATA fa;
                if (!GetFileAttributesExW(outpath, GetFileExInfoStandard,
                                          &fa) || fa.nFileSizeLow < 500)
                  {
                    ok = 0;
                    pos += (size_t) snprintf(log + pos,
                                             (size_t) log_size - pos,
                                             "paper: exported PDF is empty\n");
                  }
                else
                  {
                    pos += (size_t) snprintf(log + pos,
                                             (size_t) log_size - pos,
                                             "paper: PDF export wrote %lu bytes\n",
                                             (unsigned long) fa.nFileSizeLow);
                    char e3[384] = "";
                    if (!paper_open_pdf_path(outpath, e3, sizeof(e3)) ||
                        paper_page_count() != 1)
                      {
                        ok = 0;
                        pos += (size_t) snprintf(log + pos,
                                                 (size_t) log_size - pos,
                                                 "paper: exported PDF cannot be "
                                                 "reopened (%s)\n", e3);
                      }
                    else
                      {
                        pos += (size_t) snprintf(log + pos,
                                                 (size_t) log_size - pos,
                                                 "paper: exported PDF reopened\n");
                        {
                          FPDF_PAGE vp = FPDF_LoadPage(pdf_doc, 0);
                          int forms = 0, images = 0;
                          int text_ok = 0;
                          int has_image = paper_file_has_image(outpath);
                          if (vp)
                            {
                              int vn = FPDFPage_CountObjects(vp);
                              for (int vi = 0; vi < vn; vi++)
                                {
                                  FPDF_PAGEOBJECT vo =
                                    FPDFPage_GetObject(vp, vi);
                                  int vt = vo ? FPDFPageObj_GetType(vo) : -1;
                                  if (vt == FPDF_PAGEOBJ_FORM) forms++;
                                  else if (vt == FPDF_PAGEOBJ_IMAGE) images++;
                                }
                              {
                                FPDF_TEXTPAGE tp = FPDFText_LoadPage(vp);
                                if (tp)
                                  {
                                    int cn = FPDFText_CountChars(tp);
                                    if (cn > 0)
                                      {
                                        unsigned short *buf =
                                          (unsigned short *) malloc(
                                            ((size_t) cn + 1) *
                                            sizeof(unsigned short));
                                        if (buf)
                                          {
                                            int got = FPDFText_GetText(
                                              tp, 0, cn, buf);
                                            if (got < 0) got = 0;
                                            /* The return value includes the
                                             * trailing terminator, so it can be
                                             * cn + 1; clamp it to the buffer. */
                                            if (got > cn) got = cn;
                                            buf[got] = 0;
                                            text_ok = wcsstr(
                                              (wchar_t *) buf,
                                              L"Vector text") != NULL;
                                            free(buf);
                                          }
                                      }
                                    FPDFText_ClosePage(tp);
                                  }
                              }
                              FPDF_ClosePage(vp);
                            }
                          if (images || has_image || forms < 2 || !text_ok)
                            {
                              ok = 0;
                              pos += (size_t) snprintf(
                                log + pos, (size_t) log_size - pos,
                                "paper: PDF backdrop is not vector "
                                "(forms=%d image=%d/%d text=%d)\n",
                                forms, images, has_image, text_ok);
                            }
                          else
                            pos += (size_t) snprintf(
                              log + pos, (size_t) log_size - pos,
                              "paper: PDF page annotations vector\n");
                        }
                      }
                  }
              }
          }
      }
    paper_test_mode = 0;
    DeleteFileW(pdfpath);
    DeleteFileW(outpath);
    paper_new_document(PAPER_PLAIN);
    g.paper = PAPER_NONE;
  }

  /* Broken/missing PDF must fail with a useful message, not crash. */
  {
    char err[256] = "";
    int opened = paper_open_pdf_path(
      L"Z:\\Screenator\\missing-paper-selftest.pdf", err, sizeof(err));
    if (opened || !err[0])
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                 "paper: missing PDF accepted\n");
      }
    else
      pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                               "paper: missing PDF rejected (%s)\n", err);
  }

  /* Continuous view: scrolling, page selection and coordinate mapping. */
  {
    int old_cont = g.paper_continuous_scroll;
    Pt p;

    paper_new_document(PAPER_PLAIN);
    paper_insert_page(0);              /* a second page */
    g.paper_continuous_scroll = 1;
    paper_goto_page(0);
    p.x = 100;
    p.y = 300;
    paper_map_point(&p);
    if (!paper_continuous_active() || paper_page_count() != 2 ||
        (int) p.y != 300)
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                 "paper: continuous mapping failed\n");
      }
    else
      pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                               "paper: continuous view active\n");

    paper_scroll_step(1);
    p.x = 100;
    p.y = 300;
    paper_map_point(&p);
    if ((int) p.y != 380)
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                 "paper: continuous scroll mapping failed "
                                 "(%d)\n", (int) p.y);
      }
    else
      pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                               "paper: continuous scroll ok\n");

    p.x = 100;
    p.y = 450;                         /* page 2 top is at 418 */
    paper_prepare_point(&p);
    if (paper_page_index() != 1 || (int) p.y != 32)
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                 "paper: continuous page select failed\n");
      }
    else
      pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                               "paper: continuous page select ok\n");

    paper_goto_page(0);
    g.paper_continuous_scroll = old_cont;
    paper_new_document(PAPER_PLAIN);
  }

  /* Vector export: annotations must be paths/text, never a raster image. */
  {
    wchar_t dir[MAX_PATH];
    wchar_t out[MAX_PATH];
    char err[256] = "";
    int wrote = 0;

    app_exe_dir(dir, MAX_PATH);
    _snwprintf(out, MAX_PATH, L"%s\\paper-vector-selftest.pdf", dir);

    paper_test_mode = 1;
    paper_new_document(PAPER_PLAIN);
    g.paper = PAPER_PLAIN;

    cairo_t *v = paper_vector_context();
    if (!v)
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                 "paper: vector context failed\n");
      }
    else
      {
        Color red = { 1, 0, 0, 1 };
        Color black = { 0, 0, 0, 1 };
        LineShape line;
        FrameShape frame;

        memset(&line, 0, sizeof(line));
        line.x0 = 40;
        line.y0 = 60;
        line.x1 = 500;
        line.y1 = 90;
        draw_line_shape(v, &line, 8, red);

        memset(&frame, 0, sizeof(frame));
        frame.type = SHAPE_TEXT;
        frame.text = (char *) "Vector text";
        frame.font = (char *) "Arial";
        frame.font_size = 32;
        frame.frame.cx = 400;
        frame.frame.cy = 240;
        frame.frame.w = 240;
        frame.frame.h = 48;
        frame.color = black;
        paper_add_text(&frame);

        cairo_destroy(v);

        /* A second page with a vector line, so multi-page export is checked. */
        paper_insert_page(0);          /* after page 0 */
        v = paper_vector_context();
        if (v)
          {
            memset(&line, 0, sizeof(line));
            line.x0 = 80;
            line.y0 = 500;
            line.x1 = 700;
            line.y1 = 620;
            draw_line_shape(v, &line, 8, red);
            cairo_destroy(v);
          }
        else
          {
            ok = 0;
            pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                     "paper: second vector page failed\n");
          }

        if (!paper_export_path(out, err, sizeof(err)))
          {
            ok = 0;
            pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                     "paper: vector export failed (%s)\n", err);
          }
        else
          wrote = 1;
      }

    if (wrote)
      {
        char *narrow = paper_utf8_from_wide(out);
        FPDF_DOCUMENT vdoc = narrow ? FPDF_LoadDocument(narrow, "") : NULL;
        free(narrow);
        if (!vdoc)
          {
            ok = 0;
            pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                     "paper: vector export unreadable\n");
          }
        else
          {
            int pages = FPDF_GetPageCount(vdoc);
            int forms0 = 0, forms1 = 0, chars = 0, text_ok = 0;
            int has_image = paper_file_has_image(out);
            FPDF_PAGE vpage = pages > 0 ? FPDF_LoadPage(vdoc, 0) : NULL;
            if (vpage)
              {
                int objects = FPDFPage_CountObjects(vpage);
                for (int i = 0; i < objects; i++)
                  {
                    FPDF_PAGEOBJECT obj = FPDFPage_GetObject(vpage, i);
                    if (obj && FPDFPageObj_GetType(obj) == FPDF_PAGEOBJ_FORM)
                      forms0++;
                  }
                {
                  FPDF_TEXTPAGE tp = FPDFText_LoadPage(vpage);
                  if (tp)
                    {
                      chars = FPDFText_CountChars(tp);
                      if (chars > 0)
                        {
                          unsigned short *buf = (unsigned short *)
                            malloc(((size_t) chars + 1) *
                                   sizeof(unsigned short));
                          if (buf)
                            {
                              int got = FPDFText_GetText(tp, 0, chars, buf);
                              if (got < 0) got = 0;
                              /* Includes the trailing terminator. */
                              if (got > chars) got = chars;
                              buf[got] = 0;
                              text_ok = wcsstr((wchar_t *) buf,
                                               L"Vector text") != NULL;
                              free(buf);
                            }
                        }
                      FPDFText_ClosePage(tp);
                    }
                }
                FPDF_ClosePage(vpage);
              }
            if (pages > 1)
              {
                FPDF_PAGE vp1 = FPDF_LoadPage(vdoc, 1);
                if (vp1)
                  {
                    int objects = FPDFPage_CountObjects(vp1);
                    for (int i = 0; i < objects; i++)
                      {
                        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(vp1, i);
                        if (obj && FPDFPageObj_GetType(obj) == FPDF_PAGEOBJ_FORM)
                          forms1++;
                      }
                    FPDF_ClosePage(vp1);
                  }
              }
            FPDF_CloseDocument(vdoc);

            if (has_image || pages != 2 || forms0 < 2 || forms1 < 2 ||
                !text_ok)
              {
                ok = 0;
                pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                         "paper: vector export layers "
                                         "(pages=%d forms=%d/%d char=%d "
                                         "text=%d image=%d)\n",
                                         pages, forms0, forms1, chars, text_ok,
                                         has_image);
              }
            else
              pos += (size_t) snprintf(log + pos, (size_t) log_size - pos,
                                       "paper: multi-page vector export ok "
                                       "(forms=%d/%d text=%d image=0)\n",
                                       forms0, forms1, text_ok);
          }
        DeleteFileW(out);
      }

    paper_test_mode = 0;
    paper_new_document(PAPER_PLAIN);
    g.paper = PAPER_NONE;
  }

  cairo_surface_destroy(g.backbuf);
  g.backbuf = old_back;
  g.vw = old_vw;
  g.vh = old_vh;
  g.paper_size_kind = old_size_kind;
  g.paper_custom_w_mm = old_custom_w;
  g.paper_custom_h_mm = old_custom_h;
  g.paper = old_paper;
  paper_clear_document();

  return ok;
}

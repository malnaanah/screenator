/*
 * Screenator for Windows -- PDFium bridge.
 * GPL-2.0-or-later.  See app.h.
 */
#include "app.h"
#include <fpdfview.h>

static int pdfium_started;

void pdfium_init(void)
{
  if (pdfium_started) return;
  FPDF_InitLibrary();
  pdfium_started = 1;
  dlog("pdfium init err=%lu", (unsigned long) FPDF_GetLastError());
}

void pdfium_shutdown(void)
{
  if (!pdfium_started) return;
  FPDF_DestroyLibrary();
  pdfium_started = 0;
}

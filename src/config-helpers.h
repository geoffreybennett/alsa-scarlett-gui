// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

// Bold heading label, start-aligned. Suitable for use as a section
// heading.
GtkWidget *config_bold_label(const char *text);

// Start-aligned, dim help text. Use this for help text that uses
// explicit newlines for layout (no automatic wrapping).
GtkWidget *config_help_label(const char *text);

// Start-aligned, dim, wrap-enabled help text. max_chars caps the
// natural width so the description doesn't widen the window
// unnecessarily; pass -1 to leave the width uncapped.
GtkWidget *config_wrapped_help_label(const char *text, int max_chars);

// Horizontal separator with a small margin above and below; appended
// to box.
void config_append_separator(GtkBox *box);

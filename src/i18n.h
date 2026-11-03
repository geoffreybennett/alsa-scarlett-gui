// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <libintl.h>

#ifndef _
#define _(String) gettext(String)
#endif

#ifndef N_
#define N_(String) (String)
#endif

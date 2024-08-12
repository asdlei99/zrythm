// SPDX-FileCopyrightText: © 2024 Alexandros Theodotou <alex@zrythm.org>
// SPDX-License-Identifier: LicenseRef-ZrythmLicense

#ifndef __GTK_WRAPPER_H__
#define __GTK_WRAPPER_H__

#include "zrythm-config.h"

#include <glibmm.h>

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
#include <gtk/gtk.h>
G_GNUC_END_IGNORE_DEPRECATIONS

#ifdef HAVE_X11
/* hack to drop dumb typedefs and macros defined by X11 */
#  define Region RegionForX11
#  define None NoneForX11
#  define Status StatusForX11
#  define DestroyNotify DestroyNotifyForX11
#  define Color ColorForX11
#  include <gdk/x11/gdkx.h>
#  undef Region
#  undef None
#  undef Status
#  undef DestroyNotify
#  undef Color
#endif

#ifdef _WIN32
#  include <gdk/win32/gdkwin32.h>
#endif

#endif // __GTK_WRAPPER_H__
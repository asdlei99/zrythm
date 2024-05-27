// clang-format off
// SPDX-FileCopyrightText: © 2018-2019, 2022 Alexandros Theodotou <alex@zrythm.org>
// SPDX-License-Identifier: LicenseRef-ZrythmLicense
// clang-format on

/**
 * \file
 *
 * Base widget class for Region's.
 */

#ifndef __GUI_WIDGETS_REGION_H__
#define __GUI_WIDGETS_REGION_H__

#include "dsp/region.h"
#include "gui/widgets/arranger_object.h"
#include "utils/ui.h"

#include "gtk_wrapper.h"

/**
 * @addtogroup widgets
 *
 * @{
 */

#define REGION_NAME_FONT_NO_SIZE "Bold"
#ifdef _WIN32
#  define REGION_NAME_FONT REGION_NAME_FONT_NO_SIZE " 7"
#else
#  define REGION_NAME_FONT REGION_NAME_FONT_NO_SIZE " 8"
#endif
#define REGION_NAME_PADDING_R 5
#define REGION_NAME_BOX_PADDING 2
#define REGION_NAME_BOX_CURVINESS 4.0

/**
 * Returns the lane rectangle for the region.
 */
void
region_get_lane_full_rect (Region * self, GdkRectangle * rect);

/**
 * Draws the Region in the given cairo context in
 * relative coordinates.
 *
 * @param rect Arranger rectangle.
 */
HOT void
region_draw (Region * self, GtkSnapshot * snapshot, GdkRectangle * rect);

#endif

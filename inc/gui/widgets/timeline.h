/*
 * inc/gui/widgets/timeline.h - Timeline
 *
 * Copyright (C) 2018 Alexandros Theodotou
 *
 * This file is part of Zrythm
 *
 * Zrythm is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Zrythm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __GUI_WIDGETS_TIMELINE_H__
#define __GUI_WIDGETS_TIMELINE_H__

#include <gtk/gtk.h>

#define TIMELINE_WIDGET_TYPE                  (timeline_widget_get_type ())
#define TIMELINE_WIDGET(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TIMELINE_WIDGET_TYPE, TimelineWidget))
#define TIMELINE_WIDGET_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST  ((klass), TIMELINE_WIDGET, TimelineWidgetClass))
#define IS_TIMELINE_WIDGET(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TIMELINE_WIDGET_TYPE))
#define IS_TIMELINE_WIDGET_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE  ((klass), TIMELINE_WIDGET_TYPE))
#define TIMELINE_WIDGET_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS  ((obj), TIMELINE_WIDGET_TYPE, TimelineWidgetClass))

#define MW_TIMELINE MAIN_WINDOW->timeline

typedef struct TimelineBgWidget TimelineBgWidget;

typedef enum TimelineWidgetAction
{
  TA_NONE,
  TA_RESIZING_REGION_L,
  TA_RESIZING_REGION_R,
  TA_MOVING_REGION,
  TA_SELECTING_AREA
} TimelineWidgetAction;

/**
 * TODO rename to arranger
 */
typedef struct TimelineWidget
{
  GtkOverlay               parent_instance;
  TimelineBgWidget         * bg;
  GtkDrawingArea           drawing_area;
  GtkGestureDrag           * drag;
  GtkGestureMultiPress     * multipress;
  double                   last_y;
  double                   last_offset_x;  ///< for dragging regions
  TimelineWidgetAction     action;
  Region                   * region;  ///< region doing action upon, if any
  double                   start_x; ///< for dragging
  int                      n_press; ///< for multipress
} TimelineWidget;

typedef struct TimelineWidgetClass
{
  GtkOverlayClass       parent_class;
} TimelineWidgetClass;

/**
 * Creates a timeline widget using the given timeline data.
 */
TimelineWidget *
timeline_widget_new ();

#endif


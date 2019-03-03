/*
 * Copyright (C) 2018-2019 Alexandros Theodotou <alex at zrythm dot org>
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

#ifndef __GUI_WIDGETS_ARRANGER_H__
#define __GUI_WIDGETS_ARRANGER_H__

#include "gui/widgets/main_window.h"
#include "gui/widgets/piano_roll.h"
#include "audio/position.h"
#include "utils/ui.h"

#include <gtk/gtk.h>

#define ARRANGER_WIDGET_TYPE                  (arranger_widget_get_type ())
G_DECLARE_DERIVABLE_TYPE (ArrangerWidget,
                          arranger_widget,
                          Z,
                          ARRANGER_WIDGET,
                          GtkOverlay)

#define ARRANGER_TOGGLE_SELECT_MIDI_NOTE(self, midi_note, append) \
  arranger_widget_toggle_select ( \
    Z_ARRANGER_WIDGET (self), \
    ARRANGER_CHILD_TYPE_MIDI_NOTE, \
    (void *) midi_note, \
    append);
#define ARRANGER_WIDGET_GET_PRIVATE(self) \
  ArrangerWidgetPrivate * ar_prv = \
    arranger_widget_get_private (Z_ARRANGER_WIDGET (self));
#define ARRANGER_IS_TIMELINE(self) \
  (Z_IS_TIMELINE_ARRANGER_WIDGET (self))
#define ARRANGER_IS_MIDI(self) \
  (Z_IS_MIDI_ARRANGER_WIDGET (self))
#define ARRANGER_IS_MIDI_MODIFIER(self) \
  (Z_IS_MIDI_MODIFIER_ARRANGER_WIDGET (self))

typedef struct _ArrangerBgWidget ArrangerBgWidget;
typedef struct MidiNote MidiNote;
typedef struct SnapGrid SnapGrid;
typedef struct AutomationPoint AutomationPoint;
typedef struct _ArrangerPlayheadWidget ArrangerPlayheadWidget;

typedef struct _ArrangerBgWidget ArrangerBgWidget;

typedef struct
{
  ArrangerBgWidget *       bg;
  ArrangerPlayheadWidget * playhead;
  GtkGestureDrag *         drag;
  GtkGestureMultiPress *   multipress;
  GtkGestureMultiPress *   right_mouse_mp;
  double                   last_offset_x;  ///< for dragging regions, selections
  double                   last_offset_y;  ///< for selections
  UiOverlayAction          action;
  double                   start_x; ///< for dragging
  double                   start_y; ///< for dragging

  double                   start_pos_px; ///< for moving regions

  Position                 start_pos; ///< useful for moving
  Position                 end_pos; ///< for moving regions
  /* end */

  int                      n_press; ///< for multipress
  SnapGrid *               snap_grid; ///< associated snap grid
  /**
   * If shift was held down during the press.
   */
  int                      shift_held;

  gint64                   last_frame_time;
} ArrangerWidgetPrivate;

typedef struct _ArrangerWidgetClass
{
  GtkOverlayClass       parent_class;
} ArrangerWidgetClass;

/**
 * Creates a timeline widget using the given timeline data.
 */
void
arranger_widget_setup (ArrangerWidget *   self,
                       SnapGrid *         snap_grid);

ArrangerWidgetPrivate *
arranger_widget_get_private (ArrangerWidget * self);

int
arranger_widget_pos_to_px (ArrangerWidget * self,
                          Position * pos);

void
arranger_widget_px_to_pos (ArrangerWidget * self,
                           Position * pos,
                           int              px);

/**
 * Refreshes all arranger backgrounds.
 */
void
arranger_widget_refresh_all_backgrounds ();

void
arranger_widget_get_hit_widgets_in_range (
  ArrangerWidget *  self,
  GType             type,
  double            start_x,
  double            start_y,
  double            offset_x,
  double            offset_y,
  GtkWidget **      array, ///< array to fill
  int *             array_size); ///< array_size to fill

void
arranger_widget_toggle_select (ArrangerWidget *  self,
               GType             type,
               void *            child,
               int               append);

void
arranger_widget_select_all (
  ArrangerWidget *  self,
  int               select);

/**
 * Readd children.
 */
int
arranger_widget_refresh (
  ArrangerWidget * self);

#endif

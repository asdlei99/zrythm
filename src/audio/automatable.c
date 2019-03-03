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
 * along with Zrythm.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <math.h>

#include "audio/automatable.h"
#include "audio/automation_track.h"
#include "audio/automation_tracklist.h"
#include "audio/channel.h"
#include "audio/track.h"
#include "plugins/plugin.h"
#include "plugins/lv2_plugin.h"
#include "utils/math.h"

#include <gtk/gtk.h>

static Automatable *
_create_blank ()
{
  Automatable * automatable = calloc (1, sizeof (Automatable));
  return automatable;
}

Automatable *
automatable_create_fader (Channel * channel)
{
  Automatable * automatable = _create_blank ();

  automatable->track = channel->track;
  automatable->track_id = channel->track->id;
  automatable->label = g_strdup ("Volume");
  automatable->type = AUTOMATABLE_TYPE_CHANNEL_FADER;

  return automatable;
}

Automatable *
automatable_create_pan (Channel * channel)
{
  Automatable * automatable = _create_blank ();

  automatable->track = channel->track;
  automatable->track_id = channel->track->id;
  automatable->label = g_strdup ("Pan");
  automatable->type = AUTOMATABLE_TYPE_CHANNEL_PAN;

  return automatable;
}

Automatable *
automatable_create_mute (Channel * channel)
{
  Automatable * automatable = _create_blank ();

  automatable->track = channel->track;
  automatable->track_id = channel->track->id;
  automatable->label = g_strdup ("Mute");
  automatable->type = AUTOMATABLE_TYPE_CHANNEL_MUTE;

  return automatable;
}

Automatable *
automatable_create_lv2_control (Plugin *       plugin,
                                Lv2ControlID * control)
{
  Automatable * automatable = _create_blank ();

  automatable->control = control;
  LV2_Port * port = &control->plugin->ports[control->index];
  automatable->port = port->port;
  automatable->port_id = port->port->id;
  /*automatable->index = control->index;*/
  automatable->type = AUTOMATABLE_TYPE_PLUGIN_CONTROL;
  automatable->track = plugin->channel->track;
  automatable->track_id =
    plugin->channel->track->id;
  automatable->slot_index =
    channel_get_plugin_index (plugin->channel,
                              plugin);
  automatable->label =
    g_strdup (lv2_control_get_label (control));

  return automatable;
}

Automatable *
automatable_create_plugin_enabled (
  Plugin * plugin)
{
  Automatable * automatable = _create_blank ();

  automatable->type = AUTOMATABLE_TYPE_PLUGIN_ENABLED;
  automatable->track = plugin->channel->track;
  automatable->track_id =
    plugin->channel->track->id;
  automatable->slot_index =
    channel_get_plugin_index (plugin->channel,
                              plugin);
  automatable->label = g_strdup ("Enable/disable");

  return automatable;
}

int
automatable_is_bool (Automatable * a)
{
  switch (a->type)
    {
    case AUTOMATABLE_TYPE_PLUGIN_CONTROL:
      if (a->control->value_type ==
          a->control->plugin->forge.Bool)
        {
          return 1;
        }
      return 0;
    case AUTOMATABLE_TYPE_PLUGIN_ENABLED:
    case AUTOMATABLE_TYPE_CHANNEL_MUTE:
      return 1;
    case AUTOMATABLE_TYPE_CHANNEL_FADER:
    case AUTOMATABLE_TYPE_CHANNEL_PAN:
      return 0;
    }
  g_assert_not_reached ();
  return -1;
}

/**
 * Returns the type of its value (float, bool, etc.)
 * as a string.
 *
 * Must be free'd.
 */
char *
automatable_stringize_value_type (Automatable * a)
{
  char * val_type = NULL;
  if (automatable_is_float (a))
    val_type = g_strdup ("Float");
  else if (automatable_is_bool (a))
    val_type = g_strdup ("Boolean");

  return val_type;
}

int
automatable_is_float (Automatable * a)
{
  return 1;

  /* FIXME aren't all floats? this should only
   * be used to determine how the curve should
   * look*/
  switch (a->type)
    {
    case AUTOMATABLE_TYPE_PLUGIN_CONTROL:
      if (a->control->value_type ==
          a->control->plugin->forge.Float)
        {
          return 1;
        }
      return 0;
      break;
    case AUTOMATABLE_TYPE_PLUGIN_ENABLED:
    case AUTOMATABLE_TYPE_CHANNEL_MUTE:
      return 0;
    case AUTOMATABLE_TYPE_CHANNEL_FADER:
    case AUTOMATABLE_TYPE_CHANNEL_PAN:
      return 1;
    }
  g_assert_not_reached ();
  return -1;
}

const float
automatable_get_minf (Automatable * a)
{
  switch (a->type)
    {
    case AUTOMATABLE_TYPE_PLUGIN_CONTROL:
      return lilv_node_as_float (a->control->min);
      break;
    case AUTOMATABLE_TYPE_PLUGIN_ENABLED:
    case AUTOMATABLE_TYPE_CHANNEL_FADER:
    case AUTOMATABLE_TYPE_CHANNEL_MUTE:
    case AUTOMATABLE_TYPE_CHANNEL_PAN:
      return 0.f;
    }
  g_assert_not_reached ();
  return -1;
}

const float
automatable_get_maxf (Automatable * a)
{
  switch (a->type)
    {
    case AUTOMATABLE_TYPE_PLUGIN_CONTROL:
      return lilv_node_as_float (a->control->max);
      break;
    case AUTOMATABLE_TYPE_CHANNEL_FADER:
      return 2.f;
    case AUTOMATABLE_TYPE_PLUGIN_ENABLED:
    case AUTOMATABLE_TYPE_CHANNEL_MUTE:
    case AUTOMATABLE_TYPE_CHANNEL_PAN:
      return 1.f;
    }
  g_assert_not_reached ();
  return -1;
}

/**
 * Returns max - min for the float automatable
 */
const float
automatable_get_sizef (Automatable * automatable)
{
  return (automatable_get_maxf (automatable) -
          automatable_get_minf (automatable));
}

void
automatable_free (Automatable * automatable)
{
  /* TODO go through every track and plugins
   * and set associated automatables to NULL */

}

/**
 * Gets the current value of the parameter the
 * automatable is for.
 *
 * This does not consider the automation track, it
 * only looks in the actual parameter for its
 * current value.
 */
float
automatable_get_val (Automatable * a)
{
  Plugin * plugin;
  Channel * ch;
  switch (a->type)
    {
    case AUTOMATABLE_TYPE_PLUGIN_CONTROL:
      plugin = a->port->owner_pl;
      if (plugin->descr->protocol == PROT_LV2)
        {
          Lv2ControlID * control = a->control;
          LV2_Port* port = &control->plugin->ports[control->index];
          return port->control;
        }
      break;
    case AUTOMATABLE_TYPE_PLUGIN_ENABLED:
      plugin = a->port->owner_pl;
      return plugin->enabled;
    case AUTOMATABLE_TYPE_CHANNEL_FADER:
      ch = track_get_channel (a->track);
      return ch->fader_amp;
    case AUTOMATABLE_TYPE_CHANNEL_MUTE:
      return a->track->mute;
    case AUTOMATABLE_TYPE_CHANNEL_PAN:
      ch = track_get_channel (a->track);
      return ch->pan;
    }
  g_assert_not_reached ();
  return -1;
}

/**
 * Converts normalized value (0.0 to 1.0) to
 * real value (eg. -10.0 to 100.0).
 */
float
automatable_normalized_val_to_real (
  Automatable * a,
  float         val)
{
  Plugin * plugin;
  switch (a->type)
    {
    case AUTOMATABLE_TYPE_PLUGIN_CONTROL:
      plugin = a->port->owner_pl;
      if (plugin->descr->protocol == PROT_LV2)
        {
          Lv2ControlID * ctrl = a->control;
          float maxf =
            automatable_get_maxf (a);
          float minf =
            automatable_get_minf (a);
          float real_val;
          if (ctrl->is_logarithmic)
            {
              /* see http://lv2plug.in/ns/ext/port-props/port-props.html#rangeSteps */
              real_val = minf * pow (maxf / minf, val);
            }
          else if (ctrl->is_toggle)
            {
              real_val = val >= 0.5f ? 1.f : 0.f;
            }
          else
            {
              real_val = minf + val * (maxf - minf);
            }
          return real_val;
        }
    case AUTOMATABLE_TYPE_PLUGIN_ENABLED:
      plugin = a->port->owner_pl;
      return val > 0.5f;
    case AUTOMATABLE_TYPE_CHANNEL_FADER:
      return math_get_amp_val_from_fader (val);
    case AUTOMATABLE_TYPE_CHANNEL_MUTE:
      return val > 0.5f;
    case AUTOMATABLE_TYPE_CHANNEL_PAN:
      return val;
    }
  return -1.f;
}

/**
 * Converts real value (eg. -10.0 to 100.0) to
 * normalized value (0.0 to 1.0).
 */
float
automatable_real_val_to_normalized (
  Automatable * a,
  float         real_val)
{
  float fmax = automatable_get_maxf (a);
  float fmin = automatable_get_minf (a);
  float frange = fmax - fmin;

  Plugin * plugin;
  switch (a->type)
    {
    case AUTOMATABLE_TYPE_PLUGIN_CONTROL:
      plugin = a->port->owner_pl;
      if (plugin->descr->protocol == PROT_LV2)
        {
          Lv2ControlID * ctrl = a->control;
          float normalized_val;
          if (ctrl->is_logarithmic)
            {
              /* see http://lv2plug.in/ns/ext/port-props/port-props.html#rangeSteps */
              normalized_val =
                log (real_val / fmin) /
                (fmax / fmin);
            }
          else if (ctrl->is_toggle)
            {
              normalized_val = real_val;
            }
          else
            {
              normalized_val =
                (frange - (fmax - real_val)) / frange;
            }
          return normalized_val;
        }
    case AUTOMATABLE_TYPE_PLUGIN_ENABLED:
      return real_val;
    case AUTOMATABLE_TYPE_CHANNEL_FADER:
      return math_get_fader_val_from_amp (real_val);
    case AUTOMATABLE_TYPE_CHANNEL_MUTE:
      return real_val;
    case AUTOMATABLE_TYPE_CHANNEL_PAN:
      return real_val;
    }
  return -1.f;
}

/**
 * Updates the actual value.
 *
 * The given value is always a normalized 0.0-1.0
 * value and must be translated to the actual value
 * before setting it.
 */
void
automatable_set_val_from_normalized (
  Automatable * a,
  float         val)
{
  /*g_assert (val <= 1.f && val >= 0.f);*/
  Plugin * plugin;
  Channel * ch;
  switch (a->type)
    {
    case AUTOMATABLE_TYPE_PLUGIN_CONTROL:
      plugin = a->port->owner_pl;
      if (plugin->descr->protocol == PROT_LV2)
        {
          Lv2ControlID * ctrl = a->control;
          float maxf =
            automatable_get_maxf (a);
          float minf =
            automatable_get_minf (a);
          float real_val;
          if (ctrl->is_logarithmic)
            {
              /* see http://lv2plug.in/ns/ext/port-props/port-props.html#rangeSteps */
              real_val = minf * pow (maxf / minf, val);
            }
          else if (ctrl->is_toggle)
            {
              real_val = val >= 0.5f ? 1.f : 0.f;
            }
          else
            {
              real_val = minf + val * (maxf - minf);
            }
          ctrl->plugin->
            ports[ctrl->index].control = real_val;
        }
      return;
    case AUTOMATABLE_TYPE_PLUGIN_ENABLED:
      plugin = a->port->owner_pl;
      plugin->enabled = val > 0.5f;
      return;
    case AUTOMATABLE_TYPE_CHANNEL_FADER:
      ch = track_get_channel (a->track);
      channel_set_fader_amp (
        ch,
        math_get_amp_val_from_fader (val));
      return;
    case AUTOMATABLE_TYPE_CHANNEL_MUTE:
      track_set_muted (a->track,
                      val > 0.5f,
                      0);
      return;
    case AUTOMATABLE_TYPE_CHANNEL_PAN:
      ch = track_get_channel (a->track);
      channel_set_pan (ch, val);
      return;
    }
}

/**
 * Gets automation track for given automatable, if any.
 */
AutomationTrack *
automatable_get_automation_track (Automatable * automatable)
{
  Track * track = automatable->track;
  AutomationTracklist * automation_tracklist =
    track_get_automation_tracklist (track);

  for (int i = 0;
       i < automation_tracklist->num_automation_tracks;
       i++)
    {
      AutomationTrack * at =
        automation_tracklist->automation_tracks[i];
      if (at->automatable == automatable)
        {
          return at;
        }
    }
  return NULL;
}


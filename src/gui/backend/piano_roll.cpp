// SPDX-FileCopyrightText: © 2018-2021, 2024 Alexandros Theodotou <alex@zrythm.org>
// SPDX-License-Identifier: LicenseRef-ZrythmLicense

#include "dsp/track.h"
#include "gui/backend/event.h"
#include "gui/backend/event_manager.h"
#include "gui/backend/piano_roll.h"
#include "project.h"
#include "settings/g_settings_manager.h"
#include "utils/rt_thread_id.h"
#include "zrythm.h"
#include "zrythm_app.h"

static const char * drum_labels[47] = {
  "Acoustic Bass Drum",
  "Bass Drum 1",
  "Side Stick",
  "Acoustic Snare",
  "Hand Clap",
  "Electric Snare",
  "Low Floor Tom",
  "Closed Hi Hat",
  "High Floor Tom",
  "Pedal Hi-Hat",
  "Low Tom",
  "Open Hi-Hat",
  "Low-Mid Tom",
  "Hi-Mid Tom",
  "Crash Cymbal 1",
  "High Tom",
  "Ride Cymbal 1",
  "Chinese Cymbal",
  "Ride Bell",
  "Tambourine",
  "Splash Cymbal",
  "Cowbell",
  "Crash Cymbal 2",
  "Vibraslap",
  "Ride Cymbal 2",
  "Hi Bongo",
  "Low Bongo",
  "Mute Hi Conga",
  "Open Hi Conga",
  "Low Conga",
  "High Timbale",
  "Low Timbale",
  "High Agogo",
  "Low Agogo",
  "Cabasa",
  "Maracas",
  "Short Whistle",
  "Long Whistle",
  "Short Guiro",
  "Long Guiro",
  "Claves",
  "Hi Wood Block",
  "Low Wood Block",
  "Mute Cuica",
  "Open Cuica",
  "Mute Triangle",
  "Open Triangle"
};

void
PianoRoll::init_descriptors ()
{
  int idx = 0;
  for (int i = 127; i >= 0; --i)
    {
      /* do piano */
      auto &descr = piano_descriptors_[idx];

      descr.index_ = idx;
      descr.value_ = i;
      descr.marked_ = false;
      descr.visible_ = true;
      descr.custom_name_ = "";

      descr.note_name_ =
        std::string (ChordDescriptor::note_to_string (
          ENUM_INT_TO_VALUE (MusicalNote, i % 12)))
        + std::to_string (i / 12 - 1);
      descr.note_name_pango_ = std::format (
        "{}<sup>{}</sup>",
        ChordDescriptor::note_to_string (ENUM_INT_TO_VALUE (MusicalNote, i % 12)),
        i / 12 - 1);
      idx++;
    }

  /* do drum - put 35 to 81 first */
  idx = 0;
  for (int i = 35; i <= 81; i++)
    {
      auto &descr = drum_descriptors_[idx];

      descr.index_ = idx;
      descr.value_ = i;
      descr.marked_ = false;
      descr.visible_ = true;
      descr.custom_name_ = drum_labels[idx];

      descr.note_name_ =
        std::string (ChordDescriptor::note_to_string (
          ENUM_INT_TO_VALUE (MusicalNote, i % 12)))
        + std::to_string (i / 12 - 1);
      descr.note_name_pango_ = std::format (
        "{}<sup>{}</sup>",
        ChordDescriptor::note_to_string (ENUM_INT_TO_VALUE (MusicalNote, i % 12)),
        i / 12 - 1);
      idx++;
    }
  for (int i = 0; i < 128; i++)
    {
      if (i >= 35 && i <= 81)
        continue;

      auto &descr = drum_descriptors_[idx];

      descr.index_ = idx;
      descr.value_ = i;
      descr.marked_ = false;
      descr.visible_ = true;
      descr.custom_name_ = std::format (
        "#{}: {}{}", i,
        ChordDescriptor::note_to_string (ENUM_INT_TO_VALUE (MusicalNote, i % 12)),
        i / 12 - 1);

      descr.note_name_ =
        std::string (ChordDescriptor::note_to_string (
          ENUM_INT_TO_VALUE (MusicalNote, i % 12)))
        + std::to_string (i / 12 - 1);
      descr.note_name_pango_ = std::format (
        "{}<sup>{}</sup>",
        ChordDescriptor::note_to_string (ENUM_INT_TO_VALUE (MusicalNote, i % 12)),
        i / 12 - 1);
      idx++;
    }

  g_return_if_fail (idx == 128);
}

/* 1 = black */
static const int notes[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };

bool
PianoRoll::is_key_black (int note)
{
  return notes[note % 12] == 1;
}

void
PianoRoll::add_current_note (int note)
{
  if (std::ranges::find (current_notes_, note) != current_notes_.end ())
    {
      current_notes_.push_back (note);
    }
}

void
PianoRoll::remove_current_note (int note)
{
  auto it = std::ranges::find (current_notes_, note);
  if (it != current_notes_.end ())
    {
      current_notes_.erase (it);
    }
}

bool
PianoRoll::contains_current_note (int note)
{
  return std::ranges::find (current_notes_, note) != current_notes_.end ();
}

void
PianoRoll::init_loaded ()
{
  if (!ZRYTHM_TESTING)
    {
      highlighting_ = ENUM_INT_TO_VALUE (
        Highlighting, g_settings_get_enum (S_UI, "piano-roll-highlight"));
    }

  init_descriptors ();
}

const MidiNoteDescriptor *
PianoRoll::find_midi_note_descriptor_by_val (bool drum_mode, const uint8_t val)
{
  g_return_val_if_fail (val < 128, nullptr);

  for (int i = 0; i < 128; i++)
    {
      const MidiNoteDescriptor * descr;
      if (drum_mode)
        descr = &drum_descriptors_[i];
      else
        descr = &piano_descriptors_[i];

      if (descr->value_ == static_cast<int> (val))
        return descr;
    }
  g_return_val_if_reached (nullptr);
}

void
PianoRoll::set_highlighting (Highlighting highlighting)
{
  highlighting_ = highlighting;

  g_settings_set_enum (
    S_UI, "piano-roll-highlight", ENUM_VALUE_TO_INT (highlighting));

  EVENTS_PUSH (EventType::ET_PIANO_ROLL_HIGHLIGHTING_CHANGED, nullptr);
}

Track *
PianoRoll::get_current_track () const
{
  /* TODO */
  g_return_val_if_reached (nullptr);
}

void
PianoRoll::set_notes_zoom (float notes_zoom, bool fire_events)
{
  if (notes_zoom < 1.f || notes_zoom > 4.5f)
    return;

  notes_zoom_ = notes_zoom;

  if (fire_events)
    {
      EVENTS_PUSH (EventType::ET_PIANO_ROLL_KEY_ZOOM_CHANGED, nullptr);
    }
}

void
PianoRoll::set_midi_modifier (MidiModifier modifier)
{
  midi_modifier_ = modifier;

#if 0
  g_settings_set_enum (
    S_UI, "piano-roll-midi-modifier",
    modifier);
#endif

  EVENTS_PUSH (EventType::ET_PIANO_ROLL_MIDI_MODIFIER_CHANGED, nullptr);
}

void
PianoRoll::init ()
{
  notes_zoom_ = 3.f;

  midi_modifier_ = MidiModifier::Velocity;

  EditorSettings::copy_members_from (PianoRoll ());

  if (!ZRYTHM_TESTING)
    {
      highlighting_ = ENUM_INT_TO_VALUE (
        Highlighting, g_settings_get_enum (S_UI, "piano-roll-highlight"));
      midi_modifier_ = ENUM_INT_TO_VALUE (
        MidiModifier, g_settings_get_enum (S_UI, "piano-roll-midi-modifier"));
    }

  init_descriptors ();
}
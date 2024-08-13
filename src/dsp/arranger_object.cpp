// SPDX-FileCopyrightText: © 2019-2024 Alexandros Theodotou <alex@zrythm.org>
// SPDX-License-Identifier: LicenseRef-ZrythmLicense

#include "actions/arranger_selections.h"
#include "dsp/arranger_object.h"
#include "dsp/audio_region.h"
#include "dsp/automation_point.h"
#include "dsp/automation_region.h"
#include "dsp/chord_object.h"
#include "dsp/chord_region.h"
#include "dsp/chord_track.h"
#include "dsp/marker.h"
#include "dsp/marker_track.h"
#include "dsp/midi_region.h"
#include "dsp/router.h"
#include "dsp/tracklist.h"
#include "gui/backend/automation_selections.h"
#include "gui/backend/chord_selections.h"
#include "gui/backend/event.h"
#include "gui/backend/event_manager.h"
#include "gui/backend/timeline_selections.h"
#include "gui/widgets/arranger_object.h"
#include "gui/widgets/automation_arranger.h"
#include "gui/widgets/automation_editor_space.h"
#include "gui/widgets/chord_arranger.h"
#include "gui/widgets/chord_editor_space.h"
#include "gui/widgets/clip_editor_inner.h"
#include "gui/widgets/midi_modifier_arranger.h"
#include "gui/widgets/track.h"
#include "project.h"
#include "settings/g_settings_manager.h"
#include "utils/debug.h"
#include "utils/logger.h"
#include "utils/rt_thread_id.h"
#include "zrythm_app.h"

#include <glib/gi18n.h>

template <typename T>
T *
ArrangerObject::get_selections_for_type (Type type)
{
  switch (type)
    {
    case Type::Region:
    case Type::ScaleObject:
    case Type::Marker:
      return static_cast<T *> (TL_SELECTIONS.get ());
    case Type::MidiNote:
    case Type::Velocity:
      return static_cast<T *> (MIDI_SELECTIONS.get ());
    case Type::ChordObject:
      return static_cast<T *> (CHORD_SELECTIONS.get ());
    case Type::AutomationPoint:
      return static_cast<T *> (AUTOMATION_SELECTIONS.get ());
    default:
      return nullptr;
    }
}

void
ArrangerObject::select (
  const std::shared_ptr<ArrangerObject> &obj,
  bool                                   select,
  bool                                   append,
  bool                                   fire_events)
{
  /* if velocity, do the selection on the owner MidiNote instead */
  if (obj->type_ == ArrangerObject::Type::Velocity)
    {
      auto vel = dynamic_cast<Velocity *> (obj.get ());
      auto obj_to_select = vel->get_midi_note ();
      z_return_if_fail (obj_to_select);
      ArrangerObject::select (
        obj_to_select->shared_from_this_as<MidiNote> (), select, append,
        fire_events);
    }

  auto selections =
    ArrangerObject::get_selections_for_type<ArrangerSelections> (obj->type_);

  /* if nothing to do, return */
  bool is_selected = obj->is_selected ();
  if ((is_selected && select && append) || (!is_selected && !select))
    {
      return;
    }

  if (select)
    {
      if (!append)
        {
          selections->clear (fire_events);
        }
      selections->add_object_ref (obj);
    }
  else
    {
      selections->remove_object (*obj);
    }

  if (ZRYTHM_HAVE_UI)
    {
      bool autoselect_track = g_settings_get_boolean (S_UI, "track-autoselect");
      auto track = obj->get_track ();
      if (autoselect_track && track && track->is_in_active_project ())
        {
          track->select (true, true, true);
        }
    }

  if (fire_events)
    {
      EVENTS_PUSH (EventType::ET_ARRANGER_OBJECT_CHANGED, obj.get ());
    }
}

/**
 * Returns if the object is in the selections.
 */
bool
ArrangerObject::is_selected () const
{
  auto selections =
    ArrangerObject::get_selections_for_type<ArrangerSelections> (type_);

  return selections->contains_object (*this);
}

std::unique_ptr<ArrangerSelections>
ArrangerObject::create_arranger_selections_from_this () const
{
  ArrangerSelections::Type arranger_selections_type;
  switch (type_)
    {
    case Type::Region:
      arranger_selections_type = ArrangerSelections::Type::Timeline;
      break;
    case Type::ScaleObject:
      arranger_selections_type = ArrangerSelections::Type::Timeline;
      break;
    case Type::Marker:
      arranger_selections_type = ArrangerSelections::Type::Timeline;
      break;
    case Type::MidiNote:
      arranger_selections_type = ArrangerSelections::Type::Midi;
      break;
    case Type::Velocity:
      arranger_selections_type = ArrangerSelections::Type::Midi;
      break;
    case Type::ChordObject:
      arranger_selections_type = ArrangerSelections::Type::Chord;
      break;
    case Type::AutomationPoint:
      arranger_selections_type = ArrangerSelections::Type::Automation;
      break;
    default:
      z_return_val_if_reached (nullptr);
    }
  auto sel = ArrangerSelections::new_from_type (arranger_selections_type);
  auto variant = convert_to_variant<ArrangerObjectPtrVariant> (this);
  std::visit (
    [&] (auto &&obj) { sel->add_object_owned (obj->clone_unique ()); }, variant);
  return sel;
}

std::shared_ptr<ArrangerObject>
ArrangerObject::remove_from_project (bool fire_events)
{
  /* TODO make sure no event contains this object */
  /*event_manager_remove_events_for_obj (*/
  /*EVENT_MANAGER, obj);*/

  Region * region = nullptr;
  if (owned_by_region ())
    {
      auto ro_obj = dynamic_cast<RegionOwnedObject *> (this);
      region = ro_obj->get_region ();
      z_return_val_if_fail (region, nullptr);
    }

  /* get a shared ptr to prevent this from getting deleted (we will also return
   * this)*/
  auto ret = shared_from_this ();

  switch (type_)
    {
    case Type::AutomationPoint:
      {
        auto ap = dynamic_cast<AutomationPoint *> (this);
        dynamic_cast<AutomationRegion *> (region)->remove_object (
          *ap, fire_events);
      }
      break;
    case Type::ChordObject:
      {
        auto co = dynamic_cast<ChordObject *> (this);
        dynamic_cast<ChordRegion *> (region)->remove_object (*co, fire_events);
      }
      break;
    case Type::Region:
      {
        auto variant = convert_to_variant<RegionPtrVariant> (this);
        std::visit (
          [&] (auto &&r) {
            auto owner = r->get_region_owner ();
            owner->remove_region (*r, fire_events);
          },
          variant);
      }
      break;
    case Type::ScaleObject:
      {
        auto scale = dynamic_cast<ScaleObject *> (this);
        P_CHORD_TRACK->remove_scale (*scale);
      }
      break;
    case Type::Marker:
      {
        auto m = dynamic_cast<Marker *> (this);
        P_MARKER_TRACK->remove_marker (*m, fire_events);
      }
      break;
    case Type::MidiNote:
      {
        auto mn = dynamic_cast<MidiNote *> (this);
        dynamic_cast<MidiRegion *> (region)->remove_object (*mn, fire_events);
      }
      break;
    case Type::None:
    case Type::All:
    case Type::Velocity:
      z_return_val_if_reached (nullptr);
    }

  if (region)
    {
      region->update_link_group ();
    }

  return ret;
}

Position *
ArrangerObject::get_position_ptr (PositionType pos_type)
{
  switch (pos_type)
    {
    case PositionType::Start:
      return &pos_;
    case PositionType::End:
      {
        auto lo = dynamic_cast<LengthableObject *> (this);
        z_return_val_if_fail (lo != nullptr, nullptr);
        return &lo->end_pos_;
      }
    case PositionType::ClipStart:
      {
        auto lo = dynamic_cast<LoopableObject *> (this);
        z_return_val_if_fail (lo != nullptr, nullptr);
        return &lo->clip_start_pos_;
      }
    case PositionType::LoopStart:
      {
        auto lo = dynamic_cast<LoopableObject *> (this);
        z_return_val_if_fail (lo != nullptr, nullptr);
        return &lo->loop_start_pos_;
      }
    case PositionType::LoopEnd:
      {
        auto lo = dynamic_cast<LoopableObject *> (this);
        z_return_val_if_fail (lo != nullptr, nullptr);
        return &lo->loop_end_pos_;
      }
    case PositionType::FadeIn:
      {
        auto fo = dynamic_cast<FadeableObject *> (this);
        z_return_val_if_fail (fo != nullptr, nullptr);
        return &fo->fade_in_pos_;
      }
    case PositionType::FadeOut:
      {
        auto fo = dynamic_cast<FadeableObject *> (this);
        z_return_val_if_fail (fo != nullptr, nullptr);
        return &fo->fade_out_pos_;
      }
    }
  z_return_val_if_reached (nullptr);
}

bool
ArrangerObject::is_position_valid (const Position &pos, PositionType pos_type)
  const
{
  switch (pos_type)
    {
    case PositionType::Start:
      if (type_has_length (type_))
        {
          auto lo = dynamic_cast<const LengthableObject *> (this);
          z_return_val_if_fail (lo != nullptr, false);
          if (pos >= lo->end_pos_)
            return false;

          if (!type_owned_by_region (type_))
            {
              if (pos < Position ())
                return false;
            }

          if (can_fade ())
            {
              auto fo = dynamic_cast<const FadeableObject *> (this);
              z_return_val_if_fail (fo != nullptr, false);
              signed_frame_t frames_diff =
                pos.get_total_frames () - this->pos_.get_total_frames ();
              Position expected_fade_out = fo->fade_out_pos_;
              expected_fade_out.add_frames (-frames_diff);
              if (expected_fade_out <= fo->fade_in_pos_)
                return false;
            }

          return true;
        }
      else if (ArrangerObject::type_has_global_pos (type_))
        {
          return pos >= Position ();
        }
      else
        {
          return true;
        }
      break;
    case PositionType::LoopStart:
      {
        auto lo = dynamic_cast<const LoopableObject *> (this);
        z_return_val_if_fail (lo != nullptr, false);
        return pos < lo->loop_end_pos_ && pos >= Position ();
      }
      break;
    case PositionType::LoopEnd:
      {
        auto lo = dynamic_cast<const LoopableObject *> (this);
        z_return_val_if_fail (lo != nullptr, false);
        if (pos < lo->clip_start_pos_ || pos <= lo->loop_start_pos_)
          return false;

        bool is_valid = true;
        if (type_ == Type::Region)
          {
            auto r = dynamic_cast<const Region *> (this);
            z_return_val_if_fail (r != nullptr, false);
            if (r->id_.type_ == RegionType::Audio)
              {
                auto audio_region = dynamic_cast<const AudioRegion *> (r);
                AudioClip * clip = audio_region->get_clip ();
                is_valid =
                  pos.frames_ <= static_cast<signed_frame_t> (clip->num_frames_);
              }
          }
        return is_valid;
      }
      break;
    case PositionType::ClipStart:
      {
        auto lo = dynamic_cast<const LoopableObject *> (this);
        z_return_val_if_fail (lo != nullptr, false);
        return pos < lo->loop_end_pos_ && pos >= Position ();
      }
      break;
    case PositionType::End:
      {
        auto lo = dynamic_cast<const LengthableObject *> (this);
        z_return_val_if_fail (lo != nullptr, false);
        bool is_valid = pos > this->pos_;

        if (can_fade ())
          {
            auto fo = dynamic_cast<const FadeableObject *> (this);
            z_return_val_if_fail (fo != nullptr, false);
            signed_frame_t frames_diff =
              pos.get_total_frames () - lo->end_pos_.get_total_frames ();
            Position expected_fade_out = fo->fade_out_pos_;
            expected_fade_out.add_frames (frames_diff);
            if (expected_fade_out <= fo->fade_in_pos_)
              return false;
          }
        return is_valid;
      }
      break;
    case PositionType::FadeIn:
      {
        auto fo = dynamic_cast<const FadeableObject *> (this);
        z_return_val_if_fail (fo != nullptr, false);
        Position local_end_pos (
          fo->end_pos_.get_total_frames () - this->pos_.get_total_frames ());
        return pos >= Position () && pos.get_total_frames () >= 0
               && pos < local_end_pos && pos < fo->fade_out_pos_;
      }
      break;
    case PositionType::FadeOut:
      {
        auto fo = dynamic_cast<const FadeableObject *> (this);
        z_return_val_if_fail (fo != nullptr, false);
        Position local_end_pos (
          fo->end_pos_.get_total_frames () - this->pos_.get_total_frames ());
        return pos >= Position () && pos <= local_end_pos
               && pos > fo->fade_in_pos_;
      }
      break;
    }

  z_return_val_if_reached (false);
}

void
ArrangerObject::copy_identifier (const ArrangerObject &src)
{
  z_return_if_fail (type_ == src.type_);

  track_name_hash_ = src.track_name_hash_;

  if (type_owned_by_region (type_))
    {
      auto       &dest = dynamic_cast<RegionOwnedObject &> (*this);
      const auto &src_ro = dynamic_cast<const RegionOwnedObject &> (src);
      dest.region_id_ = src_ro.region_id_;
      dest.index_ = src_ro.index_;
    }

  if (type_has_name (type_))
    {
      auto       &dest = dynamic_cast<NameableObject &> (*this);
      const auto &src_nameable = dynamic_cast<const Marker &> (src);
      dest.name_ = src_nameable.name_;
    }

  switch (type_)
    {
    case Type::Region:
      {
        auto       &dest = dynamic_cast<Region &> (*this);
        const auto &src_r = dynamic_cast<const Region &> (src);
        dest.id_ = src_r.id_;
      }
      break;
    case Type::ChordObject:
      {
        auto       &dest = dynamic_cast<ChordObject &> (*this);
        const auto &src_co = dynamic_cast<const ChordObject &> (src);
        dest.chord_index_ = src_co.chord_index_;
      }
      break;
    default:
      break;
    }
}

bool
ArrangerObject::set_position (
  const Position * pos,
  PositionType     pos_type,
  const bool       validate)
{
  z_return_val_if_fail (pos, false);

  /* return if validate is on and position is invalid */
  if (validate && !is_position_valid (*pos, pos_type))
    return false;

  auto pos_ptr = get_position_ptr (pos_type);
  z_return_val_if_fail (pos_ptr, false);
  *pos_ptr = *pos;

  return true;
}

void
ArrangerObject::print () const
{
  z_debug (print_to_str ());
}

void
ArrangerObject::move (const double ticks)
{
  if (ArrangerObject::type_has_length (type_))
    {
      signed_frame_t length_frames =
        dynamic_cast<LengthableObject *> (this)->get_length_in_frames ();

      /* start pos */
      Position tmp;
      get_pos (&tmp);
      tmp.add_ticks (ticks);
      set_position (&tmp, PositionType::Start, false);

      /* end pos */
      if (type_ == ArrangerObject::Type::Region)
        {
          /* audio regions need the exact number of frames to match the clip.
           *
           * this should be used for all objects but currently moving objects
           * before 1.1.1.1 causes bugs hence this (temporary) if statement */
          tmp.add_frames (length_frames);
        }
      else
        {
          dynamic_cast<LengthableObject *> (this)->get_end_pos (&tmp);
          tmp.add_ticks (ticks);
        }
      set_position (&tmp, PositionType::End, false);
    }
  else
    {
      Position tmp;
      get_pos (&tmp);
      tmp.add_ticks (ticks);
      set_position (&tmp, PositionType::Start, false);
    }
}

void
ArrangerObject::get_position_from_type (Position * pos, PositionType type) const
{
  switch (type)
    {
    case PositionType::Start:
      get_pos (pos);
      break;
    case PositionType::ClipStart:
      {
        auto const * lo = dynamic_cast<const LoopableObject *> (this);
        lo->get_clip_start_pos (pos);
      }
      break;
    case PositionType::End:
      {
        auto const * lo = dynamic_cast<const LengthableObject *> (this);
        lo->get_end_pos (pos);
      }
      break;
    case PositionType::LoopStart:
      {
        auto const * lo = dynamic_cast<const LoopableObject *> (this);
        lo->get_loop_start_pos (pos);
      }
      break;
    case PositionType::LoopEnd:
      {
        auto const * lo = dynamic_cast<const LoopableObject *> (this);
        lo->get_loop_end_pos (pos);
      }
      break;
    case PositionType::FadeIn:
      {
        auto const * fo = dynamic_cast<const FadeableObject *> (this);
        fo->get_fade_in_pos (pos);
      }
      break;
    case PositionType::FadeOut:
      {
        auto const * fo = dynamic_cast<const FadeableObject *> (this);
        fo->get_fade_out_pos (pos);
      }
      break;
    }
}

void
ArrangerObject::
  update_positions (bool from_ticks, bool bpm_change, UndoableAction * action)
{
  signed_frame_t frames_len_before = 0;
  if (bpm_change && ArrangerObject::type_has_length (type_))
    {
      frames_len_before =
        dynamic_cast<LengthableObject *> (this)->get_length_in_frames ();
    }

  double ratio = 0.0;
  if (action)
    {
      if (from_ticks)
        {
          ratio = action->frames_per_tick_;
        }
      else
        {
          ratio = 1.0 / action->frames_per_tick_;
        }
    }

  pos_.update (from_ticks, ratio);
  if (ArrangerObject::type_has_length (type_))
    {
      dynamic_cast<LengthableObject *> (this)->end_pos_.update (
        from_ticks, ratio);

      if (ROUTER->is_processing_kickoff_thread ())
        {
          /* do some validation */
          z_return_if_fail (is_position_valid (
            dynamic_cast<LengthableObject *> (this)->end_pos_,
            PositionType::End));
        }
    }
  if (can_loop ())
    {
      dynamic_cast<LoopableObject *> (this)->clip_start_pos_.update (
        from_ticks, ratio);
      dynamic_cast<LoopableObject *> (this)->loop_start_pos_.update (
        from_ticks, ratio);
      dynamic_cast<LoopableObject *> (this)->loop_end_pos_.update (
        from_ticks, ratio);
    }
  if (can_fade ())
    {
      dynamic_cast<FadeableObject *> (this)->fade_in_pos_.update (
        from_ticks, ratio);
      dynamic_cast<FadeableObject *> (this)->fade_out_pos_.update (
        from_ticks, ratio);
    }

  Region * r;
  switch (type_)
    {
    case Type::Region:
      r = dynamic_cast<Region *> (this);

      /* validate */
      if (r->id_.type_ == RegionType::Audio)
        {
          auto audio_region = dynamic_cast<AudioRegion *> (r);
          if (!audio_region->get_musical_mode ())
            {
              auto           ar = dynamic_cast<AudioRegion *> (r);
              signed_frame_t frames_len_after =
                dynamic_cast<LengthableObject *> (this)->get_length_in_frames ();
              if (bpm_change && frames_len_after != frames_len_before)
                {
                  double ticks =
                    (frames_len_before - frames_len_after)
                    * AUDIO_ENGINE->ticks_per_frame_;
                  try
                    {
                      dynamic_cast<LengthableObject *> (this)->resize (
                        false,
                        ArrangerObject::ResizeType::RESIZE_STRETCH_BPM_CHANGE,
                        ticks, false);
                    }
                  catch (const ZrythmException &e)
                    {
                      e.handle ("Failed to resize object");
                      return;
                    }
                }
              z_return_if_fail_cmp (
                dynamic_cast<LoopableObject *> (this)->loop_end_pos_.frames_,
                >=, 0);
              signed_frame_t tl_frames =
                dynamic_cast<LengthableObject *> (this)->end_pos_.frames_ - 1;
              signed_frame_t local_frames;
              AudioClip *    clip;
              local_frames = r->timeline_frames_to_local (tl_frames, true);
              clip = ar->get_clip ();
              z_return_if_fail (clip);

              /* sometimes due to rounding errors, the region frames are 1 frame
               * more than the clip frames. this works around it by resizing the
               * region by -1 frame*/
              while (local_frames == (signed_frame_t) clip->num_frames_)
                {
                  z_debug ("adjusting for rounding error");
                  double ticks = -AUDIO_ENGINE->ticks_per_frame_;
                  z_debug ("ticks %f", ticks);
                  try
                    {
                      dynamic_cast<LengthableObject *> (this)->resize (
                        false,
                        ArrangerObject::ResizeType::RESIZE_STRETCH_BPM_CHANGE,
                        ticks, false);
                    }
                  catch (const ZrythmException &e)
                    {
                      e.handle ("Failed to resize object");
                      return;
                    }
                  local_frames = r->timeline_frames_to_local (tl_frames, true);
                }

              z_return_if_fail_cmp (
                local_frames, <, (signed_frame_t) clip->num_frames_);
            }
        } // end if audio region

      if (r->is_midi ())
        {
          auto mr = dynamic_cast<MidiRegion *> (r);
          for (auto &note : mr->midi_notes_)
            {
              note->update_positions (from_ticks, bpm_change, action);
            }
        }

      if (r->is_automation ())
        {
          for (auto &ap : dynamic_cast<AutomationRegion *> (r)->aps_)
            {
              ap->update_positions (from_ticks, bpm_change, action);
            }
        }

      if (r->is_chord ())
        {
          auto cr = dynamic_cast<ChordRegion *> (r);
          for (auto &chord : cr->chord_objects_)
            {
              chord->update_positions (from_ticks, bpm_change, action);
            }
        }
      break;
    default:
      break;
    }
}

void
ArrangerObject::post_deserialize ()
{
  /* TODO: this acts as if a BPM change happened (and is only effective if so),
   * so if no BPM change happened this is unnecessary, so this should be
   * refactored in the future. this was added to fix copy-pasting audio regions
   * after changing the BPM (see #4993) */
  update_positions (true, true, nullptr);

  if (is_region ())
    {
      auto r = dynamic_cast<Region *> (this);
      r->post_deserialize_children ();
    }
}

void
ArrangerObject::edit_begin () const
{
  auto arranger = get_arranger ();
  z_return_if_fail (arranger);
  auto variant = convert_to_variant<ArrangerObjectPtrVariant> (this);
  std::visit (
    [&arranger] (auto &&obj) { arranger->start_object = obj->clone_unique (); },
    variant);
}

void
ArrangerObject::edit_finish (int action_edit_type) const
{
  auto arranger = get_arranger ();
  z_return_if_fail (arranger);
  z_return_if_fail (arranger->start_object);
  try
    {
      UNDO_MANAGER->perform (std::make_unique<ArrangerSelectionsAction::EditAction> (
        *arranger->start_object, *this,
        ENUM_INT_TO_VALUE (ArrangerSelectionsAction::EditType, action_edit_type),
        true));
    }
  catch (const ZrythmException &e)
    {
      e.handle (_ ("Failed to edit object"));
    }
  arranger->start_object.reset ();
}

void
ArrangerObject::edit_position_finish () const
{
  edit_finish (ENUM_VALUE_TO_INT (ArrangerSelectionsAction::EditType::Position));
}

void
ArrangerObject::pos_setter (const Position * pos)
{
  bool success = set_position (pos, PositionType::Start, true);
  if (!success)
    {
      z_debug ("failed to set position [%s]: (invalid)", pos->to_string ());
    }
}

bool
ArrangerObject::are_members_valid (bool is_project) const
{
  if (!is_position_valid (pos_, PositionType::Start))
    {
      if (ZRYTHM_TESTING)
        {
          z_info ("invalid start pos {}", pos_);
        }
      return false;
    }
  return true;
}

Track *
ArrangerObject::get_track () const
{
  if (track_ != nullptr)
    return track_;

  const auto &tracklist =
    is_auditioner_ ? *SAMPLE_PROCESSOR->tracklist_ : *TRACKLIST;

  auto track = tracklist.find_track_by_name_hash (track_name_hash_);
  z_return_val_if_fail (track, nullptr);
  return track;
}

bool
ArrangerObject::is_frozen () const
{
  auto track = get_track ();
  z_return_val_if_fail (track, false);
  return track->frozen_;
}

bool
ArrangerObject::is_hovered () const
{
  return arranger_object_is_hovered (this, nullptr);
}
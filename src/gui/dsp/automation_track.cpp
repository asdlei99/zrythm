// SPDX-FileCopyrightText: © 2018-2024 Alexandros Theodotou <alex@zrythm.org>
// SPDX-License-Identifier: LicenseRef-ZrythmLicense

#include <cmath>
#include <cstddef>
#include <ranges>

#include "dsp/port_identifier.h"
#include "gui/backend/backend/project.h"
#include "gui/backend/backend/zrythm.h"
#include "gui/dsp/automatable_track.h"
#include "gui/dsp/automation_point.h"
#include "gui/dsp/automation_region.h"
#include "gui/dsp/automation_track.h"
#include "gui/dsp/control_port.h"
#include "gui/dsp/track.h"
#include "gui/dsp/tracklist.h"
#include "utils/gtest_wrapper.h"
#include "utils/math.h"
#include "utils/rt_thread_id.h"

AutomationTrack::AutomationTrack (ControlPort &port)
{
  z_return_if_fail (port.id_->validate ());
  port_id_ = port.get_uuid ();

  port.at_ = this;

#if 0
  if (ENUM_BITSET_TEST(PortIdentifier::Flags,port->id_.flags,PortIdentifier::Flags::MIDI_AUTOMATABLE))
    {
      self->automation_mode =
        AutomationMode::AUTOMATION_MODE_RECORD;
      self->record_mode =
        AutomationRecordMode::AUTOMATION_RECORD_MODE_TOUCH;
    }
#endif
}

void
AutomationTrack::init_loaded (AutomationTracklist * atl)
{
  atl_ = atl;

  /* init regions */
  for (auto &region : region_list_->regions_)
    {
      std::get<AutomationRegion *> (region)->init_loaded ();
    }
}

// ========================================================================
// QML Interface
// ========================================================================

QString
AutomationTrack::getLabel () const
{
  auto port = PROJECT->find_port_by_id (port_id_);
  z_return_val_if_fail (port.has_value (), QString ());
  return std::visit (
    [&] (auto &&p) { return QString::fromStdString (p->get_label ()); },
    port.value ());
}

void
AutomationTrack::setHeight (double height)
{
  if (utils::math::floats_equal (height, height_))
    return;

  height_ = height;
  Q_EMIT heightChanged (height);
}

void
AutomationTrack::setAutomationMode (int automation_mode)
{
  if (automation_mode == ENUM_VALUE_TO_INT (automation_mode_))
    return;

  automation_mode_ =
    ENUM_INT_TO_VALUE (decltype (automation_mode_), automation_mode);
  Q_EMIT automationModeChanged (automation_mode);
}

void
AutomationTrack::setRecordMode (int record_mode)
{
  if (record_mode == ENUM_VALUE_TO_INT (record_mode_))
    return;

  record_mode_ = ENUM_INT_TO_VALUE (decltype (record_mode_), record_mode);
  Q_EMIT recordModeChanged (record_mode);
}

// ========================================================================

void
AutomationTrack::set_port_id (const PortUuid &id)
{
  port_id_ = id;
  Q_EMIT labelChanged (getLabel ());
}

bool
AutomationTrack::is_in_active_project () const
{
  return get_track ()->is_in_active_project ();
}

bool
AutomationTrack::is_auditioner () const
{
  return get_track ()->is_auditioner ();
}

bool
AutomationTrack::validate () const
{
  /* this is expensive so only do this during tests */
  if (ZRYTHM_TESTING)
    {
      auto found_at = find_from_port_id (port_id_, !ZRYTHM_TESTING);
      if (found_at != this)
        {
          z_warning (
            "The automation track for port ID {} was not found", port_id_);
          z_warning ("automation tracks:");
          auto * atl = get_automation_tracklist ();
          atl->print_ats ();
          z_return_val_if_reached (false);
        }
    }

  int j = -1;
  for (auto region_var : region_list_->regions_)
    {
      auto * region = std::get<AutomationRegion *> (region_var);
      ++j;
      z_return_val_if_fail (
        region->id_.track_uuid_ == atl_->track_->get_uuid ()
          && region->id_.at_idx_ == index_ && region->id_.idx_ == j,
        false);
      for (const auto &ap : region->aps_)
        {
          z_return_val_if_fail (
            ap->region_id_.track_uuid_ == atl_->track_->get_uuid (), false);
        }
    }

  return true;
}

AutomationTracklist *
AutomationTrack::get_automation_tracklist () const
{
  auto * track = get_track ();
  z_return_val_if_fail (track, nullptr);
  return track->automation_tracklist_;
}

AutomationRegion *
AutomationTrack::get_region_before_pos (
  const Position &pos,
  bool            ends_after,
  bool            use_snapshots) const
{
  auto process_regions = [=] (const auto &regions) {
    if (ends_after)
      {
        for (auto it = regions.rbegin (); it != regions.rend (); ++it)
          {
            const auto        &region_var = *it;
            AutomationRegion * region = nullptr;
            if constexpr (
              std::is_same_v<base_type<decltype (region_var)>, AutomationRegion>)
              {
                region = region_var.get ();
              }
            else
              {
                region = std::get<AutomationRegion *> (region_var);
              }
            if (*region->pos_ <= pos && *region->end_pos_ >= pos)
              return region;
          }
      }
    else
      {
        AutomationRegion * latest_r = nullptr;
        signed_frame_t     latest_distance =
          std::numeric_limits<signed_frame_t>::min ();
        for (auto it = regions.rbegin (); it != regions.rend (); ++it)
          {
            const auto        &region_var = *it;
            AutomationRegion * region = nullptr;
            if constexpr (
              std::is_same_v<base_type<decltype (region_var)>, AutomationRegion>)
              {
                region = region_var.get ();
              }
            else
              {
                region = std::get<AutomationRegion *> (region_var);
              }
            signed_frame_t distance_from_r_end =
              region->end_pos_->frames_ - pos.frames_;
            if (*region->pos_ <= pos && distance_from_r_end > latest_distance)
              {
                latest_distance = distance_from_r_end;
                latest_r = region;
              }
          }
        return latest_r;
      }
    return static_cast<AutomationRegion *> (nullptr);
  };

  return use_snapshots
           ? process_regions (region_snapshots_)
           : process_regions (region_list_->regions_);
}

AutomationPoint *
AutomationTrack::get_ap_before_pos (
  const Position &pos,
  bool            ends_after,
  bool            use_snapshots) const
{
  auto * r = get_region_before_pos (pos, ends_after, use_snapshots);

  if (!r || r->get_muted (true))
    {
      return nullptr;
    }

  /* if region ends before pos, assume pos is the region's end pos */
  signed_frame_t local_pos = r->timeline_frames_to_local (
    !ends_after && (r->end_pos_->frames_ < pos.frames_)
      ? r->end_pos_->frames_ - 1
      : pos.frames_,
    true);

  for (auto &ap : std::ranges::reverse_view (r->aps_))
    {
      if (ap->pos_->frames_ <= local_pos)
        {
          return ap;
        }
    }

  return nullptr;
}

AutomationTrack *
AutomationTrack::find_from_port (
  const ControlPort       &port,
  const AutomatableTrack * track,
  bool                     basic_search)
{
  if (!track)
    {
      auto track_id_opt = port.id_->get_track_id ();
      z_return_val_if_fail (track_id_opt.has_value (), nullptr);
      auto track_var = PROJECT->find_track_by_id (track_id_opt.value ());
      z_return_val_if_fail (track_var.has_value (), nullptr);
      track = std::visit (
        [&] (auto &&t) { return dynamic_cast<AutomatableTrack *> (t); },
        track_var.value ());
    }
  z_return_val_if_fail (track, nullptr);

  auto &atl = track->get_automation_tracklist ();
  auto  it = std::ranges::find_if (atl.ats_, [&] (const auto &at) {
    return at->port_id_ == port.get_uuid ();
  });
  z_return_val_if_fail (it != atl.ats_.end (), nullptr);
  return *it;
}

AutomationTrack *
AutomationTrack::find_from_port_id (const PortUuid &id, bool basic_search)
{
  auto port_var = PROJECT->find_port_by_id (id);
  z_return_val_if_fail (
    port_var && std::holds_alternative<ControlPort *> (*port_var), nullptr);
  auto * port = std::get<ControlPort *> (*port_var);
  z_return_val_if_fail (id == port->get_uuid (), nullptr);

  return find_from_port (*port, nullptr, basic_search);
}

void
AutomationTrack::set_automation_mode (AutomationMode mode, bool fire_events)
{
  z_return_if_fail (ZRYTHM_IS_QT_THREAD);

  auto atl = get_automation_tracklist ();
  z_return_if_fail (atl);

  /* add to atl cache if recording */
  if (mode == AutomationMode::Record)
    {
      if (
        std::find (
          atl->ats_in_record_mode_.begin (), atl->ats_in_record_mode_.end (),
          this)
        == atl->ats_in_record_mode_.end ())
        {
          atl->ats_in_record_mode_.push_back (this);
        }
    }

  automation_mode_ = mode;

  if (fire_events)
    {
      // EVENTS_PUSH (EventType::ET_AUTOMATION_TRACK_CHANGED, this);
    }
}

bool
AutomationTrack::should_read_automation (RtTimePoint cur_time) const
{
  if (automation_mode_ == AutomationMode::Off)
    return false;

  /* TODO last argument should be true but doesnt work properly atm */
  if (should_be_recording (cur_time, false))
    return false;

  return true;
}

bool
AutomationTrack::should_be_recording (RtTimePoint cur_time, bool record_aps) const
{
  if (automation_mode_ != AutomationMode::Record) [[likely]]
    return false;

  if (record_mode_ == AutomationRecordMode::Latch)
    {
      /* in latch mode, we are always recording, even if the value doesn't
       * change (an automation point will be created as soon as latch mode is
       * armed) and then only when changes are made) */
      return true;
    }

  if (record_mode_ == AutomationRecordMode::Touch)
    {
      const auto &port = get_port ();
      const auto  diff = cur_time - port.last_change_time_;
      if (
        diff
        < static_cast<RtTimePoint> (AUTOMATION_RECORDING_TOUCH_REL_MS) * 1000)
        {
          /* still recording */
          return true;
        }

      if (!record_aps)
        return recording_started_;
    }

  return false;
}

AutomatableTrack *
AutomationTrack::get_track () const
{
  auto port_var = PROJECT->find_port_by_id (port_id_);
  z_return_val_if_fail (
    port_var && std::holds_alternative<ControlPort *> (*port_var), nullptr);
  auto * port = std::get<ControlPort *> (*port_var);
  z_return_val_if_fail (port_id_ == port->get_uuid (), nullptr);
  auto track_id_opt = port->id_->get_track_id ();
  z_return_val_if_fail (track_id_opt.has_value (), nullptr);
  auto track_var = PROJECT->find_track_by_id (track_id_opt.value ());
  z_return_val_if_fail (track_var, nullptr);
  return std::visit (
    [&] (auto &track) -> AutomatableTrack * {
      using TrackT = base_type<decltype (track)>;
      if constexpr (std::derived_from<TrackT, AutomatableTrack>)
        return track;
      z_return_val_if_reached (nullptr);
    },
    *track_var);
}

ControlPort &
AutomationTrack::get_port () const
{
  return atl_->get_port (port_id_);
}

void
AutomationTrack::set_index (int index)
{
  index_ = index;

  for (auto &region_var : region_list_->regions_)
    {
      auto * region = std::get<AutomationRegion *> (region_var);
      region->id_.at_idx_ = index;
      region->update_identifier ();
    }
}

float
AutomationTrack::get_val_at_pos (
  const Position &pos,
  bool            normalized,
  bool            ends_after,
  bool            use_snapshots) const
{
  auto ap = get_ap_before_pos (pos, ends_after, use_snapshots);

  auto port_var = PROJECT->find_port_by_id (port_id_);
  z_return_val_if_fail (
    port_var && std::holds_alternative<ControlPort *> (*port_var), 0.f);
  auto * port = std::get<ControlPort *> (*port_var);
  z_return_val_if_fail (port, 0.f);

  /* no automation points yet, return negative (no change) */
  if (!ap)
    {
      return port->get_control_value (normalized);
    }

  auto region = get_region_before_pos (pos, ends_after, use_snapshots);
  z_return_val_if_fail (region, 0.f);

  /* if region ends before pos, assume pos is the region's end pos */
  signed_frame_t localp = region->timeline_frames_to_local (
    !ends_after && (region->end_pos_->frames_ < pos.frames_)
      ? region->end_pos_->frames_ - 1
      : pos.frames_,
    true);

  auto next_ap = region->get_next_ap (*ap, false, false);

  /* return value at last ap */
  if (!next_ap)
    {
      return normalized ? ap->normalized_val_ : ap->fvalue_;
    }

  bool  prev_ap_lower = ap->normalized_val_ <= next_ap->normalized_val_;
  float cur_next_diff =
    std::abs (ap->normalized_val_ - next_ap->normalized_val_);

  /* ratio of how far in we are in the curve */
  signed_frame_t ap_frames = ap->pos_->frames_;
  signed_frame_t next_ap_frames = next_ap->pos_->frames_;
  double         ratio = 1.0;
  signed_frame_t numerator = localp - ap_frames;
  signed_frame_t denominator = next_ap_frames - ap_frames;
  if (numerator == 0)
    {
      ratio = 0.0;
    }
  else if (denominator == 0) [[unlikely]]
    {
      z_warning ("denominator is 0. this should never happen");
      ratio = 1.0;
    }
  else
    {
      ratio = (double) numerator / (double) denominator;
    }
  z_return_val_if_fail (ratio >= 0, 0.f);

  auto result =
    static_cast<float> (ap->get_normalized_value_in_curve (region, ratio));
  result = result * cur_next_diff;
  if (prev_ap_lower)
    result += ap->normalized_val_;
  else
    result += next_ap->normalized_val_;

  if (normalized)
    {
      return result;
    }
  else
    {
      return port->normalized_val_to_real (result);
    }
}

bool
AutomationTrack::verify () const
{
  for (const auto region_var : region_list_->regions_)
    {
      for (const auto &ap : std::get<AutomationRegion *> (region_var)->aps_)
        {
          if (ZRYTHM_TESTING)
            {
              if (
                !utils::math::assert_nonnann (ap->fvalue_)
                || !utils::math::assert_nonnann (ap->normalized_val_))
                {
                  return false;
                }
            }
        }
    }
  return true;
}

void
AutomationTrack::set_caches (CacheType types)
{
  if (ENUM_BITSET_TEST (types, CacheType::PlaybackSnapshots))
    {
      region_snapshots_.clear ();
      for (const auto &r_var : region_list_->regions_)
        {
          region_snapshots_.emplace_back (
            std::get<AutomationRegion *> (r_var)->clone_unique ());
        }
    }

#if 0
  if (ENUM_BITSET_TEST ( types, CacheType::AutomationLanePorts))
    {
      port_ = get_port ();
    }
#endif
}

void
AutomationTrack::init_after_cloning (
  const AutomationTrack &other,
  ObjectCloneType        clone_type)
{
  RegionOwnerImpl<AutomationRegion>::copy_members_from (other, clone_type);
  visible_ = other.visible_;
  created_ = other.created_;
  index_ = other.index_;
  y_ = other.y_;
  automation_mode_ = other.automation_mode_;
  record_mode_ = other.record_mode_;
  height_ = other.height_;
  z_warn_if_fail (height_ >= TRACK_MIN_HEIGHT);
  port_id_ = other.port_id_;
}

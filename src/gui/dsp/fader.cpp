// SPDX-FileCopyrightText: © 2019-2022, 2024 Alexandros Theodotou <alex@zrythm.org>
// SPDX-License-Identifier: LicenseRef-ZrythmLicense

#include "dsp/panning.h"
#include "gui/backend/backend/actions/tracklist_selections_action.h"
#include "gui/backend/backend/project.h"
#include "gui/backend/backend/settings_manager.h"
#include "gui/backend/backend/zrythm.h"
#include "gui/backend/channel.h"
#include "gui/dsp/audio_port.h"
#include "gui/dsp/control_port.h"
#include "gui/dsp/control_room.h"
#include "gui/dsp/engine.h"
#include "gui/dsp/fader.h"
#include "gui/dsp/group_target_track.h"
#include "gui/dsp/master_track.h"
#include "gui/dsp/midi_event.h"
#include "gui/dsp/port.h"
#include "gui/dsp/track.h"
#include "gui/dsp/tracklist.h"
#include "utils/debug.h"
#include "utils/dsp.h"
#include "utils/gtest_wrapper.h"
#include "utils/math.h"
#include "utils/midi.h"
#include "utils/rt_thread_id.h"

using namespace zrythm;

Fader::Fader (QObject * parent) : QObject (parent) { }

void
Fader::init_loaded (
  PortRegistry     &port_registry,
  Track *           track,
  ControlRoom *     control_room,
  SampleProcessor * sample_processor)
{
  port_registry_ = port_registry;
  track_ = track;
  control_room_ = control_room;
  sample_processor_ = sample_processor;

  std::vector<Port *> ports;
  append_ports (ports);
  for (auto * port : ports)
    {
      port->init_loaded (*this);
    }

  set_amp (get_amp_port ().control_);
}

void
Fader::append_ports (std::vector<Port *> &ports) const
{
  auto add_port = [&ports] (Port &port) {
    ports.push_back (std::addressof (port));
  };

  add_port (get_amp_port ());
  add_port (get_balance_port ());
  add_port (get_mute_port ());
  add_port (get_solo_port ());
  add_port (get_listen_port ());
  add_port (get_mono_compat_enabled_port ());
  add_port (get_swap_phase_port ());
  if (has_audio_ports ())
    {
      add_port (get_stereo_in_ports ().first);
      add_port (get_stereo_in_ports ().second);
      add_port (get_stereo_out_ports ().first);
      add_port (get_stereo_out_ports ().second);
    }
  if (has_midi_ports ())
    {
      add_port (get_midi_in_port ());
      add_port (get_midi_out_port ());
    }
}

ControlPort *
Fader::create_swap_phase_port (PortRegistry &port_registry, bool passthrough)
{
  auto swap_phase = port_registry.create_object<ControlPort> (
    passthrough
      ? QObject::tr ("Prefader Swap Phase").toStdString ()
      : QObject::tr ("Fader Swap Phase").toStdString ());
  swap_phase->id_->sym_ =
    passthrough ? "prefader_swap_phase" : "fader_swap_phase";
  swap_phase->id_->flags2_ |= dsp::PortIdentifier::Flags2::FaderSwapPhase;
  swap_phase->id_->flags_ |= dsp::PortIdentifier::Flags::Toggle;
  return swap_phase;
}

Fader::Fader (
  PortRegistry     &port_registry,
  Type              type,
  bool              passthrough,
  Track *           track,
  ControlRoom *     control_room,
  SampleProcessor * sample_processor)
    : type_ (type), midi_mode_ (MidiFaderMode::MIDI_FADER_MODE_VEL_MULTIPLIER),
      passthrough_ (passthrough), track_ (track), control_room_ (control_room),
      sample_processor_ (sample_processor), port_registry_ (port_registry)
{
  /* set volume */
  float amp = 1.f;
  auto  amp_port = port_registry_->create_object<ControlPort> (
    passthrough
      ? QObject::tr ("Prefader Volume").toStdString ()
      : QObject::tr ("Fader Volume").toStdString ());
  amp_id_ = amp_port->get_uuid ();
  amp_port->set_owner (*this);
  amp_port->id_->sym_ = passthrough ? "prefader_volume" : "fader_volume";
  amp_port->deff_ = amp;
  amp_port->range_.minf_ = 0.f;
  amp_port->range_.maxf_ = 2.f;
  amp_port->set_control_value (amp, false, false);
  fader_val_ = utils::math::get_fader_val_from_amp (amp);
  amp_port->id_->flags_ |= dsp::PortIdentifier::Flags::Amplitude;
  if ((type == Type::AudioChannel || type == Type::MidiChannel) && !passthrough)
    {
      amp_port->id_->flags_ |= dsp::PortIdentifier::Flags::Automatable;
      amp_port->id_->flags_ |= dsp::PortIdentifier::Flags::ChannelFader;
    }

  /* set pan */
  float balance = 0.5f;
  auto  balance_port = port_registry_->create_object<ControlPort> (
    passthrough
      ? QObject::tr ("Prefader Balance").toStdString ()
      : QObject::tr ("Fader Balance").toStdString ());
  balance_id_ = balance_port->get_uuid ();
  balance_port->set_owner (*this);
  balance_port->id_->sym_ = passthrough ? "prefader_balance" : "fader_balance";
  balance_port->set_control_value (balance, 0, 0);
  balance_port->id_->flags_ |= dsp::PortIdentifier::Flags::StereoBalance;
  if ((type == Type::AudioChannel || type == Type::MidiChannel) && !passthrough)
    {
      balance_port->id_->flags_ |= dsp::PortIdentifier::Flags::Automatable;
    }

  /* set mute */
  auto mute_port = port_registry_->create_object<ControlPort> (
    passthrough
      ? QObject::tr ("Prefader Mute").toStdString ()
      : QObject::tr ("Fader Mute").toStdString ());
  mute_id_ = mute_port->get_uuid ();
  mute_port->set_owner (*this);
  mute_port->id_->sym_ = passthrough ? "prefader_mute" : "fader_mute";
  mute_port->set_toggled (false, false);
  mute_port->id_->flags_ |= dsp::PortIdentifier::Flags::FaderMute;
  mute_port->id_->flags_ |= dsp::PortIdentifier::Flags::Toggle;
  if ((type == Type::AudioChannel || type == Type::MidiChannel) && !passthrough)
    {
      mute_port->id_->flags_ |= dsp::PortIdentifier::Flags::Automatable;
    }

  /* set solo */
  auto solo_port = port_registry_->create_object<ControlPort> (
    passthrough
      ? QObject::tr ("Prefader Solo").toStdString ()
      : QObject::tr ("Fader Solo").toStdString ());
  solo_id_ = solo_port->get_uuid ();
  solo_port->set_owner (*this);
  solo_port->id_->sym_ = passthrough ? "prefader_solo" : "fader_solo";
  solo_port->set_toggled (false, false);
  solo_port->id_->flags2_ |= dsp::PortIdentifier::Flags2::FaderSolo;
  solo_port->id_->flags_ |= dsp::PortIdentifier::Flags::Toggle;

  /* set listen */
  auto listen_port = port_registry_->create_object<ControlPort> (
    passthrough
      ? QObject::tr ("Prefader Listen").toStdString ()
      : QObject::tr ("Fader Listen").toStdString ());
  listen_id_ = listen_port->get_uuid ();
  listen_port->set_owner (*this);
  listen_port->id_->sym_ = passthrough ? "prefader_listen" : "fader_listen";
  listen_port->set_toggled (false, false);
  listen_port->id_->flags2_ |= dsp::PortIdentifier::Flags2::FaderListen;
  listen_port->id_->flags_ |= dsp::PortIdentifier::Flags::Toggle;

  /* set mono compat */
  auto mono_compat_enabled_port = port_registry_->create_object<ControlPort> (
    passthrough
      ? QObject::tr ("Prefader Mono Compat").toStdString ()
      : QObject::tr ("Fader Mono Compat").toStdString ());
  mono_compat_enabled_id_ = mono_compat_enabled_port->get_uuid ();
  mono_compat_enabled_port->set_owner (*this);
  mono_compat_enabled_port->id_->sym_ =
    passthrough ? "prefader_mono_compat_enabled" : "fader_mono_compat_enabled";
  mono_compat_enabled_port->set_toggled (false, false);
  mono_compat_enabled_port->id_->flags2_ |=
    dsp::PortIdentifier::Flags2::FaderMonoCompat;
  mono_compat_enabled_port->id_->flags_ |= dsp::PortIdentifier::Flags::Toggle;

  /* set swap phase */
  auto swap_phase_port =
    create_swap_phase_port (port_registry_.value (), passthrough);
  swap_phase_id_ = swap_phase_port->get_uuid ();
  swap_phase_port->set_toggled (false, false);
  swap_phase_port->set_owner (*this);

  if (has_audio_ports ())
    {
      std::string name;
      std::string sym;
      if (type == Type::AudioChannel)
        {
          if (passthrough)
            {
              name = QObject::tr ("Ch Pre-Fader in").toStdString ();
              sym = "ch_prefader_in";
            }
          else
            {
              name = QObject::tr ("Ch Fader in").toStdString ();
              sym = "ch_fader_in";
            }
        }
      else if (type == Type::SampleProcessor)
        {
          name = QObject::tr ("Sample Processor Fader in").toStdString ();
          sym = "sample_processor_fader_in";
        }
      else
        {
          name = QObject::tr ("Monitor Fader in").toStdString ();
          sym = "monitor_fader_in";
        }

      /* stereo in */
      auto stereo_in_ports = StereoPorts::create_stereo_ports (
        port_registry_.value (), true, name, sym);
      auto * left_port = stereo_in_ports.first;
      auto * right_port = stereo_in_ports.second;
      stereo_in_left_id_ = left_port->get_uuid ();
      stereo_in_right_id_ = right_port->get_uuid ();
      left_port->set_owner (*this);
      right_port->set_owner (*this);

      if (type == Type::AudioChannel)
        {
          if (passthrough)
            {
              name = QObject::tr ("Ch Pre-Fader out").toStdString ();
              sym = "ch_prefader_out";
            }
          else
            {
              name = QObject::tr ("Ch Fader out").toStdString ();
              sym = "ch_fader_out";
            }
        }
      else if (type == Type::SampleProcessor)
        {
          name = QObject::tr ("Sample Processor Fader out").toStdString ();
          sym = "sample_processor_fader_out";
        }
      else
        {
          name = QObject::tr ("Monitor Fader out").toStdString ();
          sym = "monitor_fader_out";
        }

      /* stereo out */
      auto stereo_out_ports = StereoPorts::create_stereo_ports (
        port_registry_.value (), false, name, sym);
      left_port = stereo_out_ports.first;
      right_port = stereo_out_ports.second;
      stereo_out_left_id_ = left_port->get_uuid ();
      stereo_out_right_id_ = right_port->get_uuid ();
      left_port->set_owner (*this);
      right_port->set_owner (*this);
    }

  if (has_midi_ports ())
    {
      /* MIDI in */
      std::string name;
      std::string sym;
      if (passthrough)
        {
          name = QObject::tr ("Ch MIDI Pre-Fader in").toStdString ();
          sym = "ch_midi_prefader_in";
        }
      else
        {
          name = QObject::tr ("Ch MIDI Fader in").toStdString ();
          sym = "ch_midi_fader_in";
        }
      auto midi_in_port =
        port_registry_->create_object<MidiPort> (name, dsp::PortFlow::Input);
      midi_in_id_ = midi_in_port->get_uuid ();
      midi_in_port->set_owner (*this);
      midi_in_port->id_->sym_ = sym;

      /* MIDI out */
      if (passthrough)
        {
          name = QObject::tr ("Ch MIDI Pre-Fader out").toStdString ();
          sym = "ch_midi_prefader_out";
        }
      else
        {
          name = QObject::tr ("Ch MIDI Fader out").toStdString ();
          sym = "ch_midi_fader_out";
        }
      auto midi_out_port =
        port_registry_->create_object<MidiPort> (name, dsp::PortFlow::Output);
      midi_out_id_ = midi_out_port->get_uuid ();
      midi_out_port->set_owner (*this);
      midi_out_port->id_->sym_ = sym;
    }
}

void
Fader::set_port_metadata_from_owner (dsp::PortIdentifier &id, PortRange &range)
  const
{
  id.owner_type_ = dsp::PortIdentifier::OwnerType::Fader;

  using PortIdentifier = dsp::PortIdentifier;

  if (type_ == Fader::Type::AudioChannel || type_ == Fader::Type::MidiChannel)
    {
      auto * track = get_track ();
      z_return_if_fail (track);
      id.track_id_ = track->get_uuid ();
      if (passthrough_)
        {
          id.flags2_ |= PortIdentifier::Flags2::Prefader;
        }
      else
        {
          id.flags2_ |= PortIdentifier::Flags2::Postfader;
        }
    }
  else if (type_ == Fader::Type::SampleProcessor)
    {
      id.flags2_ |= PortIdentifier::Flags2::SampleProcessorFader;
    }
  else
    {
      id.flags2_ |= PortIdentifier::Flags2::MonitorFader;
    }

  if (ENUM_BITSET_TEST (id.flags_, PortIdentifier::Flags::Amplitude))
    {
      range.minf_ = 0.f;
      range.maxf_ = 2.f;
      range.zerof_ = 0.f;
    }
  else if (ENUM_BITSET_TEST (id.flags_, PortIdentifier::Flags::StereoBalance))
    {
      range.minf_ = 0.f;
      range.maxf_ = 1.f;
      range.zerof_ = 0.5f;
    }
}

std::string
Fader::get_full_designation_for_port (const dsp::PortIdentifier &id) const
{
  if (
    ENUM_BITSET_TEST (id.flags2_, dsp::PortIdentifier::Flags2::Prefader)
    || ENUM_BITSET_TEST (id.flags2_, dsp::PortIdentifier::Flags2::Postfader))
    {
      auto * tr = get_track ();
      z_return_val_if_fail (tr, {});
      return fmt::format ("{}/{}", tr->get_name (), id.get_label ());
    }
  if (ENUM_BITSET_TEST (id.flags2_, dsp::PortIdentifier::Flags2::MonitorFader))
    {
      return fmt::format ("Engine/{}", id.get_label ());
    }
  if (ENUM_BITSET_TEST (
        id.flags2_, dsp::PortIdentifier::Flags2::SampleProcessorFader))
    {
      return id.get_label ();
    }
  z_return_val_if_reached ({});
}

void
Fader::on_control_change_event (
  const PortUuid            &port_uuid,
  const dsp::PortIdentifier &id,
  float                      val)
{
  using PortIdentifier = dsp::PortIdentifier;
  if (
    ENUM_BITSET_TEST (id.flags_, PortIdentifier::Flags::FaderMute)
    || ENUM_BITSET_TEST (id.flags2_, PortIdentifier::Flags2::FaderSolo)
    || ENUM_BITSET_TEST (id.flags2_, PortIdentifier::Flags2::FaderListen)
    || ENUM_BITSET_TEST (id.flags2_, PortIdentifier::Flags2::FaderMonoCompat))
    {
      // EVENTS_PUSH (EventType::ET_TRACK_FADER_BUTTON_CHANGED, track);
    }
  else if (ENUM_BITSET_TEST (id.flags_, PortIdentifier::Flags::Amplitude))
    {
      if (!utils::math::floats_equal (val, get_amp_port ().control_))
        {
          update_volume_and_fader_val ();
        }
    }
}

bool
Fader::should_bounce_to_master (utils::audio::BounceStep step) const
{
  // only pre-fader bounces make sense for faders (post-fader bounces are
  // handled by Channel)
  if (!passthrough_ || step != utils::audio::BounceStep::PreFader)
    {
      return false;
    }

  auto * track = get_track ();
  return !track->is_master () && track->bounce_to_master_;
}

void
Fader::set_muted (bool mute, bool fire_events)
{
  auto track = get_track ();
  z_return_if_fail (track);

  get_mute_port ().set_toggled (mute, fire_events);

  if (fire_events)
    {
      // EVENTS_PUSH (EventType::ET_TRACK_STATE_CHANGED, track);
    }
}

bool
Fader::get_implied_soloed () const
{
  /* only check channel faders */
  if (
    (type_ != Type::AudioChannel && type_ != Type::MidiChannel) || passthrough_
    || get_solo_port ().is_toggled ())
    {
      return false;
    }

  auto track = get_track ();
  z_return_val_if_fail (track, false);

  /* check parents */
  auto out_track = track;
  do
    {
      if (out_track->has_channel ())
        {
          auto channel_track = dynamic_cast<ChannelTrack *> (out_track);
          out_track = channel_track->channel_->get_output_track ();
          if (out_track && out_track->get_soloed ())
            {
              return true;
            }
        }
      else
        {
          out_track = nullptr;
        }
    }
  while (out_track);

  /* check children */
  if (track->can_be_group_target ())
    {
      auto * group_target = dynamic_cast<GroupTargetTrack *> (track);
      for (const auto &child_id : group_target->children_)
        {
          auto child_track_var = TRACKLIST->get_track (child_id);

          auto child_soloed_or_implied_soloed =
            child_track_var.has_value ()
            && std::visit (
              [&] (auto &&child_track) {
                return static_cast<bool> (
                  child_track->get_soloed ()
                  || child_track->get_implied_soloed ());
              },
              child_track_var.value ());
          if (child_soloed_or_implied_soloed)
            {
              return true;
            }
        }
    }

  return false;
}

void
Fader::set_soloed (bool solo, bool fire_events)
{
  auto track = get_track ();
  z_return_if_fail (track);

  get_solo_port ().set_toggled (solo, fire_events);

  if (fire_events)
    {
      // EVENTS_PUSH (EventType::ET_TRACK_STATE_CHANGED, track);
    }
}

std::string
Fader::db_string_getter () const
{
  return fmt::format (
    "{:.1f}", utils::math::amp_to_dbfs (get_amp_port ().control_));
}

bool
Fader::is_in_active_project () const
{
  return (track_ && track_->is_in_active_project ())
         || (sample_processor_ && sample_processor_->is_in_active_project ())
         || (control_room_ && control_room_->is_in_active_project ());
}

void
Fader::set_listened (bool listen, bool fire_events)
{
  auto track = get_track ();
  z_return_if_fail (track);

  get_listen_port ().set_toggled (listen, fire_events);

  if (fire_events)
    {
      // EVENTS_PUSH (EventType::ET_TRACK_STATE_CHANGED, track);
    }
}

void
Fader::update_volume_and_fader_val ()
{
  volume_ = utils::math::amp_to_dbfs (get_amp_port ().control_);
  fader_val_ = utils::math::get_fader_val_from_amp (get_amp_port ().control_);
}

void
Fader::set_amp (float amp)
{
  get_amp_port ().set_control_value (amp, false, false);
  update_volume_and_fader_val ();
}

void
Fader::set_amp_with_action (float amp_from, float amp_to, bool skip_if_equal)
{
  auto track = get_track ();
  bool is_equal = utils::math::floats_near (amp_from, amp_to, 0.0001f);
  if (!skip_if_equal || !is_equal)
    {
      try
        {
          UNDO_MANAGER->perform (new gui::actions::SingleTrackFloatAction (
            gui::actions::TracklistSelectionsAction::EditType::Volume,
            convert_to_variant<TrackPtrVariant> (track), amp_from, amp_to,
            false));
        }
      catch (const ZrythmException &e)
        {
          e.handle (QObject::tr ("Failed to change volume"));
        }
    }
}

void
Fader::add_amp (sample_t amp)
{
  float fader_amp = get_amp ();
  fader_amp = get_amp_port ().range_.clamp_to_range (fader_amp + amp);
  set_amp (fader_amp);
  update_volume_and_fader_val ();
}

void
Fader::set_midi_mode (MidiFaderMode mode, bool with_action, bool fire_events)
{
  if (with_action)
    {
      auto track = get_track ();
      z_return_if_fail (track);

      try
        {
          UNDO_MANAGER->perform (new gui::actions::SingleTrackIntAction (
            gui::actions::TracklistSelectionsAction::EditType::MidiFaderMode,
            convert_to_variant<TrackPtrVariant> (track),
            static_cast<int> (mode), false));
        }
      catch (const ZrythmException &e)
        {
          e.handle (QObject::tr ("Failed to set MIDI mode"));
        }
    }
  else
    {
      midi_mode_ = mode;
    }

  if (fire_events)
    {
      // TODO: Implement event firing
    }
}

void
Fader::set_mono_compat_enabled (bool enabled, bool fire_events)
{
  get_mono_compat_enabled_port ().set_toggled (enabled, fire_events);

  if (type_ == Type::AudioChannel || type_ == Type::MidiChannel)
    {
      auto track = get_track ();
      z_return_if_fail (track);
      if (fire_events)
        {
          // EVENTS_PUSH (EventType::ET_TRACK_STATE_CHANGED, track);
        }
    }
}

void
Fader::set_swap_phase (bool enabled, bool fire_events)
{
  get_swap_phase_port ().set_toggled (enabled, fire_events);

  if (type_ == Type::AudioChannel || type_ == Type::MidiChannel)
    {
      auto track = get_track ();
      z_return_if_fail (track);
      if (fire_events)
        {
          // EVENTS_PUSH (EventType::ET_TRACK_STATE_CHANGED, track);
        }
    }
}

void
Fader::set_fader_val (float fader_val)
{
  fader_val_ = fader_val;
  float fader_amp = utils::math::get_amp_val_from_fader (fader_val);
  fader_amp = get_amp_port ().range_.clamp_to_range (fader_amp);
  set_amp (fader_amp);
  volume_ = utils::math::amp_to_dbfs (fader_amp);

  if (this == MONITOR_FADER.get ())
    {
      gui::SettingsManager::get_instance ()->set_monitorVolume (fader_amp);
    }
  else if (this == CONTROL_ROOM->mute_fader_.get ())
    {
      gui::SettingsManager::get_instance ()->set_monitorMuteVolume (fader_amp);
    }
  else if (this == CONTROL_ROOM->listen_fader_.get ())
    {
      gui::SettingsManager::get_instance ()->set_monitorListenVolume (fader_amp);
    }
  else if (this == CONTROL_ROOM->dim_fader_.get ())
    {
      gui::SettingsManager::get_instance ()->set_monitorDimVolume (fader_amp);
    }
}

void
Fader::set_fader_val_with_action_from_db (const std::string &str)
{
  bool  is_valid = false;
  float val;
  if (utils::math::is_string_valid_float (str, &val))
    {
      if (val <= 6.f)
        {
          is_valid = true;
        }
    }

  if (is_valid)
    {
      set_amp_with_action (get_amp (), utils::math::dbfs_to_amp (val), true);
    }
  else
    {
      // ui_show_error_message (QObject::tr ("Invalid Value"), QObject::tr
      // ("Invalid value"));
    }
}

Fader::Channel *
Fader::get_channel () const
{
  auto track = dynamic_cast<ChannelTrack *> (get_track ());
  z_return_val_if_fail (track, nullptr);

  return track->get_channel ();
}

Track *
Fader::get_track () const
{
  z_return_val_if_fail (track_, nullptr);
  return track_;
}

void
Fader::clear_buffers ()
{
  if (has_audio_ports ())
    {
      auto stereo_in = get_stereo_in_ports ();
      stereo_in.first.clear_buffer (*AUDIO_ENGINE);
      stereo_in.second.clear_buffer (*AUDIO_ENGINE);
      auto stereo_out = get_stereo_out_ports ();
      stereo_out.first.clear_buffer (*AUDIO_ENGINE);
      stereo_out.second.clear_buffer (*AUDIO_ENGINE);
    }
  else if (has_midi_ports ())
    {
      auto &midi_in = get_midi_in_port ();
      midi_in.clear_buffer (*AUDIO_ENGINE);
      auto &midi_out = get_midi_out_port ();
      midi_out.clear_buffer (*AUDIO_ENGINE);
    }
}

void
Fader::disconnect_all ()
{
  if (has_audio_ports ())
    {
      auto stereo_in = get_stereo_in_ports ();
      stereo_in.first.disconnect_all ();
      stereo_in.second.disconnect_all ();
      auto stereo_out = get_stereo_out_ports ();
      stereo_out.first.disconnect_all ();
      stereo_out.second.disconnect_all ();
    }
  else if (has_midi_ports ())
    {
      auto &midi_in = get_midi_in_port ();
      midi_in.disconnect_all ();
      auto &midi_out = get_midi_out_port ();
      midi_out.disconnect_all ();
    }

  get_amp_port ().disconnect_all ();
  get_balance_port ().disconnect_all ();
  get_mute_port ().disconnect_all ();
  get_solo_port ().disconnect_all ();
  get_listen_port ().disconnect_all ();
  get_mono_compat_enabled_port ().disconnect_all ();
  get_swap_phase_port ().disconnect_all ();
}

int
Fader::fade_frames_for_type (Type type)
{
  return type == Type::Monitor
           ? FADER_DEFAULT_FADE_FRAMES
           : FADER_DEFAULT_FADE_FRAMES_SHORT;
}

std::string
Fader::get_node_name () const
{
  if (type_ == Type::AudioChannel || type_ == Type::MidiChannel)
    {
      auto * track = get_track ();
      return fmt::format (
        "{} {}", track->get_name (), passthrough_ ? "Pre-Fader" : "Fader");
    }
  if (type_ == Type::Monitor)
    {
      return "Monitor Fader";
    }

  return "Fader";
}

/**
 * Process the Fader.
 */
void
Fader::process_block (const EngineProcessTimeInfo time_nfo)
{
  if (ZRYTHM_TESTING)
    {
#if 0
      z_debug (
        "g_start %ld, start frame {}, nframes {}", time_nfo->g_start_frame_w_offset,
        time_nfo->local_offset, time_nfo->nframes);
#endif
    }

  Track * track = nullptr;
  if (type_ == Type::AudioChannel)
    {
      track = get_track ();
      z_return_if_fail (track);
    }

  const int default_fade_frames = fade_frames_for_type (type_);

  bool effectively_muted = false;
  if (!passthrough_)
    {
      /* muted if any of the following is true:
       * 1. muted
       * 2. other track(s) is soloed and this isn't
       * 3. bounce mode and the track is set to BOUNCE_OFF */
      effectively_muted =
        get_muted()
        ||
        ((type_ == Type::AudioChannel || type_ == Type::MidiChannel)
         && TRACKLIST->get_track_span().has_soloed() && !get_soloed() && !get_implied_soloed()
         && track != P_MASTER_TRACK)
        ||
        (AUDIO_ENGINE->bounce_mode_ == BounceMode::BOUNCE_ON
         &&
         (type_ == Type::AudioChannel
          || type_ == Type::MidiChannel)
         && track
         && !track->is_master()
         && !track->bounce_);

#if 0
      if (ZRYTHM_TESTING && track &&
          (self->type == Fader::Type::FADER_TYPE_AUDIO_CHANNEL ||
           self->type == Fader::Type::FADER_TYPE_MIDI_CHANNEL))
        {
          z_debug ("{} soloed {} implied soloed {} effectively muted {}",
            track->name, fader_get_soloed (self),
            fader_get_implied_soloed (self),
            effectively_muted);
        }
#endif
    }

  if (has_audio_ports ())
    {
      auto stereo_in = get_stereo_in_ports ();
      auto stereo_out = get_stereo_out_ports ();
      /* copy the input to output */
      utils::float_ranges::copy (
        &stereo_out.first.buf_[time_nfo.local_offset_],
        &stereo_in.first.buf_[time_nfo.local_offset_], time_nfo.nframes_);
      utils::float_ranges::copy (
        &stereo_out.second.buf_[time_nfo.local_offset_],
        &stereo_in.second.buf_[time_nfo.local_offset_], time_nfo.nframes_);

      /* if prefader */
      if (passthrough_)
        {

          /* if track frozen and transport is rolling */
          if (track && track->frozen_ && TRANSPORT->isRolling ())
            {
#if 0
              /* get audio from clip */
              AudioClip * clip =
                audio_pool_get_clip (
                  AUDIO_POOL, track->pool_id_);
              /* FIXME this is wrong - need to
               * also calculate the offset in the
               * clip */
              stereo_ports_fill_from_clip (
                self->stereo_out, clip,
                time_nfo->g_start_frame_w_offset,
                time_nfo->local_offset,
                time_nfo->nframes);
#endif
            }
        }
      else /* not prefader */
        {
          /* if monitor */
          float mute_amp;
          if (type_ == Fader::Type::Monitor)
            {
              mute_amp = AUDIO_ENGINE->denormal_prevention_val_;
              float dim_amp = CONTROL_ROOM->dim_fader_->get_amp ();

              /* if have listened tracks */
              if (TRACKLIST->get_track_span ().has_listened ())
                {
                  /* dim signal */
                  utils::float_ranges::mul_k2 (
                    &stereo_out.first.buf_[time_nfo.local_offset_], dim_amp,
                    time_nfo.nframes_);
                  utils::float_ranges::mul_k2 (
                    &stereo_out.second.buf_[time_nfo.local_offset_], dim_amp,
                    time_nfo.nframes_);

                  /* add listened signal */
                  /* TODO add "listen" buffer on fader struct and add listened
                   * tracks to it during processing instead of looping here */
                  float listen_amp = CONTROL_ROOM->listen_fader_->get_amp ();
                  for (auto &cur_t : TRACKLIST->get_track_span ())
                    {
                      std::visit (
                        [&] (auto &&t) {
                          using TrackT = base_type<decltype (t)>;
                          if constexpr (std::derived_from<TrackT, ChannelTrack>)
                            {
                              if (
                                t->out_signal_type_ == dsp::PortType::Audio
                                && t->get_listened ())
                                {
                                  auto f = t->get_fader (true);
                                  utils::float_ranges::mix_product (
                                    &stereo_out.first
                                       .buf_[time_nfo.local_offset_],
                                    &f->get_stereo_out_ports ()
                                       .first.buf_[time_nfo.local_offset_],
                                    listen_amp, time_nfo.nframes_);
                                  utils::float_ranges::mix_product (
                                    &stereo_out.second
                                       .buf_[time_nfo.local_offset_],
                                    &f->get_stereo_out_ports ()
                                       .second.buf_[time_nfo.local_offset_],
                                    listen_amp, time_nfo.nframes_);
                                }
                            }
                        },
                        cur_t);
                    }
                } /* endif have listened tracks */

              /* apply dim if enabled */
              if (CONTROL_ROOM->dim_output_)
                {
                  utils::float_ranges::mul_k2 (
                    &stereo_out.first.buf_[time_nfo.local_offset_], dim_amp,
                    time_nfo.nframes_);
                  utils::float_ranges::mul_k2 (
                    &stereo_out.second.buf_[time_nfo.local_offset_], dim_amp,
                    time_nfo.nframes_);
                }
            } /* endif monitor fader */
          else
            {
              mute_amp = CONTROL_ROOM->mute_fader_->get_amp ();

              /* add fade if changed from muted to non-muted or
               * vice versa */
              if (effectively_muted && !was_effectively_muted_)
                {
                  fade_out_samples_.store (default_fade_frames);
                  fading_out_.store (true);
                }
              else if (!effectively_muted && was_effectively_muted_)
                {

                  fading_out_.store (false);
                  fade_in_samples_.store (default_fade_frames);
                }
            }

          /* handle fade in */
          int fade_in_samples = fade_in_samples_.load ();
          if (fade_in_samples > 0) [[unlikely]]
            {
              z_return_if_fail_cmp (default_fade_frames, >=, fade_in_samples);
#if 0
              z_debug (
                "fading in %d samples", fade_in_samples);
#endif
              utils::float_ranges::linear_fade_in_from (
                &stereo_out.first.buf_[time_nfo.local_offset_],
                default_fade_frames - fade_in_samples, default_fade_frames,
                time_nfo.nframes_, mute_amp);
              utils::float_ranges::linear_fade_in_from (
                &stereo_out.second.buf_[time_nfo.local_offset_],
                default_fade_frames - fade_in_samples, default_fade_frames,
                time_nfo.nframes_, mute_amp);
              fade_in_samples -= (int) time_nfo.nframes_;
              fade_in_samples = std::max (fade_in_samples, 0);
              fade_in_samples_.store (fade_in_samples);
            }

          /* handle fade out */
          size_t faded_out_frames = 0;
          if (fading_out_.load ()) [[unlikely]]
            {
              int fade_out_samples = fade_out_samples_.load ();
              int samples_to_process = std::max (
                0, std::min (fade_out_samples, (int) time_nfo.nframes_));
              if (fade_out_samples > 0)
                {
                  z_return_if_fail_cmp (
                    default_fade_frames, >=, fade_out_samples);

#if 0
                  z_debug (
                    "fading out %d frames",
                    samples_to_process);
#endif
                  utils::float_ranges::linear_fade_out_to (
                    &stereo_out.first.buf_[time_nfo.local_offset_],
                    default_fade_frames - fade_out_samples, default_fade_frames,
                    (size_t) samples_to_process, mute_amp);
                  utils::float_ranges::linear_fade_out_to (
                    &stereo_out.second.buf_[time_nfo.local_offset_],
                    default_fade_frames - fade_out_samples, default_fade_frames,
                    (size_t) samples_to_process, mute_amp);
                  fade_out_samples -= samples_to_process;
                  faded_out_frames += (size_t) samples_to_process;
                  fade_out_samples_.store (fade_out_samples);
                }

              /* if still fading out and have no more fade out samples, silence */
              if (fade_out_samples == 0)
                {
                  size_t remaining_frames =
                    time_nfo.nframes_ - (size_t) samples_to_process;
#if 0
                  z_debug (
                    "silence for remaining %zu frames", remaining_frames);
#endif
                  if (remaining_frames > 0)
                    {
                      utils::float_ranges::mul_k2 (
                        &stereo_out.first
                           .buf_[time_nfo.local_offset_ + faded_out_frames],
                        mute_amp, remaining_frames);
                      utils::float_ranges::mul_k2 (
                        &stereo_out.second
                           .buf_[time_nfo.local_offset_ + faded_out_frames],
                        mute_amp, remaining_frames);
                      faded_out_frames += (size_t) remaining_frames;
                    }
                }
            }

          const float pan = get_balance_port ().get_control_value (false);
          const float amp = get_amp_port ().get_control_value (false);

          auto [calc_l, calc_r] = dsp::calculate_balance_control (
            dsp::BalanceControlAlgorithm::Linear, pan);

          /* apply fader and pan */
          utils::float_ranges::mul_k2 (
            &stereo_out.first.buf_[time_nfo.local_offset_], amp * calc_l,
            time_nfo.nframes_);
          utils::float_ranges::mul_k2 (
            &stereo_out.second.buf_[time_nfo.local_offset_], amp * calc_r,
            time_nfo.nframes_);

          /* make mono if mono compat enabled */
          if (get_mono_compat_enabled_port ().is_toggled ())
            {
              utils::float_ranges::make_mono (
                &stereo_out.first.buf_[time_nfo.local_offset_],
                &stereo_out.second.buf_[time_nfo.local_offset_],
                time_nfo.nframes_, false);
            }

          /* swap phase if need */
          if (get_swap_phase_port ().is_toggled ())
            {
              utils::float_ranges::mul_k2 (
                &stereo_out.first.buf_[time_nfo.local_offset_], -1.f,
                time_nfo.nframes_);
              utils::float_ranges::mul_k2 (
                &stereo_out.second.buf_[time_nfo.local_offset_], -1.f,
                time_nfo.nframes_);
            }

          int fade_out_samples = fade_out_samples_.load ();
          if (
            effectively_muted && fade_out_samples == 0
            && time_nfo.nframes_ - faded_out_frames > 0)
            {
#if 0
              z_debug (
                "muting %zu frames",
                time_nfo->nframes - faded_out_frames);
#endif
              /* apply mute level */
              if (mute_amp < 0.00001f)
                {
                  utils::float_ranges::fill (
                    &stereo_out.first
                       .buf_[time_nfo.local_offset_ + faded_out_frames],
                    AUDIO_ENGINE->denormal_prevention_val_,
                    time_nfo.nframes_ - faded_out_frames);
                  utils::float_ranges::fill (
                    &stereo_out.second
                       .buf_[time_nfo.local_offset_ + faded_out_frames],
                    AUDIO_ENGINE->denormal_prevention_val_,
                    time_nfo.nframes_ - faded_out_frames);
                }
              else
                {
                  utils::float_ranges::mul_k2 (
                    &stereo_out.first.buf_[time_nfo.local_offset_], mute_amp,
                    time_nfo.nframes_ - faded_out_frames);
                  utils::float_ranges::mul_k2 (
                    &stereo_out.second
                       .buf_[time_nfo.local_offset_ + faded_out_frames],
                    mute_amp, time_nfo.nframes_ - faded_out_frames);
                }
            }

          /* if master or monitor or sample processor, hard limit the output */
          if (
            (type_ == Type::AudioChannel && track && track->is_master ())
            || type_ == Type::Monitor || type_ == Type::SampleProcessor)
            {
              utils::float_ranges::clip (
                &stereo_out.first.buf_[time_nfo.local_offset_], -2.f, 2.f,
                time_nfo.nframes_);
              utils::float_ranges::clip (
                &stereo_out.second.buf_[time_nfo.local_offset_], -2.f, 2.f,
                time_nfo.nframes_);
            }
        } /* fi not prefader */
    } /* fi monitor/audio fader */
  else if (type_ == Type::MidiChannel)
    {
      if (!effectively_muted)
        {
          auto &midi_in = get_midi_in_port ();
          auto &midi_out = get_midi_out_port ();
          midi_out.midi_events_.active_events_.append (
            midi_in.midi_events_.active_events_, time_nfo.local_offset_,
            time_nfo.nframes_);

          /* if not prefader, also apply volume changes */
          if (!passthrough_)
            {
              const auto amp_val = get_amp_port ().get_control_value (false);
              for (auto &ev : midi_out.midi_events_.active_events_)
                {
                  if (
                    midi_mode_ == MidiFaderMode::MIDI_FADER_MODE_VEL_MULTIPLIER
                    && midi_is_note_on (ev.raw_buffer_.data ()))
                    {
                      const midi_byte_t prev_vel =
                        midi_get_velocity (ev.raw_buffer_.data ());
                      const auto new_vel =
                        (midi_byte_t) ((float) prev_vel * amp_val);
                      ev.set_velocity (
                        std::min (new_vel, static_cast<midi_byte_t> (127)));
                    }
                }

              if (
                midi_mode_ == MidiFaderMode::MIDI_FADER_MODE_CC_VOLUME
                && !utils::math::floats_equal (last_cc_volume_, amp_val))
                {
                  /* TODO add volume event on each channel */
                }
            }
        }
    }

  was_effectively_muted_ = effectively_muted;

  if (ZRYTHM_TESTING)
    {
#if 0
      z_debug ("{}: done", __func__);
#endif
    }
}

void
Fader::init_after_cloning (const Fader &other, ObjectCloneType clone_type)
{
  port_registry_ = other.port_registry_;

  type_ = other.type_;
  volume_ = other.volume_;
  phase_ = other.phase_;
  midi_mode_ = other.midi_mode_;
  passthrough_ = other.passthrough_;

  if (clone_type == ObjectCloneType::Snapshot)
    {
      amp_id_ = other.amp_id_;
      balance_id_ = other.balance_id_;
      mute_id_ = other.mute_id_;
      solo_id_ = other.solo_id_;
      listen_id_ = other.listen_id_;
      mono_compat_enabled_id_ = other.mono_compat_enabled_id_;
      swap_phase_id_ = other.swap_phase_id_;
      midi_in_id_ = other.midi_in_id_;
      midi_out_id_ = other.midi_out_id_;
      stereo_in_left_id_ = other.stereo_in_left_id_;
      stereo_in_right_id_ = other.stereo_in_right_id_;
      stereo_out_left_id_ = other.stereo_out_left_id_;
      stereo_out_right_id_ = other.stereo_out_right_id_;
    }
  else if (clone_type == ObjectCloneType::NewIdentity)
    {
      auto deep_clone_port = [&] (auto &own_port_id, const auto &other_port_id) {
        if (!other_port_id.has_value ())
          return;

        auto other_amp_port =
          other.port_registry_->find_by_id_or_throw (other_port_id.value ());
        std::visit (
          [&] (auto &&other_port) {
            auto new_amp_port = other_port->clone_and_register (*port_registry_);
            own_port_id = new_amp_port->get_uuid ();
          },
          other_amp_port);
      };
      deep_clone_port (amp_id_, other.amp_id_);
      deep_clone_port (balance_id_, other.balance_id_);
      deep_clone_port (mute_id_, other.mute_id_);
      deep_clone_port (solo_id_, other.solo_id_);
      deep_clone_port (listen_id_, other.listen_id_);
      deep_clone_port (mono_compat_enabled_id_, other.mono_compat_enabled_id_);
      deep_clone_port (swap_phase_id_, other.swap_phase_id_);
      deep_clone_port (midi_in_id_, other.midi_in_id_);
      deep_clone_port (midi_out_id_, other.midi_out_id_);
      deep_clone_port (stereo_in_left_id_, other.stereo_in_left_id_);
      deep_clone_port (stereo_in_right_id_, other.stereo_in_right_id_);
      deep_clone_port (stereo_out_left_id_, other.stereo_out_left_id_);
      deep_clone_port (stereo_out_right_id_, other.stereo_out_right_id_);

      /* set owner */
      std::vector<Port *> ports;
      append_ports (ports);
      for (auto &port : ports)
        {
          if (port->id_->owner_type_ == dsp::PortIdentifier::OwnerType::Fader)
            {
              /* note: don't call set_owner() because get_track () won't work
               * here. also, all other port fields are already copied*/
              // FIXME: set_owner() should eventually cover this case
              port->owner_ = this;
            }
        }
    }
}

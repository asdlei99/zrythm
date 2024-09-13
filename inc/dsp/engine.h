// SPDX-FileCopyrightText: © 2018-2024 Alexandros Theodotou <alex@zrythm.org>
// SPDX-FileCopyrightText: © 2020 Ryan Gonzalez <rymg19 at gmail dot com>
// SPDX-License-Identifier: LicenseRef-ZrythmLicense

/**
 * @file
 *
 * The audio engine.
 */

#ifndef __AUDIO_ENGINE_H__
#define __AUDIO_ENGINE_H__

#include "zrythm-config.h"

#include "dsp/audio_port.h"
#include "dsp/control_room.h"
#include "dsp/ext_port.h"
#include "dsp/hardware_processor.h"
#include "dsp/midi_port.h"
#include "dsp/pan.h"
#include "dsp/pool.h"
#include "dsp/sample_processor.h"
#include "dsp/transport.h"
#include "utils/backtrace.h"
#include "utils/concurrency.h"
#include "utils/object_pool.h"
#include "utils/types.h"

#ifdef HAVE_JACK
#  include "weak_libjack.h"
#endif

#ifdef HAVE_PULSEAUDIO
#  include <pulse/pulseaudio.h>
#endif

#ifdef HAVE_PORT_AUDIO
#  include <portaudio.h>
#endif

#ifdef HAVE_RTAUDIO
#  include <rtaudio_c.h>
#endif

#include <glibmm.h>

class Channel;
class Plugin;
class Tracklist;
class ExtPort;
class MidiMappings;
class Router;
class Metronome;
class Project;
class HardwareProcessor;

/**
 * @addtogroup dsp Audio
 *
 * @{
 */

// should be set by backend
constexpr int BLOCK_LENGTH = 4096;

// should be set by backend
constexpr int MIDI_BUF_SIZE = 1024;

#define AUDIO_ENGINE (PROJECT->audio_engine_)

enum class BounceStep
{
  BeforeInserts,
  PreFader,
  PostFader,
};

DEFINE_ENUM_FORMATTER (
  BounceStep,
  BounceStep,
  N_ ("Before inserts"),
  N_ ("Pre-fader"),
  N_ ("Post fader"));

#define DENORMAL_PREVENTION_VAL(engine_) ((engine_)->denormal_prevention_val_)

constexpr int ENGINE_MAX_EVENTS = 128;

/**
 * Push events.
 */
#define ENGINE_EVENTS_PUSH(et, _arg, _uint_arg, _float_arg) \
  { \
    auto _ev = AUDIO_ENGINE->ev_pool_.acquire (); \
    _ev->file_ = __FILE__; \
    _ev->func_ = __func__; \
    _ev->lineno_ = __LINE__; \
    _ev->type_ = et; \
    _ev->arg_ = (void *) _arg; \
    _ev->uint_arg_ = _uint_arg; \
    _ev->float_arg_ = _float_arg; \
    if (ZRYTHM_APP_IS_GTK_THREAD) \
      { \
        _ev->backtrace_ = Backtrace ().get_backtrace ("", 40, false); \
        z_debug ( \
          "pushing engine event " #et " ({}:{}) uint: {} | float: {:f}", \
          __func__, __LINE__, _uint_arg, _float_arg); \
      } \
    AUDIO_ENGINE->ev_queue_.push_back (_ev); \
  }

enum class AudioBackend
{
  AUDIO_BACKEND_DUMMY,
  AUDIO_BACKEND_DUMMY_LIBSOUNDIO,
  AUDIO_BACKEND_ALSA,
  AUDIO_BACKEND_ALSA_LIBSOUNDIO,
  AUDIO_BACKEND_ALSA_RTAUDIO,
  AUDIO_BACKEND_JACK,
  AUDIO_BACKEND_JACK_LIBSOUNDIO,
  AUDIO_BACKEND_JACK_RTAUDIO,
  AUDIO_BACKEND_PULSEAUDIO,
  AUDIO_BACKEND_PULSEAUDIO_LIBSOUNDIO,
  AUDIO_BACKEND_PULSEAUDIO_RTAUDIO,
  AUDIO_BACKEND_COREAUDIO_LIBSOUNDIO,
  AUDIO_BACKEND_COREAUDIO_RTAUDIO,
  AUDIO_BACKEND_SDL,
  AUDIO_BACKEND_WASAPI_LIBSOUNDIO,
  AUDIO_BACKEND_WASAPI_RTAUDIO,
  AUDIO_BACKEND_ASIO_RTAUDIO,
};

static inline bool
audio_backend_is_rtaudio (AudioBackend backend)
{
  return backend == AudioBackend::AUDIO_BACKEND_ALSA_RTAUDIO
         || backend == AudioBackend::AUDIO_BACKEND_JACK_RTAUDIO
         || backend == AudioBackend::AUDIO_BACKEND_PULSEAUDIO_RTAUDIO
         || backend == AudioBackend::AUDIO_BACKEND_COREAUDIO_RTAUDIO
         || backend == AudioBackend::AUDIO_BACKEND_WASAPI_RTAUDIO
         || backend == AudioBackend::AUDIO_BACKEND_ASIO_RTAUDIO;
}

/**
 * Mode used when bouncing, either during exporting
 * or when bouncing tracks or regions to audio.
 */
enum class BounceMode
{
  /** Don't bounce. */
  BOUNCE_OFF,

  /** Bounce. */
  BOUNCE_ON,

  /**
   * Bounce if parent is bouncing.
   *
   * To be used on regions to bounce if their track
   * is bouncing.
   */
  BOUNCE_INHERIT,
};

enum class MidiBackend
{
  MIDI_BACKEND_DUMMY,
  MIDI_BACKEND_ALSA,
  MIDI_BACKEND_ALSA_RTMIDI,
  MIDI_BACKEND_JACK,
  MIDI_BACKEND_JACK_RTMIDI,
  MIDI_BACKEND_WINDOWS_MME,
  MIDI_BACKEND_WINDOWS_MME_RTMIDI,
  MIDI_BACKEND_COREMIDI_RTMIDI,
  MIDI_BACKEND_WINDOWS_UWP_RTMIDI,
};

static inline bool
midi_backend_is_rtmidi (MidiBackend backend)
{
  return backend == MidiBackend::MIDI_BACKEND_ALSA_RTMIDI
         || backend == MidiBackend::MIDI_BACKEND_JACK_RTMIDI
         || backend == MidiBackend::MIDI_BACKEND_WINDOWS_MME_RTMIDI
         || backend == MidiBackend::MIDI_BACKEND_COREMIDI_RTMIDI
         || backend == MidiBackend::MIDI_BACKEND_WINDOWS_UWP_RTMIDI;
}

/**
 * The audio engine.
 */
class AudioEngine final
    : public ICloneable<AudioEngine>,
      public ISerializable<AudioEngine>
{
public:
  enum class JackTransportType
  {
    TimebaseMaster,
    TransportClient,
    NoJackTransport,
  };

  /**
   * Samplerates to be used in comboboxes.
   */
  enum class SampleRate
  {
    SR_22050,
    SR_32000,
    SR_44100,
    SR_48000,
    SR_88200,
    SR_96000,
    SR_192000,
  };

  /**
   * Audio engine event type.
   */
  enum class AudioEngineEventType
  {
    AUDIO_ENGINE_EVENT_BUFFER_SIZE_CHANGE,
    AUDIO_ENGINE_EVENT_SAMPLE_RATE_CHANGE,
  };

  /**
   * Audio engine event.
   */
  class Event
  {
  public:
    Event () : file_ (nullptr), func_ (nullptr) { }

  public:
    AudioEngineEventType type_ =
      AudioEngineEventType::AUDIO_ENGINE_EVENT_BUFFER_SIZE_CHANGE;
    void *       arg_ = nullptr;
    uint32_t     uint_arg_ = 0;
    float        float_arg_ = 0.0f;
    const char * file_ = nullptr;
    const char * func_ = nullptr;
    int          lineno_ = 0;
    std::string  backtrace_;
  };

  /**
   * Buffer sizes to be used in combo boxes.
   */
  enum class BufferSize
  {
    _16,
    _32,
    _64,
    _128,
    _256,
    _512,
    _1024,
    _2048,
    _4096,
  };

  // TODO: check if pausing/resuming can be done with RAII
  struct State
  {
    /** Engine running. */
    bool running_;
    /** Playback. */
    bool playing_;
    /** Transport loop. */
    bool looping_;
  };

  struct PositionInfo
  {
    /** Transport is rolling. */
    bool is_rolling_ = false;

    /** BPM. */
    float bpm_ = 120.f;

    /** Exact playhead position (in ticks). */
    double playhead_ticks_ = 0.0;

    /* - below are used as cache to avoid re-calculations - */

    /** Current bar. */
    int32_t bar_ = 1;

    /** Current beat (within bar). */
    int32_t beat_ = 1;

    /** Current sixteenth (within beat). */
    int32_t sixteenth_ = 1;

    /** Current sixteenth (within bar). */
    int32_t sixteenth_within_bar_ = 1;

    /** Current sixteenth (within song, ie, total
     * sixteenths). */
    int32_t sixteenth_within_song_ = 1;

    /** Current tick-within-beat. */
    double tick_within_beat_ = 0.0;

    /** Current tick (within bar). */
    double tick_within_bar_ = 0.0;

    /** Total 1/96th notes completed up to current pos. */
    int32_t ninetysixth_notes_ = 0;
  };

public:
  /**
   * Create a new audio engine.
   *
   * This only initializes the engine and does not connect to any backend.
   */
  AudioEngine (Project * project = nullptr);

  /**
   * Closes any connections and free's data.
   */
  ~AudioEngine ();

  bool has_handled_buffer_size_change () const
  {
    return audio_backend_ != AudioBackend::AUDIO_BACKEND_JACK
           || (audio_backend_ == AudioBackend::AUDIO_BACKEND_JACK && handled_jack_buffer_size_change_.load());
  }

  bool is_in_active_project () const;

  /**
   * @param force_pause Whether to force transport
   *   pause, otherwise for engine to process and
   *   handle the pause request.
   */
  void wait_for_pause (State &state, bool force_pause, bool with_fadeout);

  void realloc_port_buffers (nframes_t buf_size);

  /**
   * @brief
   *
   * @param project
   * @throw ZrythmError if failed to initialize.
   */
  ATTR_COLD void init_loaded (Project * project);

  void resume (State &state);

  /**
   * Waits for n processing cycles to finish.
   *
   * Used during tests.
   */
  void wait_n_cycles (int n);

  void append_ports (std::vector<Port *> &ports);

  /**
   * Sets up the audio engine before the project is initialized/loaded.
   */
  void pre_setup ();

  /**
   * Sets up the audio engine after the project is initialized/loaded.
   */
  void setup ();

  /**
   * Activates the audio engine to start processing and receiving events.
   *
   * @param activate Activate or deactivate.
   */
  ATTR_COLD void activate (bool activate);

  /**
   * Updates frames per tick based on the time sig, the BPM, and the sample rate
   *
   * @param thread_check Whether to throw a warning if not called from GTK
   * thread.
   * @param update_from_ticks Whether to update the positions based on ticks
   * (true) or frames (false).
   * @param bpm_change Whether this is a BPM change.
   */
  void update_frames_per_tick (
    const int           beats_per_bar,
    const bpm_t         bpm,
    const sample_rate_t sample_rate,
    bool                thread_check,
    bool                update_from_ticks,
    bool                bpm_change);

  /**
   * Timeout function to be called periodically by Glib.
   */
  bool process_events ();

  /**
   * To be called by each implementation to prepare the structures before
   * processing.
   *
   * Clears buffers, marks all as unprocessed, etc.
   *
   * @param sem SemamphoreRAII to check if acquired. If not acquired before
   * calling this function, it will only clear output buffers and return true.
   * @return Whether the cycle should be skipped.
   */
  ATTR_HOT bool process_prepare (
    nframes_t                                  nframes,
    SemaphoreRAII<std::counting_semaphore<>> * sem = nullptr);

  /**
   * Processes current cycle.
   *
   * To be called by each implementation in its callback.
   */
  ATTR_HOT int process (const nframes_t total_frames_to_process);

  /**
   * To be called after processing for common logic.
   *
   * @param roll_nframes Frames to roll (add to the playhead - if transport
   * rolling).
   * @param nframes Total frames for this processing cycle.
   */
  ATTR_HOT void
  post_process (const nframes_t roll_nframes, const nframes_t nframes);

  /**
   * Called to fill in the external buffers at the end of the processing cycle.
   */
  void fill_out_bufs (const nframes_t nframes);

  /**
   * Returns the int value corresponding to the given AudioEngineBufferSize.
   */
  static int buffer_size_enum_to_int (BufferSize buffer_size);

  /**
   * Returns the int value corresponding to the given AudioEngineSamplerate.
   */
  static int samplerate_enum_to_int (SampleRate samplerate);

  /**
   * Request the backend to set the buffer size.
   *
   * The backend is expected to call the buffer size change callbacks.
   *
   * @see jack_set_buffer_size().
   */
  void set_buffer_size (uint32_t buf_size);

  /**
   * Returns whether the port is an engine port or control room port.
   */
  bool is_port_own (const Port &port) const;

  /**
   * Reset the bounce mode on the engine, all tracks and regions to OFF.
   */
  void reset_bounce_mode ();

  /**
   * Detects the best backends on the system and sets them to GSettings.
   *
   * @param reset_to_dummy Whether to reset the backends to dummy before
   * attempting to set defaults.
   */
  static void set_default_backends (bool reset_to_dummy);

  void init_after_cloning (const AudioEngine &other) override;

  DECLARE_DEFINE_FIELDS_METHOD ();

private:
  /**
   * Cleans duplicate events and copies the events to the given vector.
   */
  int clean_duplicate_events_and_copy (std::array<Event *, 100> &ret);

  ATTR_COLD void init_common ();

  void
  update_position_info (PositionInfo &pos_nfo, const nframes_t frames_to_add);

  /**
   * Clears the underlying backend's output buffers.
   *
   * Used when returning early.
   */
  void clear_output_buffers (nframes_t nframes);

  void receive_midi_events (uint32_t nframes);

  /**
   * @brief Stops events from getting fired.
   *
   */
  void stop_events ();

public:
  /**
   * Cycle count to know which cycle we are in.
   *
   * Useful for debugging.
   */
  std::atomic_uint64_t cycle_ = 0;

#ifdef HAVE_JACK
  /** JACK client. */
  jack_client_t * client_ = nullptr;
#else
  void * client_ = nullptr;
#endif

  /**
   * Whether pending jack buffer change was handled (buffers reallocated).
   *
   * To be set to zero when a change starts and 1 when the change is fully
   * processed.
   */
  std::atomic_bool handled_jack_buffer_size_change_ = false;

  /**
   * Whether transport master/client or no connection with jack transport.
   */
  JackTransportType transport_type_ = (JackTransportType) 0;

  /** Current audio backend. */
  AudioBackend audio_backend_ = (AudioBackend) 0;

  /** Current MIDI backend. */
  MidiBackend midi_backend_ = (MidiBackend) 0;

  /** Audio buffer size (block length), per channel. */
  nframes_t block_length_ = 0;

  /** Size of MIDI port buffers in bytes. */
  size_t midi_buf_size_ = 0;

  /** Sample rate. */
  sample_rate_t sample_rate_ = 0;

  /** Number of frames/samples per tick. */
  double frames_per_tick_ = 0.0;

  /**
   * Reciprocal of @ref frames_per_tick_.
   */
  double ticks_per_frame_ = 0.0;

  /** True iff buffer size callback fired. */
  int buf_size_set_ = 0;

  /** The processing graph router. */
  std::unique_ptr<Router> router_;

  /** Input device processor. */
  std::unique_ptr<HardwareProcessor> hw_in_processor_;

  /** Output device processor. */
  std::unique_ptr<HardwareProcessor> hw_out_processor_;

  /**
   * MIDI Clock input TODO.
   *
   * This port is exposed to the backend.
   */
  std::unique_ptr<MidiPort> midi_clock_in_;

  /**
   * MIDI Clock output.
   *
   * This port is exposed to the backend.
   */
  std::unique_ptr<MidiPort> midi_clock_out_;

  /** The ControlRoom. */
  std::unique_ptr<ControlRoom> control_room_;

  /** Audio file pool. */
  std::unique_ptr<AudioPool> pool_;

  /**
   * Used during tests to pass input data for recording.
   *
   * Will be ignored if NULL.
   */
  std::unique_ptr<StereoPorts> dummy_input_;

  /**
   * Monitor - these should be the last ports in the signal
   * chain.
   *
   * The L/R ports are exposed to the backend.
   */
  std::unique_ptr<StereoPorts> monitor_out_;

  /**
   * Flag to tell the UI that this channel had MIDI activity.
   *
   * When processing this and setting it to 0, the UI should
   * create a separate event using EVENTS_PUSH.
   */
  std::atomic_bool trigger_midi_activity_{ false };

  /**
   * Manual note press events from the piano roll.
   *
   * The events from here should be read by the corresponding track
   * processor's MIDI in port (TrackProcessor.midi_in). To avoid having to
   * recalculate the graph to reattach this port to the correct track
   * processor, only connect this port to the initial processor in the
   * routing graph and fetch the events manually when processing the
   * corresponding track processor.
   */
  std::unique_ptr<MidiPort> midi_editor_manual_press_;

  /**
   * Port used for receiving MIDI in messages for binding CC and other
   * non-recording purposes.
   *
   * This port is exposed to the backend.
   */
  std::unique_ptr<MidiPort> midi_in_;

  /**
   * Number of frames/samples in the current cycle, per channel.
   *
   * @note This is used by the engine internally.
   */
  nframes_t nframes_ = 0;

  /**
   * Semaphore for blocking DSP while a plugin and its ports are deleted.
   */
  std::counting_semaphore<> port_operation_lock_{ 1 };

  /** Ok to process or not. */
  std::atomic_bool run_{ false };

  /** To be set to true when preparing to export. */
  bool preparing_to_export_ = false;

  /** Whether currently exporting. */
  std::atomic_bool exporting_{ false };

  /** Send note off MIDI everywhere. */
  std::atomic_bool panic_{ false };

#ifdef HAVE_PORT_AUDIO
  PaStream * port_audio_stream_ = nullptr;
#else
  void * port_audio_stream_ = nullptr;
#endif

  /**
   * Port Audio output buffer.
   *
   * Unlike JACK, the audio goes directly here.
   * FIXME: this is not really needed, just do the calculations in
   * port_audio_stream_cb.
   */
  float * port_audio_out_buf_ = nullptr;

#ifdef HAVE_RTAUDIO
  rtaudio_t rtaudio_ = nullptr;
#else
  void * rtaudio_ = nullptr;
#endif

#ifdef HAVE_PULSEAUDIO
  pa_threaded_mainloop * pulse_mainloop_ = nullptr;
  pa_context *           pulse_context_ = nullptr;
  pa_stream *            pulse_stream_ = nullptr;
#else
  void * pulse_mainloop_ = nullptr;
  void * pulse_context_ = nullptr;
  void * pulse_stream_ = nullptr;
#endif
  std::atomic_bool pulse_notified_underflow_{ false };

  /**
   * @brief Dummy audio DSP processing thread.
   *
   * Used during tests or when no audio backend is available.
   *
   * Use signalThreadShouldExit() to stop the thread.
   *
   * @note The thread will join automatically when the engine is destroyed.
   */
  std::unique_ptr<juce::Thread> dummy_audio_thread_;

  /**
   * Timeline metadata like BPM, time signature, etc.
   */
  std::unique_ptr<Transport> transport_;

  /* note: these 2 are ignored at the moment */
  /** Pan law. */
  PanLaw pan_law_ = {};
  /** Pan algorithm */
  PanAlgorithm pan_algo_ = {};

  /** Time taken to process in the last cycle */
  // SteadyDuration last_time_taken_;

  /**
   * @brief Max time taken in the last few processing cycles.
   *
   * This is used by the DSP usage meter, which also clears the value to 0
   * upon reading it.
   *
   * @note Could use a lock since it is written to by 2 different threads,
   * but there are no consequences if it has bad data.
   */
  RtDuration max_time_taken_;

  /** Timestamp at the start of the current cycle. */
  RtTimePoint timestamp_start_{};

  /** Expected timestamp at the end of the current cycle. */
  // SteadyTimePoint timestamp_end_{};

  /** Timestamp at start of previous cycle. */
  RtTimePoint last_timestamp_start_{};

  /** Timestamp at end of previous cycle. */
  // SteadyTimePoint last_timestamp_end_{};

  /** When first set, it is equal to the max
   * playback latency of all initial trigger
   * nodes. */
  nframes_t remaining_latency_preroll_ = 0;

  std::unique_ptr<SampleProcessor> sample_processor_;

  /** To be set to 1 when the CC from the Midi in port should be captured. */
  std::atomic_bool capture_cc_{ false };

  /** Last MIDI CC captured. */
  std::array<midi_byte_t, 3> last_cc_captured_;

  /**
   * Last time an XRUN notification was shown.
   *
   * This is to prevent multiple XRUN notifications being shown so quickly
   * that Zrythm becomes unusable.
   */
  SteadyTimePoint last_xrun_notification_;

  /**
   * Whether the denormal prevention value (1e-12 ~ 1e-20) is positive.
   *
   * This should be swapped often to avoid DC offset prevention algorithms
   * removing it.
   *
   * See https://www.earlevel.com/main/2019/04/19/floating-point-denormals/
   * for details.
   */
  bool  denormal_prevention_val_positive_ = true;
  float denormal_prevention_val_ = 1e-12;

  /**
   * If this is on, only tracks/regions marked as "for bounce" will be
   * allowed to make sound.
   *
   * Automation and everything else will work as normal.
   */
  BounceMode bounce_mode_ = BounceMode::BOUNCE_OFF;

  /** Bounce step cache. */
  BounceStep bounce_step_ = {};

  /** Whether currently bouncing with parents (cache). */
  bool bounce_with_parents_ = false;

  /** The metronome. */
  std::unique_ptr<Metronome> metronome_;

  /* --- events --- */

  /**
   * Event queue.
   *
   * Events such as buffer size change request and sample size change
   * request should be queued here.
   *
   * The engine will skip processing while the queue still has events or is
   * currently processing events.
   */
  MPMCQueue<Event *> ev_queue_{ ENGINE_MAX_EVENTS };

  /**
   * Object pool of event structs to avoid allocation.
   */
  ObjectPool<Event> ev_pool_{ ENGINE_MAX_EVENTS };

  /** ID of the event processing source func. */
  sigc::scoped_connection process_source_id_;

  /** Whether currently processing events. */
  bool processing_events_ = false;

  /** Time last event processing started. */
  SteadyTimePoint last_events_process_started_;

  /** Time last event processing completed. */
  SteadyTimePoint last_events_processed_;

  /* --- end events --- */

  /** Whether the cycle is currently running. */
  std::atomic_bool cycle_running_{ false };

  /** Whether the engine is already pre-set up. */
  bool pre_setup_ = false;

  /** Whether the engine is already set up. */
  bool setup_ = false;

  /** Whether the engine is currently activated. */
  bool activated_ = false;

  /** Whether the engine is currently undergoing destruction. */
  bool destroying_ = false;

  /** Pointer to owner project, if any. */
  Project * project_ = nullptr;

  /**
   * True while updating frames per tick.
   *
   * See engine_update_frames_per_tick().
   */
  bool updating_frames_per_tick_ = false;

  /**
   * Position info at the end of the previous cycle before moving the
   * transport.
   */
  PositionInfo pos_nfo_before_ = {};

  /**
   * Position info at the start of the current cycle.
   */
  PositionInfo pos_nfo_current_ = {};

  /**
   * Expected position info at the end of the current cycle.
   */
  PositionInfo pos_nfo_at_end_ = {};
};

DEFINE_ENUM_FORMATTER (
  AudioBackend,
  AudioBackend,
  /* TRANSLATORS: Dummy backend */
  N_ ("Dummy"),
  N_ ("Dummy (libsoundio)"),
  "ALSA (not working)",
  "ALSA (libsoundio)",
  "ALSA (rtaudio)",
  "JACK",
  "JACK (libsoundio)",
  "JACK (rtaudio)",
  "PulseAudio",
  "PulseAudio (libsoundio)",
  "PulseAudio (rtaudio)",
  "CoreAudio (libsoundio)",
  "CoreAudio (rtaudio)",
  "SDL",
  "WASAPI (libsoundio)",
  "WASAPI (rtaudio)",
  "ASIO (rtaudio)")

DEFINE_ENUM_FORMATTER (
  MidiBackend,
  MidiBackend,
  /* TRANSLATORS: Dummy backend */
  N_ ("Dummy"),
  N_ ("ALSA Sequencer (not working)"),
  N_ ("ALSA Sequencer (rtmidi)"),
  "JACK MIDI",
  "JACK MIDI (rtmidi)",
  "Windows MME",
  "Windows MME (rtmidi)",
  "CoreMIDI (rtmidi)",
  "Windows UWP (rtmidi)")

DEFINE_ENUM_FORMATTER (
  AudioEngine::SampleRate,
  AudioEngine_SampleRate,
  N_ ("22,050"),
  N_ ("32,000"),
  N_ ("44,100"),
  N_ ("48,000"),
  N_ ("88,200"),
  N_ ("96,000"),
  N_ ("192,000"))

DEFINE_ENUM_FORMATTER (
  AudioEngine::BufferSize,
  AudioEngine_BufferSize,
  "16",
  "32",
  "64",
  "128",
  "256",
  "512",
  N_ ("1,024"),
  N_ ("2,048"),
  N_ ("4,096"))

/**
 * @}
 */

#endif

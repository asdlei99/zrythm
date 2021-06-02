/*
 * Copyright (C) 2019-2021 Alexandros Theodotou <alex at zrythm dot org>
 *
 * This file is part of Zrythm
 *
 * Zrythm is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Zrythm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Zrythm.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>

#include "audio/clip.h"
#include "audio/encoder.h"
#include "audio/engine.h"
#include "audio/tempo_track.h"
#include "project.h"
#include "utils/audio.h"
#include "utils/dsp.h"
#include "utils/file.h"
#include "utils/flags.h"
#include "utils/hash.h"
#include "utils/io.h"
#include "utils/math.h"
#include "utils/objects.h"
#include "utils/string.h"
#include "zrythm_app.h"

#include <gtk/gtk.h>

static AudioClip *
_create ()
{
  AudioClip * self = object_new (AudioClip);
  self->schema_version =
    AUDIO_CLIP_SCHEMA_VERSION;

  return self;
}

/**
 * Updates the channel caches.
 *
 * See @ref AudioClip.ch_frames.
 *
 * @param start_from Frames to start from (per
 *   channel. The previous frames will be kept.
 */
void
audio_clip_update_channel_caches (
  AudioClip * self,
  size_t      start_from)
{
  g_return_if_fail (
    self->channels > 0 && self->num_frames > 0);

  /* copy the frames to the channel caches */
  for (unsigned int i = 0; i < self->channels; i++)
    {
      self->ch_frames[i] =
        g_realloc (
          self->ch_frames[i],
          sizeof (float) *
            (size_t) self->num_frames);
      for (size_t j = start_from;
           j < (size_t) self->num_frames; j++)
        {
          self->ch_frames[i][j] =
            self->frames[j * self->channels + i];
        }
    }
}

static void
audio_clip_init_from_file (
  AudioClip * self,
  const char * full_path)
{
  g_return_if_fail (self);

  self->samplerate =
    (int) AUDIO_ENGINE->sample_rate;
  g_return_if_fail (self->samplerate > 0);

  AudioEncoder * enc =
    audio_encoder_new_from_file (full_path);
  audio_encoder_decode (
    enc, self->samplerate, F_SHOW_PROGRESS);

  size_t arr_size =
    (size_t) enc->num_out_frames *
    (size_t) enc->nfo.channels;
  self->frames =
    g_realloc (
      self->frames, arr_size * sizeof (float));
  self->num_frames = enc->num_out_frames;
  dsp_copy (
    self->frames, enc->out_frames, arr_size);
  g_free_and_null (self->name);
  self->name = g_path_get_basename (full_path);
  self->channels = enc->nfo.channels;
  self->bpm =
    tempo_track_get_current_bpm (P_TEMPO_TRACK);
  switch (enc->nfo.bit_depth)
    {
    case 16:
      self->bit_depth = BIT_DEPTH_16;
      self->use_flac = true;
      break;
    case 24:
      self->bit_depth = BIT_DEPTH_24;
      self->use_flac = true;
      break;
    case 32:
      self->bit_depth = BIT_DEPTH_32;
      self->use_flac = false;
      break;
    default:
      g_debug (
        "unknown bit depth: %d", enc->nfo.bit_depth);
      self->bit_depth = BIT_DEPTH_32;
      self->use_flac = false;
    }
  /*g_message (*/
    /*"\n\n num frames %ld \n\n", self->num_frames);*/
  audio_clip_update_channel_caches (self, 0);

  audio_encoder_free (enc);
}

/**
 * Inits after loading a Project.
 */
void
audio_clip_init_loaded (
  AudioClip * self)
{
  g_debug (
    "%s: %p", __func__, self);

  char * filepath =
    audio_clip_get_path_in_pool_from_name (
      self->name, self->use_flac, F_NOT_BACKUP);

  bpm_t bpm = self->bpm;
  audio_clip_init_from_file (self, filepath);
  self->bpm = bpm;

  g_free (filepath);
}

/**
 * Creates an audio clip from a file.
 *
 * The name used is the basename of the file.
 */
AudioClip *
audio_clip_new_from_file (
  const char * full_path)
{
  AudioClip * self = _create ();

  audio_clip_init_from_file (self, full_path);

  self->pool_id = -1;
  self->bpm =
    tempo_track_get_current_bpm (P_TEMPO_TRACK);

  return self;
}

/**
 * Creates an audio clip by copying the given float
 * array.
 *
 * @param name A name for this clip.
 */
AudioClip *
audio_clip_new_from_float_array (
  const float *    arr,
  const long       nframes,
  const channels_t channels,
  BitDepth         bit_depth,
  const char *     name)
{
  AudioClip * self = _create ();

  self->frames =
    object_new_n (
      (size_t) (nframes * (long) channels),
      sample_t);
  self->num_frames = nframes;
  self->channels = channels;
  self->samplerate = (int) AUDIO_ENGINE->sample_rate;
  g_return_val_if_fail (self->samplerate > 0, NULL);
  self->name = g_strdup (name);
  self->bit_depth = bit_depth;
  self->use_flac = bit_depth < BIT_DEPTH_32;
  self->pool_id = -1;
  dsp_copy (
    self->frames, arr,
    (size_t) nframes * (size_t) channels);
  self->bpm =
    tempo_track_get_current_bpm (P_TEMPO_TRACK);
  audio_clip_update_channel_caches (self, 0);

  return self;
}

/**
 * Create an audio clip while recording.
 *
 * The frames will keep getting reallocated until
 * the recording is finished.
 *
 * @param nframes Number of frames to allocate. This
 *   should be the current cycle's frames when
 *   called during recording.
 */
AudioClip *
audio_clip_new_recording (
  const channels_t channels,
  const long       nframes,
  const char *     name)
{
  AudioClip * self = _create ();

  self->channels = channels;
  self->frames =
    object_new_n (
      (size_t) (nframes * (long) self->channels),
      sample_t);
  self->num_frames = nframes;
  self->name = g_strdup (name);
  self->pool_id = -1;
  self->bpm =
    tempo_track_get_current_bpm (P_TEMPO_TRACK);
  self->samplerate = (int) AUDIO_ENGINE->sample_rate;
  self->bit_depth = BIT_DEPTH_32;
  self->use_flac = false;
  g_return_val_if_fail (self->samplerate > 0, NULL);
  dsp_fill (
    self->frames, DENORMAL_PREVENTION_VAL,
    (size_t) nframes * (size_t) channels);
  audio_clip_update_channel_caches (self, 0);

  return self;
}

/**
 * Gets the path of a clip matching \ref name from
 * the pool.
 *
 * @param use_flac Whether to look for a FLAC file
 *   instead of a wav file.
 * @param is_backup Whether writing to a backup
 *   project.
 */
char *
audio_clip_get_path_in_pool_from_name (
  const char * name,
  bool         use_flac,
  bool         is_backup)
{
  char * prj_pool_dir =
    project_get_path (
      PROJECT, PROJECT_PATH_POOL, is_backup);
  g_return_val_if_fail (
    file_exists (prj_pool_dir), NULL);
  char * without_ext =
    io_file_strip_ext (name);
  char * basename =
    g_strdup_printf (
      "%s.%s", without_ext,
      use_flac ? "FLAC" : "wav");
  char * new_path =
    g_build_filename (
      prj_pool_dir, basename, NULL);
  g_free (without_ext);
  g_free (basename);
  g_free (prj_pool_dir);

  return new_path;
}

/**
 * Gets the path of the given clip from the pool.
 *
 * @param is_backup Whether writing to a backup
 *   project.
 */
char *
audio_clip_get_path_in_pool (
  AudioClip * self,
  bool        is_backup)
{
  return
    audio_clip_get_path_in_pool_from_name (
      self->name, self->use_flac, is_backup);
}

/**
 * Writes the clip to the pool as a wav file.
 *
 * @param parts If true, only write new data. @see
 *   AudioClip.frames_written.
 * @param is_backup Whether writing to a backup
 *   project.
 */
void
audio_clip_write_to_pool (
  AudioClip * self,
  bool        parts,
  bool        is_backup)
{
  AudioClip * pool_clip =
    audio_pool_get_clip (
      AUDIO_POOL, self->pool_id);
  g_return_if_fail (pool_clip);
  g_return_if_fail (pool_clip == self);

  /* generate a copy of the given filename in the
   * project dir */
  char * path_in_main_project =
    audio_clip_get_path_in_pool (
      self, F_NOT_BACKUP);
  char * new_path =
    audio_clip_get_path_in_pool (self, is_backup);
  g_return_if_fail (path_in_main_project);
  g_return_if_fail (new_path);

  /* skip if file with same hash already exists */
  if (file_exists (new_path))
    {
      char * existing_file_hash =
        hash_get_from_file (
          new_path, HASH_ALGORITHM_XXH3_64);
      bool same_hash =
        self->file_hash &&
        string_is_equal (
          self->file_hash, existing_file_hash);
      g_free (existing_file_hash);

      if (same_hash)
        {
          g_debug (
            "skipping writing to existing clip %s "
            "in pool",
            new_path);
          goto write_to_pool_free_and_return;
        }
    }

  /* if writing to backup and same file exists in
   * main project dir, create a symlink (fallback
   * to copying) */
  if (self->file_hash && is_backup)
    {
      bool exists_in_main_project = false;
      if (file_exists (path_in_main_project))
        {
          char * existing_file_hash =
            hash_get_from_file (
              path_in_main_project,
              HASH_ALGORITHM_XXH3_64);
          exists_in_main_project =
            string_is_equal (
              self->file_hash, existing_file_hash);
          g_free (existing_file_hash);
        }

      if (exists_in_main_project)
        {
          g_debug (
            "linking clip from main project "
            "('%s' to '%s')",
            path_in_main_project, new_path);

          if (file_symlink (
                path_in_main_project, new_path) != 0)
            {
              g_message (
                "failed to link, copying instead");

              /* copy */
              GFile * src_file =
                g_file_new_for_path (
                  path_in_main_project);
              GFile * dest_file =
                g_file_new_for_path (new_path);
              GError * err = NULL;
              g_debug (
                "copying clip from main project "
                "('%s' to '%s')",
                path_in_main_project, new_path);
              if (g_file_copy (
                    src_file, dest_file, 0, NULL, NULL,
                    NULL, &err))
                {
                  goto write_to_pool_free_and_return;
                }
              else /* else if failed */
                {
                  g_warning (
                    "Failed to copy '%s' to '%s': %s",
                    path_in_main_project, new_path,
                    err->message);
                }
            }
        }
    }

  g_debug (
    "writing clip %s to pool "
    "(parts %d, is backup  %d): '%s'",
    self->name, parts, is_backup, new_path);
  audio_clip_write_to_file (
    self, new_path, parts);

  if (!parts)
    {
      /* store file hash */
      g_free_and_null (self->file_hash);
      self->file_hash =
        hash_get_from_file (
          new_path, HASH_ALGORITHM_XXH3_64);
    }

write_to_pool_free_and_return:
  g_free (path_in_main_project);
  g_free (new_path);
}

/**
 * Writes the given audio clip data to a file.
 *
 * @param parts If true, only write new data. @see
 *   AudioClip.frames_written.
 *
 * @return Non-zero if fail.
 */
int
audio_clip_write_to_file (
  AudioClip *  self,
  const char * filepath,
  bool         parts)
{
  g_return_val_if_fail (self->samplerate > 0, -1);
  size_t before_frames =
    (size_t) self->frames_written;
  long ch_offset =
    parts ? self->frames_written : 0;
  long offset = ch_offset * self->channels;
  int ret =
    audio_write_raw_file (
      &self->frames[offset], ch_offset,
      parts ?
        (self->num_frames - self->frames_written) :
        self->num_frames,
      (uint32_t) self->samplerate,
      self->use_flac, self->bit_depth,
      self->channels, filepath);
  audio_clip_update_channel_caches (
    self, before_frames);

  if (parts && ret == 0)
    {
      self->frames_written = self->num_frames;
      self->last_write = g_get_monotonic_time ();
    }

  /* TODO move this to a unit test for this
   * function */
  if (ZRYTHM_TESTING)
    {
      AudioClip * new_clip =
        audio_clip_new_from_file (filepath);
      if (self->num_frames != new_clip->num_frames)
        {
          g_warning (
            "%zu != %zu",
            self->num_frames, new_clip->num_frames);
        }
      float epsilon = 0.0001f;
      g_warn_if_fail (
        audio_frames_equal (
         self->ch_frames[0], new_clip->ch_frames[0],
         (size_t) new_clip->num_frames,
         epsilon));
      g_warn_if_fail (
        audio_frames_equal (
         self->frames, new_clip->frames,
         (size_t)
         new_clip->num_frames * new_clip->channels,
         epsilon));
      audio_clip_free (new_clip);
    }

  return ret;
}

/**
 * Returns whether the clip is used inside the
 * project.
 *
 * @param check_undo_stack If true, this checks both
 *   project regions and the undo stack. If false,
 *   this only checks actual project regions only.
 */
bool
audio_clip_is_in_use (
  AudioClip * self,
  bool        check_undo_stack)
{
  for (int i = 0; i < TRACKLIST->num_tracks; i++)
    {
      Track * track = TRACKLIST->tracks[i];
      if (track->type != TRACK_TYPE_AUDIO)
        continue;

      for (int j = 0; j < track->num_lanes; j++)
        {
          TrackLane * lane = track->lanes[j];

          for (int k = 0; k < lane->num_regions; k++)
            {
              ZRegion * r = lane->regions[k];
              if (r->id.type != REGION_TYPE_AUDIO)
                continue;

              if (r->pool_id == self->pool_id)
                return true;
            }
        }
    }

  if (check_undo_stack)
    {
      if (undo_manager_contains_clip (
            UNDO_MANAGER, self))
        {
          return true;
        }
    }

  return false;
}

/**
 * To be called by audio_pool_remove_clip().
 *
 * Removes the file associated with the clip and
 * frees the instance.
 *
 * @param backup Whether to remove from backup
 *   directory.
 */
void
audio_clip_remove_and_free (
  AudioClip * self,
  bool        backup)
{
  char * path =
    audio_clip_get_path_in_pool (
      self, F_NOT_BACKUP);
  g_message ("removing clip at %s", path);
  g_return_if_fail (path);
  io_remove (path);

  audio_clip_free (self);
}

/**
 * Frees the audio clip.
 */
void
audio_clip_free (
  AudioClip * self)
{
  object_zero_and_free (self->frames);
  for (unsigned int i = 0; i < self->channels; i++)
    {
      object_zero_and_free_if_nonnull (
        self->ch_frames[i]);
    }
  g_free_and_null (self->name);
  g_free_and_null (self->file_hash);

  object_zero_and_free (self);
}

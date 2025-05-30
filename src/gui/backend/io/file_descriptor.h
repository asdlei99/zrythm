// SPDX-FileCopyrightText: © 2019-2021, 2023-2024 Alexandros Theodotou <alex@zrythm.org>
// SPDX-License-Identifier: LicenseRef-ZrythmLicense

#ifndef __AUDIO_SUPPORTED_FILE_H__
#define __AUDIO_SUPPORTED_FILE_H__

#include <memory>
#include <string>

#include "utils/types.h"

/**
 * @addtogroup utils
 *
 * @{
 */

#define SUPPORTED_FILE_DND_PREFIX Z_DND_STRING_PREFIX "FileDescriptor::"

/**
 * File type.
 */
enum class FileType
{
  Midi,
  Mp3,
  Flac,
  Ogg,
  Wav,
  Directory,
  /** Special entry ".." for the parent dir. */
  ParentDirectory,
  Other,
  NumFileTypes,
};

/**
 * Descriptor of a file.
 */
class FileDescriptor
{
public:
  FileDescriptor () = default;

  FileDescriptor (const std::string &abs_path);

  /**
   * @brief Creates a new FileDescriptor from a URI.
   *
   * @param uri
   * @return std::unique_ptr<FileDescriptor>
   * @throw ZrythmException on error.
   */
  static std::unique_ptr<FileDescriptor> new_from_uri (const std::string &uri);

  /**
   * Returns a human readable description of the given file type.
   *
   * Example: wav -> "Wave file".
   */
  static std::string get_type_description (FileType type);

  /**
   * Returns if the given type is supported.
   */
  static bool is_type_supported (FileType type);

  /**
   * Returns if the FileDescriptor is an audio file.
   */
  static bool is_type_audio (FileType type);

  /**
   * Returns if the FileDescriptor is a midi file.
   */
  static bool is_type_midi (FileType type);

  /**
   * Returns the most common extension for the given filetype.
   */
  static const char * get_type_ext (FileType type);

  /**
   * Returns the file type of the given file path.
   */
  static FileType get_type_from_path (const fs::path &file);

  bool is_audio () const { return is_type_audio (type_); }

  bool is_midi () const { return is_type_midi (type_); }

  bool is_supported () const { return is_type_supported (type_); }

  /**
   * Returns whether the given file should auto-play (shorter than 1 min).
   */
  bool should_autoplay () const;

  /**
   * Gets the corresponding icon name for the given FileDescriptor's type.
   */
  const char * get_icon_name () const;

  /**
   * Returns a pango markup to be used in GTK labels.
   */
  std::string get_info_text_for_label () const;

public:
  /** Absolute path. */
  std::string abs_path_;

  /** Type of file. */
  FileType type_ = {};

  /** Human readable label. */
  std::string label_;

  /** Hidden or not. */
  bool hidden_ = false;
};

/**
 * @}
 */

#endif

// SPDX-FileCopyrightText: © 2020-2022, 2024 Alexandros Theodotou <alex@zrythm.org>
// SPDX-License-Identifier: LicenseRef-ZrythmLicense

/**
 * @file
 *
 * Optimized DSP functions.
 *
 * @note More at
 * https://github.com/DISTRHO/DPF-Max-Gen/blob/master/plugins/common/gen_dsp/genlib_ops.h#L313
 */

#ifndef __UTILS_DSP_H__
#define __UTILS_DSP_H__

#include "zrythm-config.h"

#include "common/utils/math.h"
#include "gui/backend/backend/zrythm.h"

#include "juce_wrapper.h"

/**
 * Fill the buffer with the given value.
 */
ATTR_NONNULL ATTR_HOT static inline void
dsp_fill (float * buf, float val, size_t size)
{
  if (ZRYTHM_USE_OPTIMIZED_DSP)
    {
      juce::FloatVectorOperations::fill (buf, val, size);
    }
  else
    {
      std::fill_n (buf, size, val);
    }
}

/**
 * Clamp the buffer to min/max.
 */
ATTR_NONNULL ATTR_HOT static inline void
dsp_clip (float * buf, float minf, float maxf, size_t size)
{
  if (ZRYTHM_USE_OPTIMIZED_DSP)
    {
      juce::FloatVectorOperations::clip (buf, buf, minf, maxf, size);
    }
  else
    {
      std::transform (buf, buf + size, buf, [minf, maxf] (float x) {
        return std::clamp (x, minf, maxf);
      });
    }
}

/**
 * Compute dest[i] = src[i].
 */
ATTR_NONNULL ATTR_HOT static inline void
dsp_copy (float * dest, const float * src, size_t size)
{
  if (ZRYTHM_USE_OPTIMIZED_DSP)
    {
      juce::FloatVectorOperations::copy (dest, src, size);
    }
  else
    {
      std::copy_n (src, size, dest);
    }
}

/**
 * Scale: dst[i] = dst[i] * k.
 */
ATTR_NONNULL ATTR_HOT static inline void
dsp_mul_k2 (float * dest, float k, size_t size)
{
  if (ZRYTHM_USE_OPTIMIZED_DSP)
    {
      juce::FloatVectorOperations::multiply (dest, k, size);
    }
  else
    {
      std::transform (dest, dest + size, dest, [k] (float x) { return x * k; });
    }
}

/**
 * Gets the maximum absolute value of the buffer (as amplitude).
 */
[[nodiscard]] ATTR_NONNULL static inline float
dsp_abs_max (const float * buf, size_t size)
{
  if (ZRYTHM_USE_OPTIMIZED_DSP)
    {
      auto min_and_max = juce::FloatVectorOperations::findMinAndMax (buf, size);
      return std::max (
        std::abs (min_and_max.getStart ()), std::abs (min_and_max.getEnd ()));
    }

  return std::abs (*std::max_element (buf, buf + size, [] (float a, float b) {
    return std::abs (a) < std::abs (b);
  }));
}

/**
 * Gets the absolute max of the buffer.
 *
 * @return Whether the peak changed.
 */
ATTR_HOT ATTR_NONNULL static inline bool
dsp_abs_max_with_existing_peak (float * buf, float * cur_peak, size_t size)
{
  float new_peak = *cur_peak;

  if (ZRYTHM_USE_OPTIMIZED_DSP)
    {
      new_peak = dsp_abs_max (buf, size);
    }
  else
    {
      for (size_t i = 0; i < size; i++)
        {
          float val = fabsf (buf[i]);
          if (val > new_peak)
            {
              new_peak = val;
            }
        }
    }

  bool changed = !math_floats_equal (new_peak, *cur_peak);
  *cur_peak = new_peak;

  return changed;
}

/**
 * Gets the minimum of the buffer.
 */
ATTR_NONNULL float
dsp_min (const float * buf, size_t size)
{
  if (ZRYTHM_USE_OPTIMIZED_DSP)
    {
      return juce::FloatVectorOperations::findMinimum (buf, size);
    }

  return *std::min_element (buf, buf + size, [] (float a, float b) {
    return a < b;
  });
}

/**
 * Gets the maximum of the buffer.
 */
ATTR_NONNULL float
dsp_max (const float * buf, size_t size)
{
  if (ZRYTHM_USE_OPTIMIZED_DSP)
    {
      return juce::FloatVectorOperations::findMaximum (buf, size);
    }

  return *std::max_element (buf, buf + size, [] (float a, float b) {
    return a < b;
  });
}

/**
 * Calculate dst[i] = dst[i] + src[i].
 */
ATTR_NONNULL ATTR_HOT static inline void
dsp_add2 (float * dest, const float * src, size_t count)
{
  if (ZRYTHM_USE_OPTIMIZED_DSP)
    {
      juce::FloatVectorOperations::add (dest, src, count);
    }
  else
    {
      std::transform (dest, dest + count, src, dest, std::plus<> ());
    }
}

/**
 * @brief Calculate dest[i] = dest[i] + src[i] * k.
 */
ATTR_NONNULL ATTR_HOT static inline void
dsp_mix_product (float * dest, const float * src, float k, size_t size)
{
  if (ZRYTHM_USE_OPTIMIZED_DSP)
    {
      juce::FloatVectorOperations::addWithMultiply (dest, src, k, size);
    }
  else
    {
      std::transform (dest, dest + size, src, dest, [k] (float x, float y) {
        return x + y * k;
      });
    }
}

/**
 * Reverse the order of samples: dst[i] <=> src[count - i - 1].
 */
ATTR_NONNULL ATTR_HOT static inline void
dsp_reverse (float * dest, const float * src, size_t size)
{
  std::reverse_copy (src, src + size, dest);
}

/**
 * Calculate normalized values:
 * dst[i] = src[i] / (max { abs(src) }).
 */
ATTR_NONNULL ATTR_HOT static inline void
dsp_normalize (float * dest, const float * src, size_t size)
{
  dsp_copy (dest, src, size);
  const float abs_peak = dsp_abs_max (dest, size);
  dsp_mul_k2 (dest, 1.f / abs_peak, size);
}

/**
 * Calculate linear fade by multiplying from 0 to 1 for
 * @param total_frames_to_fade samples.
 *
 * @note Does not work properly when @param
 *   fade_from_multiplier is < 1k.
 *
 * @param start_offset Start offset in the fade interval.
 * @param total_frames_to_fade Total frames that should be
 *   faded.
 * @param size Number of frames to process.
 * @param fade_from_multiplier Multiplier to fade from (0 to
 *   fade from silence.)
 */
ATTR_NONNULL void
dsp_linear_fade_in_from (
  float * dest,
  int32_t start_offset,
  int32_t total_frames_to_fade,
  size_t  size,
  float   fade_from_multiplier);

/**
 * Calculate linear fade by multiplying from 0 to 1 for
 * @param total_frames_to_fade samples.
 *
 * @param start_offset Start offset in the fade interval.
 * @param total_frames_to_fade Total frames that should be
 *   faded.
 * @param size Number of frames to process.
 * @param fade_to_multiplier Multiplier to fade to (0 to fade
 *   to silence.)
 */
ATTR_NONNULL void
dsp_linear_fade_out_to (
  float * dest,
  int32_t start_offset,
  int32_t total_frames_to_fade,
  size_t  size,
  float   fade_to_multiplier);

/**
 * Makes the two signals mono.
 *
 * @param equal_power True for equal power, false for equal
 *   amplitude.
 *
 * @note Equal amplitude is more suitable for mono
 * compatibility checking. For reference:
 * - equal power sum = (L+R) * 0.7079 (-3dB)
 * - equal amplitude sum = (L+R) /2 (-6.02dB)
 */
ATTR_NONNULL void
dsp_make_mono (float * l, float * r, size_t size, bool equal_power);

#endif

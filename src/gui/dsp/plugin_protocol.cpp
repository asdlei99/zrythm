#include "gui/dsp/plugin_protocol.h"
#include "utils/logger.h"
#include "utils/types.h"

using namespace zrythm::gui::old_dsp::plugins;

constexpr std::array<const char *, 11> plugin_protocol_strings = {
  "Internal",  "LV2", "DSSI", "LADSPA", "VST",  "VST3",
  "AudioUnit", "SFZ", "SF2",  "CLAP",   "JSFX",
};

std::string
Protocol::get_icon_name (ProtocolType prot)
{
  std::string icon;
  switch (prot)
    {
    case ProtocolType::LV2:
      icon = "logo-lv2";
      break;
    case ProtocolType::LADSPA:
      icon = "logo-ladspa";
      break;
    case ProtocolType::AudioUnit:
      icon = "logo-au";
      break;
    case ProtocolType::VST:
    case ProtocolType::VST3:
      icon = "logo-vst";
      break;
    case ProtocolType::SFZ:
    case ProtocolType::SF2:
      icon = "file-music-line";
      break;
    default:
      icon = "plug";
      break;
    }
  return icon;
}

std::string
Protocol::to_string (ProtocolType prot)
{
  return plugin_protocol_strings.at (ENUM_VALUE_TO_INT (prot));
}

Protocol::ProtocolType
Protocol::from_string (const std::string &str)
{
  size_t i = 0;
  for (auto &cur_str : plugin_protocol_strings)
    {
      if (cur_str == str)
        {
          return (ProtocolType) i;
        }
      ++i;
    }
  z_return_val_if_reached (ProtocolType::LV2);
}

bool
Protocol::is_supported (ProtocolType protocol)
{
#ifndef __APPLE__
  if (protocol == ProtocolType::AudioUnit)
    return false;
#endif
#if defined(_WIN32) || defined(__APPLE__)
  if (protocol == PluginProtocol::LADSPA || protocol == PluginProtocol::DSSI)
    return false;
#endif
#ifndef CARLA_HAVE_CLAP_SUPPORT
  if (protocol == PluginProtocol::CLAP)
    return false;
#endif
  return true;
}

Protocol::ProtocolType
Protocol::from_juce_format_name (const juce::String &str)
{
  if (str == "LV2")
    return ProtocolType::LV2;
  if (str == "LADSPA")
    return ProtocolType::LADSPA;
  if (str == "VST3")
    return ProtocolType::VST3;
  if (str == "AudioUnit")
    return ProtocolType::AudioUnit;
  if (str == "SFZ")
    return ProtocolType::SFZ;
  if (str == "SF2")
    return ProtocolType::SF2;
  if (str == "CLAP")
    return ProtocolType::CLAP;
  if (str == "JSFX")
    return ProtocolType::JSFX;

  z_return_val_if_reached (ProtocolType::Internal);
}

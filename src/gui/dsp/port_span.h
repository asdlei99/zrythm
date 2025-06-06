#pragma once

#include "gui/dsp/port_all.h"
#include "utils/uuid_identifiable_object.h"

/**
 * @brief Track span that offers helper methods on a range of tracks.
 */
template <utils::UuidIdentifiableObjectPtrVariantRange Range>
class PortSpanImpl
    : public utils::UuidIdentifiableObjectCompatibleSpan<Range, PortRegistry>
{
public:
  using Base = utils::UuidIdentifiableObjectCompatibleSpan<Range, PortRegistry>;
  using VariantType = typename Base::value_type;
  using PortUuid = typename Base::UuidType;
  using Base::Base; // Inherit constructors

  static auto name_projection (const VariantType &port_var)
  {
    return std::visit (
      [] (const auto &port) { return port->get_name (); }, port_var);
  }
  static auto identifier_projection (const VariantType &port_var)
  {
    return std::visit ([] (const auto &port) { return port->id_; }, port_var);
  }
};

using PortSpan = PortSpanImpl<std::span<const PortPtrVariant>>;
using PortRegistrySpan =
  PortSpanImpl<utils::UuidIdentifiableObjectSpan<PortRegistry>>;
extern template class PortSpanImpl<std::span<const PortSpan::VariantType>>;
extern template class PortSpanImpl<
  utils::UuidIdentifiableObjectSpan<PortRegistry>>;

static_assert (std::ranges::random_access_range<PortSpan>);
static_assert (std::ranges::random_access_range<PortRegistrySpan>);

using PortSpanVariant = std::variant<PortSpan, PortRegistrySpan>;

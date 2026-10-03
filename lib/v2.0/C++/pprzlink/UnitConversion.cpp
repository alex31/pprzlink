// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @file UnitConversion.cpp
 * @brief Resolve editable aliases and explicit XML scales into coherent SI units.
 * @ingroup internals
 *
 * LLNL is used without registering aliases globally. Metadata is resolved once;
 * message/schema copies share immutable results. Unknown units remain available
 * as XML metadata and do not prevent ordinary message decoding.
 */
#include "detail/UnitConversion.h"
#include <cmath>
#include <utility>
#include <vector>

namespace pprzlink::detail {
  namespace {
    /// A parsed spelling and a separate number scale (also valid for Celsius).
    struct ResolvedUnit {
      units::precise_unit unit = units::precise::invalid; ///< LLNL unit, or invalid.
      double scale = 1.0; ///< XML number to LLNL number.
      bool disabled = false; ///< A matching rule explicitly prohibits conversion.
    };

    /// Resolve scoped aliases before LLNL's parser; never change its global state.
    /// @param[in] spelling Exact XML unit spelling.
    /// @param[in] message Owning message name for optional rule selectors.
    /// @param[in] field Field name for optional rule selectors.
    /// @return Parsed unit/scale, or a disabled/invalid result.
    ResolvedUnit resolve(std::string_view spelling, std::string_view message, std::string_view field)
    {
      const UnitAlias *selected = nullptr;
      int specificity = -1;
      for (const auto &alias : unitAliases()) {
        if (alias.xml != spelling || (!alias.message.empty() && alias.message != message) ||
            (!alias.field.empty() && alias.field != field)) continue;
        const int rank = static_cast<int>(!alias.message.empty()) + static_cast<int>(!alias.field.empty());
        if (rank > specificity) { selected = &alias; specificity = rank; }
      }
      if (selected) {
        if (selected->llnl.empty()) return {units::precise::invalid, 1.0, true};
        if (!std::isfinite(selected->scale) || selected->scale == 0.0) return {};
        return {units::unit_from_string(std::string(selected->llnl), 0), selected->scale, false};
      }
      if (spelling.empty()) return {};
      return {units::unit_from_string(std::string(spelling), 0), 1.0, false};
    }

    /// Prefer familiar SI symbols to an expanded product of base units.
    /// @param[in] unit Coherent SI unit whose flags and scale are normalized.
    /// @return A common SI symbol or LLNL's expanded base-unit representation.
    std::string siSymbol(const units::precise_unit &unit)
    {
      static const auto common = [] {
        std::vector<std::pair<std::string, units::precise_unit>> result;
        for (const char *symbol : {"1", "m", "s", "kg", "A", "K", "mol", "cd", "rad",
                                  "m/s", "m/s^2", "m/s^3", "rad/s", "rad/s^2", "Pa",
                                  "V", "W", "J", "C", "T", "Hz", "N", "kg/m^3", "m^2", "m^3"}) {
          result.emplace_back(symbol, units::unit_from_string(symbol, 0));
        }
        return result;
      }();
      for (const auto &[symbol, candidate] : common) if (unit == candidate) return symbol;
      return units::to_string(unit);
    }
  }

  std::shared_ptr<const UnitConversion> prepareUnitConversion(
      std::string_view unit, std::string_view altUnit, std::optional<double> coefficient,
      std::string_view message, std::string_view field)
  {
    auto resolved = resolve(unit, message, field);
    if (resolved.disabled) return {};
    if (coefficient) {
      auto alternative = resolve(altUnit, message, field);
      if (alternative.disabled || !units::is_valid(alternative.unit)) return {};
      if (units::is_valid(resolved.unit) &&
          !resolved.unit.base_units().has_same_base(alternative.unit.base_units())) return {};
      resolved = alternative;
      resolved.scale *= *coefficient;
    }
    if (!units::is_valid(resolved.unit) || !std::isfinite(resolved.scale) || resolved.scale == 0.0 ||
        resolved.unit.is_equation() || resolved.unit.is_per_unit()) return {};

    // Currency/data/count units have no physical SI basis in this API.
    const auto base = resolved.unit.base_units();
    if (base.currency() != 0 || base.count() != 0) return {};
    auto si = units::precise_unit(base);
    si.clear_flags(); // Celsius's flag belongs to the XML unit; the coherent unit is kelvin.
    if (!std::isfinite(units::convert(1.0, resolved.unit, si)) ||
        !std::isfinite(units::convert(1.0, si, resolved.unit))) return {};
    return std::make_shared<const UnitConversion>(resolved.unit, si, resolved.scale, siSymbol(si));
  }
}

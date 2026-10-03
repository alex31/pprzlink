// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @file UnitConversion.h
 * @brief Private LLNL unit resolution and immutable conversions shared by schema copies.
 * @ingroup internals
 */
#pragma once

#include <units/units.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace pprzlink::detail {
  /// One editable Paparazzi spelling, interpreted before LLNL's general parser.
  struct UnitAlias {
    std::string_view xml; ///< Exact Paparazzi unit spelling.
    std::string_view llnl; ///< LLNL spelling; empty explicitly disables SI conversion.
    double scale = 1.0; ///< Multiply XML numbers by this factor before LLNL conversion.
    std::string_view message = {}; ///< Optional message selector; empty matches all messages.
    std::string_view field = {}; ///< Optional field selector; empty matches all fields.
  };

  /// Borrow the immutable rules in UnitAliases.cpp; specific selectors override general rules.
  /// @return Rules with process lifetime, without changing LLNL's global registry.
  std::span<const UnitAlias> unitAliases() noexcept;

  /// Prepared units and an XML-number scale; contains no per-message value.
  struct UnitConversion {
    units::precise_unit xmlUnit; ///< Resolved LLNL unit after applying the number scale.
    units::precise_unit siUnit; ///< Coherent SI unit, with radians retained for angles.
    double xmlScale = 1.0; ///< XML number to resolved LLNL number, including explicit XML coefficients.
    std::string siSymbol; ///< Readable coherent SI symbol, including "1" for dimensionless values.
  };

  /// Resolve numeric field metadata once, preserving unsupported schemas for raw access.
  /// @param[in] unit Original XML unit, possibly absent.
  /// @param[in] altUnit Original alternative unit, possibly absent.
  /// @param[in] coefficient Explicit XML number-to-alternative-unit multiplier, if present.
  /// @param[in] message Message name used for scoped aliases.
  /// @param[in] field Field name used for scoped aliases.
  /// @return Immutable conversion, or null when metadata cannot describe a coherent SI conversion.
  std::shared_ptr<const UnitConversion> prepareUnitConversion(
      std::string_view unit, std::string_view altUnit, std::optional<double> coefficient,
      std::string_view message, std::string_view field);
}

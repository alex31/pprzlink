// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @file UnitAliases.cpp
 * @brief Editable Paparazzi-to-LLNL spellings and number scales.
 * @ingroup internals
 *
 * Add a row {"XML spelling", "LLNL spelling", scale}. The default scale is 1.
 * LLNL receives XML_value * scale, so {"1e7deg", "deg", 1e-7} represents
 * integer degrees multiplied by 10^7. Temperature scales apply BEFORE converting
 * Celsius to kelvins. An empty LLNL spelling explicitly disables SI conversion.
 * Optional fourth/fifth strings restrict a row to a message/field; the most
 * specific matching row wins. Rebuild pprzlink after editing this file.
 */
#include "detail/UnitConversion.h"

namespace pprzlink::detail {
  std::span<const UnitAlias> unitAliases() noexcept
  {
    //                 XML spelling         LLNL spelling     XML number scale
    static constexpr UnitAlias aliases[] = {
      {"1e7deg",            "deg",             1e-7},
      {"1e2_deg",           "deg",             0.01},
      {"1e4rad",            "rad",             1e-4},
      {"2^8m",              "m",               1.0 / 256.0},
      {"2^12rad",           "rad",             1.0 / 4096.0},
      {"decideg",           "deg",             0.1},
      {"centideg",          "deg",             0.01},
      {"decisec",           "s",               0.1},
      {"deciV",             "V",               0.1},
      {"deciAh",            "Ah",              0.1},
      {"usec",              "us"},
      {"mus",               "us"},
      {"hz",                "Hz"},
      {"mGauss",            "gauss",           0.001},
      {"m/s-2",             "m/s^2"},
      {"m/s2",              "m/s^2"},
      {"mBar",              "mbar"},
      {"Bar",               "bar"},
      {"deg_wind",          "deg"}, // Unit conversion keeps the wind bearing convention.
      {"s (Unix time)",     "s"},   // Unit conversion keeps the Unix epoch.

      // C and P are valid LLNL units too. Only reinterpret the legacy fields
      // whose XML uses C for Celsius and P for pressure; elsewhere C is coulomb.
      {"C",                 "degC", 1.0, "ESC",              "temperature"},
      {"C",                 "degC", 1.0, "ESC",              "temperature_dev"},
      {"C",                 "degC", 1.0, "BATTERY_MONITOR",  "bus_temp"},
      {"C",                 "degC", 1.0, "ENGINE_STATUS",    "temp"},
      {"C",                 "degC", 1.0, "IMCU_REMOTE_BARO", "pitot_temp"},
      {"Celcius",           "degC"},
      {"deg_celsius",       "degC"},
      {"deg_celcius",       "degC"},
      {"deg celcius",       "degC"},
      {"deg C",             "degC"},
      {"°C",                "degC"},
      {"dC",                "degC",            0.1},
      {"10x_deg_celsius",   "degC",            0.1},
      {"100x_deg_celsius",  "degC",            0.01},
      {"P",                 "Pa", 1.0, "BARO_MS5534A", "pressure"},

      {"none",              "1"},
      {"events",            "1"},
      {"Fragments",         "1"},
      {"packets/s",         "1/s"},
      {"msgs/s",            "1/s"},

      // These require calibration, protocol interpretation, or a reference frame.
      // Replace an empty target with a unit/scale when that information is known.
      {"adc",               ""},
      {"pprz",              ""},
      {"ticks",             ""},
      {"dshot",             ""},
      {"subpixels",         ""},
      {"rel_hum",           ""},
      {"Poles/s",           ""},
      {"bool",              ""},
      {"byte_mask",         ""},
      {"url",               ""},
      {"-",                 ""},
      {"SI (m or deg)",     ""},
      // Logarithmic levels require a power/amplitude convention and a reference.
      {"dB",                ""},
      {"dbHz",              ""},
    };
    return aliases;
  }
}

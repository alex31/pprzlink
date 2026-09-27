/*
 * Copyright 2019 garciafa
 * This file is part of PprzLinkCPP
 *
 * PprzLinkCPP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PprzLinkCPP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with ModemTester.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef PPRZLINKCPP_TEXTCODEC_H
#define PPRZLINKCPP_TEXTCODEC_H

#include <pprzlink/FieldValue.h>

namespace pprzlink {
  /// Ivy payload: comma-separated arrays, quoted char arrays and empty/spaced strings.
  /// int8/uint8 values are numeric; floating-point values use the stream precision.
  std::ostream &writeIvyField(std::ostream &stream, const FieldValue::Storage &value);

  /// Diagnostic output adds braces around numeric/string arrays.
  /// Both functions preserve the caller's stream flags and the stored value.
  std::ostream &writeDebugField(std::ostream &stream, const FieldValue::Storage &value);

  /// Convenience overloads for standalone fields; formatting needs only their value.
  std::ostream &writeIvyField(std::ostream &stream, const FieldValue &field);
  std::ostream &writeDebugField(std::ostream &stream, const FieldValue &field);
}
#endif // PPRZLINKCPP_TEXTCODEC_H

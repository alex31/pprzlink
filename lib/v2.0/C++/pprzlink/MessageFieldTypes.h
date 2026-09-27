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

#ifndef PPRZLINKCPP_MESSAGEFIELDTYPES_H
#define PPRZLINKCPP_MESSAGEFIELDTYPES_H

#include <string>
#include <optional>

namespace pprzlink {

  enum class BaseType {
    NOT_A_TYPE,
    CHAR,
    INT8,
    INT16,
    INT32,
    UINT8,
    UINT16,
    UINT32,
    FLOAT,
    DOUBLE,
    STRING
  };

  size_t sizeofBaseType(BaseType type);

  /// An XML type (e.g. float, int16[] or char[5]), without a field name or value.
  class FieldType {
  public:
    explicit FieldType(const std::string &typeString);

    [[nodiscard]] BaseType getBaseType() const;

    [[nodiscard]] bool isArray() const;

    /// Zero denotes a dynamic array; calling this on a scalar throws logic_error.
    [[nodiscard]] size_t getArraySize() const;

    [[nodiscard]] std::string toString() const;

  private:
    BaseType baseType;
    std::optional<size_t> arraySize; // nullopt: scalar; 0: dynamic; positive: fixed.
  };
}
#endif // PPRZLINKCPP_MESSAGEFIELDTYPES_H

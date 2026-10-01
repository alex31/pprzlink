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

/**
 * @file MessageField.h
 * @brief Named XML field definitions and optional display formats.
 * @ingroup messages
 *
 * Fixed data size excludes variable-length count prefixes. XML display formats affect only the legacy Ivy serializer.
 */

#ifndef PPRZLINKCPP_MESSAGEFIELD_H
#define PPRZLINKCPP_MESSAGEFIELD_H

#include <string>
#include <pprzlink/MessageFieldTypes.h>

namespace pprzlink {

  /// A field definition: its name and XML type. FieldValue holds an actual value.
  /// @ingroup messages
  class MessageField {
  public:
    /// @brief Copy a field name and an already parsed type.
    /// @param[in] name XML field name.
    /// @param[in] type Parsed type whose definition is copied.
    MessageField(const std::string &name, const FieldType &type);

    /// @brief Define a field from XML type text and an optional legacy display format.
    /// @param[in] name XML field name.
    /// @param[in] typeString Type declaration accepted by FieldType.
    /// @param[in] format Optional XML numeric display format; empty uses legacy defaults.
    /// @throws bad_message_file The type declaration is invalid.
    MessageField(const std::string &name, const std::string &typeString,
                 std::string format = {});

    /// @brief Borrow the field name.
    /// @return Name reference valid for this definition's lifetime.
    [[nodiscard]] const std::string &getName() const;

    /// @brief Borrow the parsed type definition.
    /// @return Type reference valid for this definition's lifetime.
    [[nodiscard]] const FieldType &getType() const;

    /// Fixed data size; zero for strings/dynamic arrays. Excludes count prefixes.
    /// @return Fixed schema data bytes, not the size of a concrete encoded FieldValue.
    [[nodiscard]] size_t getSize() const;
    /// Optional XML display format; interpreted only by the legacy Ivy serializer.
    /// @return Borrowed format string; an empty value requests the serializer's default.
    [[nodiscard]] const std::string &getFormat() const noexcept { return format; }
  private:
    std::string name;
    FieldType type;
    size_t size;
    std::string format;
  };
}

#endif // PPRZLINKCPP_MESSAGEFIELD_H

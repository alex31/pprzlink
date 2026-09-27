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

#ifndef PPRZLINKCPP_MESSAGEFIELD_H
#define PPRZLINKCPP_MESSAGEFIELD_H

#include <string>
#include <pprzlink/MessageFieldTypes.h>

namespace pprzlink {

  /// A field definition: its name and XML type. FieldValue holds an actual value.
  class MessageField {
  public:
    MessageField(const std::string &name, const FieldType &type);

    MessageField(const std::string &name, const std::string &typeString);

    [[nodiscard]] const std::string &getName() const;

    [[nodiscard]] const FieldType &getType() const;

    /// Fixed data size; zero for strings/dynamic arrays. Excludes count prefixes.
    [[nodiscard]] size_t getSize() const;
  private:
    std::string name;
    FieldType type;
    size_t size;
  };
}

#endif // PPRZLINKCPP_MESSAGEFIELD_H

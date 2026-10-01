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
 * @file MessageField.cpp
 * @brief Fixed schema data sizes for fields.
 * @ingroup messages
 *
 * Strings and dynamic arrays report zero fixed bytes; concrete field sizes are computed by the binary codec after values are supplied.
 */

#include <pprzlink/MessageField.h>
#include <utility>

namespace pprzlink {

  MessageField::MessageField(const std::string &name, const FieldType &type)
    : name(name), type(type), size(0)
  {
    if (type.getBaseType() == BaseType::STRING) {
      size = 0;
    } else if (type.isArray()) {
      size = sizeofBaseType(type.getBaseType()) * type.getArraySize();
    } else {
      size = sizeofBaseType(type.getBaseType());
    }
  }

  MessageField::MessageField(const std::string &name, const std::string &typeString, std::string format)
    : MessageField(name, FieldType(typeString))
  { this->format = std::move(format); }

  const std::string &MessageField::getName() const
  {
    return name;
  }

  const FieldType &MessageField::getType() const
  {
    return type;
  }

  size_t MessageField::getSize() const
  {
    return size;
  }
}

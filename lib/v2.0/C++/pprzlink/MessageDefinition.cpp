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

#include <pprzlink/MessageDefinition.h>
#include <pprzlink/exceptions/pprzlink_exception.h>
#include "detail/XmlReader.h"
#include <format>

namespace pprzlink {
  MessageDefinition::MessageDefinition() : classId(0), id(0) {}

  MessageDefinition::MessageDefinition(const tinyxml2::XMLElement *xml, int classId)
    : classId(0), id(0)
  {
    if (classId < 0 || classId > 15) throw bad_message_file("Class ID must be in [0, 15]");
    this->classId = static_cast<uint8_t>(classId);
    const auto &message = detail::xml::element(xml, "message");
    name = detail::xml::attribute(message, "name", "NAME");
    try {
      id = static_cast<uint8_t>(detail::xml::id(message, 255));
      const char *link = message.Attribute("link");
      if (!link) link = message.Attribute("LINK");
      if (link) {
        if (std::string_view(link) == "forwarded") linkMode = LinkMode::Forwarded;
        else if (std::string_view(link) == "broadcasted") linkMode = LinkMode::Broadcasted;
        else throw bad_message_file("Unknown link mode: " + std::string(link));
      }
      for (auto field = message.FirstChildElement("field"); field;
           field = field->NextSiblingElement("field")) {
        const auto fieldName = detail::xml::attribute(*field, "name", "NAME");
        try {
          const auto type = detail::xml::attribute(*field, "type", "TYPE");
          if (!fieldNameToIndex.emplace(fieldName, fields.size()).second) {
            throw bad_message_file("Duplicate field name");
          }
          const char *format = field->Attribute("format");
          if (!format) format = field->Attribute("FORMAT");
          fields.emplace_back(fieldName, type, format ? format : "");
        } catch (const bad_message_file &error) {
          throw bad_message_file(std::format("field '{}': {}", fieldName, error.what()));
        }
      }
    } catch (const bad_message_file &error) {
      throw bad_message_file(std::format("message '{}': {}", name, error.what()));
    }
  }

  uint8_t MessageDefinition::getClassId() const
  {
    return classId;
  }

  uint8_t MessageDefinition::getId() const
  {
    return id;
  }

  const std::string &MessageDefinition::getName() const
  {
    return name;
  }

  const MessageField &MessageDefinition::getField(size_t index) const
  {
    if (index >= fields.size()) {
      throw no_such_field(std::format("No field at index {} in message {}", index, name));
    }
    return fields[index];
  }

  const MessageField &MessageDefinition::getField(const std::string &name) const
  {
    const auto found = fieldNameToIndex.find(name);
    if (found == fieldNameToIndex.end()) {
      throw no_such_field(std::format("No field {} in message {}", name, getName()));
    }
    return fields[found->second];
  }

  size_t MessageDefinition::getNbFields() const
  {
    return fields.size();
  }

  bool MessageDefinition::hasFieldName(const std::string &name) const
  {
    return fieldNameToIndex.contains(name);
  }

  std::string MessageDefinition::toString() const
  {
    auto text = std::format("{}({}) in class {}\n", name, id, classId);
    for (const auto &field : fields) {
      text += std::format("\t{} : {}\n", field.getName(), field.getType().toString());
    }
    return text;
  }

  size_t MessageDefinition::getMinimumSize() const
  {
    size_t size = 0;
    for (const auto &field : fields) {
      size += field.getSize();
    }
    return size;
  }

  bool MessageDefinition::isRequest() const
  {
    return name.ends_with("_REQ");
  }
}

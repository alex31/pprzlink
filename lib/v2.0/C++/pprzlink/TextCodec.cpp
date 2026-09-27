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

#include <pprzlink/TextCodec.h>
#include <iomanip>

namespace pprzlink {
  namespace {
    enum class TextFormat { Ivy, Debug };

    template<class T>
    void writeElement(std::ostream &stream, const T &value, TextFormat format)
    {
      if constexpr (std::same_as<T, int8_t> || std::same_as<T, uint8_t>) {
        stream << static_cast<int>(value);
      } else if constexpr (std::floating_point<T>) {
        stream << std::fixed << value;
      } else if constexpr (std::same_as<T, std::string>) {
        if (format == TextFormat::Ivy && (value.empty() || value.find(' ') != std::string::npos)) {
          stream << '"' << value << '"';
        } else {
          stream << value;
        }
      } else {
        stream << value;
      }
    }

    std::ostream &writeField(std::ostream &stream, const FieldValue::Storage &storage, TextFormat format)
    {
      // Keep fixed-point formatting local instead of modifying the caller's stream.
      std::ostringstream text;
      text.imbue(stream.getloc());
      text.precision(stream.precision());
      std::visit([&]<class T>(const T &value) {
        if constexpr (Arithmetic<T> || std::same_as<T, std::string>) {
          writeElement(text, value, format);
        } else if constexpr (std::same_as<T, std::vector<char>>) {
          text << '"';
          for (char character : value) text << character;
          text << '"';
        } else {
          if (format == TextFormat::Debug) text << '{';
          bool first = true;
          for (const auto &element : value) {
            if (!first) text << ',';
            first = false;
            writeElement(text, element, format);
          }
          if (format == TextFormat::Debug) text << '}';
        }
      }, storage);
      return stream << text.str();
    }
  }

  std::ostream &writeIvyField(std::ostream &stream, const FieldValue::Storage &value)
  {
    return writeField(stream, value, TextFormat::Ivy);
  }

  std::ostream &writeDebugField(std::ostream &stream, const FieldValue::Storage &value)
  {
    return writeField(stream, value, TextFormat::Debug);
  }

  std::ostream &writeIvyField(std::ostream &stream, const FieldValue &field)
  {
    return writeIvyField(stream, field.getValue());
  }

  std::ostream &writeDebugField(std::ostream &stream, const FieldValue &field)
  {
    return writeDebugField(stream, field.getValue());
  }
}

std::ostream &operator<<(std::ostream &stream, const pprzlink::FieldValue &field)
{
  return pprzlink::writeDebugField(stream, field);
}

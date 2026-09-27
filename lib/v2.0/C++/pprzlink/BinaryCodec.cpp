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

#include <pprzlink/BinaryCodec.h>

namespace pprzlink::binary {
  namespace {
    bool hasCount(const FieldValue &field)
    {
      const auto &type = field.getType();
      return !type.isArray() ? type.getBaseType() == BaseType::STRING : type.getArraySize() == 0;
    }

    void checkRemaining(std::span<const uint8_t> buffer, size_t offset, size_t count, size_t elementSize)
    {
      if (offset > buffer.size() || count > (buffer.size() - offset) / elementSize) {
        throw std::out_of_range("Truncated PprzLink binary field");
      }
    }
  }

  size_t fieldSize(const FieldValue &field)
  {
    return std::visit([&]<class T>(const T &value) -> size_t {
      if constexpr (Arithmetic<T>) {
        return sizeof(T);
      } else if constexpr (std::same_as<T, std::vector<std::string>>) {
        throw std::logic_error("Arrays of strings have no PprzLink binary representation");
      } else {
        const bool count = hasCount(field);
        if (count && value.size() > std::numeric_limits<uint8_t>::max()) {
          throw std::length_error("Binary field " + field.getName() + " exceeds 255 elements");
        }
        return value.size() * sizeof(typename T::value_type) + (count ? 1 : 0);
      }
    }, field.getValue());
  }

  size_t writeField(BytesBuffer &buffer, const FieldValue &field)
  {
    const auto size = fieldSize(field); // Validate before appending any bytes.
    std::visit([&]<class T>(const T &value) {
      if constexpr (Arithmetic<T>) {
        writeLittleEndian(buffer, value);
      } else if constexpr (!std::same_as<T, std::vector<std::string>>) {
        if (hasCount(field)) writeLittleEndian(buffer, static_cast<uint8_t>(value.size()));
        for (const auto &element : value) writeLittleEndian(buffer, element);
      }
    }, field.getValue());
    return size;
  }

  FieldValue readField(const MessageField &field, std::span<const uint8_t> buffer, size_t &offset)
  {
    auto cursor = offset;
    const auto &type = field.getType();
    auto result = detail::visitBaseType(type.getBaseType(), [&]<class T>() -> FieldValue {
      if constexpr (std::same_as<T, std::string>) {
        if (type.isArray()) {
          throw std::logic_error("Arrays of strings have no PprzLink binary representation");
        }
        const auto count = readLittleEndian<uint8_t>(buffer, cursor);
        checkRemaining(buffer, cursor, count, 1);
        std::string text;
        text.reserve(count);
        for (size_t i = 0; i < count; ++i) text += readLittleEndian<char>(buffer, cursor);
        return FieldValue(field, text);
      } else {
        if (!type.isArray()) return FieldValue(field, readLittleEndian<T>(buffer, cursor));
        const auto count = type.getArraySize() == 0
          ? readLittleEndian<uint8_t>(buffer, cursor) : type.getArraySize();
        checkRemaining(buffer, cursor, count, sizeof(T));
        std::vector<T> values;
        values.reserve(count);
        for (size_t i = 0; i < count; ++i) values.push_back(readLittleEndian<T>(buffer, cursor));
        return FieldValue(field, values);
      }
    });
    offset = cursor;
    return result;
  }
}

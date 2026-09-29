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

#ifndef PPRZLINKCPP_FIELDVALUE_H
#define PPRZLINKCPP_FIELDVALUE_H

#include <pprzlink/MessageField.h>
#include <pprzlink/Device.h>
#include <pprzlink/detail/CheckedNumber.h>
#include <algorithm>
#include <array>
#include <concepts>
#include <format>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace pprzlink {
  template<class T>
  concept Arithmetic = std::integral<T> || std::floating_point<T>;

  /// Array input: an iterable container with a value type and a size.
  /// std::string has its own overload to distinguish text from array elements.
  template<class Container>
  concept FieldValueContainer =
    !std::same_as<Container, std::string> &&
    std::ranges::input_range<const Container> &&
    requires(const Container &container) {
      typename Container::value_type;
      { container.size() } -> std::convertible_to<size_t>;
    };

  /// Array output only needs clear/push_back; std::array has a separate overload.
  template<class Container>
  concept FieldValueOutputContainer = !std::same_as<Container, std::string> &&
    requires(Container &container, typename Container::value_type element) {
      container.clear();
      container.push_back(element);
    };

  namespace detail {
    // Keep the receiver dependent until FieldValue is completely defined.
    template<class Field, class Output>
    concept ReadableFieldValue = std::default_initializable<Output> &&
      requires(const Field &field, Output &output) { field.getValue(output); };

    /// Translate the XML base type into a C++ type in one place.
    template<class Visitor>
    decltype(auto) visitBaseType(BaseType type, Visitor &&visitor)
    {
      switch (type) {
        case BaseType::CHAR: return visitor.template operator()<char>();
        case BaseType::INT8: return visitor.template operator()<int8_t>();
        case BaseType::INT16: return visitor.template operator()<int16_t>();
        case BaseType::INT32: return visitor.template operator()<int32_t>();
        case BaseType::INT64: return visitor.template operator()<int64_t>();
        case BaseType::UINT8: return visitor.template operator()<uint8_t>();
        case BaseType::UINT16: return visitor.template operator()<uint16_t>();
        case BaseType::UINT32: return visitor.template operator()<uint32_t>();
        case BaseType::UINT64: return visitor.template operator()<uint64_t>();
        case BaseType::FLOAT: return visitor.template operator()<float>();
        case BaseType::DOUBLE: return visitor.template operator()<double>();
        case BaseType::STRING: return visitor.template operator()<std::string>();
        case BaseType::NOT_A_TYPE: break;
      }
      throw std::logic_error("Invalid field base type");
    }
  }

  /// A field definition and its actual value. The variant type follows the XML type.
  class FieldValue {
  public:
    using Storage = std::variant<
      char, int8_t, int16_t, int32_t, uint8_t, uint16_t, uint32_t, float, double, std::string,
      int64_t, uint64_t,
      std::vector<char>, std::vector<int8_t>, std::vector<int16_t>, std::vector<int32_t>,
      std::vector<uint8_t>, std::vector<uint16_t>, std::vector<uint32_t>,
      std::vector<int64_t>, std::vector<uint64_t>,
      std::vector<float>, std::vector<double>, std::vector<std::string>>;

    // A field cannot exist without both its definition and a value.
    FieldValue() = delete;

    /// Convert an arithmetic scalar to the type declared by the field.
    template<Arithmetic T>
    FieldValue(const MessageField &field, T input) : field(field), value(makeScalar(field, input)) {}

    /// Build a string or a char array; text is never parsed as a number.
    template<std::same_as<std::string> T>
    FieldValue(const MessageField &field, const T &text) : field(field), value(makeText(field, text)) {}

    template<std::same_as<char> T>
    FieldValue(const MessageField &field, const T *text) : FieldValue(field, std::string(text)) {}

    /// Convert array elements and check the fixed size, when specified in XML.
    template<FieldValueContainer Container>
    FieldValue(const MessageField &field, const Container &input)
      : field(field), value(makeArray(field, input)) {}

    template<Arithmetic T>
    FieldValue(const MessageField &field, const T *input, size_t size)
      : FieldValue(field, std::span<const T>(input, size)) {}

    /// Read the exact stored type. A mismatch throws std::bad_variant_access.
    template<Arithmetic T>
    void getValue(T &output) const { output = stored<T>(); }

    template<std::same_as<std::string> T>
    void getValue(T &output) const { output = stored<T>(); }

    template<FieldValueOutputContainer Container>
    void getValue(Container &output) const
    {
      const auto &elements = stored<std::vector<typename Container::value_type>>();
      output.clear();
      for (const auto &element : elements) output.push_back(element);
    }

    /// A fixed output array must have exactly the stored number of elements.
    template<class T, size_t Size>
    void getValue(std::array<T, Size> &output) const
    {
      const auto &elements = stored<std::vector<T>>();
      if (elements.size() != Size) {
        throw std::length_error(std::format("Field {} has {} elements, requested {}",
                                           getName(), elements.size(), Size));
      }
      std::ranges::copy(elements, output.begin());
    }

    /// Return a scalar, string or container using the same rules as the output overloads.
    template<class T>
      requires detail::ReadableFieldValue<FieldValue, T>
    [[nodiscard]] T getValue() const
    {
      T output{};
      getValue(output);
      return output;
    }

    /// Explicit numeric conversion. Fractional-to-integer and out-of-range reads throw.
    /// Floating-point destinations follow normal rounding; this is not a lossless conversion API.
    template<Arithmetic T>
    [[nodiscard]] T getValueAs() const
    {
      return std::visit([this]<class Stored>(const Stored &item) -> T {
        if constexpr (Arithmetic<Stored>) return detail::checkedNumber<T>(item, getName());
        else throw field_type_mismatch(std::format("Field '{}' has type {}, requested numeric {}",
                                                  getName(), getType().toString(), detail::valueTypeName<T>()));
      }, value);
    }

    [[nodiscard]] const MessageField &getField() const { return field; }
    [[nodiscard]] const FieldType &getType() const { return field.getType(); }
    [[nodiscard]] const std::string &getName() const { return field.getName(); }
    /// Read-only access for visitors; callers cannot change the stored type.
    [[nodiscard]] const Storage &getValue() const { return value; }

    /// Compatibility entry points delegated to the binary codec.
    size_t addToBuffer(BytesBuffer &buffer) const;
    [[nodiscard]] size_t getByteSize() const;

  private:
    MessageField field;
    Storage value;

    static void checkArraySize(const MessageField &field, size_t size);
    static Storage makeText(const MessageField &field, const std::string &text);

    template<class T>
    const T &stored() const
    {
      // Also handles requested arithmetic/container types absent from Storage.
      return std::visit([this]<class Stored>(const Stored &item) -> const T& {
        if constexpr (std::same_as<T, Stored>) return item;
        else throw field_type_mismatch(std::format("Field '{}' has type {}, requested {}",
                                                  getName(), getType().toString(), detail::valueTypeName<T>()));
      }, value);
    }

    template<class To, class From>
    static To convertElement(const From &input, const std::string &fieldName)
    {
      if constexpr (std::same_as<To, std::string>) {
        std::ostringstream stream;
        stream << input;
        return stream.str();
      } else if constexpr (Arithmetic<From>) {
        return detail::checkedNumber<To>(input, fieldName);
      } else {
        throw std::logic_error("Cannot convert this array element to a numeric field");
      }
    }

    template<Arithmetic T>
    static Storage makeScalar(const MessageField &field, T input)
    {
      if (field.getType().isArray()) {
        throw std::logic_error("Cannot build array field " + field.getName() + " from a scalar");
      }
      return detail::visitBaseType(field.getType().getBaseType(), [&]<class Target>() -> Storage {
        return convertElement<Target>(input, field.getName());
      });
    }

    template<FieldValueContainer Container>
    static Storage makeArray(const MessageField &field, const Container &input)
    {
      checkArraySize(field, input.size());
      return detail::visitBaseType(field.getType().getBaseType(), [&]<class Target>() -> Storage {
        std::vector<Target> result;
        result.reserve(input.size());
        for (const auto &element : input) {
          // Read through value_type so proxy ranges such as vector<bool> are checked too.
          result.push_back(convertElement<Target>(static_cast<typename Container::value_type>(element), field.getName()));
        }
        return result;
      });
    }
  };
}

/// Human-readable output with numeric int8/uint8 values; does not mutate the field.
std::ostream& operator<<(std::ostream &stream, const pprzlink::FieldValue &value);

#endif // PPRZLINKCPP_FIELDVALUE_H

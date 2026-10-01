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

#ifndef PPRZLINKCPP_MESSAGE_H
#define PPRZLINKCPP_MESSAGE_H

#include <pprzlink/FieldValue.h>
#include <pprzlink/MessageDefinition.h>
#include <pprzlink/exceptions/pprzlink_exception.h>
#include <concepts>
#include <map>
#include <type_traits>
#include <utility>
#include <variant>

namespace pprzlink {
  namespace detail {
    // Named constraints keep errors on the caller's setField(), before expanding pairs.
    template<class... Arguments>
    concept EvenNumberOfFieldArguments = (sizeof...(Arguments) % 2 == 0);

    template<class... Arguments>
    struct FieldNameTypes : std::false_type {};

    template<>
    struct FieldNameTypes<> : std::true_type {};

    template<class Name, class Value, class... Remaining>
    struct FieldNameTypes<Name, Value, Remaining...>
      : std::bool_constant<std::convertible_to<Name, std::string> &&
                           FieldNameTypes<Remaining...>::value> {};

    template<class... Arguments>
    concept FieldNamesAreStrings = FieldNameTypes<Arguments...>::value;
  }

  /// A copy of a message definition, its populated FieldValues and sender/receiver IDs.
  /// Create from a dictionary definition, populate fields, then send through a link/transport.
  class Message {
  public:
    using SenderId = std::variant<std::string, uint8_t>;

    explicit Message();
    explicit Message(const MessageDefinition &def);

    /// Validate and convert before inserting/replacing a field; no default value is created.
    template<class ValueType>
    void addField(const std::string &name, ValueType value)
    {
      setField(name, std::move(value));
    }

    /// Set or replace a field and return this message for sending, without copying.
    /// The returned reference is valid while this message lives.
    /// Invalid input leaves the previous value unchanged.
    template<class ValueType>
    const Message &setField(const std::string &name, ValueType value)
    {
      const auto &field = def.getField(name);
      fieldValues.insert_or_assign(name, FieldValue(field, value));
      return *this;
    }

    /// Apply multiple name/value pairs from left to right, using the same checks.
    /// Odd argument counts and non-string names are rejected at the call site.
    /// Return this message by const reference after all pairs succeed.
    /// If a pair throws, earlier pairs remain applied; the failing and subsequent
    /// pairs are unchanged.
    template<class ValueType, class... Remaining>
      requires (sizeof...(Remaining) > 0) &&
               detail::EvenNumberOfFieldArguments<Remaining...> &&
               detail::FieldNamesAreStrings<Remaining...>
    const Message &setField(const std::string &name, ValueType value,
                            Remaining&&... remaining)
    {
      setField(name, std::move(value));
      return setField(std::forward<Remaining>(remaining)...);
    }

    /// Exact-type reads. Missing definitions/values retain their distinct exceptions.
    template<class ValueType>
    void getField(const std::string &name, ValueType &value) const
    {
      fieldWithValue(name).getValue(value);
    }

    template<class ValueType>
    void getField(size_t index, ValueType &value) const
    {
      getField(def.getField(index).getName(), value);
    }

    /// Return a scalar, string or container instead of taking an output parameter.
    template<class ValueType>
    [[nodiscard]] ValueType getField(const std::string &name) const
    {
      return fieldWithValue(name).getValue<ValueType>();
    }

    template<class ValueType>
    [[nodiscard]] ValueType getField(size_t index) const
    {
      return getField<ValueType>(def.getField(index).getName());
    }

    /// Request an explicit checked numeric conversion instead of an exact-type read.
    template<Arithmetic ValueType>
    [[nodiscard]] ValueType getFieldAs(const std::string &name) const
    {
      return fieldWithValue(name).getValueAs<ValueType>();
    }

    template<Arithmetic ValueType>
    [[nodiscard]] ValueType getFieldAs(size_t index) const
    {
      return getFieldAs<ValueType>(def.getField(index).getName());
    }

    /// Read the XML-selected variant without copying. Do not retain the reference
    /// beyond destruction/assignment of this message or replacement of this field.
    [[nodiscard]] const FieldValue::Storage &getField(const std::string &name) const
    {
      return fieldWithValue(name).getValue();
    }

    [[nodiscard]] const FieldValue::Storage &getField(size_t index) const
    {
      return getField(def.getField(index).getName());
    }

    /// Binary codec wrappers; offset advances only after a complete field is stored.
    void addFieldFromBuffer(size_t index, const BytesBuffer &buffer, size_t &offset);
    void addFieldFromBuffer(size_t index, std::span<const uint8_t> buffer, size_t &offset);
    size_t addFieldToBuffer(size_t index, BytesBuffer &buffer) const;

    /// Same missing-field/value exceptions as getField; references belong to this message.
    [[nodiscard]] const FieldValue &getRawValue(size_t index) const;
    [[nodiscard]] const FieldValue &getRawValue(const std::string &name) const;
    [[nodiscard]] size_t getNbValues() const;
    [[nodiscard]] const MessageDefinition &getDefinition() const;
    [[nodiscard]] std::string toString() const;

    const SenderId &getSenderId() const;
    uint8_t getReceiverId() const;
    uint8_t getComponentId() const;
    uint8_t getClassId() const;
    void setSenderId(const SenderId &senderId);
    void setReceiverId(uint8_t receiverId);
    void setComponentId(uint8_t componentId);

    /// Numeric header IDs use the same checked conversion as numeric fields.
    /// String sender names remain available for Ivy through the SenderId overload.
    template<Arithmetic ValueType>
    void setSenderId(ValueType senderId)
    {
      setSenderId(SenderId{detail::checkedNumber<uint8_t>(senderId, "sender_id")});
    }

    template<Arithmetic ValueType> requires (!std::same_as<ValueType, uint8_t>)
    void setReceiverId(ValueType receiverId)
    {
      setReceiverId(detail::checkedNumber<uint8_t>(receiverId, "receiver_id"));
    }

    template<Arithmetic ValueType> requires (!std::same_as<ValueType, uint8_t>)
    void setComponentId(ValueType componentId)
    {
      setComponentId(detail::checkedNumber<uint8_t>(componentId, "component_id"));
    }

    /// Binary payload size; all fields must have values.
    size_t getByteSize() const;

  private:
    const FieldValue &fieldWithValue(const std::string &name) const;

    MessageDefinition def;
    std::map<std::string, FieldValue> fieldValues;
    SenderId sender_id = uint8_t{0};
    uint8_t receiver_id = 0;
    uint8_t component_id = 0;
  };
}
#endif // PPRZLINKCPP_MESSAGE_H

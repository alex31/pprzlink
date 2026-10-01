// SPDX-License-Identifier: LGPL-3.0-or-later
#include "TestSupport.h"
#include <pprzlink/Message.h>
#include <pprzlink/PprzFrameCodec.h>
#include <cmath>
#include <iostream>
#include <limits>

using namespace pprzlink;

namespace {
  template<class... Arguments>
  concept CanSetFields = requires(Message &message, Arguments&&... arguments) {
    message.setField(std::forward<Arguments>(arguments)...);
  };

  static_assert(CanSetFields<const char*, int>);
  static_assert(CanSetFields<const char*, int, std::string, double>);
  static_assert(CanSetFields<const char*, int, const char*, int, const char*, float>);
  static_assert(std::same_as<decltype(std::declval<Message&>().setField("index", 7)), const Message&>);
  static_assert(std::same_as<decltype(std::declval<Message&>().setField("index", 7, "ac_id", 42)), const Message&>);
  static_assert(!CanSetFields<>);
  static_assert(!CanSetFields<const char*>);
  static_assert(!CanSetFields<const char*, int, const char*>);
  static_assert(!CanSetFields<const char*, int, const char*, int, const char*>);
  static_assert(!CanSetFields<int, int>);
  static_assert(!CanSetFields<const char*, int, int, double>);
  static_assert(!CanSetFields<const char*, int, const char*, int, bool, float>);
  static_assert(!CanSetFields<const char*, int, const char*, int, const char*, float, int, double>);

  void testMultipleFields()
  {
    tinyxml2::XMLDocument xml;
    require(xml.Parse(R"(<message name="VALUES" id="1">
      <field name="index" type="uint8"/>
      <field name="ac_id" type="uint8"/>
      <field name="value" type="float"/>
      <field name="samples" type="int16[]"/>
      <field name="label" type="string"/>
    </message>)") == tinyxml2::XML_SUCCESS, "Multiple-field fixture parses");
    const MessageDefinition definition(xml.RootElement(), 2);
    const std::vector<int> samples{-1, 2, 300};
    const std::string aircraftField = "ac_id";

    Message individual(definition);
    individual.setField("index", 7);
    individual.setField(aircraftField, 42);
    individual.setField("value", 12.5);
    individual.setField("samples", samples);
    individual.setField("label", "settings");

    Message grouped(definition);
    const auto &configured = grouped.setField("label", "settings", "samples", samples, "value", 12.5,
                                              aircraftField, 42, "index", 7);
    require(&configured == &grouped, "Grouped setter returns the original message by reference");
    require(encodePprzFrame(configured) == encodePprzFrame(individual),
            "Grouped scalar, array and string fields preserve the existing binary encoding and XML order");

    require(&grouped.setField("index", 8) == &grouped,
            "Single-field setter returns the original message by reference");
    grouped.setField("index", 8, "ac_id", 43);
    require(grouped.getField<uint8_t>("index") == 8 && grouped.getField<uint8_t>("ac_id") == 43 &&
            grouped.getField<float>("value") == 12.5f, "Two pairs update only the requested fields");

    expectException<field_conversion_error>([&] {
      grouped.setField("value", 1.5, "index", 300, "ac_id", 44);
    });
    require(grouped.getField<float>("value") == 1.5f && grouped.getField<uint8_t>("index") == 8 &&
            grouped.getField<uint8_t>("ac_id") == 43,
            "A rejected pair preserves its old value, keeps earlier updates and stops later updates");

    expectException<no_such_field>([&] {
      grouped.setField("index", 9, "unknown", 1, "ac_id", 44);
    });
    require(grouped.getField<uint8_t>("index") == 9 && grouped.getField<uint8_t>("ac_id") == 43,
            "Unknown names use the existing XML checks and stop subsequent pairs");

    grouped.setField("index", 10, "index", 11);
    require(grouped.getField<uint8_t>("index") == 11, "Repeated names use the last value");
  }
}

int main()
{
  try {
    testMultipleFields();
    const MessageField byte("aircraft", "uint8");
    for (int invalid : {-1, 256, 300}) {
      const auto error = expectException<field_conversion_error>([&] { FieldValue ignored(byte, invalid); });
      require(error.find("aircraft") != std::string::npos && error.find("uint8") != std::string::npos,
              "Conversion error must identify the field and target type");
    }
    require(FieldValue(byte, 255).getValue<uint8_t>() == 255, "Inclusive integer upper bound");
    require(FieldValue(byte, true).getValue<uint8_t>() == 1, "Bool remains valid numeric input");
    expectException<field_conversion_error>([&] { FieldValue ignored(byte, 3.5); });
    expectException<field_conversion_error>([&] { FieldValue ignored(byte, std::numeric_limits<double>::infinity()); });
    expectException<field_conversion_error>([&] { FieldValue ignored(byte, std::numeric_limits<double>::quiet_NaN()); });
    require(FieldValue(byte, 3.0).getValue<uint8_t>() == 3, "Exact floating-point integer conversion");

    const MessageField signedWide("signed", "int64"), unsignedWide("unsigned", "uint64");
    require(FieldValue(signedWide, std::numeric_limits<int64_t>::min()).getValue<int64_t>() ==
            std::numeric_limits<int64_t>::min(), "INT64_MIN is preserved");
    require(FieldValue(unsignedWide, std::numeric_limits<uint64_t>::max()).getValue<uint64_t>() ==
            std::numeric_limits<uint64_t>::max(), "UINT64_MAX is preserved");
    expectException<field_conversion_error>([&] { FieldValue ignored(signedWide, uint64_t{1} << 63); });
    expectException<field_conversion_error>([&] { FieldValue ignored(unsignedWide, std::ldexp(1.0, 64)); });
    expectException<field_conversion_error>([&] { FieldValue ignored(signedWide, std::ldexp(1.0, 63)); });
    require(FieldValue(signedWide, -std::ldexp(1.0, 63)).getValue<int64_t>() ==
            std::numeric_limits<int64_t>::min(), "Floating-point lower bound is representable");
    const auto largestDoubleBelowUint64Limit = std::nextafter(std::ldexp(1.0, 64), 0.0);
    require(FieldValue(unsignedWide, largestDoubleBelowUint64Limit).getValue<uint64_t>() ==
            std::numeric_limits<uint64_t>::max() - 2047, "Safe conversion below 2^64");

    const MessageField real("altitude", "float");
    expectException<field_conversion_error>([&] { FieldValue ignored(real, std::numeric_limits<double>::max()); });
    const FieldValue altitude(real, 12.5f);
    require(altitude.getValueAs<double>() == 12.5, "Explicit widening read");
    const auto mismatch = expectException<std::bad_variant_access>([&] { (void)altitude.getValue<double>(); });
    require(mismatch.find("altitude") != std::string::npos && mismatch.find("float") != std::string::npos &&
            mismatch.find("double") != std::string::npos, "Strict getter supplies context and remains catch-compatible");
    expectException<field_conversion_error>([&] { (void)altitude.getValueAs<int>(); });
    expectException<field_type_mismatch>([&] {
      (void)FieldValue(MessageField("text", "string"), "12").getValueAs<int>();
    });
    require(std::isnan(FieldValue(real, std::numeric_limits<float>::quiet_NaN()).getValue<float>()),
            "Same-type floating-point values retain protocol support for NaN");
    expectException<field_conversion_error>([&] {
      FieldValue ignored(MessageField("array", "uint8[]"), std::vector<int>{1, 300, 2});
    });
    expectException<field_conversion_error>([&] {
      FieldValue ignored(MessageField("array", "int16[]"), std::vector<double>{1, 2.5});
    });

    tinyxml2::XMLDocument xml;
    xml.Parse("<message name='COMMAND' id='1'><field name='ac_id' type='uint8'/><field name='value' type='float'/></message>");
    Message command(MessageDefinition(xml.RootElement(), 2));
    command.setField("ac_id", 42);
    command.setSenderId(42);
    command.setReceiverId(255);
    command.setComponentId(7);
    expectException<field_conversion_error>([&] { command.setSenderId(300); });
    expectException<field_conversion_error>([&] { command.setReceiverId(-1); });
    expectException<field_conversion_error>([&] { command.setComponentId(256); });
    require(std::get<uint8_t>(command.getSenderId()) == 42 && command.getReceiverId() == 255 &&
            command.getComponentId() == 7, "Header ID conversions reject overflow without changing values");
    command.addField("value", 12.5); // Legacy setter obeys the same range checks.
    expectException<field_conversion_error>([&] { command.setField("ac_id", 300); });
    require(command.getField<uint8_t>("ac_id") == 42, "Invalid replacement leaves the previous value intact");
    require(command.getFieldAs<double>("value") == 12.5 && command.getFieldAs<int>(size_t{0}) == 42,
            "Converted message reads by name and index");
    return 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}

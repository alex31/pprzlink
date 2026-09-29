// SPDX-License-Identifier: LGPL-3.0-or-later
#include "TestSupport.h"
#include <pprzlink/Message.h>
#include <cmath>
#include <iostream>
#include <limits>

using namespace pprzlink;

int main()
{
  try {
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

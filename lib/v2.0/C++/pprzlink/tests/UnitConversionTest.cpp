// SPDX-License-Identifier: LGPL-3.0-or-later
#include "TestSupport.h"
#include <pprzlink/Message.h>
#include <pprzlink/MessageDictionary.h>
#include <pprzlink/PprzFrameCodec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

using namespace pprzlink;

namespace {
  void close(double actual, double expected, double tolerance = 1e-10)
  {
    require(std::abs(actual - expected) <= tolerance * std::max(1.0, std::abs(expected)),
            std::format("Expected {}, got {}", expected, actual));
  }

  MessageDictionary dictionary(const char *fields, const char *message = "UNITS")
  {
    tinyxml2::XMLDocument xml;
    const auto text = std::format("<protocol><msg_class name='test' id='1'>"
      "<message name='{}' id='8'>{}</message></msg_class></protocol>", message, fields);
    require(xml.Parse(text.c_str()) == tinyxml2::XML_SUCCESS, "Unit fixture parses");
    return MessageDictionary(xml.RootElement());
  }

  void testMetadata()
  {
    const auto dict = dictionary(R"xml(
      <field name="alt" type="int32" unit="mm" alt_unit="m" format="%.1f" FORMAT="%.2f"/>
      <field NAME="fixed" TYPE="int16" UNIT="2^8m" ALT_UNIT="m" ALT_UNIT_COEF="0.01"/>
      <field name="alt_only" type="int32" alt_unit="m" alt_unit_coef="1e-3"/>
      <field name="orphan" type="int16[]" alt_unit_coef="1e-3"/>
      <field name="missing" type="float"/>
      <field name="unknown" type="float" unit="not_a_unit"/>
      <field name="position" type="float[3]" unit="SI (m or deg)"/>
    )xml");
    Message message(dict.getDefinition("UNITS"));
    const auto &alt = message.getFieldDefinition("alt");
    require(alt.getUnit() == "mm" && alt.getAltUnit() == "m" && !alt.getAltUnitCoef() &&
            alt.getSIUnit() == "m" && alt.getFormat() == "%.1f" && alt.canConvertSI(),
            "Original strings, optional coefficient and SI unit are independently accessible");
    require(&message.getFieldDefinition(size_t{0}) == &alt, "Metadata index and name access agree");
    expectException<field_has_no_value>([&] { (void)message.getFieldSI("alt"); });
    expectException<no_such_field>([&] { (void)message.getFieldDefinition("absent"); });
    const auto &fixed = message.getFieldDefinition("fixed");
    require(fixed.getAltUnitCoef() == 0.01, "Uppercase unit attributes and explicit coefficient");
    require(fixed.fromSI<int16_t>(1.0) == 100, "Explicit XML coefficient overrides alias scale");
    close(fixed.toSI(int16_t{100}), 1.0);
    const auto &alternative = message.getFieldDefinition("alt_only");
    require(alternative.getUnit().empty() && alternative.getAltUnitCoef() == 0.001 &&
            alternative.canConvertSI(), "Alternative unit plus coefficient fully describes raw scaling");
    require(alternative.fromSI<int32_t>(1.234) == 1234, "Scientific XML coefficient is applied once");
    const auto &orphan = message.getFieldDefinition("orphan");
    require(orphan.getAltUnitCoef() == 0.001 && !orphan.canConvertSI(),
            "Legacy coefficients without units remain readable and do not invent a physical unit");
    message.setField("orphan", std::vector<int16_t>{1, 2});
    for (const char *name : {"missing", "unknown", "position"}) {
      const auto &field = message.getFieldDefinition(name);
      require(!field.canConvertSI() && field.getSIUnit().empty(), "Unsupported metadata stays loadable");
    }
    message.setField("missing", 12.0f);
    expectException<field_unit_error>([&] { (void)message.getFieldSI("missing"); });
    const auto error = expectException<field_unit_error>([&] { message.setFieldSI("unknown", 1.0); });
    require(error.find("UNITS") != std::string::npos && error.find("unknown") != std::string::npos &&
            error.find("not_a_unit") != std::string::npos, "Unit errors identify message, field and XML spelling");

    for (const char *coefficient : {"", "0", "nan", "inf", "1x", "1e10000"}) {
      const auto field = std::format("<field name='x' type='float' alt_unit='m' alt_unit_coef='{}'/>", coefficient);
      expectException<bad_message_file>([&] { (void)dictionary(field.c_str()); });
    }
    const auto contradictory = dictionary("<field name='x' type='float' unit='m' alt_unit='s' alt_unit_coef='2'/>");
    require(!contradictory.getDefinition("UNITS").getField("x").canConvertSI(),
            "Dimensionally inconsistent explicit alternative is not accepted");
  }

  void testUnitsAndAliases()
  {
    const auto dict = dictionary(R"(
      <field name="lat" type="int32" unit="1e7deg" alt_unit="deg" alt_unit_coef="0.0000001"/>
      <field name="fixed" type="int32" unit="2^8m"/>
      <field name="approx" type="int32" unit="2^8m" alt_unit="m" alt_unit_coef="0.0039063"/>
      <field name="angle" type="int16" unit="1e2_deg"/>
      <field name="acceleration" type="float" unit="m/s-2"/>
      <field name="magnetic" type="double" unit="mGauss"/>
      <field name="ratio" type="uint8" unit="%"/>
      <field name="none" type="double" unit="none"/>
      <field name="temperature" type="float" unit="deg_celsius"/>
      <field name="deci_temperature" type="int16" unit="10x_deg_celsius"/>
      <field name="fahrenheit" type="double" unit="degF"/>
      <field name="charge" type="double" unit="C"/>
      <field name="amp_hours" type="double" unit="Ah"/>
      <field name="pressure" type="float" unit="hPa"/>
      <field name="unknown_control" type="uint16" unit="adc" alt_unit="m" alt_unit_coef="1"/>
    )");
    Message message(dict.getDefinition("UNITS"));
    message.setFieldSI("lat", std::numbers::pi / 2);
    require(message.getField<int32_t>("lat") == 900000000, "Latitude SI input is radians, not degrees");
    close(message.getFieldSI("lat"), std::numbers::pi / 2);
    message.setFieldSI("fixed", 1.0);
    require(message.getField<int32_t>("fixed") == 256, "Fixed point alias uses the reciprocal encoding scale");
    close(message.getFieldSI("fixed"), 1.0);
    message.setField("approx", int32_t{256});
    close(message.getFieldSI("approx"), 256 * 0.0039063);
    message.setFieldSI("angle", std::numbers::pi / 4);
    require(message.getField<int16_t>("angle") == 4500, "Hundredths of degree alias");
    message.setFieldSI("acceleration", 9.80665);
    require(message.getFieldDefinition("acceleration").getSIUnit() == "m/s^2", "Legacy acceleration spelling");
    close(message.getFieldSI("acceleration"), 9.80665, 1e-6);
    message.setField("magnetic", 1000.0);
    close(message.getFieldSI("magnetic"), 1e-4);
    message.setFieldSI("ratio", 0.425);
    require(message.getField<uint8_t>("ratio") == 43, "Percent SI input is a dimensionless ratio, rounded for storage");
    close(message.getFieldSI("ratio"), 0.43);
    message.setFieldSI("none", 12.0);
    require(message.getField<double>("none") == 12.0 && message.getFieldDefinition("none").getSIUnit() == "1",
            "none is dimensionless, not the nano prefix followed by one");
    message.setFieldSI("temperature", 293.15);
    require(message.getField<float>("temperature") == 20.0f, "SI kelvins are converted with the Celsius offset");
    close(message.getFieldSI("temperature"), 293.15);
    message.setFieldSI("deci_temperature", 293.15);
    require(message.getField<int16_t>("deci_temperature") == 200, "Temperature number scale precedes the Celsius offset");
    close(message.getFieldSI("deci_temperature"), 293.15);
    message.setField("fahrenheit", 32.0);
    close(message.getFieldSI("fahrenheit"), 273.15);
    message.setFieldSI("charge", 12.0);
    require(message.getFieldDefinition("charge").getSIUnit() == "C" && message.getField<double>("charge") == 12.0,
            "Scoped Celsius rules preserve an unrelated field's coulomb unit");
    message.setFieldSI("amp_hours", 3600.0);
    close(message.getField<double>("amp_hours"), 1.0);
    message.setFieldSI("pressure", 101325.0);
    close(message.getField<float>("pressure"), 1013.25);
    require(!message.getFieldDefinition("unknown_control").canConvertSI(), "Explicitly unsupported calibration is not guessed");

    const auto esc = dictionary("<field name='temperature' type='float' unit='C'/>", "ESC");
    Message legacy(esc.getDefinition("ESC"));
    legacy.setFieldSI("temperature", 293.15);
    require(legacy.getField<float>("temperature") == 20.0f &&
            legacy.getFieldDefinition("temperature").getSIUnit() == "K", "Scoped Celsius alias applies to legacy ESC field");
    const auto baro = dictionary("<field name='pressure' type='uint32' unit='P'/>", "BARO_MS5534A");
    Message pressure(baro.getDefinition("BARO_MS5534A"));
    pressure.setFieldSI("pressure", 101325.0);
    require(pressure.getField<uint32_t>("pressure") == 101325, "Scoped P alias denotes legacy pressure, not poise");
  }

  template<class T> void testInteger(const char *type)
  {
    const MessageField field("x", type, {}, "m");
    require(field.fromSI<T>(9.49) == T{9} && field.fromSI<T>(9.5) == T{10}, "Integer nearest rounding");
    if constexpr (std::is_signed_v<T>) require(field.fromSI<T>(-9.5) == T{-10}, "Negative ties round away from zero");
    else expectException<field_conversion_error>([&] { (void)field.fromSI<T>(-1.0); });
    close(field.toSI(T{9}), 9.0);
    expectException<field_conversion_error>([&] { (void)field.fromSI<T>(std::numeric_limits<double>::infinity()); });
    expectException<field_conversion_error>([&] { (void)field.fromSI<T>(std::numeric_limits<double>::quiet_NaN()); });
  }

  void testTypesRoundingAndFailures()
  {
    testInteger<int8_t>("int8"); testInteger<uint8_t>("uint8");
    testInteger<int16_t>("int16"); testInteger<uint16_t>("uint16");
    testInteger<int32_t>("int32"); testInteger<uint32_t>("uint32");
    testInteger<int64_t>("int64"); testInteger<uint64_t>("uint64");
    const MessageField wide("wide", "uint64", {}, "m");
    expectException<field_conversion_error>([&] { (void)wide.fromSI<uint64_t>(std::ldexp(1.0, 64)); });
    expectException<field_type_mismatch>([&] { (void)wide.fromSI<double>(1.0); });
    expectException<field_type_mismatch>([&] { (void)wide.toSI(uint32_t{1}); });

    const auto dict = dictionary("<field name='alt' type='int8' unit='mm'/><field name='real' type='float' unit='m'/>");
    Message message(dict.getDefinition("UNITS"));
    require(&message.setFieldSI("alt", 0.1266) == &message && message.getField<int8_t>("alt") == 127,
            "Setter returns the same message and stores the nearest XML integer");
    close(message.getFieldSI(size_t{0}), 0.127);
    double output = -1;
    message.getFieldSI("alt", output);
    close(output, 0.127);
    for (double invalid : {0.1276, -0.1286, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
      expectException<field_conversion_error>([&] { message.setFieldSI("alt", invalid); });
      require(message.getField<int8_t>("alt") == 127, "A rejected SI scalar preserves the previous raw value");
    }
    expectException<field_conversion_error>([&] { message.setField("alt", 12.5); });
    require(message.getFieldAs<double>("alt") == 127.0, "Existing numeric getter remains in XML units");
    expectException<no_such_field>([&] { message.setFieldSI("absent", 1.0); });
    expectException<field_has_no_value>([&] { message.getFieldSI("real", output); });
    close(output, 0.127);
    message.setFieldSI("real", 1.25);
    require(message.getField<float>("real") == 1.25f, "Floating-point destination retains XML float type");
    expectException<field_conversion_error>([&] { message.setFieldSI("real", std::numeric_limits<double>::max()); });
    message.setField("real", std::numeric_limits<float>::quiet_NaN());
    require(std::isnan(message.getFieldSI("real")), "A protocol NaN remains a missing/non-finite SI measurement");
    const MessageField tiny("tiny", "float", {}, "1e-300m");
    expectException<field_conversion_error>([&] { (void)tiny.fromSI<float>(1e100); });
    const MessageField huge("huge", "double", {}, "1e300m");
    expectException<field_conversion_error>([&] { (void)huge.toSI(std::numeric_limits<double>::max()); });
  }

  void testArraysAndWireCompatibility()
  {
    const auto dict = dictionary(R"(
      <field name="position" type="int16[3]" unit="cm"/>
      <field name="samples" type="float[]" unit="m/s"/>
      <field name="unsupported" type="int16[]" unit="adc"/>
      <field name="scalar" type="float" unit="m"/>
      <field name="label" type="string"/>
      <field name="labels" type="string[]"/>
    )");
    Message message(dict.getDefinition("UNITS"));
    expectException<field_has_no_value>([&] { (void)message.getFieldSIArray("position"); });
    expectException<no_such_field>([&] { (void)message.getFieldSIArray("absent"); });
    expectException<no_such_field>([&] { (void)message.getFieldSIArray(size_t{6}); });
    const std::array<double, 3> position{1.234, -2.345, 0.0};
    require(&message.setFieldSIArray("position", position) == &message,
            "SI array setter returns the same message");
    require(message.getField<std::vector<int16_t>>("position") == std::vector<int16_t>({123, -235, 0}),
            "SI arrays use the XML element type and round every integer");
    auto output = message.getFieldSIArray(size_t{0});
    require(output.size() == 3 && output == message.getFieldSIArray("position"),
            "SI array getters by name and index return the same owned values");
    close(output.at(0), 1.23); close(output.at(1), -2.35); close(output.at(2), 0.0);
    std::vector<double> legacy;
    message.getFieldSI("position", legacy);
    require(legacy == output, "Legacy SI array getter by name forwards to the returning getter");
    legacy.clear();
    message.getFieldSI(size_t{0}, legacy);
    require(legacy == output, "Legacy SI array getter by index forwards to the returning getter");
    output.front() = 42.0;
    require(message.getField<std::vector<int16_t>>("position").front() == 123,
            "Returned SI array owns its elements independently of the message");
    expectException<field_conversion_error>([&] {
      message.setFieldSIArray("position", std::array<double, 3>{1.0, 1000.0, 3.0});
    });
    require(message.getField<std::vector<int16_t>>("position") == std::vector<int16_t>({123, -235, 0}),
            "A failed element preserves the whole previous array");
    expectException<std::length_error>([&] { message.setFieldSIArray("position", std::array<double, 2>{1.0, 2.0}); });
    expectException<field_type_mismatch>([&] { message.setFieldSI("position", 1.0); });
    expectException<field_type_mismatch>([&] { (void)message.getFieldSI("position"); });
    expectException<field_type_mismatch>([&] { message.setFieldSIArray("scalar", position); });
    message.setFieldSI("scalar", 1.0);
    expectException<field_type_mismatch>([&] { (void)message.getFieldSIArray("scalar"); });
    expectException<field_type_mismatch>([&] { message.setFieldSI("label", 1.0); });
    expectException<field_type_mismatch>([&] { message.setFieldSIArray("labels", std::span<const double>{}); });
    message.setField("labels", std::vector<std::string>{});
    expectException<field_type_mismatch>([&] { (void)message.getFieldSIArray("labels"); });
    const std::vector<double> samples{1.25, -2.5};
    message.setFieldSIArray("samples", samples);
    require(message.getFieldSIArray("samples") == samples, "SI array setter accepts vector storage");
    const double sample_buffer[]{0.0, 1.25, -2.5, 0.0};
    message.setFieldSIArray("samples", std::span(sample_buffer).subspan(1, 2));
    require(message.getFieldSIArray("samples") == samples, "SI array setter accepts a span over part of a C array");
    message.setFieldSI("samples", samples);
    require(message.getFieldSIArray("samples") == samples, "Legacy SI array setter forwards to the named array setter");
    message.setFieldSIArray("samples", std::span<const double>{});
    output = message.getFieldSIArray("samples");
    require(output.empty(), "Empty supported dynamic array round trip");
    message.setField("unsupported", std::vector<int16_t>{});
    output = {42.0};
    expectException<field_unit_error>([&] { output = message.getFieldSIArray("unsupported"); });
    require(output == std::vector<double>{42.0}, "Failed SI array read does not assign a partial result");
    expectException<field_unit_error>([&] { message.getFieldSI("unsupported", output); });
    require(output == std::vector<double>{42.0}, "Legacy SI array read still preserves output on failure");
    expectException<field_unit_error>([&] { message.setFieldSIArray("unsupported", std::span<const double>{}); });

    const auto wire = dictionary(R"(
      <field name="alt" type="int32" unit="mm" alt_unit="m"/>
      <field name="speed" type="uint16" unit="cm/s"/>
      <field name="course" type="int16" unit="decideg"/>
      <field name="temperature" type="float" unit="deg_celsius"/>
    )", "WIRE");
    Message native(wire.getDefinition("WIRE")), si(wire.getDefinition("WIRE"));
    native.setField("alt", int32_t{123456}, "speed", uint16_t{1234}, "course", int16_t{900}, "temperature", 20.0f);
    si.setFieldSI("alt", 123.456);
    si.setFieldSI("speed", 12.34);
    si.setFieldSI("course", std::numbers::pi / 2);
    si.setFieldSI("temperature", 293.15);
    const auto frame = encodePprzFrame(si);
    require(frame == encodePprzFrame(native), "Explicit SI setters preserve native protocol bytes");
    PprzFrameDecoder decoder(wire);
    decoder.pushBytes(frame);
    const auto received = decoder.nextMessage();
    require(received.has_value(), "SI-written frame decodes normally");
    require(received->getField<int32_t>("alt") == 123456, "Decoded storage remains the exact XML type");
    close(received->getFieldSI("temperature"), 293.15);
  }

  void testRepositoryXml(const char *path)
  {
    const MessageDictionary dict{std::string(path)};
    Message gps(dict.getDefinition("GPS"));
    gps.setFieldSI("alt", 123.456);
    gps.setFieldSI("speed", 12.34);
    require(gps.getField<int32_t>("alt") == 123456 && gps.getField<uint16_t>("speed") == 1234,
            "Real GPS metadata selects its native units and integer types");
    const auto &hybrid = dict.getDefinition("HYBRID_GUIDANCE").getField("pos_x");
    require(hybrid.canConvertSI() && hybrid.getUnit().empty(), "Real fixed-point field resolves from alternative metadata");
    const auto &orphan = dict.getDefinition("MPPT").getField("values");
    require(orphan.getAltUnitCoef().has_value() && !orphan.canConvertSI(), "Incomplete legacy metadata still loads");
    require(!dict.getDefinition("GROUND_REF").getField("pos").canConvertSI(), "Mixed-frame position requires application context");
    for (const auto &[name, field] : {std::pair{"ESC", "temperature"}, {"BATTERY_MONITOR", "bus_temp"},
                                    {"ENGINE_STATUS", "temp"}, {"IMCU_REMOTE_BARO", "pitot_temp"}}) {
      require(dict.getDefinition(name).getField(field).getSIUnit() == "K", "Repository legacy C temperatures use scoped aliases");
    }
  }
}

int main(int argc, char **argv)
{
  try {
    testMetadata();
    testUnitsAndAliases();
    testTypesRoundingAndFailures();
    testArraysAndWireCompatibility();
    if (argc > 1) testRepositoryXml(argv[1]);
    std::cout << "SI units, editable aliases, rounding, failures, native protocol bytes and repository XML passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

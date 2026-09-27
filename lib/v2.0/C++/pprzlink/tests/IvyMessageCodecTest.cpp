#include "TestSupport.h"
#include <pprzlink/IvyMessageCodec.h>
#include <pprzlink/MessageDictionary.h>
#include <array>
#include <iostream>

using namespace pprzlink;

int main()
{
  try {
    tinyxml2::XMLDocument xml;
    xml.Parse(R"(<protocol><msg_class name="test" id="1">
      <message name="M.1" id="1"><field name="n" type="uint8"/>
        <field name="values" type="int16[2]"/><field name="text" type="string"/>
        <field name="f" type="float"/></message>
    </msg_class></protocol>)");
    MessageDictionary dictionary(xml.RootElement());
    const auto &definition = dictionary.getDefinition("M.1");
    const auto msg = ivy_codec::parseMessageBody(definition, "42", "M.1 +255 -1,2 \"hello world\" 1.25e2");
    require(msg.getField<uint8_t>("n") == 255 && msg.getField<float>("f") == 125.f, "Runtime numeric conversions");
    require(msg.getField<std::string>("text") == "hello world", "Quoted text");
    require(msg.getField<std::vector<int16_t>>("values") == std::vector<int16_t>({-1, 2}), "Fixed array");
    const auto wire = ivy_codec::serializeMessage(msg);
    require(wire == "42 M.1 255 -1,2 \"hello world\" 125.000000", "Unchanged Ivy wire format");
    const auto decoded = ivy_codec::parseMessageBody(definition, "42", std::string_view(wire).substr(3));
    require(decoded.toString() == msg.toString(), "Standalone codec round trip");
    for (const auto *body : {"MX1 255 -1,2 text 1", "M.1 256 -1,2 text 1",
                             "M.1 1 -1 text 1", "M.1 1 -1,2, text 1", "M.1 1 -1,2 text nan"}) {
      expectException<wrong_message_format>([&] { (void)ivy_codec::parseMessageBody(definition, "42", body); });
    }
    const std::array<std::string_view, 4> captures{"255", "-1,2", "\"\"", "1e1000"};
    expectException<wrong_message_format>([&] { (void)ivy_codec::parseFields(definition, "42", captures); });
    expectException<wrong_message_format>([&] { (void)ivy_codec::parseFields(definition, "42", {}); });
    std::cout << "Standalone Ivy codec, wire format and invalid input passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

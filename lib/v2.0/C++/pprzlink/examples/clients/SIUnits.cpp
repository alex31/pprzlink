// SPDX-License-Identifier: LGPL-3.0-or-later
// Offline example: public SI access, XML metadata and unchanged PPRZ framing.
#include <pprzlink/MessageDictionary.h>
#include <pprzlink/Message.h>
#include <pprzlink/PprzFrameCodec.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

int main()
{
  try {
    tinyxml2::XMLDocument xml;
    if (xml.Parse(R"xml(<protocol><msg_class name="example" id="1">
      <message name="SI_EXAMPLE" id="1">
        <field name="altitude" type="int32" unit="mm" alt_unit="m"/>
        <field name="temperature" type="float" unit="deg_celsius"/>
        <field name="position" type="int16[3]" unit="cm"/>
      </message>
    </msg_class></protocol>)xml") != tinyxml2::XML_SUCCESS) {
      throw std::runtime_error("Invalid example XML");
    }
    const pprzlink::MessageDictionary dictionary(xml.RootElement());
    pprzlink::Message message(dictionary.getDefinition("SI_EXAMPLE"));
    message.setFieldSI("altitude", 1.23456); // Nearest XML integer: 1235 mm.
    message.setFieldSI("temperature", 293.15); // XML float: 20 Celsius.
    message.setFieldSIArray("position", std::array<double, 3>{1.0, -2.0, 3.0});

    const auto &field = message.getFieldDefinition("altitude");
    std::cout << field.getUnit() << " -> " << field.getSIUnit() << '\n'
              << message.getField<int32_t>("altitude") << " mm = "
              << message.getFieldSI("altitude") << " m\n"
              << message.getField<float>("temperature") << " Celsius = "
              << message.getFieldSI("temperature") << " K\n";

    // Transports receive the same native XML values; units are not encoded on the wire.
    pprzlink::PprzFrameDecoder decoder(dictionary);
    decoder.pushBytes(pprzlink::encodePprzFrame(message));
    const auto received = decoder.nextMessage();
    if (!received || received->getField<int32_t>("altitude") != 1235) {
      throw std::runtime_error("Native XML storage was not preserved");
    }
    const std::vector<double> position = received->getFieldSIArray("position");
    std::cout << "Decoded position:";
    for (double metres : position) std::cout << ' ' << metres;
    std::cout << " m\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

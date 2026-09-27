#pragma once

#include <pprzlink/exceptions/pprzlink_exception.h>
#include <tinyxml2.h>
#include <charconv>
#include <format>
#include <string_view>

namespace pprzlink::detail::xml {
  inline const tinyxml2::XMLElement &element(const tinyxml2::XMLElement *value,
                                            std::string_view expected)
  {
    if (!value || value->Name() != expected) {
      throw bad_message_file(std::format("Expected <{}> element", expected));
    }
    return *value;
  }

  // Legacy XML accepts NAME/ID/TYPE as well as their lowercase spellings.
  inline std::string attribute(const tinyxml2::XMLElement &element,
                               const char *lowercase, const char *uppercase)
  {
    const char *value = element.Attribute(uppercase);
    if (!value) value = element.Attribute(lowercase);
    if (!value || !*value) {
      throw bad_message_file(std::format("<{}> at line {}: missing '{}' attribute",
                                        element.Name(), element.GetLineNum(), lowercase));
    }
    return value;
  }

  inline int id(const tinyxml2::XMLElement &element, int maximum)
  {
    const auto text = attribute(element, "id", "ID");
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 0 || value > maximum) {
      throw bad_message_file(std::format("<{}> at line {}: id '{}' must be in [0, {}]",
                                        element.Name(), element.GetLineNum(), text, maximum));
    }
    return value;
  }
}

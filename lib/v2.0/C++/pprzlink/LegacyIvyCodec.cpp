// SPDX-License-Identifier: LGPL-3.0-or-later
#include "IvyMessageCodec.h"
#include <cstdio>
#include <regex>
#include <sstream>

namespace pprzlink::ivy_codec {
  namespace {
    template<class Number>
    std::string formatNumber(Number value, const std::string &xmlFormat)
    {
      std::string format = xmlFormat;
      if (format.empty()) {
        if constexpr (std::floating_point<Number>) format = "%f";
        else if constexpr (std::same_as<Number, char>) format = "%c";
        else format = std::is_signed_v<Number> ? "%d" : "%u";
      }
      // Accept one bounded conversion only. Never pass arbitrary XML as a printf
      // format or a mismatched variadic argument (notably %n, '*', and length flags).
      static const std::regex pattern(R"(^%([-+ #0]*)([0-9]{0,3})(\.[0-9]{1,3})?[lL]?([diuoxXfFeEgGc])$)");
      std::smatch match;
      if (!std::regex_match(format, match, pattern)) {
        throw wrong_message_format("Unsupported XML number format: " + format);
      }
      const char conversion = match[4].str()[0];
      std::string checked = "%" + match[1].str() + match[2].str() + match[3].str();
      const auto print = [&](auto argument) {
        const int size = std::snprintf(nullptr, 0, checked.c_str(), argument);
        if (size < 0 || size > 4096) throw wrong_message_format("XML number format is too large");
        std::string result(static_cast<size_t>(size) + 1, '\0');
        std::snprintf(result.data(), result.size(), checked.c_str(), argument);
        result.resize(static_cast<size_t>(size));
        return result;
      };
      if constexpr (std::floating_point<Number>) {
        if (std::string_view("fFeEgG").find(conversion) == std::string_view::npos)
          throw wrong_message_format("Expected a floating-point XML format");
        checked += conversion;
        return print(static_cast<double>(value));
      } else {
        if (conversion == 'c') {
          checked += 'c';
          return print(static_cast<int>(value));
        }
        if (std::string_view("diuoxX").find(conversion) == std::string_view::npos)
          throw wrong_message_format("Expected an integer XML format");
        checked += "ll";
        checked += conversion;
        if (conversion == 'd' || conversion == 'i') return print(static_cast<long long>(value));
        return print(static_cast<unsigned long long>(value));
      }
    }

    template<class Value>
    std::string formatScalar(const Value &value, const std::string &format)
    {
      if constexpr (std::same_as<Value, std::string>) {
        return value.find(' ') == std::string::npos ? value : '"' + value + '"';
      } else {
        return formatNumber(value, format);
      }
    }

    std::string formatField(const FieldValue::Storage &storage, const std::string &format)
    {
      return std::visit([&]<class Value>(const Value &value) -> std::string {
        if constexpr (Arithmetic<Value> || std::same_as<Value, std::string>) {
          return formatScalar(value, format);
        } else {
          constexpr bool characters = std::same_as<Value, std::vector<char>>;
          std::string text = characters ? "\"" : "";
          bool first = true;
          for (const auto &element : value) {
            if (!first && !characters) text += ',';
            text += formatScalar(element, format);
            first = false;
          }
          if constexpr (characters) text += '"';
          return text;
        }
      }, storage);
    }
  }

  std::string serializeLegacyMessage(const Message &message)
  {
    std::string text = std::visit([](const auto &sender) -> std::string {
      if constexpr (std::same_as<std::decay_t<decltype(sender)>, std::string>) return sender;
      else return std::to_string(sender);
    }, message.getSenderId());
    const auto &definition = message.getDefinition();
    text += ' ' + definition.getName();
    for (size_t i = 0; i < definition.getNbFields(); ++i) {
      text += ' ' + formatField(message.getField(i), definition.getField(i).getFormat());
    }
    return text;
  }

  Message parseLegacyMessageBody(const MessageDefinition &definition, std::string_view sender,
                                std::string_view body)
  {
    std::vector<std::string_view> tokens;
    while (!body.empty()) {
      const auto start = body.find_first_not_of(" \t");
      if (start == std::string_view::npos) break;
      body.remove_prefix(start);
      if (body.front() == '"' || body.front() == '|') {
        const char delimiter = body.front();
        body.remove_prefix(1);
        const auto end = body.find(delimiter);
        if (end == std::string_view::npos) throw wrong_message_format("Unclosed Ivy string");
        tokens.push_back(body.substr(0, end));
        body.remove_prefix(end + 1);
      } else {
        const auto end = body.find_first_of(" \t");
        tokens.push_back(body.substr(0, end));
        if (end == std::string_view::npos) break;
        body.remove_prefix(end);
      }
    }
    if (tokens.empty() || tokens.front() != definition.getName())
      throw wrong_message_format("Wrong Ivy message name");
    return parseFields(definition, sender, std::span(tokens).subspan(1));
  }
}

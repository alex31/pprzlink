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

#ifndef PPRZLINKCPP_IVYMESSAGECODEC_H
#define PPRZLINKCPP_IVYMESSAGECODEC_H

#include <pprzlink/Message.h>
#include <span>
#include <string_view>

/// Runtime XML message encoding/decoding, independent of an Ivy bus or event loop.
namespace pprzlink::ivy_codec {
  std::string escapeRegexp(std::string_view text);
  std::string unquote(std::string_view text);
  /// Message name and capturing groups for XML fields, followed by the end anchor.
  std::string messageRegexp(const MessageDefinition &definition);
  /// Sender, message name and fields in Ivy wire format.
  std::string serializeMessage(const Message &message);
  /// OCaml-compatible XML display formats, including its unquoted empty strings.
  std::string serializeLegacyMessage(const Message &message);
  /// OCaml-style whitespace and quoted/pipe-delimited fields; numeric values remain checked.
  Message parseLegacyMessageBody(const MessageDefinition &definition, std::string_view sender,
                                std::string_view body);
  /// Decode captures supplied by a dynamic Ivy binding.
  Message parseFields(const MessageDefinition &definition, std::string_view sender,
                      std::span<const std::string_view> fields);
  /// Match and decode a message body (name and fields, without sender).
  Message parseMessageBody(const MessageDefinition &definition, std::string_view sender,
                           std::string_view body);
}
#endif // PPRZLINKCPP_IVYMESSAGECODEC_H

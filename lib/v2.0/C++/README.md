# PprzLink C++

The C++ library requires C++23, Ivy **3.18 or newer** with its native C++ wrapper
(`ivy-cpp`), TinyXML2, Boost (Asio and Bimap), CMake 3.20+ and pkg-config.
Ubuntu 24.04 / GCC 13 is the intended minimum platform; GCC 15 is used for the
current validation. Ubuntu 22.04 is no longer targeted.
For sharing this branch, use [Ivy 3.18.3](https://github.com/alex31/libivy-c/commit/b0bf831702c5bfd1313e83ded62eb14d17198534),
which includes the cleanup, threading and context-ownership changes validated
with this library.

The serial device remains part of both library builds. It uses
`boost::asio::io_context` (the type previously aliased as `io_service`).
Boost.System is header-only with the supported Boost versions.

## Class roles

Definitions describe what can be sent; values describe one particular message.
For example, `MessageField("altitude", "float")` describes a field, while its
`FieldValue` can hold `123.5f` in one message and `98.0f` in another.

| Class or file | Responsibility |
| --- | --- |
| [`FieldType`](pprzlink/MessageFieldTypes.h) | XML scalar/array type, such as `float`, `int16[]` or `char[5]`. |
| [`MessageField`](pprzlink/MessageField.h) | Field name and type, without a value. |
| [`MessageDefinition`](pprzlink/MessageDefinition.h) | Message name, class/message IDs and ordered field definitions. |
| [`MessageDictionary`](pprzlink/MessageDictionary.h) | Load definitions with TinyXML2 and look them up by name or IDs. |
| [`FieldValue`](pprzlink/FieldValue.h) | Field definition and actual scalar/array value in a `std::variant`. |
| [`Message`](pprzlink/Message.h) | A copy of a definition, populated field values and sender/receiver addressing. |
| [`IvyLink`](pprzlink/IvyLink.h) | Own an Ivy bus and manage subscriptions, sends and requests. |
| [`PprzTransport`](pprzlink/PprzTransport.h) | Implement `Transport` with binary PprzLink v2 framing over an owned `Device`. |
| [`XbeeTransport`](pprzlink/XbeeTransport.h) | Carry PprzLink v2 messages in XBee 802.15.4 API frames (AP=1), with radio addressing, status events and optional transmit validation. |
| [`XbeeModem`](pprzlink/XbeeModem.h) | Initialize the modem through AT commands, with guard times, checked replies and response deadlines. |
| [`BoostSerialPortDevice`](pprzlink/BoostSerialPortDevice.h) | Implement the `Device` byte-stream interface with Boost.Asio serial I/O. |
| [`BinaryCodec`](pprzlink/BinaryCodec.h), [`IvyMessageCodec`](pprzlink/IvyMessageCodec.h), [`TextCodec`](pprzlink/TextCodec.h) | Convert messages/fields to and from bytes or text; these functions perform no device or bus I/O. |

The usual path is XML → dictionary → definition → populated message → Ivy link
or binary transport. Reception reconstructs a message using the same definitions.
The dictionary must outlive an Ivy link or transport that borrows it; a Message
owns its own definition and values. The unused `Link` placeholder has been
removed: applications use `IvyLink`, `PprzTransport` or `XbeeTransport` directly.

## Build

From this directory:

```sh
# With ivy-c / ivy-c-dev installed, the system pkg-config paths are sufficient.
pkg-config --modversion ivy-cpp ivy-c

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/install
```

If Ivy is installed in a private prefix, configure its paths before building
and running the examples:

```sh
export IVY_PREFIX=/path/to/ivy-3.18.3
export PKG_CONFIG_PATH="$IVY_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$IVY_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

The build rejects Ivy versions below 3.18. Ensure pkg-config and the runtime
loader select the same installation. Instructions for building the Ivy packages
are in [Ivy's Debian README](https://github.com/alex31/libivy-c/blob/b0bf831702c5bfd1313e83ded62eb14d17198534/debian/README).
Validation results and limitations are recorded in
[VALIDATION_CLANG22.md](pprzlink/VALIDATION_CLANG22.md); its temporary paths are
historical logs from the development machine, not build prerequisites.

`make libpprzlink++` and `make install DESTDIR=/path/to/install` are also supported.
Use the same `PKG_CONFIG_PATH`; `CXX`, `CPPFLAGS`, `CXXFLAGS`, `LDFLAGS`, and
`OBJ_DIR` may be supplied to make. Both shared and static libraries are built.
Installed CMake consumers can use `find_package(pprzlink++ CONFIG REQUIRED)` and
link `pprzlink++` or `pprzlink++_static`; dependencies and C++23 propagate.

## Complete example

[`IvyRoundTrip.cpp`](pprzlink/examples/IvyRoundTrip.cpp) loads
[`messages.xml`](pprzlink/examples/messages.xml), subscribes to `EXAMPLE_ALTITUDE`,
creates a message with altitude `123.5f`, sends it and reads the received value.
It runs two Ivy links in one process, using their owned event-loop threads.

The example is built by default and exercised by CTest. From this directory,
after configuring the dependencies as above:

```sh
cmake --build build --target ivy_round_trip
./build/ivy_round_trip pprzlink/examples/messages.xml
```

Expected output:

```text
Sent: EXAMPLE_ALTITUDE [altitude=123.500000]
Received from 42: altitude = 123.5 m
```

By default it uses a local loopback bus with a process-dependent port. An optional
second argument selects an Ivy domain, for example `127.255.255.255:2010`.
The sender waits for the receiver's subscription before sending; a `std::promise`
transfers the result back to the main thread. Both waits time out on failure,
and the links stop/join their threads when leaving scope. Serial hardware is
not needed. Set `-DPPRZLINK_BUILD_EXAMPLES=OFF` to omit the example target.

## Select the serial transport: PPRZ or XBee API

Transport selection is a runtime application choice, independent of the message
XML. The same definitions and Message objects work with `PprzTransport` (the
usual `0x99` serial frames) or `XbeeTransport` (XBee `0x7e` API frames).
Both implement `Transport` and take exclusive ownership of a `Device` through
`std::unique_ptr<Device>`, including `BoostSerialPortDevice`. Configure the serial
port before transferring ownership:

```cpp
boost::asio::io_context context; // Must outlive the transport and its device.
auto serialDevice = std::make_unique<pprzlink::BoostSerialPortDevice>(context, "/dev/ttyUSB0");
serialDevice->setBaudrate(pprzlink::BoostSerialPortDevice::Baudrate(57600));
// Set any other serial options here, before transferring ownership.
pprzlink::XbeeTransport transport(std::move(serialDevice), dictionary);
transport.startInitialization(); // Autobaud -> 57600, MY=0x100, channel unchanged, AP=1
while (!transport.pollInitialization()) {
    context.run_for(std::chrono::milliseconds(10)); // Service the serial Device's I/O.
}
transport.setSanityChecksEnabled(true);
message.setReceiverId(42);
transport.sendMessage(message); // XBee 16-bit destination 42
// For an independent radio address, preserve the PPRZLINK receiver ID:
transport.sendMessageTo16(message, 0x1234);
// Or use legacy 64-bit addressing:
transport.sendMessageTo64(message, 0x0013a200405291abULL);
```

The default radio destination is the PPRZLINK receiver ID. Receiver 255 means
broadcast and maps to radio address `0xffff`. The RF data consists of the four
PPRZLINK v2 header bytes followed by the XML fields, **without an additional
PPRZ serial envelope**. This matches the non-868 OCaml XBee transport.
The RF payload limit is 100 bytes including that four-byte header; larger
messages are rejected before writing to the device. Messages are not fragmented.

Supported radio format: **XBee 802.15.4 legacy API, AP=1 (without escaping)**,
TX16 `0x01`, TX64 `0x00`, RX16 `0x81`, RX64 `0x80`. A frequency of 2.4 GHz alone
is not sufficient to identify compatible firmware: Zigbee/DigiMesh/868 API
formats and AP=2 escaped mode are not implemented. On firmware with an `AO`
setting, select legacy RX16/RX64 output as specified by that modem's manual.
The PAN must already match the intended setup. Initialization assumes the default
command escape character `+` and a modem supporting UART command mode. Adjust the
guard time if the modem's `GT` value was customized.

`startInitialization(configuration)` begins a nonblocking AT sequence; call
`pollInitialization()` regularly while servicing the Device's I/O loop.
`hasMessage()`/`getMessage()` also advance initialization when called.
**Autobaud is enabled by default, with a target of 57600 baud.** Set
`XbeeConfiguration::targetBaudrate` to another standard rate if needed.
The initializer tries the target first, then 9600, 57600, 115200, 38400, 19200,
4800, 2400 and 1200, without duplicates. Each attempt waits for the pre-escape
guard, sends `+++` without a carriage return, respects the post-escape guard,
and waits for `OK`. A missing entry reply advances to the next rate.
Once the modem answers, `ATBD` must confirm the detected rate.

If a change is necessary, the initializer sets `ATBD`, waits for `ATCN`'s
`OK` at the old rate, then switches the host UART to the target. It re-enters
command mode and queries `ATBD` again to verify the change before issuing
`ATWR` and checking its `OK`. The next startup tries the saved target first.
There is no flash write when the modem already uses the requested rate.
**ATWR stores all current modem parameters**, so it is deliberately issued
before this initializer changes MY, CH or AP; pre-existing volatile settings
are still included in that save. The initializer then sends `ATMY`, optional
`ATCH`, `ATAP1` and the final `ATCN`, checking each `OK`. Those MY/CH/AP
changes remain volatile. The PAN and API output format (`AO`) are not changed.

Baud detection requires a `SerialDevice`, implemented by `BoostSerialPortDevice`.
Its `resetBaudrate()` changes the host UART and discards buffered input and
completions from the previous rate. Stale kernel input is drained during the
guard before `+++`; no flush is performed after AT commands.
Set `targetBaudrate = std::nullopt` for fixed-baud initialization with a generic
`Device`: the host and modem must then already match, and no BD query or flash
write is performed. Automatic targets are limited to the eight standard rates
listed above, common to legacy S1/S2C firmware. Rounded custom BD replies near
a probed rate are accepted for discovery, but arbitrary custom rates are not scanned.

The default guard time is two seconds before and after `+++`; the reply timeout
is two seconds per AT command (for entry, after the post-escape guard).
Timings can be set in `XbeeConfiguration`, along with `localAddress` and optional
`channel` (0x0c..0x17). After successful automatic initialization,
`getBaudrateInfo()` reports the detected and configured rates and whether the
change was saved to flash.

While initialization is active or failed, all message sends throw before any
binary frame is written. `ERROR`, unexpected AT replies and response timeouts
include the failed operation in the exception text. Restart explicitly with
`startInitialization()` after correcting the cause. Previously applied settings
are not rolled back. The caller must provide exclusive access to the device
throughout initialization; no other serial writer or transport may interfere.
The response deadlines do not limit a blocking `Device::writeBuffer()` call.

For an already configured modem, constructing `XbeeTransport` without calling
`startInitialization()` keeps the original direct API operation. Initialization
clears pending transport data; start it before exchanging messages. API bytes
following the final `OK` in the same read are preserved for normal reception.

`sendMessage()` returns the number of serial bytes written, not proof of RF
delivery. Frame IDs cycle through 1..255 (`getLastFrameId()`); applications must
limit outstanding transmissions so IDs are not reused while awaiting replies.
`setStatusCallback()` receives a `RadioStatus` variant containing `TransmitStatus`
(`0x89`), `ModemStatus` (`0x8a`) or `AtCommandResponse` (`0x88`). The callback runs
synchronously while `hasMessage()` or `getMessage()` processes incoming bytes.
Continue polling even if only status frames are expected. A missing callback
discards these events. Host-side retry scheduling, timeouts and pending-send
tracking belong to the application; the modem's normal acknowledgement/retry
mechanism remains enabled. A broadcast success status is not a delivery guarantee.

`getLastReceiveInfo()` exposes the source radio address, RSSI magnitude and option
bits of the last successfully decoded message. The PPRZLINK sender stays the
sender from the RF payload, which can differ from the radio address. Reception
handles fragmented/concatenated frames, discards bad checksums and impossible
lengths, and skips unhandled API indications. Checksum-valid malformed messages
are consumed before raising an exception, so the next frame remains accessible.

The `serial_messages` example exposes the choice with the same spelling as the
OCaml link: **`-transport pprz`** (default) or **`-transport xbee`**. XBee mode
initializes the modem automatically before sending or receiving messages.
No opt-in flag is needed for autobaud. `-s` sets the target (default **57600**),
and a changed rate is verified and saved to the modem's flash. The example
prints the detected/configured rates after successful initialization.
`-xbee-no-autobaud` disables discovery and baud changes while keeping AT setup;
`-s` must then match the modem. In PPRZ mode, `-s` only sets the host UART.
`-xbee_addr` sets MY (default `0x100`), `-ch` optionally sets the channel,
`-xbee-guard-ms` and `-xbee-timeout-ms` override the timing defaults.
`-xbee-no-init` skips AT initialization for a preconfigured AP=1 modem.
It receives and prints messages and optionally sends one message. It is a serial diagnostic
example; it does not start an Ivy bridge or replace the full OCaml `link` agent.
From this directory after the CMake build:

```sh
./build/serial_messages -messages /path/to/messages.xml \
  -d /dev/ttyUSB0 -transport xbee -xbeesan

# With a dictionary containing PING, send to aircraft/radio 42 and listen for 5 s:
./build/serial_messages -messages /path/to/messages.xml \
  -d /dev/ttyUSB0 -transport xbee \
  -send "PING" -sender 0 -receiver 42 -duration 5 -xbeesan

./build/serial_messages --help
```

`-xbee-dest` and `-xbee-dest64` override the radio destination independently of
`-receiver`. Radio addresses accept decimal or `0x` hexadecimal notation.
Serial settings are 8N1 without flow control by default; `-hfc` selects RTS/CTS
when the wiring and modem configuration support it. Ctrl-C stops reception.

`-xbeesan` enables an additional check of each complete transmit frame immediately
before the serial write. It verifies the `0x7e` delimiter, declared versus actual
length, TX16/TX64 header/address size, reserved option bits, the checksum, the
100-byte RF limit and the PPRZLINK payload against the loaded XML dictionary.
Any failure blocks the whole frame, prints the reason on **stderr**, and exits
the example with a nonzero status. AT initialization failures follow the same
error-reporting path and prevent the binary send. Basic checks remain enabled
without this option. `-xbeesan` is rejected with `-transport pprz`, since that
mode has no XBee API envelope to validate.

C++ applications enable this with `setSanityChecksEnabled(true)` and handle the
exception in their own reporting layer. `validateTransmitFrame(bytes)` exposes
the same check for captured frames without performing I/O. These checks validate
the supported legacy API/PPRZLINK format, not RF delivery or every firmware's
configuration-dependent payload limit. In particular they do not query `NP`.

The XBee tests compare exact TX16/TX64 fixtures with the OCaml frame format and
exercise RX metadata, radio statuses, payload limits and recovery after invalid
frames. AT tests exercise fragmented replies, both guard intervals, errors/timeouts
at every step and the send gate. Baud tests cover all eight detected rates,
an explicit target override, verification before flash write, a simulated restart,
and failure at each negotiation/save step. Sanity tests reject malformed frames and a
message whose definition disagrees with the transport's XML dictionary before
any write. The CLI integration test emulates AT replies and uses a pseudo-terminal
for serial I/O, checking host baud settings during the default 9600-to-57600
transition and a 115200 override, successful initialization, failure diagnostics
on stderr and the absence of bytes for refused sends. The simulated modem retains
the saved rate between two process runs; this does not test physical flash or UART
timing. Serial-device tests also cover baud resets with pending Asio completions. It requires Python 3 when building
examples and tests on Unix. No physical XBee is required for these tests.

Protocol references: [OCaml transport](../../common/ocaml/xbee_transport.ml),
[Digi legacy 802.15.4 manual](https://docs.digi.com/resources/documentation/digidocs/pdfs/90000982.pdf),
[Digi transmit status](https://docs.digi.com/resources/documentation/digidocs/90001500/reference/r_frame_0x89.htm),
[Digi BD command](https://docs.digi.com/resources/documentation/digidocs/90001500/reference/r_cmd_bd.htm).
The legacy manual specifies BD application after ATCN's response; its WR command
stores all current settings and requires waiting for the acknowledgement.

## Ivy integration

`IvyLink` uses `<Ivy/ivy.hpp>` and `libivy-cpp` directly. The legacy `ivy-c++`
source directory, including its Qt loop adapter, has been removed.
Existing `BindMessage`, `BindOnSrcAc`, `UnbindMessage`, `sendMessage`,
`sendRequest` and `registerRequestAnswerer` signatures are retained. The old
`IvyApplicationCallback` inheritance and helper callback classes are removed.
Binding IDs are opaque and local to each link.

Message definitions and field types are loaded at runtime from XML. Reception
uses `bind_raw`, then builds a `pprzlink::Message` from those definitions.
`bind_convert` requires a fixed callback signature and is not used for this
dynamic interface. Numeric conversions check syntax and range; strings and
arrays keep the PprzLink wire format. Request replies preserve the request ID,
and a reply subscription is canceled before invoking its one-shot callback.

Every `IvyLink` owns an independent `ivy::Bus`. Several links can use the same
bus address or separate addresses in one process:

```cpp
pprzlink::IvyLink first(dictionary, "first", "127.255.255.255:2010", true);
pprzlink::IvyLink second(dictionary, "second", "127.255.255.255:2011", true);
first.BindMessage(dictionary.getDefinition("ALIVE"),
    [](std::string sender, pprzlink::Message message) {
        // Fields are decoded according to the XML definition.
    });
```

`threadedIvy=true` owns an `ivy::LoopThread`; destruction stops and joins it
before releasing subscriptions and the bus. The default remains `false`:
call `link.run()` to run its native loop on the calling thread. `link.stop()`
requests a stop and may be called from a callback. The global
`Ivy::ivyMainLoop()`/old Qt adapter cannot drive these independent contexts.
`getBus()` borrows the native wrapper for its public APIs and context integration;
do not move/destroy the borrowed bus or run an additional loop on it.

Sends and binding changes can be called from multiple threads. Application
callback state still needs its own synchronization. The dictionary must outlive
the link, and all external callers/event loops must finish before destruction.
Never destroy a link from its callback. Unbinding permits an already executing
callback to finish; Ivy keeps its captures alive until then.

Synchronous Ivy errors throw `std::system_error`. Ivy catches exceptions from
callbacks, records an error and stops that bus. `run()` checks this error on
return; in threaded mode inspect `getBus().take_callback_error()` when the loop
has stopped. A destructor does not report errors.

The integration test uses loopback UDP/TCP on process-specific ports. It checks
XML scalar/array/string conversions, sender routing, empty messages, concurrent
requests and sends, cancellation, independent bus shutdown and error reporting.

## Typed field values and codecs

`FieldValue` stores a `std::variant` of the supported scalar types and typed
`std::vector` arrays. The XML definition selects the stored type; construction
converts numeric inputs to that type and checks fixed array lengths. There is no
`std::any`, per-element type erasure or mutable formatting flag. Named C++
concepts describe scalar, input-container and output-container overloads.

Returning getters complement the existing output-parameter API:

```cpp
// For XML fields declared as float and int16[].
auto altitude = message.getField<float>("altitude");
auto samples = message.getField<std::vector<int16_t>>("samples");
float previousStyle;
message.getField("altitude", previousStyle); // Still supported.

// Dynamic access: the XML determines which variant alternative is active.
const auto &storage = message.getField("altitude"); // const FieldValue::Storage&
const auto &firstField = message.getField(size_t{0}); // Also accessible by index.
auto sameValue = std::get<float>(storage);

// Metadata remains available through getRawValue().
const auto &field = message.getRawValue("altitude");
const auto &type = field.getType();
```

Without a template argument, `getField(name)` and `getField(index)` return a
read-only reference to the stored variant, suitable for `std::visit` or
`std::get`. The reference belongs to the message; use plain `auto` to obtain an
independent copy. A field missing from the definition throws `no_such_field`,
and a defined field without a value throws `field_has_no_value`, as with the
typed getters.

Typed getters require the exact stored scalar/element type. A mismatch throws
`std::bad_variant_access`; a `std::array` output must have exactly the stored
number of elements, otherwise it throws `std::length_error`.

`<pprzlink/TextCodec.h>` provides `writeIvyField(stream, field)` for the Ivy wire
format and `writeDebugField(stream, field)` for diagnostics. Formatting visits
stored values directly, without copying arrays or changing the field. Both use
numeric int8/uint8 output and preserve the caller's stream flags. `operator<<`
uses diagnostic formatting, including braces around non-character arrays;
`Message::toString()` retains its existing diagnostic layout.

`<pprzlink/BinaryCodec.h>` contains little-endian encoding/decoding using
`std::bit_cast` and bounded `std::span` input. Floating-point bit patterns and
full-width integer arrays are preserved. The existing `addToBuffer`,
`addFieldToBuffer` and `addFieldFromBuffer` entry points delegate to this codec.
Truncated field reads throw `std::out_of_range` without advancing their offset.
Dynamic array/string counts must fit in one byte; arrays of strings remain
unsupported by the binary format.

This changes the C++ API/ABI: rebuild dependent applications. Replace
`std::any_cast` on raw values with typed getters, `std::get` or `std::visit`, and
catch `std::bad_variant_access` instead of `std::bad_any_cast`. Construct fields
with their definition and value; the invalid default `FieldValue` constructor
has been removed. Replace `setOutputInt8AsInt`/`isOutputInt8AsInt` with the
appropriate text codec. Code that used `operator<<` for Ivy fields should use
`writeIvyField` explicitly.

The serialization tests check exact wire bytes for every scalar and array base
type, floating-point bits, truncated input, length limits, typed getters,
formatting and binary transport round trips. The field-value tests cover
concepts, supported containers and numeric conversions.

## Transport, XML and serial-device contracts

The binary transport searches iteratively for valid frames, keeps incomplete
frames, and discards noise, invalid lengths and bad checksums. A checksum-valid
frame with an unknown definition or malformed payload is consumed before the
exception is reported; the next call can process the following frame. Fields
are decoded only inside that frame's payload, and a message becomes visible
only when every field is decoded with no trailing bytes. Frames over 255 bytes
and class/component IDs outside four bits are rejected before writing. String
sender IDs must be decimal integers in [0, 255].

XML loading checks file/parse errors, required attributes, exact field types,
ID ranges and duplicate class/message/field names or IDs. Uppercase NAME/ID/TYPE
and lowercase attributes remain supported, as does a protocol nested inside a
configuration element. Errors retain filename, class, message and field context
when available. A missing file throws `messages_file_not_found`; invalid XML or
definitions throw `bad_message_file`. Array syntax is `type[]` or `type[N]`
with a positive N; `[0]` is rejected. `FieldType::getArraySize()` throws
`std::logic_error` for scalar fields.

Every indexed field accessor checks bounds through `MessageDefinition`.
`getRawValue()` now follows the same `no_such_field` / `field_has_no_value`
contract as `getField()`. Indices use `size_t`, including in `MessageDefinition`
and `getRawValue`. Borrowed field references must not outlive message
destruction/assignment or replacement of that field.

`IvyMessageCodec.h` groups runtime regexp generation, textual parsing and
serialization independently of the bus. `IvyLink` uses this codec for ordinary
messages, sender subscriptions and requests. `TextCodec.h` still handles field
formatting and diagnostics.

`Device::writeBuffer()` writes the entire buffer or throws; on failure some
bytes may already have been sent. `Device` and `Transport` have virtual
destructors. A transport takes a non-null `std::unique_ptr<Device>` and destroys the
device when it is destroyed; passing an empty pointer throws. It borrows its
dictionary, which must outlive it. Calls on one transport must be serialized
by the application.

`getDevice()` returns a borrowed reference (const for a const transport), valid until
transport destruction. Do not delete the device or give it to another owner.
Transports cannot be copied or moved; a `unique_ptr<Transport>` can still be moved.
`setDevice()` has been removed: recreate the transport with a new device so buffered
frames and modem initialization state cannot carry over from the previous one.
Existing callers must replace stack-device pointers with `std::make_unique` and
transfer ownership with `std::move`; rebuild the library and dependent applications.

`BoostSerialPortDevice` retains Boost.Asio. Reception is asynchronous; writes
are synchronous and complete. Run the supplied `io_context`, which must outlive
the device. `startReception()` is idempotent; `stopReception()` cancels reception
without closing the port. `readAll()` drains the received queue and also starts
or resumes reception, preserving polling-transport usage. Completion handlers
use their own scratch buffer and retain state independently of the device.
Destruction closes the port safely even when a callback is still pending.

Port I/O and option calls are serialized internally. Finish external calls
before destroying the device. Receive errors are reported by `availableBytes()`,
`readAll()` or `startReception()` rather than thrown through the event loop;
buffered bytes can be drained before the error is reported. Recreate the device
after a terminal receive error. Option getters now return values read from the
port. The old public completion handler and protected implementation members
are removed; callers should use the reception API. Rebuild dependent code for
these API/ABI changes.

Additional tests cover malformed binary frames and recovery, XML/file errors,
field-access contracts and the standalone Ivy codec. On Unix, pseudo-terminal
tests exercise serial data larger than the scratch buffer, complete writes,
cancellation/restart, peer closure and destruction with pending callbacks.
They do not require serial hardware.

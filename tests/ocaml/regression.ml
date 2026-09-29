(* Regression cases for the codec used by the OCaml link program. *)
open PprzLink

module Tm = Messages (struct let name = "telemetry" end)
module Dl = Messages (struct let name = "datalink" end)

(* Strings between two scalar fields expose lost or shifted Ivy arguments. *)
module Text = MessagesOfXml (struct
  let name = "test"
  let xml = Xml.Element ("protocol", [], [
    Xml.Element ("msg_class", ["name", name; "id", "1"], [
      Xml.Element ("message", ["name", "TEXT"; "id", "1"],
        List.map (fun (name, ty) ->
          Xml.Element ("field", ["name", name; "type", ty], []))
          ["before", "uint8"; "text", "string"; "after", "uint8"])
    ])
  ])
end)

let failures = ref 0
let total = ref 0
let check condition message = if not condition then failwith message
let test name f =
  incr total;
  try f (); Printf.printf "PASS %s\n%!" name
  with exn ->
    incr failures;
    Printf.printf "FAIL %s: %s\n%!" name (Printexc.to_string exn)

let roundtrip_dl text =
  let id, values = Dl.values_of_string text in
  let payload = Dl.payload_of_values id 0 42 values in
  let header, decoded = Dl.values_of_payload payload in
  check (header.sender_id = 0 && header.receiver_id = 42) "Changed routing IDs";
  check (values = decoded) "Changed datalink values";
  let frame = Pprz_transport.Transport.packet payload in
  check (Pprz_transport.Transport.checksum frame) "Invalid frame checksum";
  check (Pprz_transport.Transport.payload frame = payload) "Changed frame payload";
  payload

let () =
  test "MISSION_UPDATE: explicit empty numeric array" (fun () ->
    let payload = roundtrip_dl "MISSION_UPDATE 42 7 -1 \"\"" in
    let bytes = Protocol.bytes_of_payload payload in
    check (Bytes.length bytes = 11 && Bytes.get bytes 10 = '\x00') "Wrong empty-array length");
  test "MISSION_UPDATE: legacy empty delimiter" (fun () ->
    ignore (roundtrip_dl "MISSION_UPDATE 42 7 -1 ||"));
  test "MISSION_UPDATE: nonempty array unchanged" (fun () ->
    ignore (roundtrip_dl "MISSION_UPDATE 42 7 -1 1,2,3,"));
  test "MISSION_UPDATE: actually missing field stays rejected" (fun () ->
    let rejected = try ignore (Dl.values_of_string "MISSION_UPDATE 42 7 -1"); false with Failure _ -> true in
    check rejected "Accepted a missing field");
  test "MISSION_UPDATE: extra field stays rejected" (fun () ->
    let rejected = try ignore (Dl.values_of_string "MISSION_UPDATE 42 7 -1 1,2 99"); false with Failure _ -> true in
    check rejected "Accepted an extra field");
  List.iter (fun (message, field, text) ->
    test (message ^ ": binary empty array reaches Ivy") (fun () ->
      let id, _ = Tm.message_of_name message in
      let payload = Tm.payload_of_values id 42 0 [field, Array [||]] in
      let _, values = Tm.values_of_payload payload in
      Tm.message_send "42" message values;
      check (!Ivy.sent = "42 " ^ message ^ " \"\"") "Empty field disappeared on Ivy";
      let _, parsed = Tm.values_of_string (message ^ " \"\"") in
      check (List.assoc field parsed = text) "Wrong empty value"))
    ["PAYLOAD", "values", Array [||]; "INFO_MSG", "msg", String ""];
  test "empty array can also be rendered in diagnostics" (fun () ->
    check (string_of_value (Array [||]) = "\"\"") "Wrong empty-array representation");
  List.iter (fun text ->
    test (Printf.sprintf "string field roundtrip: %S" text) (fun () ->
      let values = ["before", Int 1; "text", String text; "after", Int 2] in
      let msg = snd (Text.message_of_name "TEXT") in
      let encoded = Text.string_of_message msg values in
      let _, decoded = Text.values_of_string encoded in
      check (decoded = values) "Changed text or lost a field"))
    [""; "a\tb"; "a b"; "plain"];
  List.iter (fun text ->
    test ("INFO_MSG: nonempty quoted text " ^ text) (fun () ->
      let _, values = Tm.values_of_string ("INFO_MSG " ^ text) in
      check (List.assoc "msg" values = String "hello world") "Changed quoted text"))
    ["\"hello world\""; "|hello world|"];
  List.iter (fun x ->
    test (Printf.sprintf "uint32 %Lu: exact four-byte write and readback" x) (fun () ->
      let buf = Bytes.make 12 '\xaa' in
      let size = sprint_value buf 1 (Scalar "uint32") (Int64 x) in
      check (size = 4) "Incorrect reported width";
      for i = 0 to 3 do
        let expected = Int64.to_int (Int64.logand (Int64.shift_right_logical x (8 * i)) 255L) in
        check (Char.code (Bytes.get buf (1 + i)) = expected) "Incorrect wire byte"
      done;
      check (Bytes.get buf 0 = '\xaa' && Bytes.sub_string buf 5 7 = String.make 7 '\xaa')
        "Overwrote bytes outside uint32 field";
      check (value_of_bin buf 1 (Scalar "uint32") = (Int64 x, 4)) "Incorrect uint32 readback"))
    [0L; 0x12345678L; 0x80000000L; 0xffffffffL];
  test "uint32 array: exact byte count" (fun () ->
    let values = Array [|Int64 1L; Int64 0xffffffffL|] in
    let buf = Bytes.make 16 '\xaa' in
    check (sprint_value buf 1 (ArrayType "uint32") values = 9) "Wrong array width";
    check (Bytes.sub_string buf 10 6 = String.make 6 '\xaa') "Array overwrote following bytes";
    check (value_of_bin buf 1 (ArrayType "uint32") = (values, 9)) "Wrong array readback");
  test "uint64 keeps all eight bytes" (fun () ->
    let buf = Bytes.make 8 '\x00' in
    let x = 0x123456789abcdef0L in
    check (sprint_value buf 0 (Scalar "uint64") (Int64 x) = 8) "Wrong uint64 width";
    check (value_of_bin buf 0 (Scalar "uint64") = (Int64 x, 8)) "Truncated uint64");
  test "WINDTURBINE_STATUS: uint32 datalink unchanged" (fun () ->
    ignore (roundtrip_dl "WINDTURBINE_STATUS 42 1 2147483648 4294967295"));
  test "BOOZ_NAV_STICK: complete signed int8 range" (fun () ->
    let payload = roundtrip_dl "BOOZ_NAV_STICK 42 -128 127 -1 0" in
    let bytes = Protocol.bytes_of_payload payload in
    check (Bytes.sub_string bytes 5 4 = "\x80\x7f\xff\x00") "Wrong int8 wire bytes");
  List.iter (fun x ->
    test (Printf.sprintf "int8 %d remains rejected" x) (fun () ->
      let rejected = try
        ignore (sprint_value (Bytes.create 1) 0 (Scalar "int8") (Int x)); false
      with Failure _ -> true in
      check rejected "Accepted an out-of-range int8")) [-129; 128];
  Printf.printf "%d/%d passed\n%!" (!total - !failures) !total;
  if !failures <> 0 then exit 1

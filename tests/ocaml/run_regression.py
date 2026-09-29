#!/usr/bin/env python3
"""Compile the real OCaml/C codec in isolation, without Ivy or xml-light installed.

XML files are parsed by Python and exposed through a small Xml tree adapter.
Ivy.send is captured; network bindings are deliberately unavailable.
Nothing is built or installed in the source tree.
"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET


PPRZLINK = Path(__file__).resolve().parents[2]

XML_ADAPTER = r'''
type xml = Element of string * (string * string) list * xml list | PCData of string
exception No_attribute of string
exception Not_element of xml
exception File_not_found of string
let tag = function Element (t, _, _) -> t | x -> raise (Not_element x)
let attribs = function Element (_, a, _) -> a | x -> raise (Not_element x)
let attrib x key = try List.assoc key (attribs x) with Not_found -> raise (No_attribute key)
let children = function Element (_, _, c) -> c | x -> raise (Not_element x)
let files : (string, xml) Hashtbl.t = Hashtbl.create 2
let parse_file path =
  try Hashtbl.find files (Filename.basename path)
  with Not_found -> raise (File_not_found path)
'''

IVY_ADAPTER = r'''
type binding = int
let sent = ref ""
let send s = sent := s
let bind (_ : unit -> string array -> unit) (_ : string) : binding =
  failwith "Network bindings are outside this codec test"
let unbind (_ : binding) = failwith "Network bindings are outside this codec test"
'''


def ocaml_string(value):
    # JSON's ASCII string escaping matches OCaml for these XML attributes.
    return json.dumps(value, ensure_ascii=False)


def xml_tree(element):
    attrs = "; ".join(
        f"({ocaml_string(k)}, {ocaml_string(v)})" for k, v in element.attrib.items()
    )
    children = [xml_tree(child) for child in element]
    if element.text and element.text.strip():
        children.insert(0, f"Xml.PCData {ocaml_string(element.text.strip())}")
    return f"Xml.Element ({ocaml_string(element.tag)}, [{attrs}], [{'; '.join(children)}])"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", help="Test a Git revision instead of the working tree")
    args = parser.parse_args()

    def read_source(source):
        if args.revision:
            return subprocess.check_output(["git", "show", f"{args.revision}:{source}"], cwd=PPRZLINK)
        return (PPRZLINK / source).read_bytes()

    with tempfile.TemporaryDirectory(prefix="link-regression-") as work:
        work = Path(work)
        (work / "xml.ml").write_text(XML_ADAPTER)
        (work / "ivy.ml").write_text(IVY_ADAPTER)
        catalog = []
        for name, folder in (("messages.xml", "v1.0"), ("units.xml", "common")):
            tree = ET.fromstring(read_source(f"message_definitions/{folder}/{name}"))
            catalog.append(f"let () = Hashtbl.add Xml.files {ocaml_string(name)} ({xml_tree(tree)})\n")
        (work / "catalog.ml").write_text("".join(catalog))
        sources = [
            "lib/common/ocaml/debugPL.ml",
            "lib/common/ocaml/protocol.ml",
            "lib/common/ocaml/pprz_transport.ml",
            "lib/common/ocaml/convert.c",
            "lib/v2.0/ocaml/pprzLink.ml",
        ]
        for source in sources:
            (work / Path(source).name).write_bytes(read_source(source))
        shutil.copyfile(Path(__file__).with_name("regression.ml"), work / "regression.ml")
        subprocess.run([
            "ocamlc", "-w", "-a", "-safe-string", "-custom", "-o", "regression",
            "str.cma", "unix.cma", "xml.ml", "ivy.ml", "catalog.ml",
            "debugPL.ml", "protocol.ml", "pprz_transport.ml", "convert.c",
            "pprzLink.ml", "regression.ml",
        ], cwd=work, check=True)
        return subprocess.run([str(work / "regression")], cwd=work).returncode


if __name__ == "__main__":
    raise SystemExit(main())

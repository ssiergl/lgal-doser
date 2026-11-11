#!/usr/bin/env python3
"""Writes THIRD-PARTY-NOTICES.txt from the license files of the components
that end up in the firmware image.

The texts are copied from the local ESP-IDF tree ($IDF_PATH), the managed
components and the vendored web libraries, so nothing is typed by hand.
Run it again after an ESP-IDF or component update and commit the result:

  source ~/esp/esp-idf-v6.0.3/export.sh   # sets IDF_PATH
  python3 tools/third_party_notices.py

The build embeds the file into the web UI (components/webui/CMakeLists.txt).
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IDF = os.environ.get("IDF_PATH", "")
OUT = os.path.join(ROOT, "THIRD-PARTY-NOTICES.txt")

HEADER = """\
LGAL-Doser firmware - license and third-party notices
=========================================================

Copyright (C) 2026 Lars Greiss / LG Aquatic Life

This program is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version. It is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License (file
LICENSE in the source repository, licenses/gpl-3.0.txt in the web UI) for
more details.

Additional permission under GNU GPL version 3 section 7: if you modify this
Program, or any covered work, by linking or combining it with the binary-only
libraries that Espressif Systems ships with ESP-IDF for its chips (Wi-Fi,
PHY, coexistence, Bluetooth and ROM code), the licensors of this Program
grant you additional permission to convey the resulting work. Corresponding
Source for a non-source form of such a combination shall not include the
source code for the parts of those libraries that Espressif does not publish.

The firmware contains the following third-party software. Each part keeps
its own license; all of them allow use in a GPLv3 program. The full license
texts follow the list.

"""

APACHE = "Apache-2.0"


def idf(*p):
    return os.path.join(IDF, "components", *p)


def mc(*p):
    return os.path.join(ROOT, "managed_components", *p)


def www(*p):
    return os.path.join(ROOT, "components", "webui", "www", *p)


def component_version(name):
    with open(mc(name, "idf_component.yml"), encoding="utf-8") as f:
        m = re.search(r"^version:\s*\"?([^\"\s]+)", f.read(), re.M)
    return m.group(1) if m else "?"


def vendored_version(package):
    with open(www("vendor", "README.md"), encoding="utf-8") as f:
        m = re.search(r"\| " + re.escape(package) + r" ([0-9][^ |]*) \|", f.read())
    return m.group(1) if m else "?"


def header_comment(path, last_line):
    """License block of a source file header, without comment markers."""
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = re.sub(r"^\s*/?\*+/?\s?", "", line.rstrip()).rstrip()
            if line.startswith("Copyright") or out:
                out.append(line)
            if out and last_line in line:
                break
    return "\n".join(out)


def text(path):
    with open(path, encoding="utf-8") as f:
        return f.read().strip("\n")


def main():
    if not os.path.isfile(os.path.join(IDF, "LICENSE")):
        sys.exit("IDF_PATH is not set or not an ESP-IDF tree (source export.sh first)")
    with open(os.path.join(IDF, "tools", "cmake", "version.cmake"), encoding="utf-8") as f:
        idf_ver = "v" + ".".join(re.findall(r"IDF_VERSION_\w+ (\d+)", f.read()))

    # Generic BSD-3-Clause terms ("the above-listed copyright holder(s)"), for
    # parts whose files carry only an SPDX tag (TLSF, MQTT encoder)
    with open(idf("wpa_supplicant", "README"), encoding="utf-8") as f:
        bsd3 = f.read().split("BSD license:\n\n", 1)[1].split("\n\n\n")[0].strip("\n")

    # (name, version, license, copyright, license text or None for Apache-2.0)
    parts = [
        ("ESP-IDF (framework, drivers, Wi-Fi/PHY libraries)", idf_ver, APACHE,
         "Espressif Systems (Shanghai) CO LTD", None),
        ("Mbed TLS (in ESP-IDF)", idf_ver, APACHE, "ARM Limited and the Mbed TLS contributors", None),
        ("espressif/mdns", component_version("espressif__mdns"), APACHE, "Espressif Systems (Shanghai) CO LTD", None),
        ("espressif/mqtt", component_version("espressif__mqtt"), APACHE, "Espressif Systems (Shanghai) CO LTD", None),
        ("espressif/w5500", component_version("espressif__w5500"), APACHE, "Espressif Systems (Shanghai) CO LTD", None),
        ("espressif/wiznet_common", component_version("espressif__wiznet_common"), APACHE,
         "Espressif Systems (Shanghai) CO LTD", None),
        ("htm (web UI)", vendored_version("htm"), APACHE, "Google Inc.", None),
        ("MQTT message encoder (in espressif/mqtt)", component_version("espressif__mqtt"), "BSD-3-Clause",
         "Stephen Robinson", "Copyright (c) 2014 Stephen Robinson\nAll rights reserved.\n\n" + bsd3),
        ("FreeRTOS kernel (in ESP-IDF)", idf_ver, "MIT", "Amazon.com, Inc. or its affiliates",
         "Copyright (C) 2017 Amazon.com, Inc. or its affiliates\n\n"
         + text(idf("freertos", "FreeRTOS-Kernel", "LICENSE.md"))),
        ("lwIP TCP/IP stack (in ESP-IDF)", idf_ver, "BSD-3-Clause", "Swedish Institute of Computer Science",
         text(idf("lwip", "lwip", "COPYING"))),
        ("wpa_supplicant (in ESP-IDF)", idf_ver, "BSD-3-Clause", "Jouni Malinen and contributors",
         text(idf("wpa_supplicant", "COPYING")).split("\n\n")[1] + "\n\n" + bsd3),
        ("TLSF allocator (in ESP-IDF)", idf_ver, "BSD-3-Clause", "Matthew Conte",
         "Copyright (c) 2006-2016 Matthew Conte\nAll rights reserved.\n\n" + bsd3),
        ("HTTP parser (in ESP-IDF)", idf_ver, "MIT", "Igor Sysoev, Joyent, Inc. and other Node contributors",
         "\n".join(l[3:] if l.startswith(" * ") else l.lstrip(" /*")
                   for l in text(idf("http_parser", "LICENSE.txt")).splitlines()).strip()),
        ("Xtensa HAL headers (in ESP-IDF)", idf_ver, "MIT", "Tensilica Inc.",
         header_comment(idf("xtensa", "include", "xtensa", "coreasm.h"), "SOFTWARE.")),
        ("Picolibc C library (in ESP-IDF toolchain)", idf_ver, "BSD and others, see below",
         "Keith Packard, The Newlib Project and others", text(idf("esp_libc", "COPYING.picolibc"))),
        ("GCC runtime (libstdc++, libgcc)", "", "GPL-3.0 with GCC Runtime Library Exception", "Free Software Foundation",
         "Covered by the GCC Runtime Library Exception 3.1; no notice is required for the compiled program."),
        ("joltwallet/littlefs (ESP-IDF port)", component_version("joltwallet__littlefs"), "MIT", "Brian Pugh",
         text(mc("joltwallet__littlefs", "LICENSE"))),
        ("littlefs", "(in joltwallet/littlefs)", "BSD-3-Clause", "The littlefs authors, Arm Limited",
         text(mc("joltwallet__littlefs", "src", "littlefs", "LICENSE.md"))),
        ("Preact and preact/hooks (web UI)", vendored_version("preact"), "MIT", "Jason Miller",
         text(www("vendor", "LICENSE-preact"))),
    ]

    out = [HEADER]
    for name, ver, lic, holder, _ in parts:
        out.append(f"- {name}{' ' + ver if ver else ''}\n  License: {lic}\n  Copyright: {holder}\n")
    rule = "\n" + "=" * 77 + "\n"
    for name, _, lic, _, body in parts:
        if body is not None:
            out.append(f"{rule}{name} - {lic}{rule}\n{body}\n")
    out.append(f"{rule}Apache License 2.0 - for "
               + ", ".join(n for n, _, l, _, b in parts if l == APACHE and b is None) + rule + "\n"
               + text(os.path.join(IDF, "LICENSE")) + "\n")
    data = "\n".join(out).replace("\r\n", "\n")
    data = re.sub(r"[ \t]+\n", "\n", data)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(data)
    print(f"{os.path.relpath(OUT, ROOT)}: {len(parts)} components, {len(data)} bytes")


if __name__ == "__main__":
    main()

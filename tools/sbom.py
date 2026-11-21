#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
# SPDX-License-Identifier: GPL-3.0-or-later

"""Writes the SBOM (SPDX 2.2 JSON) of one firmware build.

    tools/sbom.py build-d5v1-N16R8 dist/lgal_doser-0.1.0-d5v1-N16R8.spdx.json

Runs esp-idf-sbom on the build's project_description.json. The sbom.yml files in the project
root and in each own component give license and supplier. Afterwards every own component gets
the firmware version: esp-idf-sbom would take it from "git describe", which ignores the
lightweight release tags, and the package URL of the firmware gets the release tag (v<version>).
Needs esp-idf-sbom (pip install esp-idf-sbom==1.5.0)."""
import json
import os
import subprocess
import sys
import tempfile

SUPPLIER = "Organization: LG Aquatic Life"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    build, out = sys.argv[1:]
    desc = os.path.join(build, "project_description.json")
    with open(desc, encoding="utf-8") as f:
        version = json.load(f)["project_version"]
    with tempfile.TemporaryDirectory() as tmp:
        raw = os.path.join(tmp, "sbom.json")
        subprocess.run([sys.executable, "-m", "esp_idf_sbom", "create", "--rem-config", "--rem-unused",
                        "--format", "spdx-json@2.2", "-o", raw, desc], check=True)
        with open(raw, encoding="utf-8") as f:
            sbom = json.load(f)
    for pkg in sbom["packages"]:
        if pkg.get("supplier") == SUPPLIER:
            pkg["versionInfo"] = version
        for ref in pkg.get("externalRefs", []):
            if ref.get("referenceType") == "purl" and ref["referenceLocator"].endswith("/lgal-doser@" + version):
                ref["referenceLocator"] = ref["referenceLocator"][:-len(version)] + "v" + version
    with open(out, "w", encoding="utf-8") as f:
        json.dump(sbom, f, indent=1, ensure_ascii=False)
        f.write("\n")


if __name__ == "__main__":
    main()

"""Write a manufacturing BOM and package only the final generated spider outputs."""

import csv
import json
import os
import shutil
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


def main():
    """Package the checked prototype without downloaded software or credentials."""
    project = Path(os.environ["JOSHUA_CAD_PROJECT"])
    manifest = json.loads((project / "manifest.json").read_text())
    for path in (
        project / "export-validation.json",
        project / "clearance-validation.json",
        project / "mujoco/validation.json",
    ):
        if json.loads(path.read_text())["status"] != "ok":
            raise ValueError(f"Resolve the reported validation findings first: {path}")
    items = []
    for name, part in manifest["print_parts"].items():
        optional = "gauge" in name or "coupon" in name
        items.append(
            [
                "optional_print" if optional else "printed",
                name,
                part["quantity"],
                part["material"],
                "print/" + name + ".stl",
                part["notes"],
            ]
        )
    items.extend(
        [
            [
                "purchased",
                "STS3215_12V_servo",
                12,
                "motor",
                "design.json",
                "Confirm exact variant/case/cable interface",
            ],
            [
                "purchased",
                "compatible_metal_horn",
                12,
                "metal",
                "25T/5.9 mm; 4xM3 on 14 mm PCD",
                "Measure actual hub and stack height",
            ],
            [
                "purchased",
                "horn_retention_screw",
                12,
                "manufacturer matched",
                "motor/horn documentation",
                "Use the correct center screw length",
            ],
            [
                "purchased",
                "M3x6_horn_adapter_screw",
                48,
                "steel",
                "nominal length",
                "Check actual horn engagement and rotating clearance",
            ],
            [
                "purchased",
                "M3x12_cradle_mount_screw",
                48,
                "steel",
                "nominal length",
                "Four per cradle",
            ],
            ["purchased", "M3_cradle_mount_nut", 48, "steel", "M3", "Four per cradle"],
            [
                "purchased",
                "M3x10_clamp_screw",
                24,
                "steel",
                "nominal length",
                "Two per clamp",
            ],
            [
                "purchased",
                "M3_heat_set_insert",
                24,
                "brass",
                "4.1 mm pilot; 6 mm pocket",
                "Match insert supplier and test installation",
            ],
            [
                "purchased",
                "M3_threaded_rod_65mm",
                4,
                "steel",
                "nominal cut length",
                "Check stack; deburr ends",
            ],
            ["purchased", "M3_tie_rod_nut", 8, "steel", "M3", "Two per rod"],
            ["purchased", "M3_tie_rod_washer", 8, "steel", "M3", "Two per rod"],
            [
                "purchased",
                "thin_rubber_liner_stock",
                1,
                "sheet",
                "nominal 0.5 mm; cut for 12 motors",
                "Test compression against the 0.35 mm clearances",
            ],
            [
                "purchased",
                "payload_strap",
                2,
                "hook-and-loop",
                "deck slots",
                "Match actual payload",
            ],
            [
                "separate_design",
                "bus_leads_controller_power_and_protection",
                1,
                "system",
                "not selected",
                "No electrical system or hardware configuration is supplied",
            ],
        ]
    )
    with (project / "bom.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(
            ["category", "item", "quantity", "material", "reference", "notes"]
        )
        writer.writerows(items)
    shutil.copyfile(
        Path(__file__).with_name("SPIDER_STS3215.md"), project / "ASSEMBLY_GUIDE.md"
    )
    # The distributed copy points to the included design and describes the local paths.
    guide = (
        (project / "ASSEMBLY_GUIDE.md")
        .read_text()
        .replace(
            "[spider_sts3215.json](spider_sts3215.json)", "[design.json](design.json)"
        )
    )
    guide = guide.replace(
        "after [CAD setup](README.md)", "after the repository's CAD setup"
    )
    (project / "ASSEMBLY_GUIDE.md").write_text(guide)
    archive = project.parent / "spider_sts3215_outputs.zip"
    with ZipFile(archive, "w", ZIP_DEFLATED) as bundle:
        for path in sorted(project.rglob("*")):
            if (
                not path.is_file()
                or path.name.endswith((".log", ".FCStd1"))
                or path.name.endswith("error.txt")
            ):
                continue
            if path.suffix.lower() not in {
                ".fcstd",
                ".stl",
                ".urdf",
                ".dae",
                ".obj",
                ".xml",
                ".json",
                ".csv",
                ".md",
                ".png",
                ".txt",
            }:
                continue
            bundle.write(path, Path("spider_sts3215") / path.relative_to(project))
    print(
        json.dumps(
            {
                "status": "ok",
                "archive": str(archive),
                "bytes": archive.stat().st_size,
                "installed_printed_parts": sum(
                    p["quantity"]
                    for n, p in manifest["print_parts"].items()
                    if "gauge" not in n and "coupon" not in n
                ),
            }
        )
    )


if __name__ == "__main__":
    main()

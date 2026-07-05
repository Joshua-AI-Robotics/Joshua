#!/usr/bin/env python3
from argparse import ArgumentParser
from pathlib import Path


def replace_known(
    text: str, replacements: dict[str, str]
) -> tuple[str, list[tuple[str, str]], list[str]]:
    updated = text
    applied: list[tuple[str, str]] = []
    missing: list[str] = []
    for old, new in replacements.items():
        if old in updated:
            updated = updated.replace(old, new)
            applied.append((old, new))
        else:
            missing.append(old)
    return updated, applied, missing


parser = ArgumentParser(
    description=(
        "Patch Industrial Communications SDK 09 imports.mak for local TI tool paths"
    )
)
parser.add_argument(
    "--sdk-root",
    default=str(Path.home() / "ti" / "ind_comms_sdk_am243x_09_00_00_03"),
    help="Path to Industrial Communications SDK 09 root",
)
parser.add_argument(
    "--tools-path", default=str(Path.home() / "ti"), help="Path to TI tools root"
)
parser.add_argument(
    "--ccs-dir", default="ccs2100/ccs", help="Relative CCS directory under tools path"
)
parser.add_argument(
    "--sysconfig-dir",
    default="sysconfig_1.17.0",
    help="Relative SysConfig directory under tools path",
)
parser.add_argument(
    "--compiler-dir",
    default="ccs2100/ccs/tools/compiler/ti-cgt-armllvm_5.1.1.LTS",
    help="TI ARM Clang directory under tools path",
)
parser.add_argument(
    "--pru-compiler-dir",
    default="ti-cgt-pru_2.3.3/ti-cgt-pru_2.3.3",
    help="TI PRU compiler directory under tools path",
)
parser.add_argument(
    "--dry-run",
    action="store_true",
    help="Show planned changes without writing imports.mak",
)
args = parser.parse_args()

sdk_root = Path(args.sdk_root).expanduser().resolve()
imports_files = [sdk_root / "imports.mak", sdk_root / "mcu_plus_sdk" / "imports.mak"]

for imports_file in imports_files:
    if not imports_file.exists():
        raise FileNotFoundError(f"Could not find imports.mak at {imports_file}")

    text = imports_file.read_text()
    replacements = {
        "export CCS_PATH?=$(TOOLS_PATH)/ccs1240/ccs": (
            f"export CCS_PATH?=$(TOOLS_PATH)/{args.ccs_dir}"
        ),
        "SYSCFG_PATH ?= $(TOOLS_PATH)/sysconfig_1.17.0": (
            f"SYSCFG_PATH ?= $(TOOLS_PATH)/{args.sysconfig_dir}"
        ),
        "CGT_TI_ARM_CLANG_PATH=$(CCS_PATH)/tools/compiler/ti-cgt-armllvm_2.1.3.LTS": (
            f"CGT_TI_ARM_CLANG_PATH=$(CCS_PATH)/tools/compiler/{args.compiler_dir}"
        ),
        "CGT_TI_ARM_CLANG_PATH=$(TOOLS_PATH)/ti-cgt-armllvm_2.1.3.LTS": (
            f"CGT_TI_ARM_CLANG_PATH=$(TOOLS_PATH)/{args.compiler_dir}"
        ),
        "CGT_TI_PRU_PATH=$(TOOLS_PATH)/ti-cgt-pru_2.3.3": (
            f"CGT_TI_PRU_PATH=$(TOOLS_PATH)/{args.pru_compiler_dir}"
        ),
    }

    updated, applied, missing = replace_known(text, replacements)

    if updated == text:
        print(f"No changes required in {imports_file}")
    elif args.dry_run:
        print(f"Dry run would update {imports_file}")
    else:
        imports_file.write_text(updated)
        print(f"Configured {imports_file}")

    if applied:
        print(f"Applied {len(applied)} replacement(s)")
    if missing:
        print(
            f"Skipped {len(missing)} known replacement(s) "
            "because source text was not found"
        )

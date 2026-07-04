#!/usr/bin/env python3
from argparse import ArgumentParser
from pathlib import Path

parser = ArgumentParser(description="Patch TI MCU+ SDK imports.mak for AM243 build environment")
parser.add_argument("--sdk-root", default=str(Path.home() / "ti" / "mcu_plus_sdk_am243x_12_00_00_26"), help="Path to the MCU+ SDK root")
parser.add_argument("--ccs-dir", default="ccs2100/ccs", help="Relative CCS installation directory under TOOLS_PATH")
parser.add_argument("--sysconfig-dir", default="sysconfig_1.26.3", help="Relative SysConfig installation directory under TOOLS_PATH")
parser.add_argument("--compiler-dir", default="ti-cgt-armllvm_5.1.1.LTS", help="Compiler directory name under CCS_PATH/tools/compiler")
parser.add_argument("--tools-path", default=str(Path.home() / "ti"), help="Path to the TOOLS_PATH used by imports.mak")
parser.add_argument("--dry-run", action="store_true", help="Show planned changes without writing imports.mak")
args = parser.parse_args()

sdk_path = Path(args.sdk_root).expanduser().resolve()
imports_file = sdk_path / "imports.mak"

if not imports_file.exists():
    raise FileNotFoundError(f"Could not find imports.mak at {imports_file}")

text = imports_file.read_text()

replacements = {
    "export CCS_PATH?=$(TOOLS_PATH)/ccs2040/ccs":
        f"export CCS_PATH?=$(TOOLS_PATH)/{args.ccs_dir}",
    "export CCS_PATH?=$(TOOLS_PATH)/ccs2100/ccs":
        f"export CCS_PATH?=$(TOOLS_PATH)/{args.ccs_dir}",
    "SYSCFG_PATH ?= $(TOOLS_PATH)/sysconfig_1.26.0":
        f"SYSCFG_PATH ?= $(TOOLS_PATH)/{args.sysconfig_dir}",
    "SYSCFG_PATH ?= $(TOOLS_PATH)/sysconfig_1.26.3":
        f"SYSCFG_PATH ?= $(TOOLS_PATH)/{args.sysconfig_dir}",
    "CGT_TI_ARM_CLANG_PATH=$(TOOLS_PATH)/ti-cgt-armllvm_5.1.1.LTS":
        f"CGT_TI_ARM_CLANG_PATH=$(CCS_PATH)/tools/compiler/{args.compiler_dir}",
    "CGT_TI_ARM_CLANG_PATH=$(CCS_PATH)/tools/compiler/ti-cgt-armllvm_5.1.1.LTS":
        f"CGT_TI_ARM_CLANG_PATH=$(CCS_PATH)/tools/compiler/{args.compiler_dir}",
}

updated_text = text
applied = []
missing = []
for old, new in replacements.items():
    if old in updated_text:
        updated_text = updated_text.replace(old, new)
        applied.append((old, new))
    else:
        missing.append(old)

if updated_text == text:
    print(f"No changes required in {imports_file}")
else:
    if args.dry_run:
        print(f"Dry run would update {imports_file}")
    else:
        imports_file.write_text(updated_text)
        print(f"Configured {imports_file}")

if applied:
    print(f"Applied {len(applied)} replacement(s)")
if missing:
    print(f"Skipped {len(missing)} known replacement(s) because source text was not found")

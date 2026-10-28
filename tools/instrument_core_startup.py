#!/usr/bin/env python3
"""Inject SD-card-backed startup milestones into PCSX-ReARMed for Wii U debugging.

The instrumentation is deliberately applied after checking out the pinned
upstream submodule, so we do not need to fork or modify its git history.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "vendor" / "pcsx_rearmed"
LIBRETRO = CORE / "frontend" / "libretro.c"
MAIN = CORE / "frontend" / "main.c"

def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"Core startup instrumentation failed at {label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)

def instrument_libretro() -> None:
    text = LIBRETRO.read_text(encoding="utf-8")
    if "ps1wiiu_core_trace" in text:
        if 'retro_init: returned from emu_core_init' in text:
            print("PCSX-ReARMed libretro startup trace already installed")
            return
        raise SystemExit("Partial PCSX-ReARMed libretro startup trace found; refusing to double-patch")

    text = replace_once(
        text, "#include <libretro.h>\n",
        "#include <libretro.h>\n\nextern void ps1wiiu_core_trace(const char *message);\n",
        "libretro trace declaration"
    )
    text = replace_once(
        text,
        "void retro_init(void)\n{\n   unsigned dci_version = 0;",
        "void retro_init(void)\n{\n   ps1wiiu_core_trace(\"retro_init: entered\");\n   unsigned dci_version = 0;",
        "retro_init entry"
    )
    text = replace_once(
        text, "   log_mem_usage(0);\n",
        "   log_mem_usage(0);\n   ps1wiiu_core_trace(\"retro_init: after log_mem_usage\");\n",
        "memory logger"
    )
    text = replace_once(
        text,
        "   msg_interface_version = 0;\n   environ_cb(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION, &msg_interface_version);\n",
        "   msg_interface_version = 0;\n   ps1wiiu_core_trace(\"retro_init: before environment message-interface query\");\n"
        "   environ_cb(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION, &msg_interface_version);\n"
        "   ps1wiiu_core_trace(\"retro_init: after environment message-interface query\");\n",
        "message-interface query"
    )
    text = replace_once(
        text, "   ret = emu_core_preinit();\n",
        "   ps1wiiu_core_trace(\"retro_init: before emu_core_preinit\");\n"
        "   ret = emu_core_preinit();\n"
        "   ps1wiiu_core_trace(\"retro_init: after emu_core_preinit\");\n",
        "emu_core_preinit"
    )
    text = replace_once(
        text, "   init_memcards();\n\n   ret |= emu_core_init();\n",
        "   ps1wiiu_core_trace(\"retro_init: before init_memcards\");\n"
        "   init_memcards();\n"
        "   ps1wiiu_core_trace(\"retro_init: after init_memcards\");\n\n"
        "   ps1wiiu_core_trace(\"retro_init: before emu_core_init\");\n"
        "   ret |= emu_core_init();\n"
        "   ps1wiiu_core_trace(\"retro_init: returned from emu_core_init\");\n",
        "core init"
    )
    text = replace_once(
        text, "   if (ret != 0)\n   {\n      LogErr(\"PCSX init failed.\\n\");",
        "   if (ret != 0)\n   {\n      ps1wiiu_core_trace(\"retro_init: emu_core_init returned an error\");\n"
        "      LogErr(\"PCSX init failed.\\n\");",
        "core init error"
    )
    text = replace_once(
        text, "   size_t vout_buf_size = VOUT_MAX_WIDTH * VOUT_MAX_HEIGHT * 4;\n",
        "   size_t vout_buf_size = VOUT_MAX_WIDTH * VOUT_MAX_HEIGHT * 4;\n"
        "   ps1wiiu_core_trace(\"retro_init: before output-buffer allocation\");\n",
        "output buffer allocation start"
    )
    text = replace_once(
        text, "   if (vout_buf == NULL)\n",
        "   ps1wiiu_core_trace(vout_buf ? \"retro_init: output-buffer allocation succeeded\" : \"retro_init: output-buffer allocation failed\");\n"
        "   if (vout_buf == NULL)\n",
        "output buffer allocation result"
    )
    text = replace_once(
        text, "   loadPSXBios();\n",
        "   ps1wiiu_core_trace(\"retro_init: before BIOS discovery\");\n"
        "   loadPSXBios();\n"
        "   ps1wiiu_core_trace(\"retro_init: after BIOS discovery\");\n",
        "BIOS discovery"
    )
    text = replace_once(
        text, "   check_system_specs();\n}\n\nvoid retro_deinit(void)",
        "   check_system_specs();\n"
        "   ps1wiiu_core_trace(\"retro_init: completed successfully\");\n"
        "}\n\nvoid retro_deinit(void)",
        "retro_init completion"
    )
    LIBRETRO.write_text(text, encoding="utf-8")

def instrument_main() -> None:
    text = MAIN.read_text(encoding="utf-8")
    if "ps1wiiu_core_trace" not in text:
        text = replace_once(
            text, '#include "main.h"\n',
            '#include "main.h"\n\nextern void ps1wiiu_core_trace(const char *message);\n',
            "frontend main trace declaration"
        )
    if 'emu_core_preinit: returned from emu_set_default_config' in text:
        print("PCSX-ReARMed frontend main startup trace already installed")
        MAIN.write_text(text, encoding="utf-8")
        return

    text = replace_once(
        text, "int emu_core_preinit(void)\n{\n",
        'int emu_core_preinit(void)\n{\n   ps1wiiu_core_trace("emu_core_preinit: entered");\n',
        "emu_core_preinit entry"
    )
    text = replace_once(
        text, "   SetIsoFile(NULL);\n\n   memset(&Config, 0, sizeof(Config));\n\n   set_default_paths();\n   emu_set_default_config();\n",
        "   ps1wiiu_core_trace(\"emu_core_preinit: before SetIsoFile\");\n"
        "   SetIsoFile(NULL);\n"
        "   ps1wiiu_core_trace(\"emu_core_preinit: after SetIsoFile\");\n\n"
        "   memset(&Config, 0, sizeof(Config));\n"
        "   ps1wiiu_core_trace(\"emu_core_preinit: after clearing Config\");\n\n"
        "   ps1wiiu_core_trace(\"emu_core_preinit: before set_default_paths\");\n"
        "   set_default_paths();\n"
        "   ps1wiiu_core_trace(\"emu_core_preinit: after set_default_paths\");\n"
        "   ps1wiiu_core_trace(\"emu_core_preinit: before emu_set_default_config\");\n"
        "   emu_set_default_config();\n"
        "   ps1wiiu_core_trace(\"emu_core_preinit: returned from emu_set_default_config\");\n",
        "emu_core_preinit stages"
    )
    text = replace_once(
        text, "int emu_core_init(void)\n{\n",
        'int emu_core_init(void)\n{\n   ps1wiiu_core_trace("emu_core_init: entered");\n',
        "emu_core_init entry"
    )
    text = replace_once(
        text, "   if (EmuInit() == -1) {\n",
        "   ps1wiiu_core_trace(\"emu_core_init: before EmuInit/psxInit\");\n"
        "   if (EmuInit() == -1) {\n",
        "EmuInit start"
    )
    text = replace_once(
        text, "   LoadMcds(Config.Mcd1, Config.Mcd2);\n",
        "   ps1wiiu_core_trace(\"emu_core_init: EmuInit/psxInit succeeded\");\n"
        "   ps1wiiu_core_trace(\"emu_core_init: before LoadMcds\");\n"
        "   LoadMcds(Config.Mcd1, Config.Mcd2);\n"
        "   ps1wiiu_core_trace(\"emu_core_init: after LoadMcds\");\n",
        "memory card initialization"
    )
    text = replace_once(
        text, "   return 0;\n}\n\nvoid emu_core_ask_exit(void)",
        '   ps1wiiu_core_trace("emu_core_init: completed successfully");\n'
        "   return 0;\n}\n\nvoid emu_core_ask_exit(void)",
        "emu_core_init completion"
    )
    MAIN.write_text(text, encoding="utf-8")

if not LIBRETRO.is_file() or not MAIN.is_file():
    raise SystemExit("PCSX-ReARMed source files are missing; check out submodules first.")

instrument_libretro()
instrument_main()
print("PCSX-ReARMed startup instrumentation is ready")

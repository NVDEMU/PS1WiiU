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
    start = text.index("void retro_init(void)\n{")
    end = text.index("\nvoid retro_deinit(void)", start)
    section = text[start:end]

    section = replace_once(
        section,
        "void retro_init(void)\n{\n   unsigned dci_version = 0;",
        "void retro_init(void)\n{\n   ps1wiiu_core_trace(\"retro_init: entered\");\n   unsigned dci_version = 0;",
        "retro_init entry"
    )
    section = replace_once(
        section, "   log_mem_usage(0);\n",
        "   log_mem_usage(0);\n   ps1wiiu_core_trace(\"retro_init: after log_mem_usage\");\n",
        "memory logger"
    )
    section = replace_once(
        section,
        "   msg_interface_version = 0;\n   environ_cb(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION, &msg_interface_version);\n",
        "   msg_interface_version = 0;\n   ps1wiiu_core_trace(\"retro_init: before environment message-interface query\");\n"
        "   environ_cb(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION, &msg_interface_version);\n"
        "   ps1wiiu_core_trace(\"retro_init: after environment message-interface query\");\n",
        "message-interface query"
    )
    section = replace_once(
        section, "   ret = emu_core_preinit();\n",
        "   ps1wiiu_core_trace(\"retro_init: before emu_core_preinit\");\n"
        "   ret = emu_core_preinit();\n"
        "   ps1wiiu_core_trace(\"retro_init: after emu_core_preinit\");\n",
        "emu_core_preinit"
    )
    section = replace_once(
        section, "   init_memcards();\n\n   ret |= emu_core_init();\n",
        "   ps1wiiu_core_trace(\"retro_init: before init_memcards\");\n"
        "   init_memcards();\n"
        "   ps1wiiu_core_trace(\"retro_init: after init_memcards\");\n\n"
        "   ps1wiiu_core_trace(\"retro_init: before emu_core_init\");\n"
        "   ret |= emu_core_init();\n"
        "   ps1wiiu_core_trace(\"retro_init: returned from emu_core_init\");\n",
        "core init"
    )
    section = replace_once(
        section, "   if (ret != 0)\n   {\n      LogErr(\"PCSX init failed.\\n\");",
        "   if (ret != 0)\n   {\n      ps1wiiu_core_trace(\"retro_init: emu_core_init returned an error\");\n"
        "      LogErr(\"PCSX init failed.\\n\");",
        "core init error"
    )
    section = replace_once(
        section, "   size_t vout_buf_size = VOUT_MAX_WIDTH * VOUT_MAX_HEIGHT * 4;\n",
        "   size_t vout_buf_size = VOUT_MAX_WIDTH * VOUT_MAX_HEIGHT * 4;\n"
        "   ps1wiiu_core_trace(\"retro_init: before output-buffer allocation\");\n",
        "output buffer allocation start"
    )
    section = replace_once(
        section, "   if (vout_buf == NULL)\n",
        "   ps1wiiu_core_trace(vout_buf ? \"retro_init: output-buffer allocation succeeded\" : \"retro_init: output-buffer allocation failed\");\n"
        "   if (vout_buf == NULL)\n",
        "output buffer allocation result"
    )
    section = replace_once(
        section, "   loadPSXBios();\n",
        "   ps1wiiu_core_trace(\"retro_init: before BIOS discovery\");\n"
        "   loadPSXBios();\n"
        "   ps1wiiu_core_trace(\"retro_init: after BIOS discovery\");\n",
        "BIOS discovery"
    )
    section = replace_once(
        section, "   check_system_specs();\n}",
        "   check_system_specs();\n"
        "   ps1wiiu_core_trace(\"retro_init: completed successfully\");\n}",
        "retro_init completion"
    )
    text = text[:start] + section + text[end:]
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


def instrument_psxinit() -> None:
    path = CORE / "libpcsxcore" / "r3000a.c"
    text = path.read_text(encoding="utf-8")
    if "psxInit: before psxMemInit" in text:
        return
    text = replace_once(
        text,
        "int psxInit() {\n",
        "extern void ps1wiiu_core_trace(const char *message);\n\nint psxInit() {\n",
        "PSX init trace declaration"
    )
    text = replace_once(
        text,
        "\tif (psxMemInit() == -1) return -1;\n\n\treturn psxCpu->Init();\n}",
        "\tps1wiiu_core_trace(\"psxInit: before psxMemInit\");\n"
        "\tif (psxMemInit() == -1) {\n"
        "\t\tps1wiiu_core_trace(\"psxInit: psxMemInit failed\");\n"
        "\t\treturn -1;\n"
        "\t}\n"
        "\tps1wiiu_core_trace(\"psxInit: psxMemInit succeeded\");\n"
        "\tps1wiiu_core_trace(\"psxInit: before CPU backend Init\");\n"
        "\tint init_ret = psxCpu->Init();\n"
        "\tps1wiiu_core_trace(\"psxInit: returned from CPU backend Init\");\n"
        "\treturn init_ret;\n}",
        "psxInit memory and CPU setup"
    )
    text = text.replace(
        "int psxInit() {\n",
        "int psxInit() {\n\tps1wiiu_core_trace(\"psxInit: entered\");\n",
        1
    )
    if "ps1wiiu_core_trace" not in text:
        raise SystemExit("Failed to add PSX initialization trace")
    path.write_text(text, encoding="utf-8")

def instrument_psxmem() -> None:
    path = CORE / "libpcsxcore" / "psxmem.c"
    text = path.read_text(encoding="utf-8")
    if "psxMemInit: before memory-map backend" in text:
        return
    start = text.index("int psxMemInit(void)\n{")
    text = text[:start] + "extern void ps1wiiu_core_trace(const char *message);\n\n" + text[start:]
    start = text.index("int psxMemInit(void)\n{")
    end = text.index("\nvoid psxMemReset()", start)
    section = text[start:end]
    section = replace_once(
        section,
        "int psxMemInit(void)\n{\n",
        "int psxMemInit(void)\n{\n\tps1wiiu_core_trace(\"psxMemInit: entered\");\n",
        "psxMemInit entry"
    )
    section = replace_once(
        section,
        "\tif (LIGHTREC_CUSTOM_MAP)\n\t\tret = lightrec_init_mmap();\n\telse\n\t\tret = psxMemInitMap();\n",
        "\tps1wiiu_core_trace(\"psxMemInit: before memory-map backend\");\n"
        "\tif (LIGHTREC_CUSTOM_MAP)\n\t\tret = lightrec_init_mmap();\n"
        "\telse\n\t\tret = psxMemInitMap();\n"
        "\tps1wiiu_core_trace(\"psxMemInit: returned from memory-map backend\");\n",
        "memory-map backend"
    )
    section = replace_once(
        section,
        "\tif (DISABLE_MEM_LUTS)\n\t\treturn 0;\n",
        "\tif (DISABLE_MEM_LUTS) {\n"
        "\t\tps1wiiu_core_trace(\"psxMemInit: memory LUTs disabled; complete\");\n"
        "\t\treturn 0;\n\t}\n",
        "LUT disabled"
    )
    section = replace_once(
        section,
        "\tmemRLUT = malloc(table_size * sizeof(memRLUT[0]) * 2);\n",
        "\tps1wiiu_core_trace(\"psxMemInit: before memory LUT allocation\");\n"
        "\tmemRLUT = malloc(table_size * sizeof(memRLUT[0]) * 2);\n"
        "\tps1wiiu_core_trace(memRLUT ? \"psxMemInit: memory LUT allocation succeeded\" : \"psxMemInit: memory LUT allocation failed\");\n",
        "memory LUT allocation"
    )
    section = replace_once(
        section,
        "\treturn 0;\n}",
        "\tps1wiiu_core_trace(\"psxMemInit: completed successfully\");\n\treturn 0;\n}",
        "psxMemInit completion"
    )
    text = text[:start] + section + text[end:]
    path.write_text(text, encoding="utf-8")

def instrument_lightrec_map() -> None:
    path = CORE / "libpcsxcore" / "lightrec" / "mem_wiiu.c"
    text = path.read_text(encoding="utf-8")
    if "lightrec_init_mmap: before OSGetMapVirtAddrRange" in text:
        return
    text = replace_once(
        text,
        '#include "mem.h"\n',
        '#include "mem.h"\n\nextern void ps1wiiu_core_trace(const char *message);\n',
        "Wii U memory-map trace declaration"
    )
    text = replace_once(
        text,
        "int lightrec_init_mmap(void) {\n",
        'int lightrec_init_mmap(void) {\n\tps1wiiu_core_trace("lightrec_init_mmap: entered");\n',
        "Wii U memory-map entry"
    )
    text = replace_once(
        text,
        "\tif (!psx_mem || !psx_parallel || !psx_scratch || !psx_bios)\n\t\tgoto cleanup_allocations;\n",
        "\tps1wiiu_core_trace(\"lightrec_init_mmap: backing allocations attempted\");\n"
        "\tif (!psx_mem || !psx_parallel || !psx_scratch || !psx_bios) {\n"
        "\t\tps1wiiu_core_trace(\"lightrec_init_mmap: backing allocation failed\");\n"
        "\t\tgoto cleanup_allocations;\n\t}\n",
        "backing allocations"
    )
    text = replace_once(
        text,
        "\tuint32_t avail_va;\n\tuint32_t avail_va_size;\n\tOSGetMapVirtAddrRange(&avail_va, &avail_va_size);\n",
        "\tuint32_t avail_va;\n\tuint32_t avail_va_size;\n"
        "\tps1wiiu_core_trace(\"lightrec_init_mmap: before OSGetMapVirtAddrRange\");\n"
        "\tOSGetMapVirtAddrRange(&avail_va, &avail_va_size);\n"
        "\tps1wiiu_core_trace(\"lightrec_init_mmap: after OSGetMapVirtAddrRange\");\n",
        "virtual address range query"
    )
    text = replace_once(
        text,
        "\t// Map 4x ram mirrors\n",
        "\tps1wiiu_core_trace(\"lightrec_init_mmap: before mapping four RAM mirrors\");\n\t// Map 4x ram mirrors\n",
        "RAM mirror mapping"
    )
    text = replace_once(
        text,
        "\tpsxRegs.ptrs.psxM = (void*)avail_va;\n",
        "\tps1wiiu_core_trace(\"lightrec_init_mmap: four RAM mirrors mapped\");\n\tpsxRegs.ptrs.psxM = (void*)avail_va;\n",
        "RAM mirrors assigned"
    )
    text = replace_once(
        text,
        "\tpsxRegs.ptrs.psxR = wiiu_mmap(avail_va + 0x1fc00000, 0x80000, psx_bios);\n",
        "\tpsxRegs.ptrs.psxR = wiiu_mmap(avail_va + 0x1fc00000, 0x80000, psx_bios);\n"
        "\tps1wiiu_core_trace(\"lightrec_init_mmap: mapped scratchpad, parallel port, and BIOS\");\n",
        "system memory regions"
    )
    text = replace_once(
        text,
        "\tcode_buffer = WUP_RWX_MEM_BASE;\n\n\treturn 0;\n",
        "\tcode_buffer = WUP_RWX_MEM_BASE;\n"
        "\tps1wiiu_core_trace(\"lightrec_init_mmap: complete\");\n\n\treturn 0;\n",
        "Wii U memory-map completion"
    )
    text = replace_once(
        text,
        "cleanup_allocations:\n",
        'cleanup_allocations:\n\tps1wiiu_core_trace("lightrec_init_mmap: cleanup after failed mapping");\n',
        "Wii U memory-map failure cleanup"
    )
    path.write_text(text, encoding="utf-8")

if not LIBRETRO.is_file() or not MAIN.is_file():
    raise SystemExit("PCSX-ReARMed source files are missing; check out submodules first.")

instrument_libretro()
instrument_main()
instrument_psxinit()
instrument_psxmem()
instrument_lightrec_map()
print("PCSX-ReARMed startup instrumentation is ready")

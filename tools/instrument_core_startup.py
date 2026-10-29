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
def instrument_load_game() -> None:
    text = LIBRETRO.read_text(encoding="utf-8")
    if 'ps1wiiu_load_trace: entered' in text:
        print("PCSX-ReARMed game-load trace already installed")
        return

    start = text.index("bool retro_load_game(const struct retro_game_info *info)\n{")
    end = text.index("\nunsigned retro_get_region(void)", start)
    section = text[start:end]

    replacements = [
        (
            "bool retro_load_game(const struct retro_game_info *info)\n{\n   size_t i;",
            "bool retro_load_game(const struct retro_game_info *info)\n{\n   ps1wiiu_core_trace(\"ps1wiiu_load_trace: entered\");\n   size_t i;",
            "retro_load_game entry",
        ),
        (
            "   update_variables(false);\n",
            "   ps1wiiu_core_trace(\"retro_load_game: before update_variables\");\n"
            "   update_variables(false);\n"
            "   ps1wiiu_core_trace(\"retro_load_game: after update_variables\");\n",
            "game-load variable update",
        ),
        (
            "   disk_init();\n",
            "   ps1wiiu_core_trace(\"retro_load_game: before disk_init\");\n"
            "   disk_init();\n"
            "   ps1wiiu_core_trace(\"retro_load_game: after disk_init\");\n",
            "game-load disk initialization",
        ),
        (
            "   extract_directory(base_dir, info->path, sizeof(base_dir));\n",
            "   ps1wiiu_core_trace(\"retro_load_game: before extract_directory\");\n"
            "   extract_directory(base_dir, info->path, sizeof(base_dir));\n"
            "   ps1wiiu_core_trace(\"retro_load_game: after extract_directory\");\n",
            "game-load base directory",
        ),
        (
            "      disk_count = 1;\n      disks[0].fname = strdup(info->path);\n\n      get_disk_label(disk_label, info->path, PATH_MAX);\n      disks[0].flabel = strdup(disk_label);\n",
            "      disk_count = 1;\n"
            "      ps1wiiu_core_trace(\"retro_load_game: before disk path allocation\");\n"
            "      disks[0].fname = strdup(info->path);\n"
            "      ps1wiiu_core_trace(disks[0].fname ? \"retro_load_game: disk path allocation succeeded\" : \"retro_load_game: disk path allocation failed\");\n"
            "      ps1wiiu_core_trace(\"retro_load_game: before get_disk_label\");\n"
            "      get_disk_label(disk_label, info->path, PATH_MAX);\n"
            "      ps1wiiu_core_trace(\"retro_load_game: after get_disk_label\");\n"
            "      disks[0].flabel = strdup(disk_label);\n"
            "      ps1wiiu_core_trace(disks[0].flabel ? \"retro_load_game: disk label allocation succeeded\" : \"retro_load_game: disk label allocation failed\");\n",
            "game-load path and label allocation",
        ),
        (
            "   set_cd_image(disks[cd_index].fname);\n   disk_current_index = cd_index;\n\n   /* have to reload after set_cd_image for correct cdr plugin */",
            "   ps1wiiu_core_trace(\"retro_load_game: before set_cd_image\");\n"
            "   set_cd_image(disks[cd_index].fname);\n"
            "   ps1wiiu_core_trace(\"retro_load_game: after set_cd_image\");\n"
            "   disk_current_index = cd_index;\n\n   /* have to reload after set_cd_image for correct cdr plugin */",
            "game-load CD image selection",
        ),
        (
            "   if (LoadPlugins() == -1)\n   {\n      LogErr(\"failed to load plugins\\n\");",
            "   ps1wiiu_core_trace(\"retro_load_game: before LoadPlugins\");\n"
            "   if (LoadPlugins() == -1)\n   {\n"
            "      ps1wiiu_core_trace(\"retro_load_game: LoadPlugins failed\");\n"
            "      LogErr(\"failed to load plugins\\n\");",
            "game-load plugin initialization",
        ),
        (
            "   if (!strncmp(info->path, \"cdrom:\", 6))",
            "   ps1wiiu_core_trace(\"retro_load_game: LoadPlugins succeeded\");\n"
            "   if (!strncmp(info->path, \"cdrom:\", 6))",
            "game-load plugin initialization result",
        ),
        (
            "   if (OpenPlugins(0) == -1)\n   {\n      LogErr(\"failed to open plugins\\n\");",
            "   ps1wiiu_core_trace(\"retro_load_game: before OpenPlugins\");\n"
            "   if (OpenPlugins(0) == -1)\n   {\n"
            "      ps1wiiu_core_trace(\"retro_load_game: OpenPlugins failed\");\n"
            "      LogErr(\"failed to open plugins\\n\");",
            "game-load plugin opening",
        ),
        (
            "   /* Handle multi-disk images (i.e. PBP)",
            "   ps1wiiu_core_trace(\"retro_load_game: OpenPlugins succeeded\");\n\n"
            "   /* Handle multi-disk images (i.e. PBP)",
            "game-load plugin opening result",
        ),
        (
            "      if (CheckCdrom() == -1)\n      {\n         LogErr(\"unsupported/invalid CD image: %s\\n\", info->path);",
            "      ps1wiiu_core_trace(\"retro_load_game: before CheckCdrom\");\n"
            "      if (CheckCdrom() == -1)\n      {\n"
            "         ps1wiiu_core_trace(\"retro_load_game: CheckCdrom rejected image\");\n"
            "         LogErr(\"unsupported/invalid CD image: %s\\n\", info->path);",
            "game-load CD validation",
        ),
        (
            "   load_memcards();\n   plugin_call_rearmed_cbs();\n   SysReset();\n",
            "   ps1wiiu_core_trace(\"retro_load_game: before load_memcards\");\n"
            "   load_memcards();\n"
            "   ps1wiiu_core_trace(\"retro_load_game: after load_memcards\");\n"
            "   ps1wiiu_core_trace(\"retro_load_game: before plugin_call_rearmed_cbs\");\n"
            "   plugin_call_rearmed_cbs();\n"
            "   ps1wiiu_core_trace(\"retro_load_game: after plugin_call_rearmed_cbs\");\n"
            "   ps1wiiu_core_trace(\"retro_load_game: before SysReset\");\n"
            "   SysReset();\n"
            "   ps1wiiu_core_trace(\"retro_load_game: after SysReset\");\n",
            "game-load reset sequence",
        ),
        (
            "   else\n      ret = LoadCdromMainExe(NULL);\n",
            "   else\n   {\n"
            "      ps1wiiu_core_trace(\"retro_load_game: before LoadCdromMainExe\");\n"
            "      ret = LoadCdromMainExe(NULL);\n"
            "      ps1wiiu_core_trace(\"retro_load_game: after LoadCdromMainExe\");\n"
            "   }\n",
            "game-load main executable",
        ),
        (
            "   emu_on_new_cd(0);\n\n   set_retro_memmap();",
            "   ps1wiiu_core_trace(\"retro_load_game: before emu_on_new_cd\");\n"
            "   emu_on_new_cd(0);\n"
            "   ps1wiiu_core_trace(\"retro_load_game: after emu_on_new_cd\");\n\n"
            "   ps1wiiu_core_trace(\"retro_load_game: before set_retro_memmap\");\n"
            "   set_retro_memmap();\n"
            "   ps1wiiu_core_trace(\"retro_load_game: after set_retro_memmap\");",
            "game-load memory map",
        ),
        (
            "   show_enabled_hacks();\n\n   return true;\n}",
            "   show_enabled_hacks();\n"
            "   ps1wiiu_core_trace(\"retro_load_game: completed successfully\");\n\n"
            "   return true;\n}",
            "game-load completion",
        ),
    ]

    for old, new, label in replacements:
        section = replace_once(section, old, new, label)

    text = text[:start] + section + text[end:]
    LIBRETRO.write_text(text, encoding="utf-8")

def instrument_runtime_video() -> None:
    """Trace early interpreter execution and the framebuffer handed to libretro."""
    text = LIBRETRO.read_text(encoding="utf-8")
    if "ps1wiiu_run_trace_count" in text:
        print("PCSX-ReARMed execution/video trace already installed")
        return

    start = text.index("void retro_run(void)\n{")
    end = text.index("\n// see Config.Bios", start)
    section = text[start:end]

    section = replace_once(
        section,
        "void retro_run(void)\n{\n",
        "void retro_run(void)\n{\n"
        "   static unsigned ps1wiiu_run_trace_count = 0;\n"
        "   char ps1wiiu_run_trace_message[160];\n",
        "retro_run execution trace entry",
    )
    section = replace_once(
        section,
        "   psxCpu->Execute(&psxRegs);\n\n   if (pl_rearmed_cbs.fskip_dirty) {",
        "   if (ps1wiiu_run_trace_count < 8) {\n"
        "      snprintf(ps1wiiu_run_trace_message, sizeof(ps1wiiu_run_trace_message),\n"
        "            \"retro_run #%u: before Execute pc=0x%08x cycle=0x%08x\",\n"
        "            ps1wiiu_run_trace_count + 1, (unsigned)psxRegs.pc, (unsigned)psxRegs.cycle);\n"
        "      ps1wiiu_core_trace(ps1wiiu_run_trace_message);\n"
        "   }\n"
        "   psxCpu->Execute(&psxRegs);\n"
        "   if (ps1wiiu_run_trace_count < 8) {\n"
        "      snprintf(ps1wiiu_run_trace_message, sizeof(ps1wiiu_run_trace_message),\n"
        "            \"retro_run #%u: after Execute pc=0x%08x cycle=0x%08x\",\n"
        "            ps1wiiu_run_trace_count + 1, (unsigned)psxRegs.pc, (unsigned)psxRegs.cycle);\n"
        "      ps1wiiu_core_trace(ps1wiiu_run_trace_message);\n"
        "   }\n\n"
        "   if (pl_rearmed_cbs.fskip_dirty) {",
        "retro_run CPU execution trace",
    )
    section = replace_once(
        section,
        "   video_cb((vout_fb_dirty || !vout_can_dupe) ? vout_buf_ptr : NULL,\n"
        "       vout_width, vout_height, vout_pitch_b);\n"
        "   vout_fb_dirty = 0;\n",
        "   if (ps1wiiu_run_trace_count < 8) {\n"
        "      snprintf(ps1wiiu_run_trace_message, sizeof(ps1wiiu_run_trace_message),\n"
        "            \"retro_run #%u: video data=%p size=%ux%u pitch=%zu dirty=%d can_dupe=%d\",\n"
        "            ps1wiiu_run_trace_count + 1,\n"
        "            (void *)((vout_fb_dirty || !vout_can_dupe) ? vout_buf_ptr : NULL),\n"
        "            vout_width, vout_height, (size_t)vout_pitch_b, vout_fb_dirty, vout_can_dupe);\n"
        "      ps1wiiu_core_trace(ps1wiiu_run_trace_message);\n"
        "   }\n"
        "   video_cb((vout_fb_dirty || !vout_can_dupe) ? vout_buf_ptr : NULL,\n"
        "       vout_width, vout_height, vout_pitch_b);\n"
        "   if (ps1wiiu_run_trace_count < 8)\n"
        "      ps1wiiu_core_trace(\"retro_run: after video callback\");\n"
        "   ++ps1wiiu_run_trace_count;\n"
        "   vout_fb_dirty = 0;\n",
        "retro_run video callback trace",
    )

    text = text[:start] + section + text[end:]
    LIBRETRO.write_text(text, encoding="utf-8")


def instrument_main() -> None:
    text = MAIN.read_text(encoding="utf-8")
    if 'emu_core_init: completed successfully' in text:
        print("PCSX-ReARMed frontend main startup trace already installed")
        return
    if "ps1wiiu_core_trace" not in text:
        text = replace_once(
            text, '#include "main.h"\n',
            '#include "main.h"\n\nextern void ps1wiiu_core_trace(const char *message);\n',
            "frontend main trace declaration"
        )

    pre_start = text.index("int emu_core_preinit(void)\n{")
    pre_end = text.index("\nint emu_core_init(void)", pre_start)
    pre = text[pre_start:pre_end]
    pre = replace_once(
        pre, "int emu_core_preinit(void)\n{\n",
        'int emu_core_preinit(void)\n{\n\tps1wiiu_core_trace("emu_core_preinit: entered");\n',
        "emu_core_preinit entry"
    )
    pre = replace_once(
        pre,
        "\tSetIsoFile(NULL);\n\n\tmemset(&Config, 0, sizeof(Config));\n\n\tset_default_paths();\n\temu_set_default_config();\n",
        "\tps1wiiu_core_trace(\"emu_core_preinit: before SetIsoFile\");\n"
        "\tSetIsoFile(NULL);\n"
        "\tps1wiiu_core_trace(\"emu_core_preinit: after SetIsoFile\");\n\n"
        "\tmemset(&Config, 0, sizeof(Config));\n"
        "\tps1wiiu_core_trace(\"emu_core_preinit: after clearing Config\");\n\n"
        "\tps1wiiu_core_trace(\"emu_core_preinit: before set_default_paths\");\n"
        "\tset_default_paths();\n"
        "\tps1wiiu_core_trace(\"emu_core_preinit: after set_default_paths\");\n"
        "\tps1wiiu_core_trace(\"emu_core_preinit: before emu_set_default_config\");\n"
        "\temu_set_default_config();\n"
        "\tps1wiiu_core_trace(\"emu_core_preinit: returned from emu_set_default_config\");\n",
        "emu_core_preinit stages"
    )
    text = text[:pre_start] + pre + text[pre_end:]

    init_start = text.index("int emu_core_init(void)\n{")
    init_end = text.index("\nvoid emu_core_ask_exit(void)", init_start)
    init = text[init_start:init_end]
    init = replace_once(
        init, "int emu_core_init(void)\n{\n",
        'int emu_core_init(void)\n{\n\tps1wiiu_core_trace("emu_core_init: entered");\n',
        "emu_core_init entry"
    )
    init = replace_once(
        init, "\tif (EmuInit() == -1) {\n",
        "\tps1wiiu_core_trace(\"emu_core_init: before EmuInit/psxInit\");\n"
        "\tif (EmuInit() == -1) {\n"
        "\t\tps1wiiu_core_trace(\"emu_core_init: EmuInit/psxInit failed\");\n",
        "EmuInit start"
    )
    init = replace_once(
        init, "\tLoadMcds(Config.Mcd1, Config.Mcd2);\n",
        "\tps1wiiu_core_trace(\"emu_core_init: EmuInit/psxInit succeeded\");\n"
        "\tps1wiiu_core_trace(\"emu_core_init: before LoadMcds\");\n"
        "\tLoadMcds(Config.Mcd1, Config.Mcd2);\n"
        "\tps1wiiu_core_trace(\"emu_core_init: after LoadMcds\");\n",
        "memory card initialization"
    )
    init = replace_once(
        init, "\treturn 0;\n}",
        '\tps1wiiu_core_trace("emu_core_init: completed successfully");\n\treturn 0;\n}',
        "emu_core_init completion"
    )
    text = text[:init_start] + init + text[init_end:]
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
        "#ifdef HW_WUP\n"
        "\t/* Lightrec's fixed-address JIT backend is unsuitable after the Cemu software-map fallback. */\n"
        "\textern int lightrec_custom_map_active;\n"
        "\tif (!lightrec_custom_map_active) {\n"
        "\t\tConfig.Cpu = CPU_INTERPRETER;\n"
        "\t\tpsxCpu = &psxInt;\n"
        "\t\tps1wiiu_core_trace(\"psxInit: custom map unavailable; switching to interpreter CPU backend\");\n"
        "\t}\n"
        "#endif\n"
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
        '#include <malloc.h>\n',
        '#include <malloc.h>\n#include <stdio.h>\n',
        "virtual range diagnostic formatting declaration"
    )
    text = replace_once(
        text,
        "\tuint32_t avail_va;\n\tuint32_t avail_va_size;\n\tOSGetMapVirtAddrRange(&avail_va, &avail_va_size);\n",
        "\tuint32_t avail_va = 0;\n\tuint32_t avail_va_size = 0;\n"
        "\tchar range_message[128];\n"
        "\tps1wiiu_core_trace(\"lightrec_init_mmap: before OSGetMapVirtAddrRange\");\n"
        "\tOSGetMapVirtAddrRange(&avail_va, &avail_va_size);\n"
        "\tsnprintf(range_message, sizeof(range_message), \"lightrec_init_mmap: range base=0x%08x size=0x%08x\", avail_va, avail_va_size);\n"
        "\tps1wiiu_core_trace(range_message);\n"
        "\tps1wiiu_core_trace(\"lightrec_init_mmap: after OSGetMapVirtAddrRange\");\n",
        "virtual address range query"
    )
    text = replace_once(
        text,
        "\tif (!avail_va || avail_va_size < 0x20000000)\n\t\tgoto cleanup_allocations;\n",
        "\tif (!avail_va) {\n"
        "\t\tps1wiiu_core_trace(\"lightrec_init_mmap: failed because range base is zero\");\n"
        "\t\tgoto cleanup_allocations;\n\t}\n"
        "\t/* The final required window is BIOS at +0x1fc00000, size 0x80000. */\n"
        "\tif (avail_va_size < 0x1fc80000) {\n"
        "\t\tps1wiiu_core_trace(\"lightrec_init_mmap: range is too small; need 0x1fc80000 bytes\");\n"
        "\t\tgoto cleanup_allocations;\n\t}\n"
        "\tps1wiiu_core_trace(\"lightrec_init_mmap: address range is large enough; proceeding with mappings\");\n",
        "exact virtual-address range validation"
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
        "\tcode_buffer = (void *)(uintptr_t)WUP_RWX_MEM_BASE;\n\n\treturn 0;\n",
        "\tcode_buffer = (void *)(uintptr_t)WUP_RWX_MEM_BASE;\n"
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


def patch_cemu_interpreter_option() -> None:
    """Do not let option refresh re-enable Lightrec when Cemu needs software maps."""
    text = LIBRETRO.read_text(encoding="utf-8")
    marker = "PS1WiiU fallback keeps Lightrec disabled after custom-map failure"
    if marker in text:
        return

    old = (
        '   if (!environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))\n'
        '      var.value = "enabled";\n\n'
        '   {\n'
        '      R3000Acpu *prev_cpu = psxCpu;\n'
    )
    new = (
        '   if (!environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))\n'
        '      var.value = "enabled";\n\n'
        '#ifdef HW_WUP\n'
        '   /* PS1WiiU fallback keeps Lightrec disabled after custom-map failure. */\n'
        '   extern int lightrec_custom_map_active;\n'
        '   if (!lightrec_custom_map_active) {\n'
        '      var.value = "disabled";\n'
        '      ps1wiiu_core_trace("update_variables: disabled Lightrec after software-map fallback");\n'
        '   }\n'
        '#endif\n\n'
        '   {\n'
        '      R3000Acpu *prev_cpu = psxCpu;\n'
    )
    text = replace_once(text, old, new, "force interpreter after Cemu memory-map fallback")
    LIBRETRO.write_text(text, encoding="utf-8")


def patch_cemu_software_map_fallback() -> None:
    """Keep the real Wii U fast mapping, but fall back to software maps when Cemu lacks the mapping-range API."""
    mem_h = CORE / "libpcsxcore" / "lightrec" / "mem.h"
    text = mem_h.read_text(encoding="utf-8")
    if "LIGHTREC_CUSTOM_MAP_ACTIVE" not in text:
        text = replace_once(
            text,
            "extern void *code_buffer;\n",
            "extern void *code_buffer;\n\n"
            "#ifdef HW_WUP\n"
            "/* Runtime status: false means the Wii U custom memory map failed and software maps are active. */\n"
            "extern int lightrec_custom_map_active;\n"
            "#define LIGHTREC_CUSTOM_MAP_ACTIVE lightrec_custom_map_active\n"
            "#else\n"
            "#define LIGHTREC_CUSTOM_MAP_ACTIVE LIGHTREC_CUSTOM_MAP\n"
            "#endif\n",
            "runtime custom-map status declaration"
        )
        mem_h.write_text(text, encoding="utf-8")

    wiiu_mem = CORE / "libpcsxcore" / "lightrec" / "mem_wiiu.c"
    text = wiiu_mem.read_text(encoding="utf-8")
    if "int lightrec_custom_map_active = 0;" not in text:
        text = replace_once(
            text,
            "void wiiu_clear_cache(void *start, void *end);\n",
            "void wiiu_clear_cache(void *start, void *end);\n\n"
            "int lightrec_custom_map_active = 0;\n",
            "runtime custom-map status definition"
        )
        wiiu_mem.write_text(text, encoding="utf-8")

    psxmem = CORE / "libpcsxcore" / "psxmem.c"
    text = psxmem.read_text(encoding="utf-8")
    if "psxMemInit: using software memory maps after custom map failure" not in text:
        text = replace_once(
            text,
            "ptr = psxMap(0x80000000, 0x00210000, 1, MAP_TAG_RAM);",
            "#ifdef HW_WUP\n"
            "\t/* The Wii U heap-backed fallback cannot satisfy an exact-address allocation. */\n"
            "\tptr = psxMap(0x80000000, 0x00210000, lightrec_custom_map_active ? 1 : 0, MAP_TAG_RAM);\n"
            "#else\n"
            "\tptr = psxMap(0x80000000, 0x00210000, 1, MAP_TAG_RAM);\n"
            "#endif",
            "heap-backed RAM mapping fallback"
        )
        text = replace_once(
            text,
            "\tps1wiiu_core_trace(\"psxMemInit: before memory-map backend\");\n"
            "\tif (LIGHTREC_CUSTOM_MAP)\n"
            "\t\tret = lightrec_init_mmap();\n"
            "\telse\n"
            "\t\tret = psxMemInitMap();\n"
            "\tps1wiiu_core_trace(\"psxMemInit: returned from memory-map backend\");\n",
            "\tps1wiiu_core_trace(\"psxMemInit: before memory-map backend\");\n"
            "#ifdef HW_WUP\n"
            "\tif (LIGHTREC_CUSTOM_MAP) {\n"
            "\t\tlightrec_custom_map_active = 1;\n"
            "\t\tret = lightrec_init_mmap();\n"
            "\t\tif (ret) {\n"
            "\t\t\t/* Failed custom-map setup has already unmapped its partial mappings. */\n"
            "\t\t\tlightrec_custom_map_active = 0;\n"
            "\t\t\tpsxRegs.ptrs.psxM = NULL;\n"
            "\t\t\tpsxRegs.ptrs.psxP = NULL;\n"
            "\t\t\tpsxRegs.ptrs.psxH = NULL;\n"
            "\t\t\tpsxRegs.ptrs.psxR = NULL;\n"
            "\t\t\tps1wiiu_core_trace(\"psxMemInit: using software memory maps after custom map failure\");\n"
            "\t\t\tret = psxMemInitMap();\n"
            "\t\t}\n"
            "\t} else {\n"
            "\t\tlightrec_custom_map_active = 0;\n"
            "\t\tret = psxMemInitMap();\n"
            "\t}\n"
            "#else\n"
            "\tif (LIGHTREC_CUSTOM_MAP)\n"
            "\t\tret = lightrec_init_mmap();\n"
            "\telse\n"
            "\t\tret = psxMemInitMap();\n"
            "#endif\n"
            "\tps1wiiu_core_trace(\"psxMemInit: returned from memory-map backend\");\n",
            "runtime fallback after custom memory-map failure"
        )
        text = replace_once(
            text,
            "void psxMemShutdown() {\n"
            "\tif (LIGHTREC_CUSTOM_MAP)\n"
            "\t\tlightrec_free_mmap();\n"
            "\telse\n"
            "\t\tpsxMemFreeMap();\n",
            "void psxMemShutdown() {\n"
            "\tif (LIGHTREC_CUSTOM_MAP_ACTIVE)\n"
            "\t\tlightrec_free_mmap();\n"
            "\telse\n"
            "\t\tpsxMemFreeMap();\n",
            "runtime custom-map shutdown"
        )
        psxmem.write_text(text, encoding="utf-8")

    plugin = CORE / "libpcsxcore" / "lightrec" / "plugin.c"
    text = plugin.read_text(encoding="utf-8")
    if "Cemu fallback retains Wii U executable code buffer" not in text:
        # This file is only patched in the Wii U build script; preserve all other platform code paths.
        text = replace_once(
            text,
            "#include <stdbool.h>\n",
            "#include <stdbool.h>\n#include <stdint.h>\n",
            "Wii U runtime code-buffer pointer declaration"
        )
        text = text.replace("LIGHTREC_CUSTOM_MAP", "LIGHTREC_CUSTOM_MAP_ACTIVE")
        text = replace_once(
            text,
            "\tif (!LIGHTREC_CUSTOM_MAP_ACTIVE) {\n"
            "#if P_HAVE_MMAP\n"
            "\t\tcode_buffer = mmap(0, CODE_BUFFER_SIZE,\n"
            "\t\t\t\t   PROT_EXEC | PROT_READ | PROT_WRITE,\n"
            "\t\t\t\t   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);\n"
            "\t\tif (code_buffer == MAP_FAILED)\n"
            "\t\t\treturn -ENOMEM;\n"
            "#else\n"
            "\t\tcode_buffer = malloc(CODE_BUFFER_SIZE);\n"
            "\t\tif (!code_buffer)\n"
            "\t\t\treturn -ENOMEM;\n"
            "#endif\n"
            "\t}\n",
            "\tif (!LIGHTREC_CUSTOM_MAP_ACTIVE) {\n"
            "#ifdef HW_WUP\n"
            "\t\t/* Cemu fallback still needs the Wii U executable/JIT region. */\n"
            "\t\tcode_buffer = (void *)(uintptr_t)WUP_RWX_MEM_BASE;\n"
            "#else\n"
            "#if P_HAVE_MMAP\n"
            "\t\tcode_buffer = mmap(0, CODE_BUFFER_SIZE,\n"
            "\t\t\t\t   PROT_EXEC | PROT_READ | PROT_WRITE,\n"
            "\t\t\t\t   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);\n"
            "\t\tif (code_buffer == MAP_FAILED)\n"
            "\t\t\treturn -ENOMEM;\n"
            "#else\n"
            "\t\tcode_buffer = malloc(CODE_BUFFER_SIZE);\n"
            "\t\tif (!code_buffer)\n"
            "\t\t\treturn -ENOMEM;\n"
            "#endif\n"
            "#endif\n"
            "\t}\n",
            "Cemu fallback retains Wii U executable code buffer"
        )
        text = replace_once(
            text,
            "\tif (!LIGHTREC_CUSTOM_MAP_ACTIVE) {\n"
            "#if P_HAVE_MMAP\n"
            "\t\tmunmap(code_buffer, CODE_BUFFER_SIZE);\n"
            "#else\n"
            "\t\tfree(code_buffer);\n"
            "#endif\n"
            "\t}\n",
            "\tif (!LIGHTREC_CUSTOM_MAP_ACTIVE) {\n"
            "#ifndef HW_WUP\n"
            "#if P_HAVE_MMAP\n"
            "\t\tmunmap(code_buffer, CODE_BUFFER_SIZE);\n"
            "#else\n"
            "\t\tfree(code_buffer);\n"
            "#endif\n"
            "#endif\n"
            "\t}\n",
            "do not free the fixed Wii U executable code buffer"
        )
        plugin.write_text(text, encoding="utf-8")

def patch_cemu_hle_boot() -> None:
    """Use PCSX-ReARMed's HLE BIOS when Cemu cannot provide the fixed PSX memory map."""
    text = LIBRETRO.read_text(encoding="utf-8")
    marker = "PS1WiiU fallback uses HLE BIOS to bypass Cemu BIOS boot stalls"
    if marker in text:
        print("Cemu HLE BIOS fallback already installed")
        return

    old = (
        "   Config.SlowBoot = 0;\n"
        "   if (!use_hle)\n"
    )
    new = (
        "#ifdef HW_WUP\n"
        "   /* PS1WiiU fallback uses HLE BIOS to bypass Cemu BIOS boot stalls. */\n"
        "   extern int lightrec_custom_map_active;\n"
        "   if (!lightrec_custom_map_active) {\n"
        "      use_hle = true;\n"
        "      ps1wiiu_core_trace(\"BIOS selection: forcing HLE after custom-map failure\");\n"
        "   }\n"
        "#endif\n\n"
        "   Config.SlowBoot = 0;\n"
        "   if (!use_hle)\n"
    )
    text = replace_once(text, old, new, "force HLE BIOS when Cemu uses software memory maps")
    LIBRETRO.write_text(text, encoding="utf-8")


if not LIBRETRO.is_file() or not MAIN.is_file():
    raise SystemExit("PCSX-ReARMed source files are missing; check out submodules first.")

instrument_libretro()
instrument_load_game()
instrument_runtime_video()
instrument_main()
instrument_psxinit()
instrument_psxmem()
instrument_lightrec_map()
patch_cemu_software_map_fallback()
patch_cemu_hle_boot()
print("PCSX-ReARMed startup instrumentation and Cemu fallback are ready")

#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <coreinit/cache.h>
#include <coreinit/screen.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <sndcore2/core.h>
#include <sndcore2/device.h>
#include <sndcore2/voice.h>
#include <vpad/input.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/proc.h>

#include <libretro.h>

/* On hardware this volume is the SD card. Cemu maps <Cemu root>/sdcard
 * to the same fs:/vol/external01 volume, so both environments share this layout. */
#define SD_ROOT       "fs:/vol/external01"
#define APP_DIR       SD_ROOT "/wiiu/apps/PS1WiiU"
#define GAMES_DIR     APP_DIR "/games"
#define BIOS_DIR      APP_DIR "/bios"
#define SAVES_DIR     APP_DIR "/saves"
#define MEMCARDS_DIR  APP_DIR "/memcards"
#define STATES_DIR    APP_DIR "/states"
#define SCREENSHOTS_DIR APP_DIR "/screenshots"
#define SETTINGS_PATH APP_DIR "/settings.cfg"

#define MAX_REFERENCED_FILES 1024
#define MAX_SCAN_DEPTH 4

#define MAX_ROMS 256
#define MAX_ROM_NAME 256
#define MAX_ROM_PATH 512
#define MAX_FRAME_WIDTH 1024
#define MAX_FRAME_HEIGHT 512
#define AUDIO_RING_SAMPLES 16384
#define TV_WIDTH 1280
#define TV_HEIGHT 720
#define DRC_WIDTH 854
#define DRC_HEIGHT 480
#define MENU_ROWS 15

/* PCSX-ReARMed is compiled as a static libretro core. */
void retro_set_environment(retro_environment_t cb);
void retro_set_video_refresh(retro_video_refresh_t cb);
void retro_set_audio_sample(retro_audio_sample_t cb);
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb);
void retro_set_input_poll(retro_input_poll_t cb);
void retro_set_input_state(retro_input_state_t cb);
void retro_init(void);
void retro_deinit(void);
bool retro_load_game(const struct retro_game_info *game);
void retro_unload_game(void);
void retro_run(void);
void retro_reset(void);
void retro_get_system_av_info(struct retro_system_av_info *info);
void retro_set_controller_port_device(unsigned port, unsigned device);
void *retro_get_memory_data(unsigned id);
size_t retro_get_memory_size(unsigned id);
size_t retro_serialize_size(void);
bool retro_serialize(void *data, size_t size);
bool retro_unserialize(const void *data, size_t size);

typedef struct CoreOption {
    const char *key;
    const char *label;
    const char *values[8];
    unsigned value_count;
    unsigned selected;
} CoreOption;

/* These values are options understood by PCSX-ReARMed. Other core options
 * retain the core's own default instead of being forced by the frontend. */
static CoreOption g_options[] = {
    { "pcsx_rearmed_region", "Region", { "auto", "NTSC", "PAL" }, 3, 0 },
    { "pcsx_rearmed_bios", "BIOS", { "auto", "HLE" }, 2, 0 },
    { "pcsx_rearmed_drc", "Dynamic recompiler", { "enabled", "disabled" }, 2, 0 },
    { "pcsx_rearmed_psxclock", "PS1 CPU clock", { "auto", "57", "66", "75", "90", "100" }, 6, 0 },
    { "pcsx_rearmed_dithering", "Dithering", { "enabled", "disabled", "force" }, 3, 0 },
    { "pcsx_rearmed_frameskip_type", "Frameskip", { "disabled", "auto", "auto_threshold", "fixed_interval" }, 4, 0 },
    { "pcsx_rearmed_rgb32_output", "32-bit color output", { "disabled", "enabled" }, 2, 0 },
    { "pcsx_rearmed_show_bios_bootlogo", "BIOS boot logo", { "disabled", "enabled", "enabled_no_pcsx" }, 3, 0 },
    { "pcsx_rearmed_memcard1", "Memory Card 1", { "libretro", "serial", "shared", "none" }, 4, 0 },
    { "pcsx_rearmed_memcard2", "Memory Card 2", { "shared", "serial", "none" }, 3, 0 },
    { "pcsx_rearmed_gpu_slow_llists", "Accurate GPU lists", { "auto", "disabled", "enabled" }, 3, 0 },
    { "pcsx_rearmed_fractional_framerate", "Fractional frame rate", { "auto", "disabled", "enabled" }, 3, 0 }
};
#define OPTION_COUNT ((unsigned)(sizeof(g_options) / sizeof(g_options[0])))

typedef struct RomEntry {
    char name[MAX_ROM_NAME];
    char path[MAX_ROM_PATH];
} RomEntry;

typedef enum ScreenMode {
    SCREEN_LIBRARY = 0,
    SCREEN_SETTINGS,
    SCREEN_ABOUT,
    SCREEN_GAME
} ScreenMode;

static ScreenMode g_mode = SCREEN_LIBRARY;
static RomEntry g_roms[MAX_ROMS];
static unsigned g_rom_count = 0;
static unsigned g_rom_selected = 0;
static unsigned g_setting_selected = 0;
static bool g_game_loaded = false;
static bool g_core_variables_changed = true;
static bool g_suppress_game_input = false;
static bool g_save_combo_was_down = false;
static bool g_load_combo_was_down = false;
static char g_current_game_path[MAX_ROM_PATH] = "";
static char g_current_game_label[MAX_ROM_NAME] = "";
static char g_memory_card_path[MAX_ROM_PATH] = "";
static char g_state_path[MAX_ROM_PATH] = "";
static uint32_t g_last_card_hash = 0;
static unsigned g_autosave_frames = 0;
static char g_track_references[MAX_REFERENCED_FILES][MAX_ROM_PATH];
static unsigned g_track_reference_count = 0;
static char g_playlist_references[MAX_REFERENCED_FILES][MAX_ROM_PATH];
static unsigned g_playlist_reference_count = 0;
static enum retro_pixel_format g_pixel_format = RETRO_PIXEL_FORMAT_XRGB8888;
static char g_status[192] = "Ready. Copy your own PS1 images into games/.";
static char g_message[192] = "";
static unsigned g_message_frames = 0;

static void *g_tv_buffer = NULL;
static void *g_drc_buffer = NULL;
static size_t g_tv_buffer_size = 0;
static size_t g_drc_buffer_size = 0;

static uint8_t g_frame_buffer[MAX_FRAME_WIDTH * MAX_FRAME_HEIGHT * 4];
static unsigned g_frame_width = 0;
static unsigned g_frame_height = 0;
static unsigned g_frame_pitch = 0;
static bool g_frame_valid = false;

static VPADStatus g_vpad;
static VPADReadError g_vpad_error = VPAD_READ_NO_SAMPLES;

static AXVoice *g_audio_left_voice = NULL;
static AXVoice *g_audio_right_voice = NULL;
static int16_t g_audio_left[AUDIO_RING_SAMPLES] __attribute__((aligned(32)));
static int16_t g_audio_right[AUDIO_RING_SAMPLES] __attribute__((aligned(32)));
static volatile uint32_t g_audio_queued = 0;
static uint32_t g_audio_write_pos = 0;
static volatile bool g_audio_started = false;
static bool g_audio_initialized = false;
static unsigned g_audio_rate = 44100;

/*
 * PCSX-ReARMed's Lightrec backend calls this Wii U-specific executable-cache
 * hook after it writes generated machine code. Flush data cache lines before
 * invalidating instruction-cache lines so the CPU fetches the new code.
 */
void wiiu_clear_cache(void *start, void *end)
{
    uintptr_t begin = (uintptr_t)start;
    uintptr_t finish = (uintptr_t)end;
    uint32_t length;

    if (finish <= begin || finish - begin > UINT32_MAX) {
        return;
    }

    length = (uint32_t)(finish - begin);
    DCFlushRange(start, length);
    OSMemoryBarrier();
    ICInvalidateRange(start, length);
    OSMemoryBarrier();
}

static void set_status(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_status, sizeof(g_status), fmt, args);
    va_end(args);
}

static void show_message(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_message, sizeof(g_message), fmt, args);
    va_end(args);
    g_message_frames = 180;
    set_status("%s", g_message);
}

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
    char message[384];
    va_list args;
    (void)level;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    WHBLogPrintf("[PCSX-ReARMed] %s", message);
}

static void ensure_directory(const char *path)
{
    if (mkdir(path, 0777) != 0 && errno != EEXIST) {
        WHBLogPrintf("PS1WiiU: Could not create directory %s (errno %d)", path, errno);
    }
}

static void ensure_storage_layout(void)
{
    ensure_directory(APP_DIR);
    ensure_directory(GAMES_DIR);
    ensure_directory(BIOS_DIR);
    ensure_directory(SAVES_DIR);
    ensure_directory(MEMCARDS_DIR);
    ensure_directory(STATES_DIR);
    ensure_directory(SCREENSHOTS_DIR);
}

static int option_index(const char *key)
{
    unsigned i;
    for (i = 0; i < OPTION_COUNT; ++i) {
        if (strcmp(g_options[i].key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void load_settings(void)
{
    FILE *fp = fopen(SETTINGS_PATH, "r");
    char line[256];
    if (!fp) {
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *equals = strchr(line, '=');
        char *newline;
        int index;
        unsigned v;
        if (!equals) {
            continue;
        }
        *equals++ = '\0';
        newline = strpbrk(equals, "\r\n");
        if (newline) {
            *newline = '\0';
        }
        index = option_index(line);
        if (index < 0) {
            continue;
        }
        for (v = 0; v < g_options[index].value_count; ++v) {
            if (strcmp(equals, g_options[index].values[v]) == 0) {
                g_options[index].selected = v;
                break;
            }
        }
    }
    fclose(fp);
}

static void save_settings(void)
{
    FILE *fp = fopen(SETTINGS_PATH, "w");
    unsigned i;
    if (!fp) {
        set_status("Could not save settings.cfg. Check SD-card write access.");
        return;
    }
    fprintf(fp, "# PS1WiiU settings; values are PCSX-ReARMed core options.\n");
    for (i = 0; i < OPTION_COUNT; ++i) {
        fprintf(fp, "%s=%s\n", g_options[i].key,
                g_options[i].values[g_options[i].selected]);
    }
    fclose(fp);
    g_core_variables_changed = true;
    set_status("Settings saved. Most options apply to the next game launch.");
}

static bool ends_with_case(const char *text, const char *suffix)
{
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (text_len < suffix_len) {
        return false;
    }
    return strcasecmp(text + text_len - suffix_len, suffix) == 0;
}

static bool is_supported_image(const char *name)
{
    /* Matches PCSX-ReARMed's advertised valid_extensions list. */
    return ends_with_case(name, ".bin") ||
           ends_with_case(name, ".cue") ||
           ends_with_case(name, ".img") ||
           ends_with_case(name, ".mdf") ||
           ends_with_case(name, ".pbp") ||
           ends_with_case(name, ".toc") ||
           ends_with_case(name, ".cbn") ||
           ends_with_case(name, ".m3u") ||
           ends_with_case(name, ".chd") ||
           ends_with_case(name, ".iso") ||
           ends_with_case(name, ".exe");
}

static int compare_roms(const void *a, const void *b)
{
    const RomEntry *ra = (const RomEntry *)a;
    const RomEntry *rb = (const RomEntry *)b;
    return strcasecmp(ra->name, rb->name);
}

static bool is_playlist(const char *path)
{
    return ends_with_case(path, ".m3u");
}

static bool is_cue_sheet(const char *path)
{
    return ends_with_case(path, ".cue");
}

static bool is_data_track_extension(const char *path)
{
    return ends_with_case(path, ".bin") ||
           ends_with_case(path, ".img") ||
           ends_with_case(path, ".iso") ||
           ends_with_case(path, ".mdf") ||
           ends_with_case(path, ".toc");
}

static void normalize_reference_token(char *token)
{
    size_t i;
    size_t len;
    for (i = 0; token[i] != '\0'; ++i) {
        if (token[i] == '\\') token[i] = '/';
    }
    len = strlen(token);
    while (len > 0 && (token[len - 1] == '\r' || token[len - 1] == '\n' ||
                       token[len - 1] == ' ' || token[len - 1] == '\t')) {
        token[--len] = '\0';
    }
    while (token[0] == ' ' || token[0] == '\t') {
        memmove(token, token + 1, strlen(token));
    }
    while (strncmp(token, "./", 2) == 0) {
        memmove(token, token + 2, strlen(token + 2) + 1);
    }
}

static void resolve_reference_path(const char *owner_path, const char *token,
                                   char *out, size_t out_size)
{
    const char *slash;
    char directory[MAX_ROM_PATH];
    size_t dir_len;
    char clean_token[MAX_ROM_PATH];

    snprintf(clean_token, sizeof(clean_token), "%s", token);
    normalize_reference_token(clean_token);
    if (clean_token[0] == '\0') {
        out[0] = '\0';
        return;
    }

    /* A full Wii U filesystem path is accepted, though normal CUE/M3U files
     * should use paths relative to the file that contains the reference. */
    if (strstr(clean_token, "fs:/") == clean_token ||
        strstr(clean_token, "sd:/") == clean_token) {
        snprintf(out, out_size, "%s", clean_token);
        return;
    }

    snprintf(directory, sizeof(directory), "%s", owner_path);
    slash = strrchr(directory, '/');
    if (!slash) {
        out[0] = '\0';
        return;
    }
    dir_len = (size_t)(slash - directory);
    directory[dir_len] = '\0';
    snprintf(out, out_size, "%s/%s", directory, clean_token);
}

static void add_reference(char refs[MAX_REFERENCED_FILES][MAX_ROM_PATH],
                          unsigned *ref_count, const char *owner_path,
                          const char *token)
{
    char resolved[MAX_ROM_PATH];
    unsigned i;
    resolve_reference_path(owner_path, token, resolved, sizeof(resolved));
    if (resolved[0] == '\0') return;

    for (i = 0; i < *ref_count; ++i) {
        if (strcasecmp(refs[i], resolved) == 0) return;
    }
    if (*ref_count >= MAX_REFERENCED_FILES) return;
    snprintf(refs[*ref_count], MAX_ROM_PATH, "%s", resolved);
    ++(*ref_count);
}

static void collect_cue_references(const char *cue_path)
{
    FILE *fp = fopen(cue_path, "r");
    char line[1024];
    if (!fp) return;

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        char token[MAX_ROM_PATH];
        size_t len = 0;

        while (*p == ' ' || *p == '\t') ++p;
        if (strncasecmp(p, "FILE", 4) != 0 ||
            (p[4] != ' ' && p[4] != '\t')) continue;
        p += 4;
        while (*p == ' ' || *p == '\t') ++p;

        if (*p == '"') {
            ++p;
            while (*p && *p != '"' && len + 1 < sizeof(token)) {
                token[len++] = *p++;
            }
        } else {
            while (*p && *p != ' ' && *p != '\t' &&
                   *p != '\r' && *p != '\n' && len + 1 < sizeof(token)) {
                token[len++] = *p++;
            }
        }
        token[len] = '\0';
        if (len > 0) {
            add_reference(g_track_references, &g_track_reference_count,
                          cue_path, token);
        }
    }
    fclose(fp);
}

static void collect_playlist_references(const char *playlist_path)
{
    FILE *fp = fopen(playlist_path, "r");
    char line[MAX_ROM_PATH];
    if (!fp) return;

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        normalize_reference_token(p);
        if (p[0] == '\0' || p[0] == '#') continue;
        add_reference(g_playlist_references, &g_playlist_reference_count,
                      playlist_path, p);
    }
    fclose(fp);
}

static void collect_references_recursive(const char *directory, unsigned depth)
{
    DIR *dir = opendir(directory);
    struct dirent *entry;
    if (!dir) return;

    while ((entry = readdir(dir)) != NULL) {
        struct stat st;
        char path[MAX_ROM_PATH];
        if (entry->d_name[0] == '.') continue;
        if (snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name) >= (int)sizeof(path)) {
            continue;
        }
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < MAX_SCAN_DEPTH) collect_references_recursive(path, depth + 1);
        } else if (is_cue_sheet(path)) {
            collect_cue_references(path);
        } else if (is_playlist(path)) {
            collect_playlist_references(path);
        }
    }
    closedir(dir);
}

static bool path_is_referenced(const char *path,
                               char refs[MAX_REFERENCED_FILES][MAX_ROM_PATH],
                               unsigned ref_count)
{
    unsigned i;
    for (i = 0; i < ref_count; ++i) {
        if (strcasecmp(path, refs[i]) == 0) return true;
    }
    return false;
}

static void scan_games_recursive(const char *directory, unsigned depth)
{
    DIR *dir = opendir(directory);
    struct dirent *entry;
    if (!dir) return;

    while ((entry = readdir(dir)) != NULL && g_rom_count < MAX_ROMS) {
        struct stat st;
        char path[MAX_ROM_PATH];
        RomEntry *rom;

        if (entry->d_name[0] == '.') continue;
        if (snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name) >= (int)sizeof(path)) {
            continue;
        }
        if (stat(path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (depth < MAX_SCAN_DEPTH) scan_games_recursive(path, depth + 1);
            continue;
        }
        if (!is_supported_image(entry->d_name)) continue;

        /* Track files referenced by a CUE are not separate launch entries;
         * a .bin without a matching CUE remains launchable on its own. */
        if (is_data_track_extension(path) &&
            path_is_referenced(path, g_track_references, g_track_reference_count)) {
            continue;
        }

        /* In a multi-disc M3U, list the playlist once rather than each disc
         * again as a separate library entry. */
        if (!is_playlist(path) &&
            path_is_referenced(path, g_playlist_references, g_playlist_reference_count)) {
            continue;
        }

        rom = &g_roms[g_rom_count];
        snprintf(rom->path, sizeof(rom->path), "%s", path);
        snprintf(rom->name, sizeof(rom->name), "%s", path + strlen(GAMES_DIR) + 1);
        ++g_rom_count;
    }
    closedir(dir);
}

static void scan_games(void)
{
    struct stat st;
    g_rom_count = 0;
    g_rom_selected = 0;
    g_track_reference_count = 0;
    g_playlist_reference_count = 0;

    if (stat(GAMES_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
        set_status("Cannot open games folder. Check SD-card access.");
        return;
    }

    /* Collect CUE and M3U references first, so library order can't cause
     * a .bin track to be shown before its .cue is discovered. */
    collect_references_recursive(GAMES_DIR, 0);
    scan_games_recursive(GAMES_DIR, 0);
    qsort(g_roms, g_rom_count, sizeof(g_roms[0]), compare_roms);

    if (g_rom_count == 0) {
        set_status("No games found. Supported formats: BIN/CUE, CHD, PBP, ISO, IMG, MDF, TOC, CBN, M3U and PS-X EXE.");
    } else {
        set_status("Found %u game image(s). Select one with the GamePad.", g_rom_count);
    }
}

static bool core_environment(unsigned command, void *data)
{
    switch (command) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format format = *(enum retro_pixel_format *)data;
        if (format == RETRO_PIXEL_FORMAT_XRGB8888 ||
            format == RETRO_PIXEL_FORMAT_RGB565 ||
            format == RETRO_PIXEL_FORMAT_0RGB1555) {
            g_pixel_format = format;
            return true;
        }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = BIOS_DIR;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = SAVES_DIR;
        return true;
    case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
        *(const char **)data = GAMES_DIR;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *variable = (struct retro_variable *)data;
        int index = option_index(variable->key);
        if (index < 0) {
            return false;
        }
        variable->value = g_options[index].values[g_options[index].selected];
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = g_core_variables_changed;
        g_core_variables_changed = false;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 2;
        return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
        return true;
    case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE:
        *(float *)data = 60.0f;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE: {
        const struct retro_message *message = (const struct retro_message *)data;
        if (message && message->msg) {
            snprintf(g_message, sizeof(g_message), "%s", message->msg);
            g_message_frames = message->frames;
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        *(bool *)data = false;
        return true;
    default:
        return false;
    }
}

static void video_refresh(const void *data, unsigned width, unsigned height,
                          size_t pitch)
{
    unsigned pixel_size;
    size_t bytes;
    if (!data) {
        return;
    }
    if (!width || !height || width > MAX_FRAME_WIDTH || height > MAX_FRAME_HEIGHT) {
        return;
    }
    pixel_size = (g_pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) ? 4u : 2u;
    if (pitch < (size_t)width * pixel_size || pitch > MAX_FRAME_WIDTH * 4u) {
        return;
    }
    bytes = pitch * height;
    if (bytes > sizeof(g_frame_buffer)) {
        return;
    }
    memcpy(g_frame_buffer, data, bytes);
    g_frame_width = width;
    g_frame_height = height;
    g_frame_pitch = (unsigned)pitch;
    g_frame_valid = true;
}

static void audio_set_voice_mix(AXVoice *voice, bool right_channel)
{
    AXVoiceDeviceMixData tv_mix[6] = { 0 };
    AXVoiceDeviceMixData drc_mix[4] = { 0 };
    unsigned channel = right_channel ? 1u : 0u;

    tv_mix[channel].bus[0].volume = 0x8000;
    drc_mix[channel].bus[0].volume = 0x8000;
    AXVoiceBegin(voice);
    AXSetVoiceType(voice, 0);
    AXSetVoiceDeviceMix(voice, AX_DEVICE_TYPE_TV, 0, tv_mix);
    AXSetVoiceDeviceMix(voice, AX_DEVICE_TYPE_DRC, 0, drc_mix);

    AXVoiceOffsets offsets = { 0 };
    offsets.data = right_channel ? g_audio_right : g_audio_left;
    offsets.dataType = AX_VOICE_FORMAT_LPCM16;
    offsets.loopingEnabled = AX_VOICE_LOOP_ENABLED;
    offsets.loopOffset = 0;
    offsets.endOffset = AUDIO_RING_SAMPLES - 1;
    offsets.currentOffset = 0;
    AXSetVoiceOffsets(voice, &offsets);

    AXVoiceVeData volume = { 0x8000, 0 };
    AXSetVoiceVe(voice, &volume);
    AXSetVoiceState(voice, AX_VOICE_STATE_STOPPED);
    AXVoiceEnd(voice);

    AXVoiceSrc src = { 0 };
    src.ratio = (uint32_t)(65536.0f * ((float)g_audio_rate / (float)AXGetInputSamplesPerSec()));
    AXSetVoiceSrc(voice, &src);
    AXSetVoiceSrcType(voice, src.ratio == 0x10000 ? AX_VOICE_SRC_TYPE_NONE : AX_VOICE_SRC_TYPE_LINEAR);
}

static void audio_set_sample_rate(unsigned rate)
{
    if (rate < 8000 || rate > 96000) {
        rate = 44100;
    }
    g_audio_rate = rate;
    if (g_audio_initialized && g_audio_left_voice && g_audio_right_voice) {
        AXVoiceSrc src = { 0 };
        src.ratio = (uint32_t)(65536.0f * ((float)g_audio_rate / (float)AXGetInputSamplesPerSec()));
        AXSetVoiceSrc(g_audio_left_voice, &src);
        AXSetVoiceSrcType(g_audio_left_voice, src.ratio == 0x10000 ? AX_VOICE_SRC_TYPE_NONE : AX_VOICE_SRC_TYPE_LINEAR);
        AXSetVoiceSrc(g_audio_right_voice, &src);
        AXSetVoiceSrcType(g_audio_right_voice, src.ratio == 0x10000 ? AX_VOICE_SRC_TYPE_NONE : AX_VOICE_SRC_TYPE_LINEAR);
    }
}

static void audio_frame_callback(void)
{
    uint32_t queued;
    uint32_t consume;
    if (!g_audio_initialized || !g_audio_started) {
        return;
    }
    consume = (uint32_t)(((uint64_t)AXGetInputSamplesPerFrame() * g_audio_rate) /
                         (AXGetInputSamplesPerSec() ? AXGetInputSamplesPerSec() : 48000));
    queued = __atomic_load_n(&g_audio_queued, __ATOMIC_RELAXED);
    for (;;) {
        uint32_t next = queued > consume ? queued - consume : 0;
        if (__atomic_compare_exchange_n(&g_audio_queued, &queued, next, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            queued = next;
            break;
        }
    }
    if (queued < 128) {
        if (g_audio_left_voice) {
            AXSetVoiceState(g_audio_left_voice, AX_VOICE_STATE_STOPPED);
        }
        if (g_audio_right_voice) {
            AXSetVoiceState(g_audio_right_voice, AX_VOICE_STATE_STOPPED);
        }
        g_audio_started = false;
        __atomic_store_n(&g_audio_queued, 0, __ATOMIC_RELAXED);
    }
}

static void audio_init(void)
{
    AXInitParams params = { 0 };
    params.renderer = AX_INIT_RENDERER_48KHZ;
    params.pipeline = AX_INIT_PIPELINE_SINGLE;
    AXInitWithParams(&params);

    g_audio_left_voice = AXAcquireVoice(31, NULL, NULL);
    g_audio_right_voice = AXAcquireVoice(31, NULL, NULL);
    if (!g_audio_left_voice || !g_audio_right_voice) {
        if (g_audio_left_voice) {
            AXFreeVoice(g_audio_left_voice);
            g_audio_left_voice = NULL;
        }
        if (g_audio_right_voice) {
            AXFreeVoice(g_audio_right_voice);
            g_audio_right_voice = NULL;
        }
        AXQuit();
        set_status("Audio voices unavailable; continuing without sound.");
        return;
    }

    memset(g_audio_left, 0, sizeof(g_audio_left));
    memset(g_audio_right, 0, sizeof(g_audio_right));
    DCStoreRange(g_audio_left, sizeof(g_audio_left));
    DCStoreRange(g_audio_right, sizeof(g_audio_right));
    audio_set_voice_mix(g_audio_left_voice, false);
    audio_set_voice_mix(g_audio_right_voice, true);
    g_audio_write_pos = 0;
    g_audio_queued = 0;
    g_audio_started = false;
    g_audio_initialized = true;
    AXRegisterAppFrameCallback(audio_frame_callback);
}

static void audio_reset(void)
{
    g_audio_write_pos = 0;
    __atomic_store_n(&g_audio_queued, 0, __ATOMIC_RELAXED);
    g_audio_started = false;
    memset(g_audio_left, 0, sizeof(g_audio_left));
    memset(g_audio_right, 0, sizeof(g_audio_right));
    DCStoreRange(g_audio_left, sizeof(g_audio_left));
    DCStoreRange(g_audio_right, sizeof(g_audio_right));
    if (g_audio_left_voice) {
        AXSetVoiceState(g_audio_left_voice, AX_VOICE_STATE_STOPPED);
        AXSetVoiceCurrentOffset(g_audio_left_voice, 0);
    }
    if (g_audio_right_voice) {
        AXSetVoiceState(g_audio_right_voice, AX_VOICE_STATE_STOPPED);
        AXSetVoiceCurrentOffset(g_audio_right_voice, 0);
    }
}

static void audio_shutdown(void)
{
    if (!g_audio_initialized) {
        return;
    }
    AXDeregisterAppFrameCallback(audio_frame_callback);
    if (g_audio_left_voice) {
        AXSetVoiceState(g_audio_left_voice, AX_VOICE_STATE_STOPPED);
        AXFreeVoice(g_audio_left_voice);
        g_audio_left_voice = NULL;
    }
    if (g_audio_right_voice) {
        AXSetVoiceState(g_audio_right_voice, AX_VOICE_STATE_STOPPED);
        AXFreeVoice(g_audio_right_voice);
        g_audio_right_voice = NULL;
    }
    AXQuit();
    g_audio_initialized = false;
    g_audio_started = false;
}

static void audio_push(const int16_t *samples, size_t frames)
{
    uint32_t queued;
    size_t writable;
    size_t first;
    size_t i;
    if (!g_audio_initialized || !g_audio_left_voice || !g_audio_right_voice || !samples || frames == 0) {
        return;
    }

    queued = __atomic_load_n(&g_audio_queued, __ATOMIC_RELAXED);
    if (queued >= AUDIO_RING_SAMPLES - 1) {
        return;
    }
    writable = (AUDIO_RING_SAMPLES - 1) - queued;
    if (frames > writable) {
        frames = writable;
    }
    if (frames == 0) {
        return;
    }

    first = frames;
    if (first > AUDIO_RING_SAMPLES - g_audio_write_pos) {
        first = AUDIO_RING_SAMPLES - g_audio_write_pos;
    }
    for (i = 0; i < first; ++i) {
        g_audio_left[g_audio_write_pos + i] = samples[i * 2];
        g_audio_right[g_audio_write_pos + i] = samples[i * 2 + 1];
    }
    for (i = first; i < frames; ++i) {
        g_audio_left[i - first] = samples[i * 2];
        g_audio_right[i - first] = samples[i * 2 + 1];
    }
    if (first) {
        DCStoreRange(&g_audio_left[g_audio_write_pos], first * sizeof(int16_t));
        DCStoreRange(&g_audio_right[g_audio_write_pos], first * sizeof(int16_t));
    }
    if (frames > first) {
        DCStoreRange(g_audio_left, (frames - first) * sizeof(int16_t));
        DCStoreRange(g_audio_right, (frames - first) * sizeof(int16_t));
    }

    g_audio_write_pos = (g_audio_write_pos + (uint32_t)frames) % AUDIO_RING_SAMPLES;
    queued = __atomic_add_fetch(&g_audio_queued, (uint32_t)frames, __ATOMIC_RELAXED);

    if (!g_audio_started && queued >= 3072) {
        uint32_t start = (g_audio_write_pos + AUDIO_RING_SAMPLES - queued) % AUDIO_RING_SAMPLES;
        AXSetVoiceCurrentOffset(g_audio_left_voice, start);
        AXSetVoiceCurrentOffset(g_audio_right_voice, start);
        AXSetVoiceState(g_audio_left_voice, AX_VOICE_STATE_PLAYING);
        AXSetVoiceState(g_audio_right_voice, AX_VOICE_STATE_PLAYING);
        g_audio_started = true;
    }
}

static void audio_sample(int16_t left, int16_t right)
{
    int16_t pair[2] = { left, right };
    audio_push(pair, 1);
}

static size_t audio_sample_batch(const int16_t *data, size_t frames)
{
    audio_push(data, frames);
    return frames;
}

static int16_t analog_value(float value)
{
    if (value > 1.0f) value = 1.0f;
    if (value < -1.0f) value = -1.0f;
    return (int16_t)(value * 32767.0f);
}

static void input_poll(void)
{
    /* The outer app loop samples VPAD once per frame. Reuse that exact sample
     * for every libretro input query within retro_run(). */
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    uint32_t hold = g_vpad.hold;
    if (g_suppress_game_input || port != 0) {
        return 0;
    }

    if (device == RETRO_DEVICE_JOYPAD) {
        switch (id) {
        case RETRO_DEVICE_ID_JOYPAD_B: return (hold & VPAD_BUTTON_A) != 0; /* Cross */
        case RETRO_DEVICE_ID_JOYPAD_A: return (hold & VPAD_BUTTON_B) != 0; /* Circle */
        case RETRO_DEVICE_ID_JOYPAD_Y: return (hold & VPAD_BUTTON_Y) != 0; /* Square */
        case RETRO_DEVICE_ID_JOYPAD_X: return (hold & VPAD_BUTTON_X) != 0; /* Triangle */
        case RETRO_DEVICE_ID_JOYPAD_UP: return (hold & VPAD_BUTTON_UP) != 0;
        case RETRO_DEVICE_ID_JOYPAD_DOWN: return (hold & VPAD_BUTTON_DOWN) != 0;
        case RETRO_DEVICE_ID_JOYPAD_LEFT: return (hold & VPAD_BUTTON_LEFT) != 0;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT: return (hold & VPAD_BUTTON_RIGHT) != 0;
        case RETRO_DEVICE_ID_JOYPAD_L: return (hold & VPAD_BUTTON_L) != 0;
        case RETRO_DEVICE_ID_JOYPAD_R: return (hold & VPAD_BUTTON_R) != 0;
        case RETRO_DEVICE_ID_JOYPAD_L2: return (hold & VPAD_BUTTON_ZL) != 0;
        case RETRO_DEVICE_ID_JOYPAD_R2: return (hold & VPAD_BUTTON_ZR) != 0;
        case RETRO_DEVICE_ID_JOYPAD_SELECT: return (hold & VPAD_BUTTON_MINUS) != 0;
        case RETRO_DEVICE_ID_JOYPAD_START: return (hold & VPAD_BUTTON_PLUS) != 0;
        case RETRO_DEVICE_ID_JOYPAD_L3: return (hold & VPAD_BUTTON_STICK_L) != 0;
        case RETRO_DEVICE_ID_JOYPAD_R3: return (hold & VPAD_BUTTON_STICK_R) != 0;
        default: return 0;
        }
    }

    if (device == RETRO_DEVICE_ANALOG) {
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
            if (id == RETRO_DEVICE_ID_ANALOG_X) return analog_value(g_vpad.leftStick.x);
            if (id == RETRO_DEVICE_ID_ANALOG_Y) return analog_value(-g_vpad.leftStick.y);
        } else if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
            if (id == RETRO_DEVICE_ID_ANALOG_X) return analog_value(g_vpad.rightStick.x);
            if (id == RETRO_DEVICE_ID_ANALOG_Y) return analog_value(-g_vpad.rightStick.y);
        }
    }
    return 0;
}

static void render_one_pixel(uint8_t *dst, unsigned sx, unsigned sy)
{
    unsigned r = 0, g = 0, b = 0;
    const uint8_t *row = g_frame_buffer + (size_t)sy * g_frame_pitch;
    if (g_pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
        uint32_t value;
        memcpy(&value, row + (size_t)sx * 4, sizeof(value));
        r = (value >> 16) & 0xff;
        g = (value >> 8) & 0xff;
        b = value & 0xff;
    } else if (g_pixel_format == RETRO_PIXEL_FORMAT_RGB565) {
        uint16_t value;
        memcpy(&value, row + (size_t)sx * 2, sizeof(value));
        r = ((value >> 11) & 0x1f) * 255 / 31;
        g = ((value >> 5) & 0x3f) * 255 / 63;
        b = (value & 0x1f) * 255 / 31;
    } else {
        uint16_t value;
        memcpy(&value, row + (size_t)sx * 2, sizeof(value));
        r = ((value >> 10) & 0x1f) * 255 / 31;
        g = ((value >> 5) & 0x1f) * 255 / 31;
        b = (value & 0x1f) * 255 / 31;
    }
    /* OSScreen's RGBX framebuffer expects R,G,B in the high three bytes. */
    *(uint32_t *)dst = (r << 24) | (g << 16) | (b << 8);
}

static void draw_scaled_frame(void *buffer, unsigned screen_width, unsigned screen_height,
                              unsigned dst_x, unsigned dst_y, unsigned dst_width, unsigned dst_height)
{
    unsigned y, x;
    uint8_t *pixels = (uint8_t *)buffer;
    for (y = 0; y < dst_height; ++y) {
        unsigned sy = (unsigned)(((uint64_t)y * g_frame_height) / dst_height);
        uint8_t *row = pixels + (size_t)(dst_y + y) * screen_width * 4 + (size_t)dst_x * 4;
        for (x = 0; x < dst_width; ++x) {
            unsigned sx = (unsigned)(((uint64_t)x * g_frame_width) / dst_width);
            render_one_pixel(row + (size_t)x * 4, sx, sy);
        }
    }
    (void)screen_height;
}

static void draw_text_both(const char *text, unsigned tv_x, unsigned tv_y,
                           unsigned drc_x, unsigned drc_y)
{
    OSScreenPutFontEx(SCREEN_TV, tv_x, tv_y, text);
    OSScreenPutFontEx(SCREEN_DRC, drc_x, drc_y, text);
}

static void draw_next_both(const char *text, unsigned *line)
{
    draw_text_both(text, 2, *line, 2, *line);
    ++(*line);
}

static void draw_menu(void)
{
    unsigned i;
    unsigned line = 5;
    char text[160];

    OSScreenClearBufferEx(SCREEN_TV, 0x10182000);
    OSScreenClearBufferEx(SCREEN_DRC, 0x10182000);

    draw_text_both("PS1WiiU  |  Native PlayStation emulator", 2, 1, 2, 1);
    draw_text_both(g_mode == SCREEN_LIBRARY ? "[ Library ]  Settings  About" :
                   (g_mode == SCREEN_SETTINGS ? "Library  [ Settings ]  About" :
                    "Library  Settings  [ About ]"), 2, 3, 2, 3);
    draw_text_both("-----------------------------------------------", 2, 4, 2, 4);

    if (g_mode == SCREEN_LIBRARY) {
        snprintf(text, sizeof(text), "Games: %u / %u", g_rom_count, MAX_ROMS);
        draw_next_both(text, &line);
        if (g_rom_count == 0) {
            draw_next_both("Place your own .cue, .chd, .pbp, .iso or .img files in:", &line);
            draw_next_both(GAMES_DIR, &line);
            draw_next_both("Press Y to scan again.", &line);
        } else {
            unsigned first = g_rom_selected >= 7 ? g_rom_selected - 7 : 0;
            unsigned end = first + MENU_ROWS;
            if (end > g_rom_count) {
                end = g_rom_count;
                first = end > MENU_ROWS ? end - MENU_ROWS : 0;
            }
            for (i = first; i < end; ++i) {
                snprintf(text, sizeof(text), "%c %s", i == g_rom_selected ? '>' : ' ', g_roms[i].name);
                OSScreenPutFontEx(SCREEN_TV, 2, line, text);
                OSScreenPutFontEx(SCREEN_DRC, 2, line, text);
                ++line;
            }
        }
        draw_text_both("A: Launch   Y: Rescan   X: Settings   B: About", 2, 25, 2, 25);
    } else if (g_mode == SCREEN_SETTINGS) {
        draw_next_both("A / Left / Right: change option; settings persist on SD.", &line);
        for (i = 0; i < OPTION_COUNT && i < 17; ++i) {
            snprintf(text, sizeof(text), "%c %-27s : %s",
                     i == g_setting_selected ? '>' : ' ',
                     g_options[i].label,
                     g_options[i].values[g_options[i].selected]);
            OSScreenPutFontEx(SCREEN_TV, 2, line, text);
            OSScreenPutFontEx(SCREEN_DRC, 2, line, text);
            ++line;
        }
        draw_text_both("Settings take effect when the next game is loaded.", 2, 25, 2, 25);
    } else {
        draw_next_both("Emulation core: PCSX-ReARMed (GPL-2.0-or-later)", &line);
        draw_next_both("GamePad controls: D-pad to navigate; left stick in games.", &line);
        draw_next_both("A: confirm    X: change tab    Y: rescan games", &line);
        draw_next_both("PS Cross=A, Circle=B, Square=Y, Triangle=X.", &line);
        draw_next_both("BIOS files are read from the BIOS folder.", &line);
        draw_next_both(GAMES_DIR, &line);
    }

    if (g_status[0]) {
        snprintf(text, sizeof(text), "Status: %.125s", g_status);
        OSScreenPutFontEx(SCREEN_TV, 2, 27, text);
        OSScreenPutFontEx(SCREEN_DRC, 2, 27, text);
    }
}

static void draw_game(void)
{
    OSScreenClearBufferEx(SCREEN_TV, 0x00000000);
    OSScreenClearBufferEx(SCREEN_DRC, 0x00000000);
    if (g_frame_valid) {
        draw_scaled_frame(g_tv_buffer, TV_WIDTH, TV_HEIGHT, 160, 0, 960, 720);
        draw_scaled_frame(g_drc_buffer, DRC_WIDTH, DRC_HEIGHT, 107, 0, 640, 480);
    } else {
        OSScreenPutFontEx(SCREEN_TV, 2, 2, "Waiting for PS1 video...");
        OSScreenPutFontEx(SCREEN_DRC, 2, 2, "Waiting for PS1 video...");
    }
    OSScreenPutFontEx(SCREEN_DRC, 2, 26, "Save: ZL+ZR+Plus | Load: ZL+ZR+Minus");
    OSScreenPutFontEx(SCREEN_DRC, 2, 28, "Hold L + R + - to return to library");
    if (g_message_frames > 0 && g_message[0]) {
        OSScreenPutFontEx(SCREEN_TV, 2, 2, g_message);
        OSScreenPutFontEx(SCREEN_DRC, 2, 2, g_message);
    }
}

static void flip_screens(void)
{
    DCStoreRange(g_tv_buffer, g_tv_buffer_size);
    DCStoreRange(g_drc_buffer, g_drc_buffer_size);
    OSScreenFlipBuffersEx(SCREEN_TV);
    OSScreenFlipBuffersEx(SCREEN_DRC);
}

static void cycle_option(CoreOption *option, int direction)
{
    int value = (int)option->selected + direction;
    if (value < 0) value = (int)option->value_count - 1;
    if (value >= (int)option->value_count) value = 0;
    option->selected = (unsigned)value;
    save_settings();
}

static bool build_game_sidecar_path(const char *directory, const char *extension,
                                    char *out, size_t out_size)
{
    char safe_name[MAX_ROM_PATH];
    const char *relative;
    size_t i, out_pos = 0;
    char *last_dot;

    if (g_current_game_path[0] == '\0' || strlen(GAMES_DIR) >= strlen(g_current_game_path)) {
        return false;
    }
    relative = g_current_game_path + strlen(GAMES_DIR);
    if (*relative == '/') ++relative;

    for (i = 0; relative[i] != '\0' && out_pos + 1 < sizeof(safe_name); ++i) {
        unsigned char c = (unsigned char)relative[i];
        if (c == '/' || c == '\\') {
            safe_name[out_pos++] = '_';
        } else if (c < 32 || c == ':' || c == '*' || c == '?' ||
                   c == '"' || c == '<' || c == '>' || c == '|') {
            safe_name[out_pos++] = '_';
        } else {
            safe_name[out_pos++] = (char)c;
        }
    }
    safe_name[out_pos] = '\0';
    last_dot = strrchr(safe_name, '.');
    if (last_dot) *last_dot = '\0';
    if (safe_name[0] == '\0') {
        snprintf(safe_name, sizeof(safe_name), "game");
    }
    return snprintf(out, out_size, "%s/%s%s", directory, safe_name, extension) < (int)out_size;
}

static uint32_t hash_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static void load_memory_card(void)
{
    void *data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    FILE *fp;
    long file_size;

    g_last_card_hash = 0;
    g_autosave_frames = 0;
    g_memory_card_path[0] = '\0';
    if (!data || size == 0 ||
        !build_game_sidecar_path(MEMCARDS_DIR, ".srm",
                                 g_memory_card_path, sizeof(g_memory_card_path))) {
        return;
    }

    fp = fopen(g_memory_card_path, "rb");
    if (fp) {
        if (fseek(fp, 0, SEEK_END) == 0) {
            file_size = ftell(fp);
            rewind(fp);
            if (file_size == (long)size) {
                if (fread(data, 1, size, fp) == size) {
                    show_message("Loaded memory card: %s", g_current_game_label);
                }
            } else {
                set_status("Ignoring invalid memory-card file for %s.", g_current_game_label);
            }
        }
        fclose(fp);
    }
    g_last_card_hash = hash_bytes(data, size);
}

static void save_memory_card(bool force)
{
    void *data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    uint32_t hash;
    FILE *fp;

    if (!g_game_loaded || !data || size == 0 || g_memory_card_path[0] == '\0') {
        return;
    }
    hash = hash_bytes(data, size);
    if (!force && hash == g_last_card_hash) return;

    fp = fopen(g_memory_card_path, "wb");
    if (!fp) {
        show_message("Cannot write memory card. Check SD-card permissions.");
        return;
    }
    if (fwrite(data, 1, size, fp) != size) {
        fclose(fp);
        show_message("Memory-card write failed.");
        return;
    }
    fclose(fp);
    g_last_card_hash = hash;
}

static bool save_state_to_disk(void)
{
    size_t size = retro_serialize_size();
    void *buffer;
    FILE *fp;
    bool ok;

    if (!g_game_loaded || size == 0 || size > (16u * 1024u * 1024u) ||
        !build_game_sidecar_path(STATES_DIR, ".state", g_state_path, sizeof(g_state_path))) {
        show_message("Cannot save state for this game.");
        return false;
    }
    buffer = malloc(size);
    if (!buffer) {
        show_message("Not enough memory for a save state.");
        return false;
    }
    if (!retro_serialize(buffer, size)) {
        free(buffer);
        show_message("The PS1 core could not create a save state.");
        return false;
    }
    fp = fopen(g_state_path, "wb");
    if (!fp) {
        free(buffer);
        show_message("Cannot write save state. Check SD-card permissions.");
        return false;
    }
    ok = fwrite(buffer, 1, size, fp) == size;
    fclose(fp);
    free(buffer);
    if (ok) {
        show_message("State saved: %s", g_current_game_label);
    } else {
        show_message("Save-state write failed.");
    }
    return ok;
}

static bool load_state_from_disk(void)
{
    size_t size = retro_serialize_size();
    void *buffer;
    FILE *fp;
    long file_size;
    bool ok = false;

    if (!g_game_loaded || size == 0 || size > (16u * 1024u * 1024u) ||
        !build_game_sidecar_path(STATES_DIR, ".state", g_state_path, sizeof(g_state_path))) {
        show_message("Cannot load state for this game.");
        return false;
    }
    fp = fopen(g_state_path, "rb");
    if (!fp) {
        show_message("No save state found for %s.", g_current_game_label);
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        show_message("Could not read save state.");
        return false;
    }
    file_size = ftell(fp);
    rewind(fp);
    if (file_size != (long)size) {
        fclose(fp);
        show_message("Save state size does not match this core build.");
        return false;
    }

    buffer = malloc(size);
    if (!buffer) {
        fclose(fp);
        show_message("Not enough memory to load a save state.");
        return false;
    }
    if (fread(buffer, 1, size, fp) == size) {
        ok = retro_unserialize(buffer, size);
    }
    fclose(fp);
    free(buffer);
    if (ok) {
        audio_reset();
        g_last_card_hash = 0;
        load_memory_card();
        show_message("State loaded: %s", g_current_game_label);
    } else {
        show_message("The PS1 core rejected this save state.");
    }
    return ok;
}

static void launch_selected_game(void)
{
    struct retro_game_info game = { 0 };
    struct retro_system_av_info av_info;

    if (g_rom_count == 0 || g_rom_selected >= g_rom_count) {
        set_status("No game selected. Add your own PS1 image files to games/.");
        return;
    }

    snprintf(g_current_game_path, sizeof(g_current_game_path), "%s", g_roms[g_rom_selected].path);
    snprintf(g_current_game_label, sizeof(g_current_game_label), "%s", g_roms[g_rom_selected].name);
    g_memory_card_path[0] = '\0';
    g_state_path[0] = '\0';
    audio_reset();
    g_frame_valid = false;

    /* Request the DualShock profile so PCSX-ReARMed actually polls both
     * analog sticks instead of leaving the virtual pad in digital-only mode. */
    retro_set_controller_port_device(0, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1));
    game.path = g_current_game_path;
    if (!retro_load_game(&game)) {
        set_status("Could not load %s. Check the image and BIOS settings.", g_current_game_label);
        g_current_game_path[0] = '\0';
        g_current_game_label[0] = '\0';
        return;
    }

    g_game_loaded = true;
    g_mode = SCREEN_GAME;
    load_memory_card();
    retro_get_system_av_info(&av_info);
    if (av_info.timing.sample_rate > 0.0) {
        audio_set_sample_rate((unsigned)av_info.timing.sample_rate);
    }
    set_status("Loaded %s", g_current_game_label);
}

static void leave_game(void)
{
    if (g_game_loaded) {
        save_memory_card(true);
        retro_unload_game();
        g_game_loaded = false;
    }
    audio_reset();
    g_frame_valid = false;
    g_suppress_game_input = false;
    g_current_game_path[0] = '\0';
    g_current_game_label[0] = '\0';
    g_memory_card_path[0] = '\0';
    g_state_path[0] = '\0';
    g_mode = SCREEN_LIBRARY;
    scan_games();
}

static void handle_menu_input(void)
{
    uint32_t trigger = g_vpad.trigger;
    uint32_t hold = g_vpad.hold;

    if (trigger & VPAD_BUTTON_X) {
        if (g_mode == SCREEN_LIBRARY) g_mode = SCREEN_SETTINGS;
        else if (g_mode == SCREEN_SETTINGS) g_mode = SCREEN_ABOUT;
        else g_mode = SCREEN_LIBRARY;
        return;
    }
    if (g_mode == SCREEN_LIBRARY) {
        if (trigger & VPAD_BUTTON_UP) {
            if (g_rom_count && g_rom_selected > 0) --g_rom_selected;
        } else if (trigger & VPAD_BUTTON_DOWN) {
            if (g_rom_count && g_rom_selected + 1 < g_rom_count) ++g_rom_selected;
        } else if (trigger & VPAD_BUTTON_A) {
            launch_selected_game();
        } else if (trigger & VPAD_BUTTON_Y) {
            scan_games();
        } else if (trigger & VPAD_BUTTON_B) {
            g_mode = SCREEN_ABOUT;
        }
    } else if (g_mode == SCREEN_SETTINGS) {
        if (trigger & VPAD_BUTTON_UP) {
            if (g_setting_selected > 0) --g_setting_selected;
        } else if (trigger & VPAD_BUTTON_DOWN) {
            if (g_setting_selected + 1 < OPTION_COUNT) ++g_setting_selected;
        } else if (trigger & VPAD_BUTTON_LEFT) {
            cycle_option(&g_options[g_setting_selected], -1);
        } else if (trigger & VPAD_BUTTON_RIGHT) {
            cycle_option(&g_options[g_setting_selected], 1);
        } else if (trigger & VPAD_BUTTON_A) {
            cycle_option(&g_options[g_setting_selected], 1);
        } else if (trigger & VPAD_BUTTON_B) {
            g_mode = SCREEN_LIBRARY;
        } else if (hold & VPAD_BUTTON_Y) {
            save_settings();
        }
    } else {
        if (trigger & VPAD_BUTTON_A) {
            g_mode = SCREEN_LIBRARY;
        } else if (trigger & VPAD_BUTTON_B) {
            g_mode = SCREEN_LIBRARY;
        }
    }
}

int main(void)
{
    unsigned exit_chord_frames = 0;
    uint64_t frame_budget;

    WHBProcInit();
    WHBLogCafeInit();
    ensure_storage_layout();
    load_settings();
    scan_games();

    OSScreenInit();
    g_tv_buffer_size = OSScreenGetBufferSizeEx(SCREEN_TV);
    g_drc_buffer_size = OSScreenGetBufferSizeEx(SCREEN_DRC);
    g_tv_buffer = memalign(0x100, g_tv_buffer_size);
    g_drc_buffer = memalign(0x100, g_drc_buffer_size);
    if (!g_tv_buffer || !g_drc_buffer) {
        set_status("Out of memory creating video buffers.");
        free(g_tv_buffer);
        free(g_drc_buffer);
        OSScreenShutdown();
        WHBLogCafeDeinit();
        WHBProcShutdown();
        return 1;
    }

    OSScreenSetBufferEx(SCREEN_TV, g_tv_buffer);
    OSScreenSetBufferEx(SCREEN_DRC, g_drc_buffer);
    OSScreenEnableEx(SCREEN_TV, true);
    OSScreenEnableEx(SCREEN_DRC, true);

    audio_init();

    retro_set_environment(core_environment);
    retro_set_video_refresh(video_refresh);
    retro_set_audio_sample(audio_sample);
    retro_set_audio_sample_batch(audio_sample_batch);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
    retro_init();

    frame_budget = OSMillisecondsToTicks(16);
    while (WHBProcIsRunning()) {
        uint64_t start = OSGetTime();
        uint32_t hold;
        const uint32_t save_mask = VPAD_BUTTON_ZL | VPAD_BUTTON_ZR | VPAD_BUTTON_PLUS;
        const uint32_t load_mask = VPAD_BUTTON_ZL | VPAD_BUTTON_ZR | VPAD_BUTTON_MINUS;
        const uint32_t exit_mask = VPAD_BUTTON_L | VPAD_BUTTON_R | VPAD_BUTTON_MINUS;
        bool save_down;
        bool load_down;
        bool exit_down;

        VPADRead(VPAD_CHAN_0, &g_vpad, 1, &g_vpad_error);
        hold = (uint32_t)g_vpad.hold;
        save_down = (hold & save_mask) == save_mask;
        load_down = (hold & load_mask) == load_mask;
        exit_down = (hold & exit_mask) == exit_mask;

        if (g_mode == SCREEN_GAME && g_game_loaded) {
            g_suppress_game_input = save_down || load_down || exit_down;

            if (save_down && !g_save_combo_was_down) {
                save_state_to_disk();
            } else if (load_down && !g_load_combo_was_down) {
                load_state_from_disk();
            }
            g_save_combo_was_down = save_down;
            g_load_combo_was_down = load_down;

            if (exit_down) {
                ++exit_chord_frames;
                if (exit_chord_frames >= 45) {
                    leave_game();
                    exit_chord_frames = 0;
                    g_save_combo_was_down = false;
                    g_load_combo_was_down = false;
                }
            } else {
                exit_chord_frames = 0;
            }

            if (g_mode == SCREEN_GAME && g_game_loaded) {
                retro_run();
                if ((++g_autosave_frames % 120u) == 0u) {
                    save_memory_card(false);
                }
                draw_game();
            } else {
                draw_menu();
            }
        } else {
            g_suppress_game_input = false;
            g_save_combo_was_down = false;
            g_load_combo_was_down = false;
            handle_menu_input();
            draw_menu();
        }

        flip_screens();
        if (g_message_frames > 0) {
            --g_message_frames;
        }
        {
            uint64_t elapsed = OSGetTime() - start;
            if (elapsed < frame_budget) {
                OSSleepTicks(frame_budget - elapsed);
            }
        }
    }

    if (g_game_loaded) {
        save_memory_card(true);
        retro_unload_game();
        g_game_loaded = false;
    }
    retro_deinit();
    audio_shutdown();
    free(g_tv_buffer);
    free(g_drc_buffer);
    OSScreenShutdown();
    WHBLogCafeDeinit();
    WHBProcShutdown();
    return 0;
}

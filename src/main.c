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

#define APP_DIR       "fs:/vol/external01/wiiu/apps/PS1WiiU"
#define GAMES_DIR     APP_DIR "/games"
#define BIOS_DIR      APP_DIR "/bios"
#define SAVES_DIR     APP_DIR "/saves"
#define MEMCARDS_DIR  APP_DIR "/memcards"
#define SCREENSHOTS_DIR APP_DIR "/screenshots"
#define SETTINGS_PATH APP_DIR "/settings.cfg"

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
    return ends_with_case(name, ".cue") ||
           ends_with_case(name, ".chd") ||
           ends_with_case(name, ".pbp") ||
           ends_with_case(name, ".iso") ||
           ends_with_case(name, ".img");
}

static int compare_roms(const void *a, const void *b)
{
    const RomEntry *ra = (const RomEntry *)a;
    const RomEntry *rb = (const RomEntry *)b;
    return strcasecmp(ra->name, rb->name);
}

static void scan_games(void)
{
    DIR *dir;
    struct dirent *entry;
    g_rom_count = 0;
    g_rom_selected = 0;

    dir = opendir(GAMES_DIR);
    if (!dir) {
        set_status("Cannot open games folder. Check SD-card access.");
        return;
    }

    while ((entry = readdir(dir)) != NULL && g_rom_count < MAX_ROMS) {
        struct stat st;
        RomEntry *rom;
        if (entry->d_name[0] == '.' || !is_supported_image(entry->d_name)) {
            continue;
        }
        rom = &g_roms[g_rom_count];
        snprintf(rom->name, sizeof(rom->name), "%s", entry->d_name);
        snprintf(rom->path, sizeof(rom->path), "%s/%s", GAMES_DIR, entry->d_name);
        if (stat(rom->path, &st) == 0 && S_ISDIR(st.st_mode)) {
            continue;
        }
        ++g_rom_count;
    }
    closedir(dir);
    qsort(g_roms, g_rom_count, sizeof(g_roms[0]), compare_roms);

    if (g_rom_count == 0) {
        set_status("No supported PS1 images found. Add .cue, .chd, .pbp, .iso or .img files.");
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
    VPADRead(VPAD_CHAN_0, &g_vpad, 1, &g_vpad_error);
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    uint32_t hold = g_vpad.hold;
    if (port != 0) {
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
    OSScreenPutFontEx(SCREEN_DRC, 2, 28, "Hold L + R + - to return to library");
    if (g_message_frames > 0 && g_message[0]) {
        OSScreenPutFontEx(SCREEN_TV, 2, 2, g_message);
        OSScreenPutFontEx(SCREEN_DRC, 2, 2, g_message);
        --g_message_frames;
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

static void launch_selected_game(void)
{
    struct retro_game_info game = { 0 };
    struct retro_system_av_info av_info;
    if (g_rom_count == 0 || g_rom_selected >= g_rom_count) {
        set_status("No game selected. Add your own PS1 image files to games/.");
        return;
    }

    audio_reset();
    g_frame_valid = false;
    game.path = g_roms[g_rom_selected].path;
    if (!retro_load_game(&game)) {
        set_status("Could not load %s. Check the image and BIOS settings.", g_roms[g_rom_selected].name);
        return;
    }

    g_game_loaded = true;
    g_mode = SCREEN_GAME;
    retro_get_system_av_info(&av_info);
    if (av_info.timing.sample_rate > 0.0) {
        audio_set_sample_rate((unsigned)av_info.timing.sample_rate);
    }
    set_status("Loaded %s", g_roms[g_rom_selected].name);
}

static void leave_game(void)
{
    if (g_game_loaded) {
        retro_unload_game();
        g_game_loaded = false;
    }
    audio_reset();
    g_frame_valid = false;
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
        VPADRead(VPAD_CHAN_0, &g_vpad, 1, &g_vpad_error);

        if (g_mode == SCREEN_GAME && g_game_loaded) {
            if ((g_vpad.hold & (VPAD_BUTTON_L | VPAD_BUTTON_R | VPAD_BUTTON_MINUS)) ==
                (VPAD_BUTTON_L | VPAD_BUTTON_R | VPAD_BUTTON_MINUS)) {
                ++exit_chord_frames;
                if (exit_chord_frames >= 45) {
                    leave_game();
                    exit_chord_frames = 0;
                }
            } else {
                exit_chord_frames = 0;
            }

            if (g_mode == SCREEN_GAME && g_game_loaded) {
                retro_run();
                draw_game();
            } else {
                draw_menu();
            }
        } else {
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

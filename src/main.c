#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <coreinit/screen.h>
#include <vpad/input.h>
#include <whb/proc.h>

static void draw_text(void *tv_buffer, size_t tv_size,
                      void *drc_buffer, size_t drc_size)
{
    OSScreenSetBufferEx(SCREEN_TV, tv_buffer);
    OSScreenSetBufferEx(SCREEN_DRC, drc_buffer);
    OSScreenEnableEx(SCREEN_TV, true);
    OSScreenEnableEx(SCREEN_DRC, true);

    OSScreenClearBufferEx(SCREEN_TV, 0x10182000);
    OSScreenClearBufferEx(SCREEN_DRC, 0x10182000);

    OSScreenPutFontEx(SCREEN_TV, 2, 2, "PS1WiiU");
    OSScreenPutFontEx(SCREEN_TV, 2, 4, "Native Wii U PlayStation emulator");
    OSScreenPutFontEx(SCREEN_TV, 2, 7, "Development build - emulator core not integrated yet.");
    OSScreenPutFontEx(SCREEN_TV, 2, 9, "The build pipeline and app shell are being established.");

    OSScreenPutFontEx(SCREEN_DRC, 2, 2, "PS1WiiU");
    OSScreenPutFontEx(SCREEN_DRC, 2, 4, "PS1 emulator for Aroma");
    OSScreenPutFontEx(SCREEN_DRC, 2, 7, "Next: integrate a PS1 core.");
    OSScreenPutFontEx(SCREEN_DRC, 2, 9, "Game files: sd:/wiiu/apps/PS1WiiU/games/");
    OSScreenPutFontEx(SCREEN_DRC, 2, 10, "BIOS files: sd:/wiiu/apps/PS1WiiU/bios/");

    (void)tv_size;
    (void)drc_size;
}

int main(void)
{
    WHBProcInit();
    OSScreenInit();

    const size_t tv_size = OSScreenGetBufferSizeEx(SCREEN_TV);
    const size_t drc_size = OSScreenGetBufferSizeEx(SCREEN_DRC);
    void *tv_buffer = memalign(0x100, tv_size);
    void *drc_buffer = memalign(0x100, drc_size);

    if (tv_buffer == NULL || drc_buffer == NULL)
    {
        free(tv_buffer);
        free(drc_buffer);
        OSScreenShutdown();
        WHBProcShutdown();
        return 1;
    }

    VPADStatus status;
    VPADReadError error;

    while (WHBProcIsRunning())
    {
        draw_text(tv_buffer, tv_size, drc_buffer, drc_size);
        VPADRead(VPAD_CHAN_0, &status, 1, &error);
        OSScreenFlipBuffersEx(SCREEN_TV);
        OSScreenFlipBuffersEx(SCREEN_DRC);
    }

    free(tv_buffer);
    free(drc_buffer);
    OSScreenShutdown();
    WHBProcShutdown();
    return 0;
}

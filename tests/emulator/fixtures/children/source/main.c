// SPDX-License-Identifier: CC0-1.0
#include <nds.h>
#include <stdio.h>

#if FIXTURE_CHILD == 1
#define MARKER "PICO CHILD A"
#elif FIXTURE_CHILD == 2
#define MARKER "PICO CHILD B"
#else
#error Define FIXTURE_CHILD as 1 or 2
#endif

int main(void)
{
    PrintConsole top;
    PrintConsole bottom;
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&top, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
    consoleInit(&bottom, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);
    consoleSelect(&top);
    printf("\x1b[5;7H" MARKER);
    consoleSelect(&bottom);
    printf("\x1b[5;7H" MARKER "\n\n      LAUNCH VERIFIED");
    while (1)
        swiWaitForVBlank();
}

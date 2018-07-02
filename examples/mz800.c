//------------------------------------------------------------------------------
// mz800.c 
//
// Emulator for the SHARP MZ-800
//
//------------------------------------------------------------------------------

#include "sokol_app.h"
#include "sokol_time.h"
#define CHIPS_IMPL
#include "chips/z80.h"
#include "chips/z80pio.h"
#include "chips/z80ctc.h"
#include "chips/crt.h"
#include "chips/kbd.h"
#include "chips/mem.h"
#include "roms/mz800-roms.h"
#include "common/gfx.h"
#include <ctype.h> /* isupper, islower, toupper, tolower */

/* MZ-800 emulator state and callbacks */
#define MZ800_FREQ (4000000)
#define MZ800_DISP_WIDTH (640)
#define MZ800_DISP_HEIGHT (200)

typedef struct {
    z80_t cpu;
    
    uint32_t tick_count;
    
    crt_t crt;
    kbd_t kbd;
    mem_t mem;
    
    
} mz800_t;
mz800_t mz800;

// Memory layout
const uint32_t mz800_mem_banks[9] = {
    0x100e0, // R | e0 | x    | CGROM | VRAM | x    |
    0x100e1, // R | e1 | x    | DRAM  | DRAM | x    |
    0x000e0, // W | e0 | DRAM | DRAM  | x    | x    |
    0x000e1, // W | e1 | x    | x     | x    | DRAM |
    0x000e2, // W | e2 | ROM  | x     | x    | x    |
    0x000e3, // W | e3 | x    | x     | x    | ROM  |
    0x000e4, // W | e4 | ROM  | CGROM | VRAM | ROM  |
    0x000e5, // W | e5 | x    | x     | x    | PROH | // Prohibited
    0x000e6  // W | e6 | x    | x     | x    | RET  | // Return to previous state
};

/// Colors - the MZ-800 has only 16 fixed colors.
const uint32_t mz800_colors[16] = {
    0x000000, // black
    0x000030, // blue
    0x003000, // red
    0x003030, // purple
    0x300000, // green
    0x300030, // cyan
    0x303000, // yellow
    0x303030, // white
    
    0x151515, // gray
    0x00003f, // light blue
    0x003f00, // light red
    0x003f3f, // light purple
    0x3f0000, // light green
    0x3f003f, // light cyan
    0x3f3f00, // light yellow
    0x3f3f3f  // light white
};


uint32_t overrun_ticks;
uint64_t last_time_stamp;

/* sokol-app entry, configure application callbacks and window */
void app_init(void);
void app_frame(void);
void app_input(const sapp_event*);
void app_cleanup(void);

sapp_desc sokol_main(int argc, char* argv[]) {
    return (sapp_desc) {
        .init_cb = app_init,
        .frame_cb = app_frame,
        .event_cb = app_input,
        .cleanup_cb = app_cleanup,
        .width = MZ800_DISP_WIDTH,
        .height = 2 * MZ800_DISP_HEIGHT,
        .window_title = "MZ-800"
    };
}

/* one-time application init */
void app_init() {
    gfx_init(MZ800_DISP_WIDTH, MZ800_DISP_HEIGHT);
    last_time_stamp = stm_now();
}

/* per frame stuff, tick the emulator, handle input, decode and draw emulator display */
void app_frame() {
}

/* keyboard input handling */
void app_input(const sapp_event* event) {
}

/* application cleanup callback */
void app_cleanup() {
    gfx_shutdown();
}

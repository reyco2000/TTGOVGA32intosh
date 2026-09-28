#ifndef CONFIG_H
#define CONFIG_H

// Macintosh Emulation Settings
#ifndef UMAC_MEMSIZE
#define UMAC_MEMSIZE 1024
#endif

#ifndef DISP_WIDTH
#define DISP_WIDTH 512
#endif

#ifndef DISP_HEIGHT
#define DISP_HEIGHT 342
#endif

#ifndef ENABLE_DASM
#define ENABLE_DASM 0
#endif

// Multiplier applied to raw PS/2 mouse deltas before they reach the emulator
#ifndef MOUSE_SENSITIVITY
#define MOUSE_SENSITIVITY 2.0
#endif

// Max queued mouse-quadrature steps per axis. Measured drain rate on this
// hardware is only ~75-150 px/sec/axis (umac_task can't sustain real-time
// 68k emulation), so a big backlog from a fast swipe visibly "traces" to
// its destination for a long time. Lower = snappier catch-up but less
// reach per swipe.
#ifndef MOUSE_MAX_PENDING_PIX
#define MOUSE_MAX_PENDING_PIX 24
#endif

#endif

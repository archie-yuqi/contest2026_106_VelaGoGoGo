#ifndef HOME_SCENSE_CLAUDE_ASSETS_H
#define HOME_SCENSE_CLAUDE_ASSETS_H
#include <stdint.h>
#define CLAUDE_FRAME_W 320
#define CLAUDE_FRAME_H 132
#define CLAUDE_FRAME_BYTES (CLAUDE_FRAME_W * CLAUDE_FRAME_H * 2)
#define CLAUDE_IDLE_FRAMES 10
#define CLAUDE_THINKING_FRAMES 3
#define CLAUDE_EXECUTING_FRAMES 10
extern const unsigned char claude_idle_data[];
extern const unsigned char claude_thinking_data[];
extern const unsigned char claude_executing_data[];
#endif

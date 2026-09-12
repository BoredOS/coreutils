// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <errno.h>
#include "vterm_font.h"

#define GRAPHICAL_VT_COUNT 10

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOSET_DIRTY       0x4606

typedef struct {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct { uint32_t offset; uint32_t length; } red, green, blue, transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
} fb_var_screeninfo_t;

typedef struct {
    char id[16];
    uint64_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uint64_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t reserved[3];
} fb_fix_screeninfo_t;

#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define KDSETMODE   0x4B3A
#define KDGETMODE   0x4B3B
#define KD_TEXT     0x00
#define KD_GRAPHICS 0x01

#define VT_ACTIVATE    0x5606
#define VT_GETACTIVE   0x5607
#define VT_GETSTATE    0x5603
#define VT_GETBOOTLOG  0x5608
#define VT_STOPBOOTLOG 0x5609

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

struct vt_bootlog {
    char *buf;
    size_t size;
    size_t written;
};

#define VTERM_EVENT_SWITCH   1
#define VTERM_EVENT_KDMODE   2
#define VTERM_EVENT_RESIZE   3

typedef struct {
    uint32_t type;
    int vt_id;
    int val;
} vterm_event_t;

typedef struct {
    uint32_t codepoint;
    uint32_t fg;   // 32-bit ARGB
    uint32_t bg;   // 32-bit ARGB
    uint8_t  attrs;// bit 0: bold, bit 1: underline, bit 2: reverse
    uint8_t  dirty;
} vterm_cell_t;

static const uint32_t g_ansi_standard[8] = {
    0xFF000000, // 0: Black
    0xFFFF4444, // 1: Red
    0xFF6A9955, // 2: Green
    0xFFFFCC00, // 3: Yellow
    0xFF569CD6, // 4: Blue
    0xFFC586C0, // 5: Magenta
    0xFF4EC9B0, // 6: Cyan
    0xFFFFFFFF  // 7: White
};

static const uint32_t g_ansi_bright[8] = {
    0xFF555555, // 8:  grey
    0xFFFF5555, // 9:  Bright Red
    0xFF55FF55, // 10: Bright Green
    0xFFFFFF55, // 11: Bright Yellow
    0xFF5555FF, // 12: Bright Blue
    0xFFFF55FF, // 13: Bright Magenta
    0xFF55FFFF, // 14: Bright Cyan
    0xFFFFFFFF  // 15: Bright White
};

static const uint32_t g_xterm_16[16] = {
    0xFF000000, 0xFF800000, 0xFF008000, 0xFF808000,
    0xFF000080, 0xFF800080, 0xFF008080, 0xFFC0C0C0,
    0xFF808080, 0xFFFF0000, 0xFF00FF00, 0xFFFFFF00,
    0xFF0000FF, 0xFFFF00FF, 0xFF00FFFF, 0xFFFFFFFF
};

static inline uint32_t color_256_to_argb(uint8_t idx) {
    if (idx < 16) return g_xterm_16[idx];
    if (idx >= 232) {
        uint8_t gray = (uint8_t)(8 + (idx - 232) * 10);
        return 0xFF000000 | (gray << 16) | (gray << 8) | gray;
    }
    uint8_t c = idx - 16;
    uint8_t r = c / 36;
    uint8_t g = (c / 6) % 6;
    uint8_t b = c % 6;
    static const uint8_t v[] = { 0, 95, 135, 175, 215, 255 };
    return 0xFF000000 | (v[r] << 16) | (v[g] << 8) | v[b];
}

#define DEFAULT_FG_COLOR 0xFFFFFFFF
#define DEFAULT_BG_COLOR 0xFF000000

typedef struct {
    int id;
    int master_fd;
    bool opened;
    vterm_cell_t *grid;
    int cursor_x;
    int cursor_y;
    int last_cursor_x;
    int last_cursor_y;
    bool cursor_drawn;
    bool cursor_visible;
    int saved_x;
    int saved_y;

    uint32_t curr_fg;
    uint32_t curr_bg;
    uint8_t  curr_attrs;

    int utf8_state;
    uint32_t utf8_codepoint;

    int esc_state;
    int esc_params[32];
    int esc_num_params;
    bool esc_dec_private;

    bool dirty;
    int dirty_row_start;
    int dirty_row_end;
} vterm_vt_t;

static vterm_vt_t g_vts[GRAPHICAL_VT_COUNT];
static int g_active_vt = 0;
static int g_kd_mode = KD_TEXT;
static volatile sig_atomic_t g_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static int g_fb_fd = -1;
static uint8_t *g_fb_mem = NULL;
static size_t g_fb_size = 0;
static int g_screen_w = 0;
static int g_screen_h = 0;
static int g_line_pitch = 0;
static int g_bpp = 32;
static int g_cols = 80;
static int g_rows = 25;

static int g_ctl_fd = -1;

static void ensure_grid(vterm_vt_t *vt) {
    if (vt->grid) return;
    size_t count = (size_t)g_cols * g_rows;
    vt->grid = (vterm_cell_t *)malloc(count * sizeof(vterm_cell_t));
    if (!vt->grid) return;

    for (size_t i = 0; i < count; i++) {
        vt->grid[i].codepoint = ' ';
        vt->grid[i].fg = DEFAULT_FG_COLOR;
        vt->grid[i].bg = DEFAULT_BG_COLOR;
        vt->grid[i].attrs = 0;
        vt->grid[i].dirty = 1;
    }
    vt->dirty = true;
    vt->dirty_row_start = 0;
    vt->dirty_row_end = g_rows - 1;
}

static inline void vterm_render_cell_to_fb(int col, int row, vterm_cell_t cell) {
    if (!g_fb_mem) return;
    int px = col * FONT_W;
    int py = row * FONT_H;
    if (px < 0 || px + FONT_W > g_screen_w || py < 0 || py + FONT_H > g_screen_h) return;

    uint32_t fg = cell.fg;
    uint32_t bg = cell.bg;
    if (cell.attrs & 4) { // Reverse video
        uint32_t tmp = fg; fg = bg; bg = tmp;
    }

    uint8_t custom_glyph[8];
    const uint8_t *glyph_data;
    if (get_box_drawing_glyph(cell.codepoint, custom_glyph)) {
        glyph_data = custom_glyph;
    } else {
        uint32_t uc = cell.codepoint;
        if (uc > 127) uc = 0;
        glyph_data = font8x8_basic[uc];
    }

    for (int r = 0; r < FONT_H; r++) {
        uint32_t *dst = (uint32_t *)(g_fb_mem + (size_t)(py + r) * g_line_pitch) + px;
        uint8_t bits = glyph_data[r];
        for (int c = 0; c < FONT_W; c++) {
            dst[c] = (bits & (0x80 >> c)) ? fg : bg;
        }
    }
}

static inline void vterm_invert_cursor_cell(int col, int row) {
    if (!g_fb_mem) return;
    int px = col * FONT_W;
    int py = row * FONT_H;
    if (px < 0 || px + FONT_W > g_screen_w || py < 0 || py + FONT_H > g_screen_h) return;

    for (int r = 0; r < FONT_H; r++) {
        uint32_t *dst = (uint32_t *)(g_fb_mem + (size_t)(py + r) * g_line_pitch) + px;
        for (int c = 0; c < FONT_W; c++) {
            dst[c] ^= 0x00FFFFFF;
        }
    }
}

static void vterm_mark_row(vterm_vt_t *vt, int r) {
    if (r < 0 || r >= g_rows) return;
    vt->dirty = true;
    if (r < vt->dirty_row_start) vt->dirty_row_start = r;
    if (r > vt->dirty_row_end) vt->dirty_row_end = r;
}

static void vterm_flush_vt(vterm_vt_t *vt) {
    if (vt->id != g_active_vt || g_kd_mode == KD_GRAPHICS || !g_fb_mem || !vt->grid) {
        return;
    }

    if (vt->cursor_drawn && vt->last_cursor_x >= 0 && vt->last_cursor_x < g_cols &&
        vt->last_cursor_y >= 0 && vt->last_cursor_y < g_rows) {
        int old_idx = vt->last_cursor_y * g_cols + vt->last_cursor_x;
        vterm_render_cell_to_fb(vt->last_cursor_x, vt->last_cursor_y, vt->grid[old_idx]);
        vt->cursor_drawn = false;
    }

    if (vt->dirty) {
        int r_start = vt->dirty_row_start < 0 ? 0 : vt->dirty_row_start;
        int r_end = vt->dirty_row_end >= g_rows ? (g_rows - 1) : vt->dirty_row_end;

        for (int r = r_start; r <= r_end; r++) {
            for (int c = 0; c < g_cols; c++) {
                int idx = r * g_cols + c;
                if (vt->grid[idx].dirty) {
                    vterm_render_cell_to_fb(c, r, vt->grid[idx]);
                    vt->grid[idx].dirty = 0;
                }
            }
        }
        vt->dirty = false;
        vt->dirty_row_start = g_rows;
        vt->dirty_row_end = -1;
    }

    if (vt->cursor_visible && vt->cursor_x >= 0 && vt->cursor_x < g_cols &&
        vt->cursor_y >= 0 && vt->cursor_y < g_rows) {
        vterm_invert_cursor_cell(vt->cursor_x, vt->cursor_y);
        vt->cursor_drawn = true;
        vt->last_cursor_x = vt->cursor_x;
        vt->last_cursor_y = vt->cursor_y;
    }
}

static void vterm_full_refresh(vterm_vt_t *vt) {
    if (vt->id != g_active_vt || g_kd_mode == KD_GRAPHICS || !g_fb_mem) return;
    ensure_grid(vt);
    for (int r = 0; r < g_rows; r++) {
        for (int c = 0; c < g_cols; c++) {
            int idx = r * g_cols + c;
            vterm_render_cell_to_fb(c, r, vt->grid[idx]);
            vt->grid[idx].dirty = 0;
        }
    }
    if (vt->cursor_visible && vt->cursor_x >= 0 && vt->cursor_x < g_cols &&
        vt->cursor_y >= 0 && vt->cursor_y < g_rows) {
        vterm_invert_cursor_cell(vt->cursor_x, vt->cursor_y);
        vt->cursor_drawn = true;
        vt->last_cursor_x = vt->cursor_x;
        vt->last_cursor_y = vt->cursor_y;
    } else {
        vt->cursor_drawn = false;
    }
    vt->dirty = false;
    vt->dirty_row_start = g_rows;
    vt->dirty_row_end = -1;
}

static void vterm_scroll(vterm_vt_t *vt) {
    ensure_grid(vt);
    memmove(vt->grid, vt->grid + g_cols, (size_t)(g_rows - 1) * g_cols * sizeof(vterm_cell_t));
    for (int c = 0; c < g_cols; c++) {
        int idx = (g_rows - 1) * g_cols + c;
        vt->grid[idx].codepoint = ' ';
        vt->grid[idx].fg = vt->curr_fg;
        vt->grid[idx].bg = vt->curr_bg;
        vt->grid[idx].attrs = vt->curr_attrs;
        vt->grid[idx].dirty = 1;
    }
    for (int r = 0; r < g_rows; r++) {
        for (int c = 0; c < g_cols; c++) {
            vt->grid[r * g_cols + c].dirty = 1;
        }
    }
    vt->cursor_y = g_rows - 1;
    vt->dirty = true;
    vt->dirty_row_start = 0;
    vt->dirty_row_end = g_rows - 1;
}

static void vterm_write_cell(vterm_vt_t *vt, int col, int row, uint32_t cp) {
    if (col < 0 || col >= g_cols || row < 0 || row >= g_rows) return;
    ensure_grid(vt);
    int idx = row * g_cols + col;
    if (vt->grid[idx].codepoint == cp && vt->grid[idx].fg == vt->curr_fg &&
        vt->grid[idx].bg == vt->curr_bg && vt->grid[idx].attrs == vt->curr_attrs) {
        return;
    }
    vt->grid[idx].codepoint = cp;
    vt->grid[idx].fg = vt->curr_fg;
    vt->grid[idx].bg = vt->curr_bg;
    vt->grid[idx].attrs = vt->curr_attrs;
    vt->grid[idx].dirty = 1;
    vterm_mark_row(vt, row);
}

static void vterm_erase_in_line(vterm_vt_t *vt, int mode) {
    ensure_grid(vt);
    int start = 0, end = g_cols - 1;
    if (mode == 0) start = vt->cursor_x;
    else if (mode == 1) end = vt->cursor_x;
    for (int c = start; c <= end && c < g_cols; c++) {
        vterm_write_cell(vt, c, vt->cursor_y, ' ');
    }
}

static void vterm_erase_in_display(vterm_vt_t *vt, int mode) {
    ensure_grid(vt);
    if (mode == 2) {
        for (int r = 0; r < g_rows; r++) {
            for (int c = 0; c < g_cols; c++) {
                vterm_write_cell(vt, c, r, ' ');
            }
        }
    } else if (mode == 1) { // Start to cursor
        for (int r = 0; r < vt->cursor_y; r++) {
            for (int c = 0; c < g_cols; c++) vterm_write_cell(vt, c, r, ' ');
        }
        for (int c = 0; c <= vt->cursor_x && c < g_cols; c++) {
            vterm_write_cell(vt, c, vt->cursor_y, ' ');
        }
    } else { // Cursor to end
        for (int c = vt->cursor_x; c < g_cols; c++) {
            vterm_write_cell(vt, c, vt->cursor_y, ' ');
        }
        for (int r = vt->cursor_y + 1; r < g_rows; r++) {
            for (int c = 0; c < g_cols; c++) vterm_write_cell(vt, c, r, ' ');
        }
    }
}

static void vterm_process_sgr(vterm_vt_t *vt) {
    int count = vt->esc_num_params + 1;
    if (count <= 0) count = 1;

    for (int j = 0; j < count; j++) {
        int p = vt->esc_params[j];
        if (p == 0) {
            vt->curr_fg = DEFAULT_FG_COLOR;
            vt->curr_bg = DEFAULT_BG_COLOR;
            vt->curr_attrs = 0;
        } else if (p == 1) {
            vt->curr_attrs |= 1; // Bold
        } else if (p == 4) {
            vt->curr_attrs |= 2; // Underline
        } else if (p == 7) {
            vt->curr_attrs |= 4; // Reverse video
        } else if (p == 22) {
            vt->curr_attrs &= ~1; // Normal intensity
        } else if (p == 24) {
            vt->curr_attrs &= ~2; // Not underline
        } else if (p == 27) {
            vt->curr_attrs &= ~4; // Not reverse
        } else if (p >= 30 && p <= 37) {
            vt->curr_fg = g_ansi_standard[p - 30];
        } else if (p == 38) {
            if (j + 2 < count && vt->esc_params[j + 1] == 5) {
                uint8_t idx = (uint8_t)vt->esc_params[j + 2];
                vt->curr_fg = color_256_to_argb(idx);
                j += 2;
            } else if (j + 4 < count && vt->esc_params[j + 1] == 2) {
                uint8_t r = (uint8_t)vt->esc_params[j + 2];
                uint8_t g = (uint8_t)vt->esc_params[j + 3];
                uint8_t b = (uint8_t)vt->esc_params[j + 4];
                vt->curr_fg = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                j += 4;
            }
        } else if (p == 39) {
            vt->curr_fg = DEFAULT_FG_COLOR;
        } else if (p >= 40 && p <= 47) {
            vt->curr_bg = g_ansi_standard[p - 40];
        } else if (p == 48) {
            if (j + 2 < count && vt->esc_params[j + 1] == 5) {
                uint8_t idx = (uint8_t)vt->esc_params[j + 2];
                vt->curr_bg = color_256_to_argb(idx);
                j += 2;
            } else if (j + 4 < count && vt->esc_params[j + 1] == 2) {
                uint8_t r = (uint8_t)vt->esc_params[j + 2];
                uint8_t g = (uint8_t)vt->esc_params[j + 3];
                uint8_t b = (uint8_t)vt->esc_params[j + 4];
                vt->curr_bg = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                j += 4;
            }
        } else if (p == 49) {
            vt->curr_bg = DEFAULT_BG_COLOR;
        } else if (p >= 90 && p <= 97) {
            vt->curr_fg = g_ansi_bright[p - 90];
        } else if (p >= 100 && p <= 107) {
            vt->curr_bg = g_ansi_bright[p - 100];
        }
    }
}

static void vterm_process_char(vterm_vt_t *vt, uint32_t c) {
    if (vt->esc_state == 1) { // Saw ESC
        if (c == '[') {
            vt->esc_state = 2;
            vt->esc_num_params = 0;
            vt->esc_dec_private = false;
            for (int p = 0; p < 32; p++) vt->esc_params[p] = 0;
        } else if (c == 's') {
            vt->saved_x = vt->cursor_x;
            vt->saved_y = vt->cursor_y;
            vt->esc_state = 0;
        } else if (c == 'u') {
            vt->cursor_x = vt->saved_x;
            vt->cursor_y = vt->saved_y;
            vt->esc_state = 0;
        } else {
            vt->esc_state = 0;
        }
        return;
    } else if (vt->esc_state == 2) { // Saw ESC [
        if (c == '?') {
            vt->esc_dec_private = true;
            return;
        }
        if (c >= '0' && c <= '9') {
            vt->esc_params[vt->esc_num_params] = vt->esc_params[vt->esc_num_params] * 10 + (int)(c - '0');
            return;
        }
        if (c == ';') {
            if (vt->esc_num_params < 31) vt->esc_num_params++;
            return;
        }

        if (vt->esc_dec_private) {
            if (c == 'h' || c == 'l') {
                if (vt->esc_params[0] == 25) {
                    vt->cursor_visible = (c == 'h');
                }
            }
        } else {
            if (c == 'H' || c == 'f') { // Move cursor (1-indexed)
                int r = vt->esc_params[0];
                int col = vt->esc_params[1];
                vt->cursor_y = (r > 0) ? (r - 1) : 0;
                vt->cursor_x = (col > 0) ? (col - 1) : 0;
                if (vt->cursor_x >= g_cols) vt->cursor_x = g_cols - 1;
                if (vt->cursor_y >= g_rows) vt->cursor_y = g_rows - 1;
            } else if (c == 'A') { // Up
                int n = vt->esc_params[0] ? vt->esc_params[0] : 1;
                vt->cursor_y -= n;
                if (vt->cursor_y < 0) vt->cursor_y = 0;
            } else if (c == 'B') { // Down
                int n = vt->esc_params[0] ? vt->esc_params[0] : 1;
                vt->cursor_y += n;
                if (vt->cursor_y >= g_rows) vt->cursor_y = g_rows - 1;
            } else if (c == 'C') { // Right
                int n = vt->esc_params[0] ? vt->esc_params[0] : 1;
                vt->cursor_x += n;
                if (vt->cursor_x >= g_cols) vt->cursor_x = g_cols - 1;
            } else if (c == 'D') { // Left
                int n = vt->esc_params[0] ? vt->esc_params[0] : 1;
                vt->cursor_x -= n;
                if (vt->cursor_x < 0) vt->cursor_x = 0;
            } else if (c == 'G' || c == '`') { // Absolute column (1-indexed)
                int col = vt->esc_params[0] > 0 ? (vt->esc_params[0] - 1) : 0;
                vt->cursor_x = col < g_cols ? col : (g_cols - 1);
            } else if (c == 'd') { // Absolute row (1-indexed)
                int row = vt->esc_params[0] > 0 ? (vt->esc_params[0] - 1) : 0;
                vt->cursor_y = row < g_rows ? row : (g_rows - 1);
            } else if (c == 'J') { // Erase in Display
                vterm_erase_in_display(vt, vt->esc_params[0]);
            } else if (c == 'K') { // Erase in Line
                vterm_erase_in_line(vt, vt->esc_params[0]);
            } else if (c == 'm') { // Set Graphic Rendition (Color/Attr)
                vterm_process_sgr(vt);
            } else if (c == 's') { // Save cursor
                vt->saved_x = vt->cursor_x;
                vt->saved_y = vt->cursor_y;
            } else if (c == 'u') { // Restore cursor
                vt->cursor_x = vt->saved_x;
                vt->cursor_y = vt->saved_y;
            }
        }
        vt->esc_state = 0;
        return;
    }

    if (c == 0x1B) { // ESC
        vt->esc_state = 1;
        return;
    }
    if (c == '\n') {
        vt->cursor_x = 0;
        vt->cursor_y++;
    } else if (c == '\r') {
        vt->cursor_x = 0;
    } else if (c == '\t') {
        vt->cursor_x = (vt->cursor_x + 8) & ~7;
    } else if (c == '\b') {
        if (vt->cursor_x > 0) {
            vt->cursor_x--;
        }
    } else if (c >= 32) {
        vterm_write_cell(vt, vt->cursor_x, vt->cursor_y, c);
        vt->cursor_x++;
    }

    if (vt->cursor_x >= g_cols) {
        vt->cursor_x = 0;
        vt->cursor_y++;
    }
    if (vt->cursor_y >= g_rows) {
        vterm_scroll(vt);
    }
}

static void vterm_process_bytes(vterm_vt_t *vt, const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char uc = (unsigned char)data[i];
        uint32_t c = 0;
        bool has_cp = false;

        if (vt->utf8_state > 0 && (uc & 0xC0) == 0x80) {
            vt->utf8_codepoint = (vt->utf8_codepoint << 6) | (uc & 0x3F);
            vt->utf8_state--;
            if (vt->utf8_state == 0) {
                c = vt->utf8_codepoint;
                has_cp = true;
            }
        } else {
            vt->utf8_state = 0;
            if ((uc & 0x80) == 0) {
                c = uc;
                has_cp = true;
            } else if ((uc & 0xE0) == 0xC0) {
                vt->utf8_codepoint = uc & 0x1F;
                vt->utf8_state = 1;
            } else if ((uc & 0xF0) == 0xE0) {
                vt->utf8_codepoint = uc & 0x0F;
                vt->utf8_state = 2;
            } else if ((uc & 0xF8) == 0xF0) {
                vt->utf8_codepoint = uc & 0x07;
                vt->utf8_state = 3;
            }
        }

        if (has_cp) {
            vterm_process_char(vt, c);
        }
    }
}

static bool init_display(void) {
    g_fb_fd = open("/dev/fb0", O_RDWR);
    if (g_fb_fd < 0) {
        fprintf(stderr, "[vterm] Cannot open /dev/fb0\n");
        return false;
    }

    fb_var_screeninfo_t vinfo;
    fb_fix_screeninfo_t finfo;
    if (ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(g_fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        fprintf(stderr, "[vterm] Failed to query screen info\n");
        close(g_fb_fd);
        g_fb_fd = -1;
        return false;
    }

    g_screen_w = vinfo.xres;
    g_screen_h = vinfo.yres;
    g_bpp = vinfo.bits_per_pixel;
    g_line_pitch = finfo.line_length > 0 ? (int)finfo.line_length : (g_screen_w * 4);
    g_fb_size = (size_t)g_line_pitch * g_screen_h;

    g_fb_mem = (uint8_t *)mmap(NULL, g_fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, g_fb_fd, 0);
    if (g_fb_mem == MAP_FAILED) {
        fprintf(stderr, "[vterm] mmap framebuffer failed\n");
        close(g_fb_fd);
        g_fb_fd = -1;
        g_fb_mem = NULL;
        return false;
    }

    g_cols = g_screen_w / FONT_W;
    g_rows = g_screen_h / FONT_H;
    if (g_cols < 80) g_cols = 80;
    if (g_rows < 25) g_rows = 25;

    return true;
}

int main(void) {
    if (!init_display()) {
        return 1;
    }

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    g_ctl_fd = open("/dev/vterm_ctl", O_RDONLY | O_NONBLOCK);

    struct winsize ws;
    ws.ws_row = (unsigned short)g_rows;
    ws.ws_col = (unsigned short)g_cols;
    ws.ws_xpixel = (unsigned short)g_screen_w;
    ws.ws_ypixel = (unsigned short)g_screen_h;

    for (int i = 0; i < GRAPHICAL_VT_COUNT; i++) {
        char devname[32];
        snprintf(devname, sizeof(devname), "/dev/vterm%d", i + 1);
        int fd = open(devname, O_RDWR | O_NONBLOCK);

        g_vts[i].id = i;
        g_vts[i].master_fd = fd;
        g_vts[i].opened = (fd >= 0);
        g_vts[i].grid = NULL;
        g_vts[i].cursor_x = 0;
        g_vts[i].cursor_y = 0;
        g_vts[i].last_cursor_x = -1;
        g_vts[i].last_cursor_y = -1;
        g_vts[i].cursor_drawn = false;
        g_vts[i].cursor_visible = true;
        g_vts[i].saved_x = 0;
        g_vts[i].saved_y = 0;
        g_vts[i].curr_fg = DEFAULT_FG_COLOR;
        g_vts[i].curr_bg = DEFAULT_BG_COLOR;
        g_vts[i].curr_attrs = 0;
        g_vts[i].utf8_state = 0;
        g_vts[i].utf8_codepoint = 0;
        g_vts[i].esc_state = 0;
        g_vts[i].dirty = true;
        g_vts[i].dirty_row_start = 0;
        g_vts[i].dirty_row_end = g_rows - 1;

        if (fd >= 0) {
            ioctl(fd, TIOCSWINSZ, &ws);
        }
    }

    if (g_vts[0].master_fd >= 0) {
        char bootbuf[32768];
        struct vt_bootlog bl;
        bl.buf = bootbuf;
        bl.size = sizeof(bootbuf) - 1;
        bl.written = 0;
        if (ioctl(g_vts[0].master_fd, VT_GETBOOTLOG, &bl) == 0 && bl.written > 0) {
            bootbuf[bl.written] = '\0';
            ensure_grid(&g_vts[0]);
            vterm_process_bytes(&g_vts[0], bootbuf, bl.written);
            ioctl(g_vts[0].master_fd, VT_STOPBOOTLOG, NULL);
        }
    }

    g_active_vt = 0;
    vterm_full_refresh(&g_vts[g_active_vt]);

    struct pollfd pfds[GRAPHICAL_VT_COUNT + 1];
    char read_buf[4096];

    while (g_running) {
        int nfds = 0;
        if (g_ctl_fd >= 0) {
            pfds[nfds].fd = g_ctl_fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }

        int vt_map[GRAPHICAL_VT_COUNT + 1];
        for (int i = 0; i < GRAPHICAL_VT_COUNT; i++) {
            if (g_vts[i].master_fd >= 0) {
                pfds[nfds].fd = g_vts[i].master_fd;
                pfds[nfds].events = POLLIN;
                pfds[nfds].revents = 0;
                vt_map[nfds] = i;
                nfds++;
            }
        }

        int ret = poll(pfds, nfds, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (!g_running) break;

        int idx = 0;
        if (g_ctl_fd >= 0) {
            if (pfds[0].revents & POLLIN) {
                vterm_event_t ev;
                while (1) {
                    ssize_t n = read(g_ctl_fd, &ev, sizeof(ev));
                    if (n != (ssize_t)sizeof(ev)) break;
                    if (ev.type == VTERM_EVENT_SWITCH) {
                        if (ev.vt_id >= 0 && ev.vt_id < GRAPHICAL_VT_COUNT && ev.vt_id != g_active_vt) {
                            g_active_vt = ev.vt_id;
                            vterm_full_refresh(&g_vts[g_active_vt]);
                        }
                    } else if (ev.type == VTERM_EVENT_KDMODE) {
                        g_kd_mode = ev.val;
                        if (g_kd_mode == KD_TEXT) {
                            vterm_full_refresh(&g_vts[g_active_vt]);
                        }
                    }
                }
            }
            idx = 1;
        }

        bool active_changed = false;
        for (; idx < nfds; idx++) {
            if (pfds[idx].revents & POLLIN) {
                int vt_idx = vt_map[idx];
                vterm_vt_t *vt = &g_vts[vt_idx];
                ssize_t n = read(vt->master_fd, read_buf, sizeof(read_buf));
                if (n > 0) {
                    ensure_grid(vt);
                    vterm_process_bytes(vt, read_buf, (size_t)n);
                    if (vt_idx == g_active_vt) {
                        active_changed = true;
                    }
                }
            }
        }

        if (active_changed) {
            vterm_flush_vt(&g_vts[g_active_vt]);
        }
    }

    if (g_fb_mem && g_screen_w > 0 && g_screen_h > 0) {
        memset(g_fb_mem, 0, g_fb_size);
    }
    for (int i = 0; i < GRAPHICAL_VT_COUNT; i++) {
        if (g_vts[i].master_fd >= 0) {
            close(g_vts[i].master_fd);
            g_vts[i].master_fd = -1;
        }
        if (g_vts[i].grid) {
            free(g_vts[i].grid);
            g_vts[i].grid = NULL;
        }
    }
    if (g_ctl_fd >= 0) {
        close(g_ctl_fd);
        g_ctl_fd = -1;
    }
    if (g_fb_mem && g_fb_size > 0) {
        munmap(g_fb_mem, g_fb_size);
        g_fb_mem = NULL;
    }
    if (g_fb_fd >= 0) {
        close(g_fb_fd);
        g_fb_fd = -1;
    }

    return 0;
}

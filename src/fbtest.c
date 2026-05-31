// Framebuffer test application - demonstrates Linux-compatible framebuffer device
// Usage: ./fbtest [test]
//   fbtest info     - Show framebuffer information
//   fbtest pattern  - Draw a test pattern
//   fbtest clear    - Clear framebuffer to black

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/kd.h>

void print_info(int fd) {
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    
    int vret = ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
    
    if (vret == -1) {
        printf("Error: FBIOGET_VSCREENINFO failed\n");
        return;
    }
    
    int fret = ioctl(fd, FBIOGET_FSCREENINFO, &finfo);
    
    if (fret == -1) {
        printf("Error: FBIOGET_FSCREENINFO failed\n");
        return;
    }
    
    printf("=== Framebuffer Information ===\n");
    printf("Device ID: %s\n", finfo.id);
    printf("\nVariable Screen Info:\n");
    printf("  Resolution: %ux%u pixels\n", vinfo.xres, vinfo.yres);
    printf("  Virtual Resolution: %ux%u\n", vinfo.xres_virtual, vinfo.yres_virtual);
    printf("  Bits Per Pixel: %u\n", vinfo.bits_per_pixel);
    printf("  Pixel Format:\n");
    printf("    Red:   offset=%u, length=%u\n", vinfo.red.offset, vinfo.red.length);
    printf("    Green: offset=%u, length=%u\n", vinfo.green.offset, vinfo.green.length);
    printf("    Blue:  offset=%u, length=%u\n", vinfo.blue.offset, vinfo.blue.length);
    
    printf("\nFixed Screen Info:\n");
    printf("  Physical FB Address: 0x%lx\n", finfo.smem_start);
    printf("  FB Memory Size: %u bytes (%u KB)\n", finfo.smem_len, finfo.smem_len / 1024);
    printf("  Line Length (pitch): %u bytes\n", finfo.line_length);
    printf("  Visual Type: %u (2=TRUECOLOR)\n", finfo.visual);
}

void clear_framebuffer(int fd, const struct fb_var_screeninfo *vinfo, 
                       const struct fb_fix_screeninfo *finfo) {
    uint32_t size = vinfo->xres * vinfo->yres * (vinfo->bits_per_pixel / 8);
    uint8_t *buffer = malloc(size);
    
    if (!buffer) {
        fprintf(stderr, "Failed to allocate framebuffer buffer\n");
        return;
    }
    
    memset(buffer, 0, size);
    
    lseek(fd, 0, SEEK_SET);
    if (write(fd, buffer, size) != (int)size) {
        printf("Error: failed to write framebuffer\n");
    } else {
        printf("Framebuffer cleared to black (%u bytes written)\n", size);
    }
    
    free(buffer);
}

void draw_pattern(int fd, const struct fb_var_screeninfo *vinfo,
                  const struct fb_fix_screeninfo *finfo) {
    uint32_t width = vinfo->xres;
    uint32_t height = vinfo->yres;
    uint32_t bpp = vinfo->bits_per_pixel / 8;
    uint32_t line_length = finfo->line_length;
    
    // Create a simple color pattern
    uint32_t colors[4] = {
        0xFF0000FF,  // Red (BGRA format)
        0xFF00FF00,  // Green
        0xFFFF0000,  // Blue
        0xFFFFFFFF   // White
    };
    
    // Draw four quadrants with different colors
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t color;
            
            // Pick color based on quadrant
            if (x < width / 2 && y < height / 2) color = colors[0];      // Top-left: Red
            else if (x >= width / 2 && y < height / 2) color = colors[1]; // Top-right: Green
            else if (x < width / 2 && y >= height / 2) color = colors[2]; // Bottom-left: Blue
            else color = colors[3];                                        // Bottom-right: White
            
            // Write pixel
            uint64_t offset = (uint64_t)y * line_length + (uint64_t)x * bpp;
            lseek(fd, offset, SEEK_SET);
            if (write(fd, &color, bpp) != (int)bpp) {
                fprintf(stderr, "Failed to write pixel at (%u,%u)\n", x, y);
                return;
            }
        }
    }
    
    printf("Pattern drawn: %ux%u with 4 color quadrants\n", width, height);
}

void draw_pattern_mmap(void *fb_mem, const struct fb_var_screeninfo *vinfo,
                       const struct fb_fix_screeninfo *finfo) {
    uint32_t width = vinfo->xres;
    uint32_t height = vinfo->yres;
    uint32_t line_length = finfo->line_length;
    
    uint8_t *fb = (uint8_t *)fb_mem;
    for (uint32_t y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)(fb + (uint64_t)y * line_length);
        for (uint32_t x = 0; x < width; x++) {
            uint32_t r = (x * 255) / width;
            uint32_t g = (y * 255) / height;
            uint32_t b = ((x + y) * 255) / (width + height);
            row[x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

int main(int argc, char *argv[]) {
    const char *fbdev = "/dev/fb0";
    int fd = open(fbdev, O_RDWR);
    
    if (fd < 0) {
        printf("Error: cannot open framebuffer device\n");
        printf("Make sure /dev/fb0 exists and is accessible\n");
        return 1;
    }
    
    const char *cmd = argc > 1 ? argv[1] : "info";
    
    if (strcmp(cmd, "info") == 0) {
        print_info(fd);
    } else if (strcmp(cmd, "clear") == 0) {
        struct fb_var_screeninfo vinfo;
        struct fb_fix_screeninfo finfo;
        
        ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
        ioctl(fd, FBIOGET_FSCREENINFO, &finfo);
        
        clear_framebuffer(fd, &vinfo, &finfo);
    } else if (strcmp(cmd, "pattern") == 0) {
        struct fb_var_screeninfo vinfo;
        struct fb_fix_screeninfo finfo;
        
        ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
        ioctl(fd, FBIOGET_FSCREENINFO, &finfo);
        
        printf("Drawing test pattern... this may take a moment\n");
        draw_pattern(fd, &vinfo, &finfo);
    } else if (strcmp(cmd, "mmap") == 0) {
        struct fb_var_screeninfo vinfo;
        struct fb_fix_screeninfo finfo;
        
        ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
        ioctl(fd, FBIOGET_FSCREENINFO, &finfo);
        
        printf("Switching TTY console to graphics mode (KD_GRAPHICS)...\n");
        if (ioctl(0, KDSETMODE, (void*)KD_GRAPHICS) < 0) {
            printf("Warning: could not set console to graphics mode\n");
        }
        
        uint32_t size = finfo.line_length * vinfo.yres;
        printf("Mapping framebuffer of size %u bytes via mmap...\n", size);
        void *fb_mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (fb_mem == MAP_FAILED) {
            printf("Error: mmap failed!\n");
            ioctl(0, KDSETMODE, (void*)KD_TEXT);
            close(fd);
            return 1;
        }
        
        printf("Framebuffer successfully mapped at address %p\n", fb_mem);
        printf("Rendering direct-memory gradient pattern instantly...\n");
        
        // Wait 50ms to let any active kernel TTY blitting cycle completely finish and settle
        usleep(50 * 1000);
        
        draw_pattern_mmap(fb_mem, &vinfo, &finfo);
        ioctl(fd, FBIOPAN_DISPLAY, NULL);
        
        printf("Holding screen for 5 seconds to display the pattern...\n");
        usleep(5000 * 1000);
        
        printf("Restoring TTY console to text mode (KD_TEXT)...\n");
        ioctl(0, KDSETMODE, (void*)KD_TEXT);
        
        printf("Unmapping framebuffer...\n");
        munmap(fb_mem, size);
        printf("Done!\n");
    } else {
        printf("Linux-compatible Framebuffer Test Utility\n");
        printf("Usage: fbtest [command]\n");
        printf("Commands:\n");
        printf("  info    - Display framebuffer information (default)\n");
        printf("  clear   - Clear framebuffer to black\n");
        printf("  pattern - Draw a 4-color test pattern\n");
        printf("  mmap    - Draw a beautiful gradient instantly via mmap\n");
    }
    
    close(fd);
    return 0;
}

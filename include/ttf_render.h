#ifndef TTF_RENDER_H
#define TTF_RENDER_H

#include <tiny3d.h>


int TTFLoadFont(int set, char * path, void * from_memory, int size_from_memory);
void TTFUnloadFont();

void TTF_to_Bitmap(u8 chr, u8 * bitmap, short *w, short *h, short *y_correction);

int Render_String_UTF8(u16 * bitmap, int w, int h, u8 *string, int sw, int sh);


// initialize and create textures slots for ttf
u16 * init_ttf_table(u16 *texture);

// do one time per frame to reset the character use flag
void reset_ttf_frame(void);

// define window mode and size

#define WIN_AUTO_LF 1
#define WIN_SKIP_LF 2
#define WIN_DOUBLE_LF  4

void set_ttf_window(int x, int y, int width, int height, u32 mode);

extern float Y_ttf;
extern float Z_ttf;

// display UTF8 string int posx/posy position. With color 0 don't display and refresh/calculate the width. 
// color is the character color and sw/sh the width/height of the characters

int display_ttf_string(int posx, int posy, const char *string, u32 color, u32 bkcolor, int sw, int sh);


#endif

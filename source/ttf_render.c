
// you need the Oopo ps3libraries to work with freetype

#include <ft2build.h>
#include <freetype/freetype.h> 
#include <freetype/ftglyph.h>
#include "ttf_render.h"

/******************************************************************************************************************************************************/
/* TTF functions to load and convert fonts                                                                                                             */
/******************************************************************************************************************************************************/

static int ttf_inited = 0;

static FT_Library freetype;
static FT_Face face[4];
static int f_face[4] = {0, 0, 0, 0};

int TTFLoadFont(int set, char * path, void * from_memory, int size_from_memory)
{
   
    if(!ttf_inited)
        FT_Init_FreeType(&freetype);
    ttf_inited = 1;

    f_face[set] = 0;

    if(path) {
        if(FT_New_Face(freetype, path, 0, &face[set])<0) return -1;
    } else {
        if(FT_New_Memory_Face(freetype, from_memory, size_from_memory, 0, &face[set])) return -1;
        }

    f_face[set] = 1;

    return 0;
}

/* release all */

void TTFUnloadFont()
{
   if(!ttf_inited) return;
   FT_Done_FreeType(freetype);
   ttf_inited = 0;
}

/* function to render the character

chr : character from 0 to 255

bitmap: u8 bitmap passed to render the character character (max 256 x 256 x 1 (8 bits Alpha))

*w : w is the bitmap width as input and the width of the character (used to increase X) as output
*h : h is the bitmap height as input and the height of the character (used to Y correction combined with y_correction) as output

y_correction : the Y correction to display the character correctly in the screen

*/

void TTF_to_Bitmap(u8 chr, u8 * bitmap, short *w, short *h, short *y_correction)
{
    if(f_face[0]) FT_Set_Pixel_Sizes(face[0], (*w), (*h));
    if(f_face[1]) FT_Set_Pixel_Sizes(face[1], (*w), (*h));
    if(f_face[2]) FT_Set_Pixel_Sizes(face[2], (*w), (*h));
    if(f_face[3]) FT_Set_Pixel_Sizes(face[3], (*w), (*h));
    
    FT_GlyphSlot slot;

    memset(bitmap, 0, (*w) * (*h));

    FT_UInt index;

    if(f_face[0] && (index = FT_Get_Char_Index(face[0], (char) chr))!=0 
        && !FT_Load_Glyph(face[0], index, FT_LOAD_RENDER )) slot = face[0]->glyph;
    else if(f_face[1] && (index = FT_Get_Char_Index(face[1], (char) chr))!=0 
        && !FT_Load_Glyph(face[1], index, FT_LOAD_RENDER )) slot = face[1]->glyph;
    else if(f_face[2] && (index = FT_Get_Char_Index(face[2], (char) chr))!=0 
        && !FT_Load_Glyph(face[2], index, FT_LOAD_RENDER )) slot = face[2]->glyph;
    else if(f_face[3] && (index = FT_Get_Char_Index(face[3], (char) chr))!=0 
        && !FT_Load_Glyph(face[3], index, FT_LOAD_RENDER )) slot = face[3]->glyph;
    else {(*w) = 0; return;}

    int n, m, ww;

    *y_correction = (*h) - 1 - slot->bitmap_top;
    
    ww = 0;

    for(n = 0; n < slot->bitmap.rows; n++) {
        for (m = 0; m < slot->bitmap.width; m++) {

            if(m >= (*w) || n >= (*h)) continue;
            
            bitmap[m] = (u8) slot->bitmap.buffer[ww + m];
        }
    
    bitmap += *w;

    ww += slot->bitmap.width;
    }

    *w = ((slot->advance.x + 31) >> 6) + ((slot->bitmap_left < 0) ? -slot->bitmap_left : 0);
    *h = slot->bitmap.rows;
}

int Render_String_UTF8(u16 * bitmap, int w, int h, u8 *string, int sw, int sh)
{
    int posx = 0;
    int n, m, ww, ww2;
    u8 color;
    u32 ttf_char;

    if(f_face[0]) FT_Set_Pixel_Sizes(face[0], sw, sh);
    if(f_face[1]) FT_Set_Pixel_Sizes(face[1], sw, sh);
    if(f_face[2]) FT_Set_Pixel_Sizes(face[2], sw, sh);
    if(f_face[3]) FT_Set_Pixel_Sizes(face[3], sw, sh);

    //FT_Set_Pixel_Sizes(face, sw, sh);
    FT_GlyphSlot slot = NULL;

    memset(bitmap, 0, w * h * 2);

    while(*string) {

        if(*string == 32 || *string == 9) {posx += sw>>1; string++; continue;}

        if(*string & 128) {
            m = 1;

            if((*string & 0xf8)==0xf0) { // 4 bytes
                ttf_char = (u32) (*(string++) & 3);
                m = 3;
            } else if((*string & 0xE0)==0xE0) { // 3 bytes
                ttf_char = (u32) (*(string++) & 0xf);
                m = 2;
            } else if((*string & 0xE0)==0xC0) { // 2 bytes
                ttf_char = (u32) (*(string++) & 0x1f);
                m = 1;
            } else {string++;continue;} // error!

             for(n = 0; n < m; n++) {
                if(!*string) break; // error!
                    if((*string & 0xc0) != 0x80) break; // error!
                    ttf_char = (ttf_char <<6) |((u32) (*(string++) & 63));
             }
           
            if((n != m) && !*string) break;
        
        } else ttf_char = (u32) *(string++);

        if(ttf_char == 13 || ttf_char == 10) ttf_char='/';

        FT_UInt index;

        if(f_face[0] && (index = FT_Get_Char_Index(face[0], ttf_char))!=0 
            && !FT_Load_Glyph(face[0], index, FT_LOAD_RENDER )) slot = face[0]->glyph;
        else if(f_face[1] && (index = FT_Get_Char_Index(face[1], ttf_char))!=0 
            && !FT_Load_Glyph(face[1], index, FT_LOAD_RENDER )) slot = face[1]->glyph;
        else if(f_face[2] && (index = FT_Get_Char_Index(face[2], ttf_char))!=0 
            && !FT_Load_Glyph(face[2], index, FT_LOAD_RENDER )) slot = face[2]->glyph;
        else if(f_face[3] && (index = FT_Get_Char_Index(face[3], ttf_char))!=0 
            && !FT_Load_Glyph(face[3], index, FT_LOAD_RENDER )) slot = face[3]->glyph;
        else ttf_char = 0;

        if(ttf_char!=0 && slot->bitmap.buffer) {
            ww = ww2 = 0;

            int y_correction = sh - 1 - slot->bitmap_top;
            if(y_correction < 0) y_correction = 0;
            ww2 = y_correction * w;

            for(n = 0; n < slot->bitmap.rows; n++) {
                if(n + y_correction >= h) break;
                for (m = 0; m < slot->bitmap.width; m++) {

                    if(m + posx >= w) continue;
                    
                    color = (u8) slot->bitmap.buffer[ww + m];
                    
                    if(color) bitmap[posx + m + ww2] = (color<<8) | 0xfff;
                }
            
            ww2 += w;

            ww += slot->bitmap.width;
            }
            
        }

        if(slot) posx+= slot->bitmap.width;
    }
    return posx;
}

// constructor dinamico de fuentes 32 x 32

typedef struct ttf_dyn {
    u32 ttf;
    u16 *text;
    u32 r_use;
    u16 y_start;
    u16 width;
    u16 height;
    u16 flags;

} ttf_dyn;

#define MAX_CHARS 1600

static ttf_dyn ttf_font_datas[MAX_CHARS];

static u32 r_use= 0;

float Y_ttf = 0.0f;
float Z_ttf = 0.0f;

static int Win_X_ttf = 0;
static int Win_Y_ttf = 0;
static int Win_W_ttf = 848;
static int Win_H_ttf = 512;


static u32 Win_flag = 0;

void set_ttf_window(int x, int y, int width, int height, u32 mode)
{
    Win_X_ttf = x;
    Win_Y_ttf = y;
    Win_W_ttf = width;
    Win_H_ttf = height;
    Win_flag = mode;
    Y_ttf = 0.0f;
    Z_ttf = 0.0f;
   
}

u16 * init_ttf_table(u16 *texture)
{
    int n;

    r_use= 0;
    for(n= 0; n <  MAX_CHARS; n++) {
        memset(&ttf_font_datas[n], 0, sizeof(ttf_dyn));
        ttf_font_datas[n].text = texture;

        texture+= 32*32;
    }

    return texture;

}

void reset_ttf_frame(void)
{
    int n;

    for(n= 0; n <  MAX_CHARS; n++) {
        
        ttf_font_datas[n].flags &= 1;

    }

    r_use++;

}

static void DrawBox_ttf(float x, float y, float z, float w, float h, u32 rgba)
{
    tiny3d_SetPolygon(TINY3D_QUADS);
    
   
    tiny3d_VertexPos(x    , y    , z);
    tiny3d_VertexColor(rgba);

    tiny3d_VertexPos(x + w, y    , z);

    tiny3d_VertexPos(x + w, y + h, z);

    tiny3d_VertexPos(x    , y + h, z);

    tiny3d_End();
}

static void DrawTextBox_ttf(float x, float y, float z, float w, float h, u32 rgba, float tx, float ty)
{
    tiny3d_SetPolygon(TINY3D_QUADS);
    
   
    tiny3d_VertexPos(x    , y    , z);
    tiny3d_VertexColor(rgba);
    tiny3d_VertexTexture(0.0f , 0.0f);

    tiny3d_VertexPos(x + w, y    , z);
    tiny3d_VertexTexture(tx, 0.0f);

    tiny3d_VertexPos(x + w, y + h, z);
    tiny3d_VertexTexture(tx, ty);

    tiny3d_VertexPos(x    , y + h, z);
    tiny3d_VertexTexture(0.0f , ty);

    tiny3d_End();
}


#define TTF_UX 30
#define TTF_UY 24


int display_ttf_string(int posx, int posy, const char *string, u32 color, u32 bkcolor, int sw, int sh)
{
    int l,n, m, ww, ww2;
    u8 colorc;
    u32 ttf_char;
    u8 *ustring = (u8 *) string;

    int lenx = 0;

    while(*ustring) {

        if(posy >= Win_H_ttf) break;

        if(*ustring == 32 || *ustring == 9) {posx += sw>>1; ustring++; continue;}

        if(*ustring & 128) {
            m = 1;

            if((*ustring & 0xf8)==0xf0) { // 4 bytes
                ttf_char = (u32) (*(ustring++) & 3);
                m = 3;
            } else if((*ustring & 0xE0)==0xE0) { // 3 bytes
                ttf_char = (u32) (*(ustring++) & 0xf);
                m = 2;
            } else if((*ustring & 0xE0)==0xC0) { // 2 bytes
                ttf_char = (u32) (*(ustring++) & 0x1f);
                m = 1;
            } else {ustring++;continue;} // error!

             for(n = 0; n < m; n++) {
                if(!*ustring) break; // error!
                    if((*ustring & 0xc0) != 0x80) break; // error!
                    ttf_char = (ttf_char <<6) |((u32) (*(ustring++) & 63));
             }
           
            if((n != m) && !*ustring) break;
        
        } else ttf_char = (u32) *(ustring++);


        if(Win_flag & WIN_SKIP_LF) {
            if(ttf_char == '\r' || ttf_char == '\n') ttf_char='/';
        } else {
            if(Win_flag & WIN_DOUBLE_LF) {
                if(ttf_char == '\r') {if(posx > lenx) lenx = posx; posx = 0;continue;}
                if(ttf_char == '\n') {posy += sh;continue;}
            } else {
                if(ttf_char == '\n') {if(posx > lenx) lenx = posx; posx = 0;posy += sh;continue;}
            }
        }

        if(ttf_char < 32) ttf_char='?';

        // search ttf_char
        if(ttf_char < 128) n= ttf_char;
        else {
            m= 0;
            int rel=0;
            
            for(n= 128; n < MAX_CHARS; n++) {
                if(!(ttf_font_datas[n].flags & 1)) m= n;
                
                if((ttf_font_datas[n].flags & 3)==1) {
                    int trel= r_use - ttf_font_datas[n].r_use;
                    if(m==0) {m= n;rel = trel;}
                    else if(rel > trel) {m= n;rel = trel;}

                }
                if(ttf_font_datas[n].ttf == ttf_char) break;
            }

            if(m==0) m = 128;

        }

        if(n >= MAX_CHARS) {ttf_font_datas[m].flags = 0; l= m;} else l=n;

        u16 * bitmap = ttf_font_datas[l].text;
        
        // building the character

        if(!(ttf_font_datas[l].flags & 1)) { 

            if(f_face[0]) FT_Set_Pixel_Sizes(face[0], TTF_UX, TTF_UY);
            if(f_face[1]) FT_Set_Pixel_Sizes(face[1], TTF_UX, TTF_UY);
            if(f_face[2]) FT_Set_Pixel_Sizes(face[2], TTF_UX, TTF_UY);
            if(f_face[3]) FT_Set_Pixel_Sizes(face[3], TTF_UX, TTF_UY);

            FT_GlyphSlot slot = NULL;

            memset(bitmap, 0, 32 * 32 * 2);

            ///////////

            FT_UInt index;

            if(f_face[0] && (index = FT_Get_Char_Index(face[0], ttf_char))!=0 
                && !FT_Load_Glyph(face[0], index, FT_LOAD_RENDER )) slot = face[0]->glyph;
            else if(f_face[1] && (index = FT_Get_Char_Index(face[1], ttf_char))!=0 
                && !FT_Load_Glyph(face[1], index, FT_LOAD_RENDER )) slot = face[1]->glyph;
            else if(f_face[2] && (index = FT_Get_Char_Index(face[2], ttf_char))!=0 
                && !FT_Load_Glyph(face[2], index, FT_LOAD_RENDER )) slot = face[2]->glyph;
            else if(f_face[3] && (index = FT_Get_Char_Index(face[3], ttf_char))!=0 
                && !FT_Load_Glyph(face[3], index, FT_LOAD_RENDER )) slot = face[3]->glyph;
            else ttf_char = 0;

            if(ttf_char!=0) {
                ww = ww2 = 0;

                int y_correction = TTF_UY - 1 - slot->bitmap_top;
                if(y_correction < 0) y_correction = 0;

                ttf_font_datas[l].flags = 1;
                ttf_font_datas[l].y_start = y_correction;
                ttf_font_datas[l].height = slot->bitmap.rows;
                ttf_font_datas[l].width = slot->bitmap.width;
                ttf_font_datas[l].ttf = ttf_char;
                

                for(n = 0; n < slot->bitmap.rows; n++) {
                    if(n >= 32) break;
                    for (m = 0; m < slot->bitmap.width; m++) {

                        if(m >= 32) continue;
                        
                        colorc = (u8) slot->bitmap.buffer[ww + m];
                        
                        if(colorc) bitmap[m + ww2] = (colorc<<8) | 0xfff;
                    }
                
                ww2 += 32;

                ww += slot->bitmap.width;
                }
                
            }
        }

        // displaying the character
        ttf_font_datas[l].flags |= 2; // in use
        ttf_font_datas[l].r_use = r_use;

        if((Win_flag & WIN_AUTO_LF) && (posx + (ttf_font_datas[l].width * sw / 32) + 1) > Win_W_ttf) {
            posx = 0;
            posy += sh;
        }

        u32 ccolor = color;
        u32 cx =(ttf_font_datas[l].width * sw / 32) + 1;

        // skip if out of window
        if((posx + cx) > Win_W_ttf || (posy + sh) > Win_H_ttf ) ccolor = 0;

        if(ccolor) {
            tiny3d_SetTextureWrap(0, tiny3d_TextureOffset(bitmap), 32, 32, 32 * 2,
                TINY3D_TEX_FORMAT_A4R4G4B4, TEXTWRAP_CLAMP, TEXTWRAP_CLAMP, TEXTURE_LINEAR);

            if (bkcolor != 0) DrawBox_ttf((float) (Win_X_ttf + posx), (float) (Win_Y_ttf + posy) + ((float) ttf_font_datas[l].y_start * sh) * 0.03125f,
            Z_ttf, (float) sw, (float) sh, bkcolor);
            DrawTextBox_ttf((float) (Win_X_ttf + posx), (float) (Win_Y_ttf + posy) + ((float) ttf_font_datas[l].y_start * sh) * 0.03125f,
            Z_ttf, (float) sw, (float) sh, color,
            0.99f, 0.99f);
        }

        posx+= cx;
    }

    Y_ttf = (float) posy + sh;

    if(posx < lenx) posx = lenx;
    return posx;
}

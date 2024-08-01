/*
    These are my custom extensions to libtonc for printing characters upside down.
    They are based directly on the libtonc implementations which is licensed as follows:
*/

/*
Copyright 2005-2009 J Vijn

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "tonc_ext.h"

typedef u16 pixel_t;
#define PXSIZE	sizeof(pixel_t)
#define PXPTR(psrf, x, y)   \
    (pixel_t*)(psrf->data + (y)*psrf->pitch + (x)*sizeof(pixel_t) )

extern uint utf8_decode_char(const char *ptr, char **endptr);

void bmp16_drawg_b1cts_ud(uint gid)
{
    TTE_BASE_VARS(tc, font);
    TTE_CHAR_VARS(font, gid, u8, srcD, srcL, charW, charH);
    TTE_DST_VARS(tc, u16, dstD, dstL, dstP, x0, y0);
    uint srcP= font->cellH;
    dstD += x0;

    u32 ink= tc->cattr[TTE_INK], raw;

    //# Fixme (src, dst, nx)
    int ix, iy, iw;
    for(iw=0; iw<charW; iw += 8)
    {
        dstL= &dstD[iw];
        for(iy=0; iy<charH; iy++)
        {
            raw= srcL[iy];
            for(ix=0; raw>0; raw>>=1, ix--)
                if(raw&1)
                    dstL[ix - dstP/2]= ink;

            dstL -= dstP/2;
        }
        srcL += srcP;
    }
}

int	tte_write_ud(const char *text)
{
	if(text == NULL)
		return 0;

	uint ch, gid;
	char *str= (char*)text;
	TTC *tc= tte_get_context();
	TFont *font;

	while( (ch=*str) != '\0' )
	{
		str++;
		switch(ch)
		{
		// --- Newline/carriage return ---
		case '\r':
			if(str[0] == '\n')	// deal with CRLF pair
				str++;
			// FALLTHRU
		case '\n':
			tc->cursorY += tc->font->charH;
			tc->cursorX  = tc->marginLeft;
			break;
		// --- Tab ---
		case '\t':
			tc->cursorX= (tc->cursorX/TTE_TAB_WIDTH+1)*TTE_TAB_WIDTH;
			break;

		// --- Normal char ---
		default:
			// Command sequence
			if(ch=='#' && str[0]=='{')
			{
				str= tte_cmd_default(str+1);
				break;
			}
			// Escaped command: skip '\\' and print '#'
			else if(ch=='\\' && str[0]=='#')
				ch= *str++;
			// Check for UTF8 code
			else if(ch>=0x80)
				ch= utf8_decode_char(str-1, &str);

			// Get glyph index and call renderer
			font= tc->font;
			gid= ch - font->charOffset;
			if(tc->charLut)
				gid= tc->charLut[gid];

			// Character wrap
			int charW= font->widths ? font->widths[gid] : font->charW;
			if(tc->cursorX+charW > tc->marginRight)
			{
				tc->cursorY += font->charH;
				tc->cursorX  = tc->marginLeft;
			}

			// Draw and update position
			//tc->drawgProc(gid);
            bmp16_drawg_b1cts_ud(gid);
			tc->cursorX += charW;
		}
	}

	// Return characters used (PONDER: is this really the right thing?)
	return str - text;
}

void memcpy16_rev(u16 *target, u16 *source, int length)
{
    int i = 0;
    do
    {
        target[i++] = source[length - 1];
    }
    while (--length);
}

void memset16_rev(u16 *target, u16 value, int length)
{
    int i = 0;
    do
    {
        target[i++] = value;
    }
    while (--length);
}

void sbmp16_blit_ud(const TSurface *dst, int dstX, int dstY,
    uint width, uint height, const TSurface *src, int srcX, int srcY)
{
    // Safety checks
    if(src==NULL || dst==NULL || src->data==NULL || dst->data==NULL)
        return;

    // --- Clip ---
    int w= width, h= height;

    dstX = SCREEN_WIDTH - dstX - 1;
    dstY = SCREEN_HEIGHT - dstY - 1;

/// Temporary bliter clipping macro
#define BLIT_CLIP(_ax, _aw, _w, _bx)                \
    do {                                            \
        if( (_ax) >= (_aw) || (_ax)+(_w) <= 0 )     \
            return;                                 \
        if( (_ax)<0 )                               \
        {   _w += (_ax); _bx += (_ax); _ax= 0;  }   \
        if( (_w) > (_aw)-(_ax) )                    \
            _w = (_aw)-(_ax);                       \
    } while(0)

    // Clip horizontal
    BLIT_CLIP(dstX, dst->width, w, srcX);
    BLIT_CLIP(srcX, src->width, w, dstX);

    // Clip vertical
    BLIT_CLIP(dstY, dst->height, h, srcY);
    BLIT_CLIP(srcY, src->height, h, dstY);

    pixel_t *srcL= PXPTR(src, srcX, srcY);
    pixel_t *dstL= PXPTR(dst, dstX, dstY);
    uint srcP= src->pitch/PXSIZE, dstP= dst->pitch/PXSIZE;

    // Copy clipped rectangle.
    while(h--)
    {
        dstL -= dstP;
        memcpy16_rev(dstL, srcL, w);
        srcL += srcP;
    }

#undef BLIT_CLIP
}

void sbmp16_rect_ud(const TSurface *dst,
    int left, int top, int right, int bottom, u32 clr)
{
    if(left==right || top==bottom)
        return;

    if(right<left)  {   int tmp= left; left= right; right= tmp; }
    if(bottom<top)  {   int tmp= top; top= bottom; bottom= tmp; }

    u32 width= right-left, height= bottom-top;
    pixel_t *dstL= PXPTR(dst, SCREEN_WIDTH - left - 1, SCREEN_HEIGHT - top - 1);
    u32 dstP= dst->pitch/PXSIZE;

    // --- Draw ---
    while(height--)
    {   dstL -= dstP; memset16_rev(dstL, clr, width);   }
}


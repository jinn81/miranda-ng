#ifndef __fonts_h__
#define __fonts_h__

/////////////////////////////////////////////////////////////////////////////////////////

enum
{
	COLOR_INMSG, COLOR_OUTMSG,
	COLOR_INFILE, COLOR_OUTFILE,
	COLOR_STATUS,
	COLOR_INOTHER, COLOR_OUTOTHER,
	COLOR_SELTEXT, COLOR_SELBACK, COLOR_SELFRAME,
	COLOR_BACK, COLOR_FRAME,
	COLOR_COUNT
};

struct MyColourID
{
	const char *szName, *szSetting;
	COLORREF defaultValue, cl;
};

extern MyColourID g_colorTable[COLOR_COUNT];

/////////////////////////////////////////////////////////////////////////////////////////

enum
{
	FONT_INNICK,
	FONT_OUTNICK,
	FONT_INMSG,
	FONT_OUTMSG,
	FONT_INFILE,
	FONT_OUTFILE,
	FONT_STATUS,
	FONT_INOTHER,
	FONT_OUTOTHER,
	FONT_COUNT
};

struct MyFontID
{
	const char *szName, *szSetting;

	COLORREF defaultValue, cl;
	LOGFONTA lf;
	HFONT    hfnt;
};

extern MyFontID g_fontTable[FONT_COUNT];

/////////////////////////////////////////////////////////////////////////////////////////

void InitFonts();
void DestroyFonts();

#endif // __fonts_h__
#ifndef __mtext_h__
#define __mtext_h__

// Text control
#define MTEXTCONTROLCLASS "MTextControl"
#define MTM_SETUSER WM_USER
#define MTM_UPDATE  WM_USER+1

#ifdef MTEXTCONTROL_EXPORTS
	#define MTEXTCONTROL_EXPORT __declspec(dllexport)
#else
	#define MTEXTCONTROL_EXPORT __declspec(dllimport)
#endif

#define MTEXTCONTROL_DLL(T) MTEXTCONTROL_EXPORT T __stdcall

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	// visual text options, used in MS_TEXT_REGISTER
	MTEXT_FANCY_SMILEYS   = 0x00000010, // SmileyAdd smileys
	MTEXT_FANCY_BBCODES   = 0x00000020, // [b], [u], [i]
	MTEXT_FANCY_MATHMOD   = 0x00000040, // enable math module formula parsing
	MTEXT_FANCY_URLS      = 0x00000080, // underline urls
	MTEXT_FANCY_BBCODES2  = 0x00000100, // [color], [img], [url], not implemented yet
	MTEXT_FANCY_SIMPLEFMT = 0x00000200, // simple formatting ("_", "/" and "*")
	MTEXT_FANCY_MASK      = 0x00007fff,
	MTEXT_FANCY_DEFAULT   = 0x00008000, // Use default options

	// text options, used in MS_TEXT_REGISTER
	MTEXT_SYSTEM_HICONS   = 0x00010000, // [$handle=i<HICON as dword>$]
	MTEXT_SYSTEM_HBITMAPS = 0x00010000, // [$handle=b<HBITMAP as dword>$], not implemented yet
	MTEXT_SYSTEM_ESCAPED  = 0x00020000, // passed text is escaped with slashes, not implemented yet
	MTEXT_SYSTEM_MASK     = 0x7fff0000,
	MTEXT_SYSTEM_DEFAULT  = 0x80000000,  // Use default option -- just nothing system is used :)

	// text object flags
	MTEXT_FLG_CHAR        = 0x00000000,
	MTEXT_FLG_WCHAR       = 0x00000001,
	MTEXT_FLG_BIDI_RTL    = 0x00000002
};

// subscribe to MText services
MTEXTCONTROL_DLL(HANDLE) MTextRegister(const char *userTitle, DWORD options);

// allocate text object (unicode)
MTEXTCONTROL_DLL(HANDLE) MTextCreateW(HANDLE userHandle, const char *szProto, const wchar_t *text);

// allocate text object (advanced)
MTEXTCONTROL_DLL(HANDLE) MTextCreateEx(HANDLE userHandle, void *text, DWORD flags);

// destroys text object
MTEXTCONTROL_DLL(int) MTextDestroy(HANDLE text);

// measures text object
// result = 1 (success), 0 (failure)
// sz->cx is interpreted as maximum width allowed.
// wrapped text size is stored in sz, text
MTEXTCONTROL_DLL(int) MTextMeasure(HDC dc, SIZE *sz, HANDLE text);

// display text object
// result = 1 (success), 0 (failure)
MTEXTCONTROL_DLL(int) MTextDisplay(HDC dc, POINT pos, SIZE sz, HANDLE text);

// set parent window for text object (this is required for mouse handling, etc)
MTEXTCONTROL_DLL(int) MTextSetParent(HANDLE text, HWND hwnd);

// send message to an object
MTEXTCONTROL_DLL(int) MTextSendMessage(HWND hwnd, HANDLE text, UINT msg, WPARAM wParam, LPARAM lParam);

#ifdef __cplusplus
}
#endif

#endif // __mtext_h__

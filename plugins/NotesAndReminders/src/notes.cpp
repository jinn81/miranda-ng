#include "stdafx.h"

#ifndef MONITOR_DEFAULTTONULL
#define MONITOR_DEFAULTTONULL       0x00000000
#endif

// NotesData DB data params
#define DATATAG_TEXT       1	// %s
#define DATATAG_SCROLLPOS  2	// %u (specifies rich edit controls scroll post as first visible line)
#define DATATAG_BGCOL      3	// %x (custom background color)
#define DATATAG_FGCOL      4	// %x (custom text/fg colors)
#define DATATAG_TITLE      5	// %s (custom note title)
#define DATATAG_FONT       6	// %d:%u:%u:%s (custom font)

#define MAX_TITLE_LEN	63
#define MAX_NOTE_LEN	16384

// delay before saving note changes (ms)
#define NOTE_CHANGE_COMMIT_DELAY 1000

#ifndef WS_EX_NOACTIVATE
#define WS_EX_NOACTIVATE 0x08000000
#endif

#define WS_EX_LAYERED 0x00080000
#define LWA_ALPHA 0x00000002

#define WM_RELOAD (WM_USER + 100)

#define NOTIFY_LIST() if (ListNotesVisible) PostMessage(LV,WM_RELOAD,0,0)

#define PENLINK ENLINK *

#define NOTE_WND_CLASS L"MIM_StickyNote"

#define IDM_COLORPRESET_BG 41000
#define IDM_COLORPRESET_FG 41100

static bool ListNotesVisible = false;
static HWND LV;

struct ColorPreset
{
	wchar_t *szName;
	COLORREF color;
};

static struct ColorPreset clrPresets[] =
{
	{ LPGENW("Black"),   RGB(0,0,0)       },
	{ LPGENW("Maroon"),  RGB(128,0,0)     },
	{ LPGENW("Green"),   RGB(0,128,0)     },
	{ LPGENW("Olive"),   RGB(128,128,0)   },
	{ LPGENW("Navy"),    RGB(0,0,128)     },
	{ LPGENW("Purple"),  RGB(128,0,128)   },
	{ LPGENW("Teal"),    RGB(0,128,128)   },
	{ LPGENW("Gray"),    RGB(128,128,128) },
	{ LPGENW("Silver"),  RGB(192,192,192) },
	{ LPGENW("Red"),     RGB(255,0,0)     },
	{ LPGENW("Orange"),  RGB(255,155,0)   },
	{ LPGENW("Lime"),    RGB(0,255,0)     },
	{ LPGENW("Yellow"),  RGB(255,255,0)   },
	{ LPGENW("Blue"),    RGB(0,0,255)     },
	{ LPGENW("Fuchsia"), RGB(255,0,255)   },
	{ LPGENW("Aqua"),    RGB(0,255,255)   },
	{ LPGENW("White"),   RGB(255,255,255) }
};

/////////////////////////////////////////////////////////////////////////////////////////

struct STICKYNOTEFONT : public MZeroedObject
{
	HFONT hFont;
	char  size;
	BYTE  style;					// see the DBFONTF_* flags
	BYTE  charset;
	wchar_t szFace[LF_FACESIZE];
};

struct STICKYNOTE : public MZeroedObject
{
	HWND SNHwnd, REHwnd;
	BOOL bVisible, bOnTop;
	CMStringA szText;
	ULARGE_INTEGER ID;		// FILETIME in UTC
	wchar_t *title;
	BOOL CustomTitle;
	DWORD BgColor;			// custom bg color override (only valid if non-zero)
	DWORD FgColor;			// custom fg/text color override (only valid if non-zero)
	STICKYNOTEFONT *pCustomFont;// custom (body) font override (NULL if default font is used)

	~STICKYNOTE()
	{
		if (SNHwnd)
			DestroyWindow(SNHwnd);
		SAFE_FREE((void**)&title);
		if (pCustomFont) {
			DeleteObject(pCustomFont->hFont);
			free(pCustomFont);
		}
	}
};

static OBJLIST<STICKYNOTE> g_arStickies(1, PtrKeySortT);

void GetTriggerTimeString(const ULARGE_INTEGER *When, wchar_t *s, size_t strSize, BOOL bUtc);
void OnListResize(HWND hwndDlg);
void FileTimeToTzLocalST(const FILETIME *lpUtc, SYSTEMTIME *tmLocal);

COLORREF GetCaptionColor(COLORREF bodyClr)
{
	const DWORD r = ((bodyClr & 0xff) * 4) / 5;
	const DWORD g = (((bodyClr & 0xff00) * 4) / 5) & 0xff00;
	const DWORD b = (((bodyClr & 0xff0000) * 4) / 5) & 0xff0000;

	return (COLORREF)(r | g | b);
}

static void EnsureUniqueID(STICKYNOTE *TSN)
{
	if (!g_arStickies.getCount())
		return;

try_next:
	// check existing notes if id is in use
	for (auto &it : g_arStickies) {
		if (it->ID.QuadPart == TSN->ID.QuadPart) {
			// id in use, try new (increases the ID/time stamp by 100 nanosecond steps until an unused time is found,
			// allthough it's very unlikely that there will be duplicated id's it's better to make 100% sure)
			TSN->ID.QuadPart++;
			goto try_next;
		}
	}
}

static void InitNoteTitle(STICKYNOTE *TSN)
{
	if (g_NoteTitleDate) {
		wchar_t TempStr[MAX_PATH];
		SYSTEMTIME tm;
		LCID lc = GetUserDefaultLCID();

		TempStr[0] = 0;

		memset(&tm, 0, sizeof(tm));
		FileTimeToTzLocalST((FILETIME*)&TSN->ID, &tm);

		if (GetDateFormatW(lc, 0, &tm, GetDateFormatStr(), TempStr, MAX_PATH)) {
			// append time if requested
			if (g_NoteTitleTime) {
				int n = (int)mir_wstrlen(TempStr);
				TempStr[n++] = ' ';
				TempStr[n] = 0;

				GetTimeFormat(MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), 0), 0, &tm, GetTimeFormatStr(), TempStr + n, MAX_PATH - n);
			}

			TSN->title = _wcsdup(TempStr);
		}
	}

	TSN->CustomTitle = FALSE;
}

static void InitStickyNoteLogFont(STICKYNOTEFONT *pCustomFont, LOGFONT *lf)
{
	if (!pCustomFont->size) {
		SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(*lf), &lf, FALSE);
		lf->lfHeight = 10;
		HDC hdc = GetDC(nullptr);
		lf->lfHeight = -MulDiv(lf->lfHeight, GetDeviceCaps(hdc, LOGPIXELSY), 72);
		ReleaseDC(nullptr, hdc);
	}
	else {
		lf->lfHeight = pCustomFont->size;
	}

	wcsncpy_s(lf->lfFaceName, pCustomFont->szFace, _TRUNCATE);
	lf->lfWidth = lf->lfEscapement = lf->lfOrientation = 0;
	lf->lfWeight = pCustomFont->style & DBFONTF_BOLD ? FW_BOLD : FW_NORMAL;
	lf->lfItalic = (pCustomFont->style & DBFONTF_ITALIC) != 0;
	lf->lfUnderline = (pCustomFont->style & DBFONTF_UNDERLINE) != 0;
	lf->lfStrikeOut = (pCustomFont->style & DBFONTF_STRIKEOUT) != 0;
	lf->lfCharSet = pCustomFont->charset;
	lf->lfOutPrecision = OUT_DEFAULT_PRECIS;
	lf->lfClipPrecision = CLIP_DEFAULT_PRECIS;
	lf->lfQuality = DEFAULT_QUALITY;
	lf->lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
}

static bool CreateStickyNoteFont(STICKYNOTEFONT *pCustomFont, LOGFONT *plf)
{
	LOGFONT lf = {};

	if (!plf) {
		InitStickyNoteLogFont(pCustomFont, &lf);
		plf = &lf;
	}

	if (pCustomFont->hFont)
		DeleteObject(pCustomFont->hFont);

	pCustomFont->hFont = CreateFontIndirectW(plf);
	return pCustomFont->hFont != nullptr;
}

void CloseNotesList()
{
	if (ListNotesVisible) {
		DestroyWindow(LV);
		ListNotesVisible = false;
	}
}

void PurgeNotes(void)
{
	char ValueName[16];

	int NotesCount = g_plugin.getDword("NotesData", 0);
	for (int i = 0; i < NotesCount; i++) {
		mir_snprintf(ValueName, "NotesData%d", i);
		g_plugin.delSetting(ValueName);
	}
}

void DeleteNotes(void)
{
	PurgeNotes();
	g_plugin.setDword("NotesData", 0);
	g_arStickies.destroy();
	NOTIFY_LIST();
}

void BringAllNotesToFront(STICKYNOTE *pActive)
{
	if (!g_arStickies.getCount())
		return;

	// NOTE: for some reason there are issues when bringing to top through hotkey while another app (like Explorer)
	//       is active, it refuses to move notes to top like it should with HWND_TOP. as a workaround still doesn't
	//       work 100% of the time, but at least more often, we first move not to top-most then for non-always-on-top
	//       notes we demote them back as a non top-most window
	for (auto &SN : g_arStickies) {
		if (SN->bVisible && pActive != SN) {
			SetWindowPos(SN->SNHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			if (!SN->bOnTop)
				SetWindowPos(SN->SNHwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		}
	}

	if (pActive) {
		SetWindowPos(pActive->SNHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		if (!pActive->bOnTop)
			SetWindowPos(pActive->SNHwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	}
}

// pModified optionally points to the modified note that invoked the JustSaveNotes call
static void JustSaveNotes(STICKYNOTE *pModified = nullptr)
{
	int i = 0, NotesCount = g_arStickies.getCount();
	char ValueName[32];

	const int OldNotesCount = g_plugin.getDword("NotesData", 0);

	g_plugin.setDword("NotesData", NotesCount);

	for (auto &pNote : g_arStickies) {
		int scrollV = 0;
		char *tData = nullptr;

		// window pos and size
		WINDOWPLACEMENT wp;
		wp.length = sizeof(WINDOWPLACEMENT);
		GetWindowPlacement(pNote->SNHwnd, &wp);
		int TX = wp.rcNormalPosition.left;
		int TY = wp.rcNormalPosition.top;
		int TW = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
		int TH = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;

		// set flags
		DWORD flags = 0;
		if (pNote->bVisible) flags |= 1;
		if (pNote->bOnTop) flags |= 2;

		// get note text
		int SzT = GetWindowTextLength(pNote->REHwnd);
		if (SzT) { // TODO: change to support unicode and rtf, use EM_STREAMOUT
			if (SzT > MAX_NOTE_LEN)
				SzT = MAX_NOTE_LEN; // we want to be far below the 64k limit
			tData = (char*)malloc(SzT + 1);
			if (tData)
				GetWindowTextA(pNote->REHwnd, tData, SzT + 1);
		}

		// update the data of the modified note
		if (pNote == pModified)
			pNote->szText = tData ? tData : "";

		if (!tData) // empty note
			SzT = 0;
		else // get current scroll position
			scrollV = SendMessage(pNote->REHwnd, EM_GETFIRSTVISIBLELINE, 0, 0);

		// data header
		CMStringA szValue;
		szValue.AppendFormat("X%I64x:%d:%d:%d:%d:%x", pNote->ID.QuadPart, TX, TY, TW, TH, flags);

		// scroll pos
		if (scrollV > 0)
			szValue.AppendFormat("\033""%u:%u", DATATAG_SCROLLPOS, (UINT)scrollV);

		// custom bg color
		if (pNote->BgColor)
			szValue.AppendFormat("\033""%u:%x", DATATAG_BGCOL, (UINT)(pNote->BgColor & 0xffffff));

		// custom fg color
		if (pNote->FgColor)
			szValue.AppendFormat("\033""%u:%x", DATATAG_FGCOL, (UINT)(pNote->FgColor & 0xffffff));

		if (pNote->pCustomFont) {
			szValue.AppendFormat("\033""%u:%d:%u:%u:%s", DATATAG_FONT,
				(int)pNote->pCustomFont->size, (UINT)pNote->pCustomFont->style, (UINT)pNote->pCustomFont->charset,
				pNote->pCustomFont->szFace);
		}

		// custom title
		if (pNote->CustomTitle && pNote->title)
			szValue.AppendFormat("\033""%u:%s", DATATAG_TITLE, pNote->title);

		// note text (ALWAYS PUT THIS PARAM LAST)
		if (tData)
			szValue.AppendFormat("\033""%u:%s", DATATAG_TEXT, tData);

		mir_snprintf(ValueName, "NotesData%d", i++); // we do not reverse notes in DB
		db_set_blob(0, MODULENAME, ValueName, szValue.GetBuffer(), szValue.GetLength() + 1);

		SAFE_FREE((void**)&tData);

		// make no save is queued for the note
		if (pNote->SNHwnd)
			KillTimer(pNote->SNHwnd, 1025);
	}

	// delete any left over DB note entries
	for (; i < OldNotesCount; i++) {
		mir_snprintf(ValueName, "NotesData%d", i);
		g_plugin.delSetting(ValueName);
	}

	NOTIFY_LIST();
}

void OnDeleteNote(HWND hdlg, STICKYNOTE *SN)
{
	if (MessageBoxW(hdlg, TranslateT("Are you sure you want to delete this note?"), TranslateT(SECTIONNAME), MB_OKCANCEL) == IDOK) {
		g_arStickies.remove(SN);
		JustSaveNotes();
		NOTIFY_LIST();
	}
}

void ShowHideNotes(void)
{
	if (!g_arStickies.getCount())
		return;

	// if some notes are hidden but others visible then first make all visible
	// only toggle vis state if all are hidden or all are visible

	UINT nHideCount = 0, nVisCount = 0;

	for (auto &SN : g_arStickies) {
		if (SN->bVisible)
			nVisCount++;
		else
			nHideCount++;
	}

	bool bVisible;
	if (!nVisCount)
		bVisible = true;
	else if (!nHideCount)
		bVisible = false;
	else
		bVisible = true;

	int bShow = bVisible ? SW_SHOWNA : SW_HIDE;
	for (auto &SN : g_arStickies) {
		if ((!bVisible) != (!SN->bVisible)) {
			ShowWindow(SN->SNHwnd, bShow);
			SN->bVisible = bVisible;
		}
	}

	JustSaveNotes();
}

void SaveNotes(void)
{
	JustSaveNotes();
	g_arStickies.destroy();
}

/////////////////////////////////////////////////////////////////////////////////////////
// Note Window

static int FindMenuItem(HMENU h, LPSTR lpszName)
{
	int n = GetMenuItemCount(h);
	if (n <= 0)
		return -1;

	// searches for a menu item based on name (used to avoid hardcoding item indices for sub-menus)
	for (int i = 0; i < n; i++) {
		char s[128];

		if (GetMenuStringA(h, i, s, 128, MF_BYPOSITION))
			if (!mir_strcmp(s, lpszName))
				return (int)i;
	}

	return -1;
}

static BOOL DoContextMenu(HWND AhWnd, WPARAM, LPARAM lParam)
{
	STICKYNOTE *SN = (STICKYNOTE*)GetPropA(AhWnd, "ctrldata");

	HMENU hMenuLoad, FhMenu, hSub;
	hMenuLoad = LoadMenuA(g_plugin.getInst(), "MNU_NOTEPOPUP");
	FhMenu = GetSubMenu(hMenuLoad, 0);

	if (SN->bOnTop)
		CheckMenuItem(FhMenu, ID_CONTEXTMENUNOTE_TOGGLEONTOP, MF_CHECKED | MF_BYCOMMAND);

	EnableMenuItem(FhMenu, ID_CONTEXTMENUNOTE_PASTETITLE, MF_BYCOMMAND | (IsClipboardFormatAvailable(CF_TEXT) ? MF_ENABLED : MF_GRAYED));

	if (!SN->CustomTitle)
		EnableMenuItem(FhMenu, ID_CONTEXTMENUNOTE_RESETTITLE, MF_BYCOMMAND | MF_GRAYED);

	// NOTE: names used for FindMenuItem would need to include & chars if such shortcuts are added to the menus

	int n = FindMenuItem(FhMenu, "Appearance");
	if (n >= 0 && (hSub = GetSubMenu(FhMenu, n))) {
		HMENU hBg = GetSubMenu(hSub, FindMenuItem(hSub, "Background Color"));
		HMENU hFg = GetSubMenu(hSub, FindMenuItem(hSub, "Text Color"));

		for (int i = 0; i < _countof(clrPresets); i++)
			InsertMenu(hBg, i, MF_BYPOSITION | MF_OWNERDRAW, IDM_COLORPRESET_BG + i, TranslateW(clrPresets[i].szName));

		for (int i = 0; i < _countof(clrPresets); i++)
			InsertMenu(hFg, i, MF_BYPOSITION | MF_OWNERDRAW, IDM_COLORPRESET_FG + i, TranslateW(clrPresets[i].szName));
	}

	TranslateMenu(FhMenu);
	TrackPopupMenu(FhMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, LOWORD(lParam), HIWORD(lParam), 0, AhWnd, nullptr);
	DestroyMenu(hMenuLoad);
	return TRUE;
}

static void MeasureColorPresetMenuItem(HWND hdlg, LPMEASUREITEMSTRUCT lpMeasureItem, struct ColorPreset *clrPresets2)
{
	HDC hdc = GetDC(hdlg);
	wchar_t *lpsz = TranslateW(clrPresets2->szName);
	SIZE sz;
	GetTextExtentPoint32(hdc, lpsz, (int)mir_wstrlen(lpsz), &sz);
	ReleaseDC(hdlg, hdc);

	lpMeasureItem->itemWidth = 50 + sz.cx;
	lpMeasureItem->itemHeight = (sz.cy + 2) > 18 ? sz.cy + 2 : 18;
}

static void PaintColorPresetMenuItem(LPDRAWITEMSTRUCT lpDrawItem, struct ColorPreset *clrPresets2)
{
	//	UINT n = lpDrawItem->itemID - IDM_COLORPRESET_BG;
	RECT rect;
	rect.left = lpDrawItem->rcItem.left + 50;
	rect.top = lpDrawItem->rcItem.top;
	rect.right = lpDrawItem->rcItem.right;
	rect.bottom = lpDrawItem->rcItem.bottom;

	if (lpDrawItem->itemState & ODS_SELECTED) {
		SetDCBrushColor(lpDrawItem->hDC, GetSysColor(COLOR_MENUHILIGHT));
		FillRect(lpDrawItem->hDC, &lpDrawItem->rcItem, (HBRUSH)GetStockObject(DC_BRUSH));

		SetTextColor(lpDrawItem->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
	}
	else {
		SetDCBrushColor(lpDrawItem->hDC, GetSysColor(COLOR_MENU));
		FillRect(lpDrawItem->hDC, &lpDrawItem->rcItem, (HBRUSH)GetStockObject(DC_BRUSH));

		SetTextColor(lpDrawItem->hDC, GetSysColor(COLOR_MENUTEXT));
	}

	SetBkMode(lpDrawItem->hDC, TRANSPARENT);
	DrawText(lpDrawItem->hDC, clrPresets2->szName, -1, &rect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);

	int h = lpDrawItem->rcItem.bottom - lpDrawItem->rcItem.top;
	rect.left = lpDrawItem->rcItem.left + 5;
	rect.top = lpDrawItem->rcItem.top + ((h - 14) >> 1);
	rect.right = rect.left + 40;
	rect.bottom = rect.top + 14;

	FrameRect(lpDrawItem->hDC, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
	rect.left++; rect.top++;
	rect.right--; rect.bottom--;
	SetDCBrushColor(lpDrawItem->hDC, clrPresets2->color);
	FillRect(lpDrawItem->hDC, &rect, (HBRUSH)GetStockObject(DC_BRUSH));
}

static BOOL GetClipboardText_Title(wchar_t *pOut, int size)
{
	BOOL bResult = FALSE;

	if (OpenClipboard(nullptr)) {
		HANDLE hData = GetClipboardData(CF_UNICODETEXT);
		wchar_t *buffer;

		if (hData && (buffer = (wchar_t*)GlobalLock(hData))) {
			// trim initial white spaces
			while (*buffer && iswspace(*buffer))
				buffer++;

			size_t n = mir_wstrlen(buffer);
			if (n >= size)
				n = size - 1;
			wcsncpy_s(pOut, size, buffer, _TRUNCATE);

			// end string on line break and convert tabs to spaces
			wchar_t *p = pOut;
			while (*p) {
				if (*p == '\r' || *p == '\n') {
					*p = 0;
					n = mir_wstrlen(pOut);
					break;
				}
				else if (*p == '\t') {
					*p = ' ';
				}
				p++;
			}

			// trim trailing white spaces
			rtrimw(pOut);
			if (pOut[0])
				bResult = TRUE;

			GlobalUnlock(hData);
		}

		CloseClipboard();
	}

	return bResult;
}

static void SetNoteTextControl(STICKYNOTE *SN)
{
	CHARFORMAT CF = {};
	CF.cbSize = sizeof(CHARFORMAT);
	CF.dwMask = CFM_COLOR;
	CF.crTextColor = SN->FgColor ? (SN->FgColor & 0xffffff) : BodyFontColor;
	SendMessage(SN->REHwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&CF);

	SetWindowTextA(SN->REHwnd, SN->szText);
}


static UINT_PTR CALLBACK CFHookProc(HWND hdlg, UINT msg, WPARAM, LPARAM)
{
	if (msg == WM_INITDIALOG) {
		// hide color selector
		ShowWindow(GetDlgItem(hdlg, 0x443), SW_HIDE);
		ShowWindow(GetDlgItem(hdlg, 0x473), SW_HIDE);
		TranslateDialogDefault(hdlg);
	}

	return 0;
}

LRESULT CALLBACK StickyNoteWndProc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	STICKYNOTE *SN = (STICKYNOTE*)GetPropA(hdlg, "ctrldata");

	switch (message) {
	case WM_CLOSE:
		return TRUE;

	case WM_SIZE:
		RECT SZ;
		GetClientRect(hdlg, &SZ);

		MoveWindow(GetDlgItem(hdlg, 1), 0, 0, SZ.right, SZ.bottom, TRUE);

		KillTimer(hdlg, 1025);
		SetTimer(hdlg, 1025, NOTE_CHANGE_COMMIT_DELAY, nullptr);
		return TRUE;

	case WM_TIMER:
		if (wParam == 1025) {
			KillTimer(hdlg, 1025);
			JustSaveNotes(SN);
		}
		break;

	case WM_MOVE:
		KillTimer(hdlg, 1025);
		SetTimer(hdlg, 1025, NOTE_CHANGE_COMMIT_DELAY, nullptr);
		return TRUE;

	case WM_CREATE:
		{
			CREATESTRUCT *CS = (CREATESTRUCT *)lParam;
			DWORD mystyle;

			SN = (STICKYNOTE*)CS->lpCreateParams;
			SetPropA(hdlg, "ctrldata", (HANDLE)SN);
			BringWindowToTop(hdlg);
			mystyle = WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
			if (g_plugin.bShowScrollbar)
				mystyle |= WS_VSCROLL;
			HWND H = CreateWindowW(MSFTEDIT_CLASS, nullptr, mystyle, 0, 0, CS->cx - 3 - 3, CS->cy - 3 - (3 + 14), hdlg, (HMENU)1, hmiranda, nullptr);
			SN->REHwnd = H;
			SendMessage(H, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
			SendMessage(H, EM_LIMITTEXT, MAX_NOTE_LEN, 0);
			SendMessage(H, WM_SETFONT, (WPARAM)(SN->pCustomFont ? SN->pCustomFont->hFont : hBodyFont), 1);
			SendMessage(H, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_LINK);
			SendMessage(H, EM_SETBKGNDCOLOR, 0, SN->BgColor ? (SN->BgColor & 0xffffff) : BodyColor);
			SendMessage(H, EM_AUTOURLDETECT, 1, 0);
			SetNoteTextControl(SN);
		}
		return TRUE;

	case WM_GETMINMAXINFO:
		{
			MINMAXINFO *mm = (MINMAXINFO*)lParam;
			// min width accomodates frame, buttons and some extra space for sanity
			mm->ptMinTrackSize.x = 48 + 3 + 3 + 8 + 40;
			// min height allows collapsing entire client area, only leaving frame and caption
			mm->ptMinTrackSize.y = 3 + 3 + 14;
		}
		return 0;

	case WM_ERASEBKGND:
		// no BG needed as edit control takes up entire client area
		return TRUE;

	case WM_NCPAINT:
		// make window borders have the same color as caption
		{
			RECT rect, wr, r;
			HDC hdc = GetWindowDC(hdlg);

			GetWindowRect(hdlg, &wr);
			if (wParam && wParam != 1) {
				SelectClipRgn(hdc, (HRGN)wParam);
				OffsetClipRgn(hdc, -wr.left, -wr.top);
			}

			rect = wr;
			OffsetRect(&rect, -wr.left, -wr.top);

			HBRUSH hBkBrush = (HBRUSH)GetStockObject(DC_BRUSH);
			SetDCBrushColor(hdc, GetCaptionColor((SN && SN->BgColor) ? SN->BgColor : BodyColor));

			// draw all frame sides separately to avoid filling client area (which flickers)
			{
				// top
				r.left = rect.left; r.right = rect.right;
				r.top = rect.top; r.bottom = r.top + 3 + 14;
				FillRect(hdc, &r, hBkBrush);
				// bottom
				r.top = rect.bottom - 3; r.bottom = rect.bottom;
				FillRect(hdc, &r, hBkBrush);
				// left
				r.left = rect.left; r.right = r.left + 3;
				r.top = rect.top + 3 + 14; r.bottom = rect.bottom - 3;
				FillRect(hdc, &r, hBkBrush);
				// right
				r.left = rect.right - 3; r.right = rect.right;
				FillRect(hdc, &r, hBkBrush);
			}

			// paint title bar contents (time stamp and buttons)

			if (SN && SN->title) {
				RECT R;
				SelectObject(hdc, hCaptionFont);
				R.top = 3 + 1; R.bottom = 3 + 11; R.left = 3 + 2; R.right = rect.right - 3 - 1;
				if (g_plugin.bShowNoteButtons)
					R.right -= 48;

				SetTextColor(hdc, SN->FgColor ? (SN->FgColor & 0xffffff) : CaptionFontColor);
				SetBkMode(hdc, TRANSPARENT);
				DrawTextW(hdc, SN->title, -1, &R, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
			}

			if (g_plugin.bShowNoteButtons) {
				HICON hcIcon;
				if (SN->bOnTop)
					hcIcon = IcoLib_GetIconByHandle(iconList[4].hIcolib);
				else
					hcIcon = IcoLib_GetIconByHandle(iconList[7].hIcolib);
				DrawIcon(hdc, wr.right - wr.left - 16, 0 + 3, hcIcon);
				IcoLib_ReleaseIcon(hcIcon);

				hcIcon = IcoLib_GetIconByHandle(iconList[9].hIcolib);
				DrawIcon(hdc, wr.right - wr.left - 32, 1 + 3, hcIcon);
				IcoLib_ReleaseIcon(hcIcon);

				hcIcon = IcoLib_GetIconByHandle(iconList[8].hIcolib);
				DrawIcon(hdc, wr.right - wr.left - 48, 1 + 3, hcIcon);
				IcoLib_ReleaseIcon(hcIcon);
			}

			if (wParam && wParam != 1)
				SelectClipRgn(hdc, nullptr);

			ReleaseDC(hdlg, hdc);
		}
		return TRUE;

	case WM_NCCALCSIZE:
		{
			RECT *pRect = wParam ? &((NCCALCSIZE_PARAMS*)lParam)->rgrc[0] : (RECT*)lParam;
			pRect->bottom -= 3;
			pRect->right -= 3;
			pRect->left += 3;
			pRect->top += 3 + 14;
		}
		return WVR_REDRAW;

	case WM_NCACTIVATE:
		// update window (so that parts that potentially became visible through activation get redrawn immediately)
		RedrawWindow(hdlg, nullptr, nullptr, RDW_UPDATENOW);
		return TRUE;

	case WM_NOTIFY:
		if (LOWORD(wParam) == 1) {
			char *Buff;
			PENLINK PEnLnk = (PENLINK)lParam;

			if (PEnLnk->msg == WM_LBUTTONDOWN) {
				SendDlgItemMessage(hdlg, 1, EM_EXSETSEL, 0, (LPARAM)&(PEnLnk->chrg));
				Buff = (char*)malloc(PEnLnk->chrg.cpMax - PEnLnk->chrg.cpMin + 1);
				SendDlgItemMessage(hdlg, 1, EM_GETSELTEXT, 0, (LPARAM)Buff);
				if ((GetAsyncKeyState(VK_CONTROL) >> 15) != 0)
					ShellExecuteA(hdlg, "open", "iexplore", Buff, "", SW_SHOWNORMAL);
				else if (g_lpszAltBrowser && *g_lpszAltBrowser)
					ShellExecuteA(hdlg, "open", g_lpszAltBrowser, Buff, "", SW_SHOWNORMAL);
				else
					ShellExecuteA(hdlg, "open", Buff, "", "", SW_SHOWNORMAL);
				SAFE_FREE((void**)&Buff);
				return TRUE;
			}
			return FALSE;
		}
		break;

	case WM_NCHITTEST:
		{
			int r = DefWindowProc(hdlg, message, wParam, lParam);
			// filter out potential hits on windows default title bar buttons
			switch (r) {
			case HTSYSMENU:
			case HTCLOSE:
			case HTMINBUTTON:
			case HTMAXBUTTON:
				return HTCAPTION;
			}
			return r;
		}

	case WM_NCLBUTTONDOWN:
		if (wParam == HTCAPTION && g_plugin.bShowNoteButtons) {
			long X, Y;
			RECT rect;
			int Tw;

			GetWindowRect(hdlg, &rect);
			Tw = rect.right - rect.left;

			X = LOWORD(lParam) - rect.left;
			Y = HIWORD(lParam) - rect.top;

			if (X > Tw - 16) {
				SendMessage(hdlg, WM_COMMAND, ID_CONTEXTMENUNOTE_TOGGLEONTOP, 0);
				return TRUE;
			}
			else if (X > Tw - 31 && X < Tw - 16) {
				SendMessage(hdlg, WM_COMMAND, ID_CONTEXTMENUNOTE_REMOVENOTE, 0);
				return TRUE;
			}
			else if (X > Tw - 48 && X < Tw - 32) {
				SendMessage(hdlg, WM_COMMAND, ID_CONTEXTMENUNOTE_HIDENOTE, 0);
				return TRUE;
			}
		}
		return DefWindowProc(hdlg, message, wParam, lParam);

	case WM_MEASUREITEM:
		{
			LPMEASUREITEMSTRUCT lpMeasureItem = (LPMEASUREITEMSTRUCT)lParam;

			if (lpMeasureItem->CtlType != ODT_MENU)
				break;

			if (lpMeasureItem->itemID >= IDM_COLORPRESET_BG && lpMeasureItem->itemID <= IDM_COLORPRESET_BG + _countof(clrPresets)) {
				MeasureColorPresetMenuItem(hdlg, lpMeasureItem, clrPresets + (lpMeasureItem->itemID - IDM_COLORPRESET_BG));
				return TRUE;
			}
			else if (lpMeasureItem->itemID >= IDM_COLORPRESET_FG && lpMeasureItem->itemID <= IDM_COLORPRESET_FG + _countof(clrPresets)) {
				MeasureColorPresetMenuItem(hdlg, lpMeasureItem, clrPresets + (lpMeasureItem->itemID - IDM_COLORPRESET_FG));
				return TRUE;
			}
		}
		break;

	case WM_DRAWITEM:
		if (!wParam) {
			LPDRAWITEMSTRUCT lpDrawItem = (LPDRAWITEMSTRUCT)lParam;

			if (lpDrawItem->CtlType != ODT_MENU)
				break;

			if (lpDrawItem->itemID >= IDM_COLORPRESET_BG && lpDrawItem->itemID <= IDM_COLORPRESET_BG + _countof(clrPresets)) {
				PaintColorPresetMenuItem(lpDrawItem, clrPresets + (lpDrawItem->itemID - IDM_COLORPRESET_BG));
				return TRUE;
			}
			else if (lpDrawItem->itemID >= IDM_COLORPRESET_FG && lpDrawItem->itemID <= IDM_COLORPRESET_FG + _countof(clrPresets)) {
				PaintColorPresetMenuItem(lpDrawItem, clrPresets + (lpDrawItem->itemID - IDM_COLORPRESET_FG));
				return TRUE;
			}
		}
		break;

	case WM_COMMAND:
		UINT id;
		switch (HIWORD(wParam)) {
		case EN_CHANGE:
		case EN_VSCROLL:
		case EN_HSCROLL:
			KillTimer(hdlg, 1025);
			SetTimer(hdlg, 1025, NOTE_CHANGE_COMMIT_DELAY, nullptr);
			break;
		}

		id = (UINT)LOWORD(wParam);
		if (id >= IDM_COLORPRESET_BG && id <= IDM_COLORPRESET_BG + _countof(clrPresets)) {
			SN->BgColor = clrPresets[id - IDM_COLORPRESET_BG].color | 0xff000000;
			SendMessage(SN->REHwnd, EM_SETBKGNDCOLOR, 0, SN->BgColor & 0xffffff);
			RedrawWindow(SN->SNHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
			JustSaveNotes();
			return FALSE;
		}
		
		if (id >= IDM_COLORPRESET_FG && id <= IDM_COLORPRESET_FG + _countof(clrPresets)) {
			SN->FgColor = clrPresets[id - IDM_COLORPRESET_FG].color | 0xff000000;

			CHARFORMAT CF = {};
			CF.cbSize = sizeof(CHARFORMAT);
			CF.dwMask = CFM_COLOR;
			CF.crTextColor = SN->FgColor & 0xffffff;
			SendMessage(SN->REHwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&CF);

			RedrawWindow(SN->SNHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
			JustSaveNotes();
			return FALSE;
		}

		switch (id) {
		case ID_CONTEXTMENUNOTE_NEWNOTE:
			PluginMenuCommandAddNew(0, 0);
			break;

		case ID_BACKGROUNDCOLOR_CUSTOM:
			{
				COLORREF custclr[16] = {0};
				CHOOSECOLOR cc = {0};
				COLORREF orgclr = SN->BgColor ? (COLORREF)(SN->BgColor & 0xffffff) : (COLORREF)(BodyColor & 0xffffff);
				cc.lStructSize = sizeof(cc);
				cc.hwndOwner = SN->SNHwnd;
				cc.rgbResult = orgclr;
				cc.Flags = CC_ANYCOLOR | CC_FULLOPEN | CC_RGBINIT | CC_SOLIDCOLOR;
				cc.lpCustColors = custclr;

				if (ChooseColor(&cc) && cc.rgbResult != orgclr) {
					SN->BgColor = cc.rgbResult | 0xff000000;
					SendMessage(SN->REHwnd, EM_SETBKGNDCOLOR, 0, SN->BgColor & 0xffffff);
					RedrawWindow(SN->SNHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
					JustSaveNotes();
				}
			}
			break;
		case ID_TEXTCOLOR_CUSTOM:
			{
				COLORREF custclr[16] = {0};
				COLORREF orgclr = SN->FgColor ? (COLORREF)(SN->FgColor & 0xffffff) : (COLORREF)(BodyFontColor & 0xffffff);

				CHOOSECOLOR cc = {0};
				cc.lStructSize = sizeof(cc);
				cc.hwndOwner = SN->SNHwnd;
				cc.rgbResult = orgclr;
				cc.Flags = CC_ANYCOLOR | CC_FULLOPEN | CC_RGBINIT | CC_SOLIDCOLOR;
				cc.lpCustColors = custclr;

				if (ChooseColor(&cc) && cc.rgbResult != orgclr) {
					SN->FgColor = cc.rgbResult | 0xff000000;

					CHARFORMAT CF = {0};
					CF.cbSize = sizeof(CHARFORMAT);
					CF.dwMask = CFM_COLOR;
					CF.crTextColor = SN->FgColor & 0xffffff;
					SendMessage(SN->REHwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&CF);

					RedrawWindow(SN->SNHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
					JustSaveNotes();
				}
			}
			break;

		case ID_FONT_CUSTOM:
			{
				LOGFONT lf = {};
				if (SN->pCustomFont)
					InitStickyNoteLogFont(SN->pCustomFont, &lf);
				else
					LoadNRFont(NR_FONTID_BODY, &lf, nullptr);

				CHOOSEFONT cf = {};
				cf.lStructSize = sizeof(cf);
				cf.hwndOwner = SN->SNHwnd;
				cf.lpLogFont = &lf;
				cf.Flags = CF_EFFECTS | CF_FORCEFONTEXIST | CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_ENABLEHOOK;
				cf.lpfnHook = CFHookProc;
				if (!ChooseFontW(&cf))
					break;

				if (!SN->pCustomFont) {
					SN->pCustomFont = (STICKYNOTEFONT*)malloc(sizeof(STICKYNOTEFONT));
					SN->pCustomFont->hFont = nullptr;
				}

				SN->pCustomFont->size = (char)lf.lfHeight;
				SN->pCustomFont->style = (lf.lfWeight >= FW_BOLD ? DBFONTF_BOLD : 0) | (lf.lfItalic ? DBFONTF_ITALIC : 0) | (lf.lfUnderline ? DBFONTF_UNDERLINE : 0) | (lf.lfStrikeOut ? DBFONTF_STRIKEOUT : 0);
				SN->pCustomFont->charset = lf.lfCharSet;
				wcsncpy_s(SN->pCustomFont->szFace, lf.lfFaceName, _TRUNCATE);

				if (!CreateStickyNoteFont(SN->pCustomFont, &lf)) {
					// failed
					free(SN->pCustomFont);
					SN->pCustomFont = nullptr;
				}

				// clear text first to force a reformatting w.r.w scrollbar
				SetWindowTextA(SN->REHwnd, "");
				SendMessage(SN->REHwnd, WM_SETFONT, (WPARAM)(SN->pCustomFont ? SN->pCustomFont->hFont : hBodyFont), FALSE);
				SetNoteTextControl(SN);
				RedrawWindow(SN->SNHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
				JustSaveNotes();
			}
			break;

		case ID_BACKGROUNDCOLOR_RESET:
			SN->BgColor = 0;
			SendMessage(SN->REHwnd, EM_SETBKGNDCOLOR, 0, (LPARAM)BodyColor);
			RedrawWindow(SN->SNHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
			JustSaveNotes();
			break;

		case ID_TEXTCOLOR_RESET:
			SN->FgColor = 0;
			{
				CHARFORMAT CF = {};
				CF.cbSize = sizeof(CHARFORMAT);
				CF.dwMask = CFM_COLOR;
				CF.crTextColor = BodyFontColor;
				SendMessage(SN->REHwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&CF);
			}
			RedrawWindow(SN->SNHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
			JustSaveNotes();
			break;

		case ID_FONT_RESET:
			if (SN->pCustomFont) {
				DeleteObject(SN->pCustomFont->hFont);
				free(SN->pCustomFont);
				SN->pCustomFont = nullptr;

				// clear text first to force a reformatting w.r.w scrollbar
				SetWindowTextA(SN->REHwnd, "");
				SendMessage(SN->REHwnd, WM_SETFONT, (WPARAM)hBodyFont, FALSE);
				SetNoteTextControl(SN);
				RedrawWindow(SN->SNHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
				JustSaveNotes();
			}
			break;

		case ID_CONTEXTMENUNOTE_PASTETITLE:
			{
				wchar_t s[MAX_TITLE_LEN + 1];
				if (GetClipboardText_Title(s, _countof(s))) {
					if (SN->title)
						free(SN->title);
					SN->title = _wcsdup(s);
					SN->CustomTitle = TRUE;
					RedrawWindow(SN->SNHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
					JustSaveNotes();
				}
			}
			break;

		case ID_CONTEXTMENUNOTE_RESETTITLE:
			if (SN->CustomTitle) {
				if (SN->title) {
					free(SN->title);
					SN->title = nullptr;
				}
				InitNoteTitle(SN);
				RedrawWindow(SN->SNHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
				JustSaveNotes();
			}
			break;

		case ID_CONTEXTMENUNOTE_REMOVENOTE:
			OnDeleteNote(hdlg, SN);
			break;

		case ID_CONTEXTMENUNOTE_HIDENOTE:
			SN->bVisible = false;
			ShowWindow(hdlg, SW_HIDE);
			JustSaveNotes();
			break;

		case ID_CONTEXTMENUNOTE_COPY:
			SendMessage(SN->REHwnd, WM_COPY, 0, 0);
			break;

		case ID_CONTEXTMENUNOTE_PASTE:
			SendMessage(SN->REHwnd, WM_PASTE, 0, 0);
			break;

		case ID_CONTEXTMENUNOTE_CUT:
			SendMessage(SN->REHwnd, WM_CUT, 0, 0);
			break;

		case ID_CONTEXTMENUNOTE_CLEAR:
			SendMessage(SN->REHwnd, WM_CLEAR, 0, 0);
			break;

		case ID_CONTEXTMENUNOTE_UNDO:
			SendMessage(SN->REHwnd, WM_UNDO, 0, 0);
			break;

		case ID_CONTEXTMENUNOTE_TOGGLEONTOP:
			SN->bOnTop = !SN->bOnTop;
			SetWindowPos(hdlg, SN->bOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE);
			RedrawWindow(hdlg, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
			JustSaveNotes();
			break;

		case ID_CONTEXTMENUNOTE_VIEWNOTES:
			PluginMenuCommandViewNotes(0, 0);
			break;

		case ID_CONTEXTMENUNOTE_BRINGALLTOTOP:
			BringAllNotesToFront(SN);
			break;
		}
		return TRUE;

	case WM_NCDESTROY:
		RemovePropA(hdlg, "ctrldata");
		break;

	case WM_CONTEXTMENU:
		if (DoContextMenu(hdlg, wParam, lParam))
			return FALSE;

	default:
		return DefWindowProc(hdlg, message, wParam, lParam);
	}
	return FALSE;
}

static STICKYNOTE* NewNoteEx(int Ax, int Ay, int Aw, int Ah, const char *pszText, ULARGE_INTEGER *ID, BOOL bVisible, BOOL bOnTop, int scrollV, COLORREF bgClr, COLORREF fgClr, wchar_t *Title, STICKYNOTEFONT *pCustomFont, BOOL bLoading)
{
	WNDCLASSEX TWC = {0};
	WINDOWPLACEMENT TWP;
	DWORD L1, L2;
	SYSTEMTIME tm;

	const BOOL bIsStartup = bVisible & 0x10000;
	bVisible &= ~0x10000;

	if (!GetClassInfoEx(hmiranda, NOTE_WND_CLASS, &TWC)) {
		TWC.style = CS_HREDRAW | CS_VREDRAW | CS_NOCLOSE;
		TWC.cbClsExtra = 0;
		TWC.cbWndExtra = 0;
		TWC.hInstance = hmiranda;
		TWC.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
		TWC.hCursor = LoadCursor(nullptr, IDC_ARROW);
		TWC.hbrBackground = nullptr;
		TWC.lpszMenuName = nullptr;
		TWC.lpszClassName = NOTE_WND_CLASS;
		TWC.cbSize = sizeof(WNDCLASSEX);
		TWC.lpfnWndProc = StickyNoteWndProc;
		if (!RegisterClassEx(&TWC)) return nullptr;
	}

	if (!pszText || Aw < 0 || Ah < 0) {
		TWP.length = sizeof(WINDOWPLACEMENT);
		GetWindowPlacement(GetDesktopWindow(), &TWP);
		Aw = g_NoteWidth; Ah = g_NoteHeight;
		Ax = ((TWP.rcNormalPosition.right - TWP.rcNormalPosition.left) / 2) - (Aw / 2);
		Ay = ((TWP.rcNormalPosition.bottom - TWP.rcNormalPosition.top) / 2) - (Ah / 2);
	}

	STICKYNOTE *TSN = new STICKYNOTE();

	if (ID)
		TSN->ID = *ID;
	else {
		GetSystemTime(&tm);
		SystemTimeToFileTime(&tm, (FILETIME*)&TSN->ID);
	}

	EnsureUniqueID(TSN);

	g_arStickies.insert(TSN);

	if (pszText)
		TSN->szText = pszText;

	// init note title (time-stamp)
	if (Title) {
		TSN->title = Title;
		TSN->CustomTitle = TRUE;
	}
	else {
		TSN->title = nullptr;
		InitNoteTitle(TSN);
	}

	TSN->bVisible = bVisible;
	TSN->bOnTop = bOnTop;
	TSN->BgColor = bgClr;
	TSN->FgColor = fgClr;
	TSN->pCustomFont = pCustomFont;

	L1 = WS_EX_TOOLWINDOW;
	if (g_Transparency < 255) L1 |= WS_EX_LAYERED;
	if (bOnTop) L1 |= WS_EX_TOPMOST;

	L2 = WS_POPUP | WS_THICKFRAME | WS_CAPTION;

	// NOTE: loaded note positions stem from GetWindowPlacement, which normally have a different coord space than
	//       CreateWindow/SetWindowPos, BUT since we now use WS_EX_TOOLWINDOW they use the same coord space so
	//       we don't have to worry about notes "drifting" between sessions
	TSN->SNHwnd = CreateWindowEx(L1, NOTE_WND_CLASS, L"StickyNote", L2, Ax, Ay, Aw, Ah, nullptr, nullptr, hmiranda, TSN);

	if (g_Transparency < 255)
		SetLayeredWindowAttributes(TSN->SNHwnd, 0, (BYTE)g_Transparency, LWA_ALPHA);

	// ensure that window is not placed off-screen (if previous session had different monitor count or resolution)
	// NOTE: SetWindowPlacement should do this, but it's extremly flakey
	if (pszText) {
		if (!MonitorFromWindow(TSN->SNHwnd, MONITOR_DEFAULTTONULL)) {
			TWP.length = sizeof(WINDOWPLACEMENT);
			GetWindowPlacement(GetDesktopWindow(), &TWP);

			if (Aw > 500) Aw = 500;
			if (Ay < TWP.rcNormalPosition.left + 10 || Ax > TWP.rcNormalPosition.right - 120)
				Ax = ((TWP.rcNormalPosition.right - TWP.rcNormalPosition.left) / 2) - (Aw / 2) + (rand() & 0x3f);
			if (Ay < TWP.rcNormalPosition.top + 50 || Ay > TWP.rcNormalPosition.bottom - 50)
				Ay = ((TWP.rcNormalPosition.bottom - TWP.rcNormalPosition.top) / 4) + (rand() & 0x1f);

			SetWindowPos(TSN->SNHwnd, nullptr, Ax, Ay, Aw, Ah, SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}

	if (bVisible) {
		ShowWindow(TSN->SNHwnd, SW_SHOWNA);

		// when loading notes (only at startup), place all non-top notes at the bottom so they don't cover other windows
		if (pszText && !bOnTop && bIsStartup)
			SetWindowPos(TSN->SNHwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
	}

	if (scrollV)
		SendMessage(TSN->REHwnd, EM_LINESCROLL, 0, scrollV);

	// make sure that any event triggered by init doesn't cause a meaningless save
	KillTimer(TSN->SNHwnd, 1025);

	if (!bLoading) {
		NOTIFY_LIST();
	}

	return TSN;
}

void NewNote(int Ax, int Ay, int Aw, int Ah, const char *pszText, ULARGE_INTEGER *ID, BOOL bVisible, BOOL bOnTop, int scrollV)
{
	auto *PSN = NewNoteEx(Ax, Ay, Aw, Ah, pszText, ID, bVisible, bOnTop, scrollV, 0, 0, nullptr, nullptr, FALSE);
	if (PSN)
		SetFocus(PSN->REHwnd);
}

void LoadNotes(BOOL bIsStartup)
{
	WORD Size = 0;
	char *Value = nullptr, *TVal = nullptr;
	char ValueName[32];

	g_arStickies.destroy();

	int NotesCount = g_plugin.getDword("NotesData", 0);

	for (int i = 0; i < NotesCount; i++) {
		mir_snprintf(ValueName, "NotesData%d", i);

		if (Value) {
			FreeSettingBlob(Size, Value);
			Value = nullptr;
		}

		Size = 65535; // does not get used

		ReadSettingBlob(0, MODULENAME, ValueName, &Size, (void**)&Value);

		if (!Size || !Value)
			continue; // the setting could not be read from DB -> skip

		if (Value[0] == 'X') {
			// new eXtended/fleXible data format

			int scrollV = 0;
			STICKYNOTEFONT *pCustomFont = nullptr;

			char *DelPos = strchr(Value + 1, 0x1B);
			if (DelPos)
				*DelPos = 0;

			// id:x:y:w:h:flags

			TVal = strchr(Value + 1, ':');
			if (!TVal || (DelPos && TVal > DelPos))
				continue;

			*TVal++ = 0;
			ULARGE_INTEGER id;
			id.QuadPart = _strtoui64(Value + 1, nullptr, 16);

			int rect[4];
			for (auto &it : rect) {
				char *sep = strchr(TVal, ':');
				if (!sep || (DelPos && sep > DelPos))
					goto skip;
				*sep++ = 0;

				it = strtol(TVal, nullptr, 10);
				TVal = sep;
			}

			BOOL bVisible = 0, bOnTop = 0;
			DWORD flags = strtoul(TVal, nullptr, 16);
			if (flags & 1)
				bVisible = TRUE;
			if (flags & 2)
				bOnTop = TRUE;

			// optional \033 separated params
			char *data = 0;
			wchar_t *title = 0;
			COLORREF BgColor = 0, FgColor = 0;

			while (DelPos) {
				TVal = DelPos + 1;
				// find param end and make sure it's null-terminated (if end of data then it's already null-terminated)
				DelPos = strchr(TVal, 0x1B);
				if (DelPos)
					*DelPos = 0;

				// tag:<data>
				char *sep = strchr(TVal, ':');
				if (!sep || (DelPos && sep > DelPos))
					goto skip;

				UINT tag = strtoul(TVal, nullptr, 10);
				TVal = sep + 1;

				switch (tag) {
				case DATATAG_TEXT:
					data = _strdup(TVal);
					break;

				case DATATAG_SCROLLPOS:
					scrollV = (int)strtoul(TVal, nullptr, 10);
					break;

				case DATATAG_BGCOL:
					BgColor = strtoul(TVal, nullptr, 16) | 0xff000000;
					break;

				case DATATAG_FGCOL:
					FgColor = strtoul(TVal, nullptr, 16) | 0xff000000;
					break;

				case DATATAG_TITLE:
					if (mir_strlen(TVal) > MAX_TITLE_LEN)
						TVal[MAX_TITLE_LEN] = 0;
					title = _wcsdup(_A2T(TVal));
					break;

				case DATATAG_FONT:
					int fsize;
					UINT fstyle, fcharset;

					char *TVal2 = TVal;
					sep = strchr(TVal2, ':');
					if (!sep || (DelPos && sep > DelPos))
						goto skip;
					*sep++ = 0;
					fsize = strtol(TVal2, nullptr, 10);
					TVal2 = sep;

					sep = strchr(TVal2, ':');
					if (!sep || (DelPos && sep > DelPos))
						goto skip;
					*sep++ = 0;
					fstyle = strtoul(TVal2, nullptr, 10);
					TVal2 = sep;

					sep = strchr(TVal2, ':');
					if (!sep || (DelPos && sep > DelPos))
						goto skip;
					*sep++ = 0;
					fcharset = strtoul(TVal2, nullptr, 10);
					TVal2 = sep;

					if (TVal2 >= DelPos)
						goto skip;

					pCustomFont = (STICKYNOTEFONT*)malloc(sizeof(STICKYNOTEFONT));
					pCustomFont->size = (char)fsize;
					pCustomFont->style = (BYTE)fstyle;
					pCustomFont->charset = (BYTE)fcharset;
					wcsncpy_s(pCustomFont->szFace, _A2T(TVal2), _TRUNCATE);
					pCustomFont->hFont = nullptr;

					if (!CreateStickyNoteFont(pCustomFont, nullptr)) {
						free(pCustomFont);
						pCustomFont = nullptr;
					}
					break;
				}
			}

			if (!data)
				data = _strdup("");

			bVisible = bVisible && (!bIsStartup || g_plugin.bShowNotesAtStart);
			if (bIsStartup)
				bVisible |= 0x10000;

			NewNoteEx(rect[0], rect[1], rect[2], rect[3], data, &id, bVisible, bOnTop, scrollV, BgColor, FgColor, title, pCustomFont, TRUE);
		}
		else {
			// old format (for DB backward compatibility)

			int Tx, Ty, Tw, Th, TV, OT;
			BOOL V;
			char *Data, *ID;
			ULARGE_INTEGER newid;

			OT = 1; TV = 1;
			Tx = 100; Ty = 100;
			Tw = 179; Th = 35;
			Data = nullptr; ID = nullptr;

			if (char *DelPos = strchr(Value, 0x1B)) {	// get first delimiter
				Data = nullptr;
				ID = nullptr;
				TVal = Value;
				DelPos[0] = 0x0;
				Tx = strtol(TVal, nullptr, 10);

				TVal = DelPos + 1;
				DelPos = strchr(TVal, 0x1B);
				if (!DelPos) continue; // setting is broken, do not crash
				DelPos[0] = 0x0;
				Ty = strtol(TVal, nullptr, 10);

				TVal = DelPos + 1;
				DelPos = strchr(TVal, 0x1B);
				if (!DelPos) continue; // setting is broken, do not crash
				DelPos[0] = 0x0;
				Tw = strtol(TVal, nullptr, 10);

				TVal = DelPos + 1;
				DelPos = strchr(TVal, 0x1B);
				if (!DelPos) continue; // setting is broken, do not crash
				DelPos[0] = 0x0;
				Th = strtol(TVal, nullptr, 10);

				TVal = DelPos + 1;
				DelPos = strchr(TVal, 0x1B);
				if (!DelPos) continue; // setting is broken, do not crash
				DelPos[0] = 0x0;
				TV = strtol(TVal, nullptr, 10);

				TVal = DelPos + 1;
				DelPos = strchr(TVal, 0x1B);
				if (!DelPos) continue; // setting is broken, do not crash
				DelPos[0] = 0x0;
				OT = strtol(TVal, nullptr, 10);

				TVal = DelPos + 1;
				DelPos = strchr(TVal, 0x1B);
				if (!DelPos) continue; // setting is broken, do not crash
				DelPos[0] = 0x0;
				Data = _strdup(TVal);

				TVal = DelPos + 1;
				ID = TVal;

				V = (BOOL)TV && (!bIsStartup || g_plugin.bShowNotesAtStart);

				if (bIsStartup)
					V |= 0x10000;

				// convert old ID format to new
				if (strchr(ID, '-')) {
					// validate format (otherwise create new)
					if (mir_strlen(ID) < 19 || ID[2] != '-' || ID[5] != '-' || ID[10] != ' ' || ID[13] != ':' || ID[16] != ':') {
						ID = nullptr;
					}
					else {
						SYSTEMTIME tm;

						ID[2] = ID[5] = ID[10] = ID[13] = ID[16] = 0;

						memset(&tm, 0, sizeof(tm));
						tm.wDay = (WORD)strtoul(ID, nullptr, 10);
						tm.wMonth = (WORD)strtoul(ID + 3, nullptr, 10);
						tm.wYear = (WORD)strtoul(ID + 6, nullptr, 10);
						tm.wHour = (WORD)strtoul(ID + 11, nullptr, 10);
						tm.wMinute = (WORD)strtoul(ID + 14, nullptr, 10);
						tm.wSecond = (WORD)strtoul(ID + 17, nullptr, 10);

						SystemTimeToFileTime(&tm, (FILETIME*)&newid);
					}
				}
				else ID = nullptr;

				NewNoteEx(Tx, Ty, Tw, Th, Data, ID ? &newid : nullptr, V, (BOOL)OT, 0, 0, 0, nullptr, nullptr, TRUE);
			}
		}
skip:;
	}

	if (Value)
		FreeSettingBlob(Size, Value); // we do not leak on bad setting

	NOTIFY_LIST();
}

/////////////////////////////////////////////////////////////////////////////////////////

static void EditNote(STICKYNOTE *SN)
{
	if (!SN)
		return;

	if (!SN->bVisible) {
		SN->bVisible = TRUE;
		JustSaveNotes();
	}

	SetWindowPos(SN->SNHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	if (!SN->bOnTop)
		SetWindowPos(SN->SNHwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

	SetFocus(SN->REHwnd);
}

wchar_t* GetPreviewString(const char *lpsz)
{
	const int MaxLen = 80;
	static wchar_t s[MaxLen + 8];

	if (!lpsz)
		return L"";

	// trim leading spaces
	while (isspace(*lpsz))
		lpsz++;

	size_t l = mir_strlen(lpsz);
	if (!l)
		return L"";

	if (l <= MaxLen) {
		mir_wstrcpy(s, _A2T(lpsz));
	}
	else {
		mir_wstrncpy(s, _A2T(lpsz), MaxLen);
		s[MaxLen] = '.';
		s[MaxLen + 1] = '.';
		s[MaxLen + 2] = '.';
		s[MaxLen + 3] = 0;
	}

	// convert line breaks and tabs to spaces
	wchar_t *p = s;
	while (*p) {
		if (iswspace(*p))
			*p = ' ';
		p++;
	}

	return s;
}

static void InitListView(HWND AHLV)
{
	int i = 0;

	wchar_t S1[128];
	wchar_t *V = TranslateT("Visible");
	wchar_t *T = TranslateT("Top");

	ListView_SetHoverTime(AHLV, 700);
	ListView_SetExtendedListViewStyle(AHLV, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_TRACKSELECT);
	ListView_DeleteAllItems(AHLV);

	for (auto &pNote : g_arStickies) {
		LV_ITEM lvTIt;
		lvTIt.mask = LVIF_TEXT;

		if (!pNote->CustomTitle || !pNote->title)
			GetTriggerTimeString(&pNote->ID, S1, _countof(S1), TRUE);

		lvTIt.iItem = i;
		lvTIt.iSubItem = 0;
		lvTIt.pszText = (pNote->CustomTitle && pNote->title) ? pNote->title : S1;
		ListView_InsertItem(AHLV, &lvTIt);

		if (pNote->bVisible) {
			lvTIt.iItem = i;
			lvTIt.iSubItem = 1;
			lvTIt.pszText = V;
			ListView_SetItem(AHLV, &lvTIt);
		}

		if (pNote->bOnTop) {
			lvTIt.iItem = i;
			lvTIt.iSubItem = 2;
			lvTIt.pszText = T;
			ListView_SetItem(AHLV, &lvTIt);
		}

		lvTIt.iItem = i;
		lvTIt.iSubItem = 3;
		lvTIt.pszText = GetPreviewString(pNote->szText);
		ListView_SetItem(AHLV, &lvTIt);

		i++;
	}

	ListView_SetItemState(AHLV, 0, LVIS_SELECTED, LVIS_SELECTED);
}

static BOOL DoListContextMenu(HWND AhWnd, WPARAM wParam, LPARAM lParam, STICKYNOTE *pNote)
{
	HWND hwndListView = (HWND)wParam;
	if (hwndListView != GetDlgItem(AhWnd, IDC_LISTREMINDERS))
		return FALSE;

	HMENU hMenuLoad = LoadMenuA(g_plugin.getInst(), "MNU_NOTELISTPOPUP");
	HMENU FhMenu = GetSubMenu(hMenuLoad, 0);

	MENUITEMINFO mii = {0};
	mii.cbSize = sizeof(mii);
	mii.fMask = MIIM_STATE;
	mii.fState = MFS_DEFAULT;
	if (!pNote)
		mii.fState |= MFS_GRAYED;
	SetMenuItemInfo(FhMenu, ID_CONTEXTMENUNOTE_EDITNOTE, FALSE, &mii);

	if (!pNote) {
		EnableMenuItem(FhMenu, ID_CONTEXTMENUNOTE_REMOVENOTE, MF_GRAYED | MF_BYCOMMAND);
		EnableMenuItem(FhMenu, ID_CONTEXTMENUNOTE_TOGGLEVISIBILITY, MF_GRAYED | MF_BYCOMMAND);
		EnableMenuItem(FhMenu, ID_CONTEXTMENUNOTE_TOGGLEONTOP, MF_GRAYED | MF_BYCOMMAND);
	}
	else {
		if (pNote->bVisible)
			CheckMenuItem(FhMenu, ID_CONTEXTMENUNOTE_TOGGLEVISIBILITY, MF_CHECKED | MF_BYCOMMAND);
		if (pNote->bOnTop)
			CheckMenuItem(FhMenu, ID_CONTEXTMENUNOTE_TOGGLEONTOP, MF_CHECKED | MF_BYCOMMAND);
	}

	TranslateMenu(FhMenu);
	TrackPopupMenu(FhMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, LOWORD(lParam), HIWORD(lParam), 0, AhWnd, nullptr);
	DestroyMenu(hMenuLoad);
	return TRUE;
}


static INT_PTR CALLBACK DlgProcViewNotes(HWND hwndDlg, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message) {
	case WM_SIZE:
		OnListResize(hwndDlg);
		break;

	case WM_GETMINMAXINFO:
		{
			MINMAXINFO *mm = (MINMAXINFO*)lParam;
			mm->ptMinTrackSize.x = 394;
			mm->ptMinTrackSize.y = 300;
		}
		return 0;

	case WM_RELOAD:
		SetDlgItemTextA(hwndDlg, IDC_REMINDERDATA, "");
		InitListView(GetDlgItem(hwndDlg, IDC_LISTREMINDERS));
		return TRUE;

	case WM_CONTEXTMENU:
		{
			STICKYNOTE *pNote = nullptr;

			HWND H = GetDlgItem(hwndDlg, IDC_LISTREMINDERS);
			if (ListView_GetSelectedCount(H)) {
				int i = ListView_GetSelectionMark(H);
				if (i != -1)
					pNote = &g_arStickies[i];
			}

			if (DoListContextMenu(hwndDlg, wParam, lParam, pNote))
				return TRUE;
		}
		break;

	case WM_INITDIALOG:
		Window_SetIcon_IcoLib(hwndDlg, iconList[13].hIcolib);

		SetWindowText(hwndDlg, LPGENW("Notes"));

		TranslateDialogDefault(hwndDlg);

		SetDlgItemText(hwndDlg, IDC_REMINDERDATA, L"");
		{
			HWND H = GetDlgItem(hwndDlg, IDC_LISTREMINDERS);

			LV_COLUMN lvCol;
			lvCol.mask = LVCF_TEXT | LVCF_WIDTH;

			lvCol.pszText = TranslateT("Note text");
			lvCol.cx = 150;
			ListView_InsertColumn(H, 0, &lvCol);

			lvCol.pszText = TranslateT("Top");
			lvCol.cx = 20;
			ListView_InsertColumn(H, 0, &lvCol);

			lvCol.pszText = TranslateT("Visible");
			lvCol.cx = 20;
			ListView_InsertColumn(H, 0, &lvCol);

			lvCol.pszText = TranslateT("Date/Title");
			lvCol.cx = 165;
			ListView_InsertColumn(H, 0, &lvCol);

			InitListView(H);
			SetWindowLongPtr(GetDlgItem(H, 0), GWL_ID, IDC_LISTREMINDERS_HEADER);
			LV = hwndDlg;

			Utils_RestoreWindowPosition(hwndDlg, 0, MODULENAME, "ListNotes");
		}
		return TRUE;

	case WM_CLOSE:
		DestroyWindow(hwndDlg);
		ListNotesVisible = false;
		return TRUE;

	case WM_DESTROY:
		ListNotesVisible = false;
		Utils_SaveWindowPosition(hwndDlg, 0, MODULENAME, "ListNotes");
		Window_FreeIcon_IcoLib(hwndDlg);
		return TRUE;

	case WM_NOTIFY:
		if (wParam == IDC_LISTREMINDERS) {
			LPNMLISTVIEW NM = (LPNMLISTVIEW)lParam;
			switch (NM->hdr.code) {
			case LVN_ITEMCHANGED:
				SetDlgItemTextA(hwndDlg, IDC_REMINDERDATA, g_arStickies[NM->iItem].szText);
				break;

			case NM_DBLCLK:
				if (ListView_GetSelectedCount(NM->hdr.hwndFrom)) {
					int i = ListView_GetSelectionMark(NM->hdr.hwndFrom);
					if (i != -1)
						EditNote(&g_arStickies[i]);
				}
				break;
			}
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_CONTEXTMENUNOTE_EDITNOTE:
			{
				HWND H = GetDlgItem(hwndDlg, IDC_LISTREMINDERS);
				if (ListView_GetSelectedCount(H)) {
					int i = ListView_GetSelectionMark(H);
					if (i != -1) {
						EditNote(&g_arStickies[i]);
					}
				}
			}
			return TRUE;

		case ID_CONTEXTMENUNOTE_TOGGLEVISIBILITY:
			{
				HWND H = GetDlgItem(hwndDlg, IDC_LISTREMINDERS);
				if (ListView_GetSelectedCount(H)) {
					int i = ListView_GetSelectionMark(H);
					if (i != -1) {
						auto &SN = g_arStickies[i];
						SN.bVisible = !SN.bVisible;
						ShowWindow(SN.SNHwnd, SN.bVisible ? SW_SHOWNA : SW_HIDE);
						JustSaveNotes();
					}
				}
			}
			return TRUE;

		case ID_CONTEXTMENUNOTE_TOGGLEONTOP:
			{
				HWND H = GetDlgItem(hwndDlg, IDC_LISTREMINDERS);
				if (ListView_GetSelectedCount(H)) {
					int i = ListView_GetSelectionMark(H);
					if (i != -1) {
						auto &SN = g_arStickies[i];
						SN.bOnTop = !SN.bOnTop;
						SetWindowPos(SN.SNHwnd, SN.bOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE);
						RedrawWindow(SN.SNHwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
						JustSaveNotes();
					}
				}
			}
			return TRUE;

		case IDCANCEL:
			DestroyWindow(hwndDlg);
			ListNotesVisible = false;
			return TRUE;

		case ID_CONTEXTMENUNOTE_NEWNOTE:
		case IDC_ADDNEWREMINDER:
			PluginMenuCommandAddNew(0, 0);
			return TRUE;

		case ID_CONTEXTMENUNOTE_DELETEALLNOTES:
			PluginMenuCommandDeleteNotes(0, 0);
			return TRUE;

		case ID_CONTEXTMENUNOTE_REMOVENOTE:
			{
				HWND H = GetDlgItem(hwndDlg, IDC_LISTREMINDERS);
				if (ListView_GetSelectedCount(H)) {
					int i = ListView_GetSelectionMark(H);
					if (i != -1)
						OnDeleteNote(hwndDlg, &g_arStickies[i]);
				}
			}
			return TRUE;

		case ID_CONTEXTMENUNOTE_SHOW:
			ShowHideNotes();
			return TRUE;

		case ID_CONTEXTMENUNOTE_BRINGALLTOTOP:
			BringAllNotesToFront(nullptr);
			return TRUE;
		}
	}
	return FALSE;
}


/////////////////////////////////////////////////////////////////////
// Notes List hwndDlg (uses same dialog template as reminder list)

INT_PTR PluginMenuCommandAddNew(WPARAM, LPARAM)
{
	NewNote(0, 0, 0, 0, nullptr, nullptr, TRUE, TRUE, 0);
	return 0;
}

INT_PTR PluginMenuCommandShowHide(WPARAM, LPARAM)
{
	ShowHideNotes();
	return 0;
}

INT_PTR PluginMenuCommandViewNotes(WPARAM, LPARAM)
{
	if (!ListNotesVisible) {
		CreateDialog(g_plugin.getInst(), MAKEINTRESOURCE(IDD_LISTREMINDERS), nullptr, DlgProcViewNotes);
		ListNotesVisible = true;
	}
	else BringWindowToTop(LV);
	return 0;
}

INT_PTR PluginMenuCommandAllBringFront(WPARAM, LPARAM)
{
	BringAllNotesToFront(nullptr);
	return 0;
}

INT_PTR PluginMenuCommandDeleteNotes(WPARAM, LPARAM)
{
	if (g_arStickies.getCount())
		if (IDOK == MessageBox(nullptr, TranslateT("Are you sure you want to delete all notes?"), TranslateT(SECTIONNAME), MB_OKCANCEL))
			DeleteNotes();
	return 0;
}

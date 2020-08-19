/////////////////////////////////////////////////////////////////////////////////////////
// Miranda NG: the free IM client for Microsoft* Windows*
//
// Copyright (C) 2012-20 Miranda NG team,
// Copyright (c) 2000-09 Miranda ICQ/IM project,
// all portions of this codebase are copyrighted to the people
// listed in contributors.txt.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// you should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// part of tabSRMM messaging plugin for Miranda.
//
// (C) 2005-2010 by silvercircle _at_ gmail _dot_ com and contributors
//
// implements the event notification module for tabSRMM. The code
// is largely based on the NewEventNotify plugin for Miranda NG. See
// notices below.
//
//  Name: NewEventNotify - Plugin for Miranda ICQ
// 	Description: Notifies you when you receive a message
// 	Author: icebreaker, <icebreaker@newmail.net>
// 	Date: 18.07.02 13:59 / Update: 16.09.02 17:45
// 	Copyright: (C) 2002 Starzinger Michael

#include "stdafx.h"

static LIST<PLUGIN_DATAT> arPopupList(10, NumericKeySortT);

static PLUGIN_DATAT* PU_GetByContact(const MCONTACT hContact)
{
	return arPopupList.find((PLUGIN_DATAT*)&hContact);
}

/**
 * remove stale popup data which has been marked for removal by the popup
 * window procedure.
 *
 */
static void PU_CleanUp()
{
	for (auto &p : arPopupList.rev_iter()) {
		if (p->hContact != 0)
			continue;

		mir_free(p->eventData);
		mir_free(p);
		arPopupList.removeItem(&p);
	}
}

static void CheckForRemoveMask()
{
	if (!db_get_b(0, MODULE, "firsttime", 0) && (nen_options.maskActL & MASK_REMOVE || nen_options.maskActR & MASK_REMOVE || nen_options.maskActTE & MASK_REMOVE)) {
		MessageBox(nullptr, TranslateT("One of your popup actions is set to DISMISS EVENT.\nNote that this options may have unwanted side effects as it REMOVES the event from the unread queue.\nThis may lead to events not showing up as \"new\". If you don't want this behavior, please review the 'Event notifications' settings page."), TranslateT("TabSRMM warning message"), MB_OK | MB_ICONSTOP);
		db_set_b(0, MODULE, "firsttime", 1);
	}
}

int TSAPI NEN_ReadOptions(NEN_OPTIONS *options)
{
	options->bPreview = (BOOL)db_get_b(0, MODULE, OPT_PREVIEW, TRUE);
	options->bDefaultColorMsg = (BOOL)db_get_b(0, MODULE, OPT_COLDEFAULT_MESSAGE, TRUE);
	options->bDefaultColorOthers = (BOOL)db_get_b(0, MODULE, OPT_COLDEFAULT_OTHERS, TRUE);
	options->bDefaultColorErr = (BOOL)db_get_b(0, MODULE, OPT_COLDEFAULT_ERR, TRUE);
	options->colBackMsg = db_get_dw(0, MODULE, OPT_COLBACK_MESSAGE, DEFAULT_COLBACK);
	options->colTextMsg = db_get_dw(0, MODULE, OPT_COLTEXT_MESSAGE, DEFAULT_COLTEXT);
	options->colBackOthers = db_get_dw(0, MODULE, OPT_COLBACK_OTHERS, DEFAULT_COLBACK);
	options->colTextOthers = db_get_dw(0, MODULE, OPT_COLTEXT_OTHERS, DEFAULT_COLTEXT);
	options->colBackErr = db_get_dw(0, MODULE, OPT_COLBACK_ERR, DEFAULT_COLBACK);
	options->colTextErr = db_get_dw(0, MODULE, OPT_COLTEXT_ERR, DEFAULT_COLTEXT);
	options->maskActL = (UINT)db_get_b(0, MODULE, OPT_MASKACTL, DEFAULT_MASKACTL);
	options->maskActR = (UINT)db_get_b(0, MODULE, OPT_MASKACTR, DEFAULT_MASKACTR);
	options->maskActTE = (UINT)db_get_b(0, MODULE, OPT_MASKACTTE, DEFAULT_MASKACTR) & (MASK_OPEN | MASK_DISMISS);
	options->bMergePopup = (BOOL)db_get_b(0, MODULE, OPT_MERGEPOPUP, 0);
	options->iDelayMsg = db_get_dw(0, MODULE, OPT_DELAY_MESSAGE, DEFAULT_DELAY);
	options->iDelayOthers = db_get_dw(0, MODULE, OPT_DELAY_OTHERS, DEFAULT_DELAY);
	options->iDelayErr = db_get_dw(0, MODULE, OPT_DELAY_ERR, DEFAULT_DELAY);
	options->iDelayDefault = (int)DBGetContactSettingRangedWord(0, "Popup", "Seconds", SETTING_LIFETIME_DEFAULT, SETTING_LIFETIME_MIN, SETTING_LIFETIME_MAX);
	options->bShowHeaders = (BYTE)db_get_b(0, MODULE, OPT_SHOW_HEADERS, FALSE);
	options->bNoRSS = (BOOL)db_get_b(0, MODULE, OPT_NORSS, FALSE);
	options->iDisable = (BYTE)db_get_b(0, MODULE, OPT_DISABLE, 0);
	options->iMUCDisable = (BYTE)db_get_b(0, MODULE, OPT_MUCDISABLE, 0);
	options->dwStatusMask = db_get_dw(0, MODULE, "statusmask", (DWORD)-1);
	options->bWindowCheck = (BOOL)db_get_b(0, MODULE, OPT_WINDOWCHECK, 0);
	options->bNoRSS = (BOOL)db_get_b(0, MODULE, OPT_NORSS, 0);
	options->iLimitPreview = db_get_dw(0, MODULE, OPT_LIMITPREVIEW, 0);
	options->wMaxFavorites = 15;
	options->wMaxRecent = 15;
	options->dwRemoveMask = db_get_dw(0, MODULE, OPT_REMOVEMASK, 0);
	options->bDisableNonMessage = db_get_b(0, MODULE, "disablenonmessage", 0);
	CheckForRemoveMask();
	return 0;
}

int TSAPI NEN_WriteOptions(NEN_OPTIONS *options)
{
	db_set_b(0, MODULE, OPT_PREVIEW, (BYTE)options->bPreview);
	db_set_b(0, MODULE, OPT_COLDEFAULT_MESSAGE, (BYTE)options->bDefaultColorMsg);
	db_set_b(0, MODULE, OPT_COLDEFAULT_OTHERS, (BYTE)options->bDefaultColorOthers);
	db_set_b(0, MODULE, OPT_COLDEFAULT_ERR, (BYTE)options->bDefaultColorErr);
	db_set_dw(0, MODULE, OPT_COLBACK_MESSAGE, (DWORD)options->colBackMsg);
	db_set_dw(0, MODULE, OPT_COLTEXT_MESSAGE, (DWORD)options->colTextMsg);
	db_set_dw(0, MODULE, OPT_COLBACK_OTHERS, (DWORD)options->colBackOthers);
	db_set_dw(0, MODULE, OPT_COLTEXT_OTHERS, (DWORD)options->colTextOthers);
	db_set_dw(0, MODULE, OPT_COLBACK_ERR, (DWORD)options->colBackErr);
	db_set_dw(0, MODULE, OPT_COLTEXT_ERR, (DWORD)options->colTextErr);
	db_set_b(0, MODULE, OPT_MASKACTL, (BYTE)options->maskActL);
	db_set_b(0, MODULE, OPT_MASKACTR, (BYTE)options->maskActR);
	db_set_b(0, MODULE, OPT_MASKACTTE, (BYTE)options->maskActTE);
	db_set_b(0, MODULE, OPT_MERGEPOPUP, (BYTE)options->bMergePopup);
	db_set_dw(0, MODULE, OPT_DELAY_MESSAGE, (DWORD)options->iDelayMsg);
	db_set_dw(0, MODULE, OPT_DELAY_OTHERS, (DWORD)options->iDelayOthers);
	db_set_dw(0, MODULE, OPT_DELAY_ERR, (DWORD)options->iDelayErr);
	db_set_b(0, MODULE, OPT_SHOW_HEADERS, (BYTE)options->bShowHeaders);
	db_set_b(0, MODULE, OPT_DISABLE, (BYTE)options->iDisable);
	db_set_b(0, MODULE, OPT_MUCDISABLE, (BYTE)options->iMUCDisable);
	db_set_b(0, MODULE, OPT_WINDOWCHECK, (BYTE)options->bWindowCheck);
	db_set_b(0, MODULE, OPT_NORSS, (BYTE)options->bNoRSS);
	db_set_dw(0, MODULE, OPT_LIMITPREVIEW, options->iLimitPreview);
	db_set_dw(0, MODULE, OPT_REMOVEMASK, options->dwRemoveMask);
	db_set_b(0, MODULE, "disablenonmessage", options->bDisableNonMessage);
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

static int PopupAct(HWND hWnd, UINT mask, PLUGIN_DATAT* pdata)
{
	pdata->iActionTaken = TRUE;
	if (mask & MASK_OPEN) {
		for (int i = 0; i < pdata->nrMerged; i++) {
			if (pdata->eventData[i].hEvent != 0) {
				PostMessage(PluginConfig.g_hwndHotkeyHandler, DM_HANDLECLISTEVENT, pdata->hContact, pdata->eventData[i].hEvent);
				pdata->eventData[i].hEvent = 0;
			}
		}
	}
	if (mask & MASK_REMOVE) {
		for (int i = 0; i < pdata->nrMerged; i++) {
			if (pdata->eventData[i].hEvent != 0) {
				PostMessage(PluginConfig.g_hwndHotkeyHandler, DM_REMOVECLISTEVENT, pdata->hContact, pdata->eventData[i].hEvent);
				pdata->eventData[i].hEvent = 0;
			}
		}
	}
	if (mask & MASK_DISMISS) {
		PUDeletePopup(hWnd);
		if (pdata->hContainer) {
			FLASHWINFO fwi = { sizeof(fwi) };
			fwi.dwFlags = FLASHW_STOP;
			fwi.hwnd = pdata->hContainer;
			FlashWindowEx(&fwi);
		}
	}
	return 0;
}

static LRESULT CALLBACK PopupDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	PLUGIN_DATAT *pdata = (PLUGIN_DATAT*)PUGetPluginData(hWnd);
	if (pdata == nullptr)
		return FALSE;

	switch (message) {
	case WM_COMMAND:
		PopupAct(hWnd, pdata->pluginOptions->maskActL, pdata);
		break;
	case WM_CONTEXTMENU:
		PopupAct(hWnd, pdata->pluginOptions->maskActR, pdata);
		break;
	case UM_FREEPLUGINDATA:
		pdata->hContact = 0;								// mark as removeable
		pdata->hWnd = nullptr;
		return TRUE;
	case UM_INITPOPUP:
		pdata->hWnd = hWnd;
		if (pdata->iSeconds > 0)
			SetTimer(hWnd, TIMER_TO_ACTION, pdata->iSeconds * 1000, nullptr);
		break;
	case WM_MOUSEWHEEL:
		break;
	case WM_SETCURSOR:
		break;
	case WM_TIMER:
		POINT	pt;
		RECT	rc;

		if (wParam != TIMER_TO_ACTION)
			break;

		GetCursorPos(&pt);
		GetWindowRect(hWnd, &rc);
		if (PtInRect(&rc, pt))
			break;

		if (pdata->iSeconds > 0)
			KillTimer(hWnd, TIMER_TO_ACTION);
		PopupAct(hWnd, pdata->pluginOptions->maskActTE, pdata);
		break;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

/**
* Get a preview for the message
* caller must always mir_free() the return value
*
* @param eventType the event type
* @param dbe       DBEVENTINFO *: database event structure
*
* @return
*/

static wchar_t* ShortenPreview(DBEVENTINFO *dbe)
{
	bool	fAddEllipsis = false;
	size_t iPreviewLimit = nen_options.iLimitPreview;
	if (iPreviewLimit > 500 || iPreviewLimit == 0)
		iPreviewLimit = 500;

	wchar_t *buf = DbEvent_GetTextW(dbe, CP_ACP);
	if (mir_wstrlen(buf) > iPreviewLimit) {
		fAddEllipsis = true;
		size_t iIndex = iPreviewLimit;
		size_t iWordThreshold = 20;
		while (iIndex && buf[iIndex] != ' ' && iWordThreshold--)
			buf[iIndex--] = 0;

		buf[iIndex] = 0;
	}
	if (fAddEllipsis) {
		buf = (wchar_t*)mir_realloc(buf, (mir_wstrlen(buf) + 5) * sizeof(wchar_t));
		mir_wstrcat(buf, L"...");
	}
	return buf;
}

static wchar_t* GetPreviewT(WORD eventType, DBEVENTINFO *dbe)
{
	char *pBlob = (char*)dbe->pBlob;

	switch (eventType) {
	case EVENTTYPE_MESSAGE:
		if (pBlob && nen_options.bPreview)
			return ShortenPreview(dbe);

		return mir_wstrdup(TranslateT("Message"));

	case EVENTTYPE_FILE:
		if (pBlob) {
			if (!nen_options.bPreview)
				return mir_wstrdup(TranslateT("Incoming file"));

			if (dbe->cbBlob > 5) { // min valid size = (sizeof(DWORD) + 1 character file name + terminating 0)
				char* szFileName = (char *)dbe->pBlob + sizeof(DWORD);
				char* szDescr = nullptr;
				size_t namelength = Utils::safe_strlen(szFileName, dbe->cbBlob - sizeof(DWORD));

				if (dbe->cbBlob > (sizeof(DWORD) + namelength + 1))
					szDescr = szFileName + namelength + 1;

				ptrW tszFileName(DbEvent_GetString(dbe, szFileName));
				wchar_t buf[1024];

				if (szDescr && Utils::safe_strlen(szDescr, dbe->cbBlob - sizeof(DWORD) - namelength - 1) > 0) {
					ptrW tszDescr(DbEvent_GetString(dbe, szDescr));
					if (tszFileName && tszDescr) {
						mir_snwprintf(buf, L"%s: %s (%s)", TranslateT("Incoming file"), tszFileName.get(), tszDescr.get());
						return mir_wstrdup(buf);
					}
				}

				if (tszFileName) {
					mir_snwprintf(buf, L"%s: %s (%s)", TranslateT("Incoming file"), tszFileName.get(), TranslateT("No description given"));
					return mir_wstrdup(buf);
				}
			}
		}
		return mir_wstrdup(TranslateT("Incoming file (invalid format)"));

	default:
		if (nen_options.bPreview)
			return ShortenPreview(dbe);

		return mir_wstrdup(TranslateT("Unknown event"));
	}
}

static int PopupUpdateT(MCONTACT hContact, MEVENT hEvent)
{
	PLUGIN_DATAT *pdata = const_cast<PLUGIN_DATAT *>(PU_GetByContact(hContact));
	if (!pdata)
		return 1;

	if (hEvent == 0)
		return 0;

	wchar_t szHeader[256];
	if (pdata->pluginOptions->bShowHeaders)
		mir_snwprintf(szHeader, L"%s %d\n", TranslateT("New messages: "), pdata->nrMerged + 1);
	else
		szHeader[0] = 0;

	DBEVENTINFO dbe = {};
	if (pdata->pluginOptions->bPreview && hContact) {
		dbe.cbBlob = db_event_getBlobSize(hEvent);
		dbe.pBlob = (PBYTE)mir_alloc(dbe.cbBlob);
	}
	db_event_get(hEvent, &dbe);

	wchar_t timestamp[MAX_DATASIZE];
	wcsftime(timestamp, MAX_DATASIZE, L"%Y.%m.%d %H:%M", _localtime32((__time32_t *)&dbe.timestamp));
	mir_snwprintf(pdata->eventData[pdata->nrMerged].tszText, L"\n\n%s\n", timestamp);

	wchar_t *szPreview = GetPreviewT(dbe.eventType, &dbe);
	if (szPreview) {
		mir_wstrncat(pdata->eventData[pdata->nrMerged].tszText, szPreview, _countof(pdata->eventData[pdata->nrMerged].tszText) - mir_wstrlen(pdata->eventData[pdata->nrMerged].tszText));
		mir_free(szPreview);
	}
	else mir_wstrncat(pdata->eventData[pdata->nrMerged].tszText, L" ", _countof(pdata->eventData[pdata->nrMerged].tszText) - mir_wstrlen(pdata->eventData[pdata->nrMerged].tszText));

	pdata->eventData[pdata->nrMerged].tszText[MAX_SECONDLINE - 1] = 0;

	// now, reassemble the popup text, make sure the *last* event is shown, and then show the most recent events
	// for which there is enough space in the popup text

	wchar_t lpzText[MAX_SECONDLINE];
	int i, available = MAX_SECONDLINE - 1;
	if (pdata->pluginOptions->bShowHeaders) {
		wcsncpy_s(lpzText, szHeader, _TRUNCATE);
		available -= (int)mir_wstrlen(szHeader);
	}
	else lpzText[0] = 0;

	for (i = pdata->nrMerged; i >= 0; i--) {
		available -= (int)mir_wstrlen(pdata->eventData[i].tszText);
		if (available <= 0)
			break;
	}
	i = (available > 0) ? i + 1 : i + 2;
	for (; i <= pdata->nrMerged; i++)
		mir_wstrncat(lpzText, pdata->eventData[i].tszText, _countof(lpzText) - mir_wstrlen(lpzText));

	pdata->eventData[pdata->nrMerged].hEvent = hEvent;
	pdata->eventData[pdata->nrMerged].timestamp = dbe.timestamp;
	pdata->nrMerged++;
	if (pdata->nrMerged >= pdata->nrEventsAlloced) {
		pdata->nrEventsAlloced += 5;
		pdata->eventData = (EVENT_DATAT *)mir_realloc(pdata->eventData, pdata->nrEventsAlloced * sizeof(EVENT_DATAT));
	}
	if (dbe.pBlob)
		mir_free(dbe.pBlob);

	PUChangeTextW(pdata->hWnd, lpzText);
	return 0;
}

static int PopupShowT(NEN_OPTIONS *pluginOptions, MCONTACT hContact, MEVENT hEvent, UINT eventType, HWND hContainer)
{
	// there has to be a maximum number of popups shown at the same time
	if (arPopupList.getCount() >= MAX_POPUPS)
		return 2;

	DBEVENTINFO dbe = {};
	// fix for a crash
	if (hEvent && (pluginOptions->bPreview || hContact == 0)) {
		dbe.cbBlob = db_event_getBlobSize(hEvent);
		dbe.pBlob = (PBYTE)mir_alloc(dbe.cbBlob);
	}
	db_event_get(hEvent, &dbe);

	if (hEvent == 0 && hContact == 0)
		dbe.szModule = Translate("Unknown module or contact");

	POPUPDATAW pud;
	long iSeconds;
	switch (eventType) {
	case EVENTTYPE_MESSAGE:
		pud.lchIcon = Skin_LoadIcon(SKINICON_EVENT_MESSAGE);
		pud.colorBack = pluginOptions->bDefaultColorMsg ? 0 : pluginOptions->colBackMsg;
		pud.colorText = pluginOptions->bDefaultColorMsg ? 0 : pluginOptions->colTextMsg;
		iSeconds = pluginOptions->iDelayMsg;
		break;

	default:
		pud.lchIcon = DbEvent_GetIcon(&dbe, LR_SHARED);
		pud.colorBack = pluginOptions->bDefaultColorOthers ? 0 : pluginOptions->colBackOthers;
		pud.colorText = pluginOptions->bDefaultColorOthers ? 0 : pluginOptions->colTextOthers;
		iSeconds = pluginOptions->iDelayOthers;
		break;
	}

	PLUGIN_DATAT *pdata = (PLUGIN_DATAT *)mir_calloc(sizeof(PLUGIN_DATAT));
	pdata->eventType = eventType;
	pdata->hContact = hContact;
	pdata->pluginOptions = pluginOptions;
	pdata->pud = &pud;
	pdata->iSeconds = iSeconds; // ? iSeconds : pluginOptions->iDelayDefault;
	pdata->hContainer = hContainer;
	pud.iSeconds = pdata->iSeconds ? -1 : 0;

	// finally create the popup
	pud.lchContact = hContact;
	pud.PluginWindowProc = PopupDlgProc;
	pud.PluginData = pdata;

	if (hContact)
		wcsncpy_s(pud.lpwzContactName, Clist_GetContactDisplayName(hContact), _TRUNCATE);
	else
		wcsncpy_s(pud.lpwzContactName, _A2T(dbe.szModule), _TRUNCATE);

	wchar_t *szPreview = GetPreviewT((WORD)eventType, &dbe);
	if (szPreview) {
		wcsncpy_s(pud.lpwzText, szPreview, _TRUNCATE);
		mir_free(szPreview);
	}
	else wcsncpy_s(pud.lpwzText, L" ", _TRUNCATE);

	pdata->eventData = (EVENT_DATAT *)mir_alloc(NR_MERGED * sizeof(EVENT_DATAT));
	pdata->eventData[0].hEvent = hEvent;
	pdata->eventData[0].timestamp = dbe.timestamp;
	wcsncpy_s(pdata->eventData[0].tszText, pud.lpwzText, _TRUNCATE);
	pdata->eventData[0].tszText[MAX_SECONDLINE - 1] = 0;
	pdata->nrEventsAlloced = NR_MERGED;
	pdata->nrMerged = 1;
	PUAddPopupW(&pud);
	arPopupList.insert(pdata);

	if (dbe.pBlob)
		mir_free(dbe.pBlob);

	return 0;
}

static TOptionListItem lvItemsNEN[] =
{
	{ 0, LPGENW("Show a preview of the event"), IDC_CHKPREVIEW, LOI_TYPE_SETTING, (UINT_PTR)&nen_options.bPreview, 1 },
	{ 0, LPGENW("Don't announce event when message dialog is open"), IDC_CHKWINDOWCHECK, LOI_TYPE_SETTING, (UINT_PTR)&nen_options.bWindowCheck, 1 },
	{ 0, LPGENW("Don't announce events from RSS protocols"), IDC_NORSS, LOI_TYPE_SETTING, (UINT_PTR)&nen_options.bNoRSS, 1 },
	{ 0, LPGENW("Merge new events for the same contact into existing popup"), 1, LOI_TYPE_SETTING, (UINT_PTR)&nen_options.bMergePopup, 6 },
	{ 0, LPGENW("Show headers"), IDC_CHKSHOWHEADERS, LOI_TYPE_SETTING, (UINT_PTR)&nen_options.bShowHeaders, 6 },
	{ 0, LPGENW("Dismiss popup"), MASK_DISMISS, LOI_TYPE_FLAG, (UINT_PTR)&nen_options.maskActL, 3 },
	{ 0, LPGENW("Open event"), MASK_OPEN, LOI_TYPE_FLAG, (UINT_PTR)&nen_options.maskActL, 3 },
	{ 0, LPGENW("Dismiss event"), MASK_REMOVE, LOI_TYPE_FLAG, (UINT_PTR)&nen_options.maskActL, 3 },

	{ 0, LPGENW("Dismiss popup"), MASK_DISMISS, LOI_TYPE_FLAG, (UINT_PTR)&nen_options.maskActR, 4 },
	{ 0, LPGENW("Open event"), MASK_OPEN, LOI_TYPE_FLAG, (UINT_PTR)&nen_options.maskActR, 4 },
	{ 0, LPGENW("Dismiss event"), MASK_REMOVE, LOI_TYPE_FLAG, (UINT_PTR)&nen_options.maskActR, 4 },

	{ 0, LPGENW("Dismiss popup"), MASK_DISMISS, LOI_TYPE_FLAG, (UINT_PTR)&nen_options.maskActTE, 5 },
	{ 0, LPGENW("Open event"), MASK_OPEN, LOI_TYPE_FLAG, (UINT_PTR)&nen_options.maskActTE, 5 },

	{ 0, LPGENW("Disable event notifications for instant messages"), IDC_CHKWINDOWCHECK, LOI_TYPE_SETTING, (UINT_PTR)&nen_options.iDisable, 0 },
	{ 0, LPGENW("Disable event notifications for group chats"), IDC_CHKWINDOWCHECK, LOI_TYPE_SETTING, (UINT_PTR)&nen_options.iMUCDisable, 0 },
	{ 0, LPGENW("Disable notifications for non-message events"), IDC_CHKWINDOWCHECK, LOI_TYPE_SETTING, (UINT_PTR)&nen_options.bDisableNonMessage, 0 },

	{ 0, LPGENW("Remove popups for a contact when the message window is focused"), PU_REMOVE_ON_FOCUS, LOI_TYPE_FLAG, (UINT_PTR)&nen_options.dwRemoveMask, 7 },
	{ 0, LPGENW("Remove popups for a contact when I start typing a reply"), PU_REMOVE_ON_TYPE, LOI_TYPE_FLAG, (UINT_PTR)&nen_options.dwRemoveMask, 7 },
	{ 0, LPGENW("Remove popups for a contact when I send a reply"), PU_REMOVE_ON_SEND, LOI_TYPE_FLAG, (UINT_PTR)&nen_options.dwRemoveMask, 7 },

	{ 0, nullptr, 0, 0, 0, 0 }
};

static TOptionListGroup lvGroupsNEN[] =
{
	{ 0, LPGENW("Disable notifications") },
	{ 0, LPGENW("General options") },
	{ 0, LPGENW("System tray icon") },
	{ 0, LPGENW("Left click actions (popups only)") },
	{ 0, LPGENW("Right click actions (popups only)") },
	{ 0, LPGENW("Timeout actions (popups only)") },
	{ 0, LPGENW("Combine notifications for the same contact") },
	{ 0, LPGENW("Remove popups under following conditions") },
	{ 0, nullptr }
};

class CPopupOptionsDlg : public CDlgBase
{
	CCtrlTreeView eventOptions;
	CCtrlButton btnPreview, btnModes;

public:
	CPopupOptionsDlg() :
		CDlgBase(g_plugin, IDD_POPUP_OPT),
		btnModes(this, IDC_POPUPSTATUSMODES),
		btnPreview(this, IDC_PREVIEW),
		eventOptions(this, IDC_EVENTOPTIONS)
	{
		btnModes.OnClick = Callback(this, &CPopupOptionsDlg::onClick_Modes);
		btnPreview.OnClick = Callback(this, &CPopupOptionsDlg::onClick_Preview);
	}

	bool OnInitDialog() override
	{
		TreeViewInit(eventOptions, lvGroupsNEN, lvItemsNEN, 0, 0, TRUE);

		SendDlgItemMessage(m_hwnd, IDC_COLBACK_MESSAGE, CPM_SETCOLOUR, 0, nen_options.colBackMsg);
		SendDlgItemMessage(m_hwnd, IDC_COLTEXT_MESSAGE, CPM_SETCOLOUR, 0, nen_options.colTextMsg);
		SendDlgItemMessage(m_hwnd, IDC_COLBACK_OTHERS, CPM_SETCOLOUR, 0, nen_options.colBackOthers);
		SendDlgItemMessage(m_hwnd, IDC_COLTEXT_OTHERS, CPM_SETCOLOUR, 0, nen_options.colTextOthers);
		SendDlgItemMessage(m_hwnd, IDC_COLBACK_ERR, CPM_SETCOLOUR, 0, nen_options.colBackErr);
		SendDlgItemMessage(m_hwnd, IDC_COLTEXT_ERR, CPM_SETCOLOUR, 0, nen_options.colTextErr);
		CheckDlgButton(m_hwnd, IDC_CHKDEFAULTCOL_MESSAGE, nen_options.bDefaultColorMsg ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(m_hwnd, IDC_CHKDEFAULTCOL_OTHERS, nen_options.bDefaultColorOthers ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(m_hwnd, IDC_CHKDEFAULTCOL_ERR, nen_options.bDefaultColorErr ? BST_CHECKED : BST_UNCHECKED);

		SendDlgItemMessage(m_hwnd, IDC_COLTEXT_MUC, CPM_SETCOLOUR, 0, g_Settings.crPUTextColour);
		SendDlgItemMessage(m_hwnd, IDC_COLBACK_MUC, CPM_SETCOLOUR, 0, g_Settings.crPUBkgColour);
		CheckDlgButton(m_hwnd, IDC_CHKDEFAULTCOL_MUC, g_Settings.iPopupStyle == 2 ? BST_CHECKED : BST_UNCHECKED);

		SendDlgItemMessage(m_hwnd, IDC_DELAY_MESSAGE_SPIN, UDM_SETRANGE, 0, MAKELONG(3600, -1));
		SendDlgItemMessage(m_hwnd, IDC_DELAY_OTHERS_SPIN, UDM_SETRANGE, 0, MAKELONG(3600, -1));
		SendDlgItemMessage(m_hwnd, IDC_DELAY_MESSAGE_MUC_SPIN, UDM_SETRANGE, 0, MAKELONG(3600, -1));
		SendDlgItemMessage(m_hwnd, IDC_DELAY_ERR_SPIN, UDM_SETRANGE, 0, MAKELONG(3600, -1));

		SendDlgItemMessage(m_hwnd, IDC_DELAY_MESSAGE_SPIN, UDM_SETPOS, 0, (LPARAM)nen_options.iDelayMsg);
		SendDlgItemMessage(m_hwnd, IDC_DELAY_OTHERS_SPIN, UDM_SETPOS, 0, (LPARAM)nen_options.iDelayOthers);
		SendDlgItemMessage(m_hwnd, IDC_DELAY_ERR_SPIN, UDM_SETPOS, 0, (LPARAM)nen_options.iDelayErr);
		SendDlgItemMessage(m_hwnd, IDC_DELAY_MESSAGE_MUC_SPIN, UDM_SETPOS, 0, (LPARAM)g_Settings.iPopupTimeout);

		Utils::enableDlgControl(m_hwnd, IDC_COLBACK_MESSAGE, !nen_options.bDefaultColorMsg);
		Utils::enableDlgControl(m_hwnd, IDC_COLTEXT_MESSAGE, !nen_options.bDefaultColorMsg);
		Utils::enableDlgControl(m_hwnd, IDC_COLBACK_OTHERS, !nen_options.bDefaultColorOthers);
		Utils::enableDlgControl(m_hwnd, IDC_COLTEXT_OTHERS, !nen_options.bDefaultColorOthers);
		Utils::enableDlgControl(m_hwnd, IDC_COLBACK_ERR, !nen_options.bDefaultColorErr);
		Utils::enableDlgControl(m_hwnd, IDC_COLTEXT_ERR, !nen_options.bDefaultColorErr);
		Utils::enableDlgControl(m_hwnd, IDC_COLTEXT_MUC, g_Settings.iPopupStyle == 3);
		Utils::enableDlgControl(m_hwnd, IDC_COLBACK_MUC, g_Settings.iPopupStyle == 3);

		CheckDlgButton(m_hwnd, IDC_MUC_LOGCOLORS, g_Settings.iPopupStyle < 2 ? BST_CHECKED : BST_UNCHECKED);
		Utils::enableDlgControl(m_hwnd, IDC_MUC_LOGCOLORS, g_Settings.iPopupStyle != 2);

		SetDlgItemInt(m_hwnd, IDC_MESSAGEPREVIEWLIMIT, nen_options.iLimitPreview, FALSE);
		CheckDlgButton(m_hwnd, IDC_LIMITPREVIEW, (nen_options.iLimitPreview > 0) ? BST_CHECKED : BST_UNCHECKED);
		SendDlgItemMessage(m_hwnd, IDC_MESSAGEPREVIEWLIMITSPIN, UDM_SETRANGE, 0, MAKELONG(2048, nen_options.iLimitPreview > 0 ? 50 : 0));
		SendDlgItemMessage(m_hwnd, IDC_MESSAGEPREVIEWLIMITSPIN, UDM_SETPOS, 0, (LPARAM)nen_options.iLimitPreview);
		Utils::enableDlgControl(m_hwnd, IDC_MESSAGEPREVIEWLIMIT, IsDlgButtonChecked(m_hwnd, IDC_LIMITPREVIEW) != 0);
		Utils::enableDlgControl(m_hwnd, IDC_MESSAGEPREVIEWLIMITSPIN, IsDlgButtonChecked(m_hwnd, IDC_LIMITPREVIEW) != 0);
		return true;
	}

	bool OnApply() override
	{
		// scan the tree view and obtain the options...
		TreeViewToDB(eventOptions, lvItemsNEN, nullptr, nullptr);

		db_set_b(0, CHAT_MODULE, "PopupStyle", (BYTE)g_Settings.iPopupStyle);
		db_set_w(0, CHAT_MODULE, "PopupTimeout", g_Settings.iPopupTimeout);

		g_Settings.crPUBkgColour = SendDlgItemMessage(m_hwnd, IDC_COLBACK_MUC, CPM_GETCOLOUR, 0, 0);
		db_set_dw(0, CHAT_MODULE, "PopupColorBG", (DWORD)g_Settings.crPUBkgColour);
		g_Settings.crPUTextColour = SendDlgItemMessage(m_hwnd, IDC_COLTEXT_MUC, CPM_GETCOLOUR, 0, 0);
		db_set_dw(0, CHAT_MODULE, "PopupColorText", (DWORD)g_Settings.crPUTextColour);

		NEN_WriteOptions(&nen_options);
		CheckForRemoveMask();
		return true;
	}

	INT_PTR DlgProc(UINT msg, WPARAM wParam, LPARAM lParam) override
	{
		if (msg == WM_COMMAND && wParam == DM_STATUSMASKSET) {
			db_set_dw(0, MODULE, "statusmask", (DWORD)lParam);
			nen_options.dwStatusMask = (int)lParam;
		}

		return CDlgBase::DlgProc(msg, wParam, lParam);
	}

	void onClick_Preview(CCtrlButton *)
	{
		PopupShowT(&nen_options, 0, 0, EVENTTYPE_MESSAGE, nullptr);
	}

	void onClick_Modes(CCtrlButton *)
	{
		CreateDialogParam(g_plugin.getInst(), MAKEINTRESOURCE(IDD_CHOOSESTATUSMODES), m_hwnd, DlgProcSetupStatusModes, db_get_dw(0, MODULE, "statusmask", (DWORD)-1));
	}

	void OnChange() override
	{
		if (IsDlgButtonChecked(m_hwnd, IDC_CHKDEFAULTCOL_MUC))
			g_Settings.iPopupStyle = 2;
		else if (IsDlgButtonChecked(m_hwnd, IDC_MUC_LOGCOLORS))
			g_Settings.iPopupStyle = 1;
		else
			g_Settings.iPopupStyle = 3;

		Utils::enableDlgControl(m_hwnd, IDC_MUC_LOGCOLORS, g_Settings.iPopupStyle != 2);

		nen_options.bDefaultColorMsg = IsDlgButtonChecked(m_hwnd, IDC_CHKDEFAULTCOL_MESSAGE);
		nen_options.bDefaultColorOthers = IsDlgButtonChecked(m_hwnd, IDC_CHKDEFAULTCOL_OTHERS);
		nen_options.bDefaultColorErr = IsDlgButtonChecked(m_hwnd, IDC_CHKDEFAULTCOL_ERR);

		nen_options.iDelayMsg = SendDlgItemMessage(m_hwnd, IDC_DELAY_MESSAGE_SPIN, UDM_GETPOS, 0, 0);
		nen_options.iDelayOthers = SendDlgItemMessage(m_hwnd, IDC_DELAY_OTHERS_SPIN, UDM_GETPOS, 0, 0);
		nen_options.iDelayErr = SendDlgItemMessage(m_hwnd, IDC_DELAY_ERR_SPIN, UDM_GETPOS, 0, 0);

		g_Settings.iPopupTimeout = SendDlgItemMessage(m_hwnd, IDC_DELAY_MESSAGE_MUC_SPIN, UDM_GETPOS, 0, 0);

		if (IsDlgButtonChecked(m_hwnd, IDC_LIMITPREVIEW))
			nen_options.iLimitPreview = GetDlgItemInt(m_hwnd, IDC_MESSAGEPREVIEWLIMIT, nullptr, FALSE);
		else
			nen_options.iLimitPreview = 0;
		Utils::enableDlgControl(m_hwnd, IDC_COLBACK_MESSAGE, !nen_options.bDefaultColorMsg);
		Utils::enableDlgControl(m_hwnd, IDC_COLTEXT_MESSAGE, !nen_options.bDefaultColorMsg);
		Utils::enableDlgControl(m_hwnd, IDC_COLBACK_OTHERS, !nen_options.bDefaultColorOthers);
		Utils::enableDlgControl(m_hwnd, IDC_COLTEXT_OTHERS, !nen_options.bDefaultColorOthers);
		Utils::enableDlgControl(m_hwnd, IDC_COLBACK_ERR, !nen_options.bDefaultColorErr);
		Utils::enableDlgControl(m_hwnd, IDC_COLTEXT_ERR, !nen_options.bDefaultColorErr);
		Utils::enableDlgControl(m_hwnd, IDC_COLTEXT_MUC, g_Settings.iPopupStyle == 3);
		Utils::enableDlgControl(m_hwnd, IDC_COLBACK_MUC, g_Settings.iPopupStyle == 3);

		Utils::enableDlgControl(m_hwnd, IDC_MESSAGEPREVIEWLIMIT, IsDlgButtonChecked(m_hwnd, IDC_LIMITPREVIEW) != 0);
		Utils::enableDlgControl(m_hwnd, IDC_MESSAGEPREVIEWLIMITSPIN, IsDlgButtonChecked(m_hwnd, IDC_LIMITPREVIEW) != 0);

		// disable delay textbox when infinite is checked
		Utils::enableDlgControl(m_hwnd, IDC_DELAY_MESSAGE, nen_options.iDelayMsg != -1);
		Utils::enableDlgControl(m_hwnd, IDC_DELAY_OTHERS, nen_options.iDelayOthers != -1);
		Utils::enableDlgControl(m_hwnd, IDC_DELAY_ERR, nen_options.iDelayErr != -1);
		Utils::enableDlgControl(m_hwnd, IDC_DELAY_MUC, g_Settings.iPopupTimeout != -1);

		nen_options.colBackMsg = SendDlgItemMessage(m_hwnd, IDC_COLBACK_MESSAGE, CPM_GETCOLOUR, 0, 0);
		nen_options.colTextMsg = SendDlgItemMessage(m_hwnd, IDC_COLTEXT_MESSAGE, CPM_GETCOLOUR, 0, 0);
		nen_options.colBackOthers = SendDlgItemMessage(m_hwnd, IDC_COLBACK_OTHERS, CPM_GETCOLOUR, 0, 0);
		nen_options.colTextOthers = SendDlgItemMessage(m_hwnd, IDC_COLTEXT_OTHERS, CPM_GETCOLOUR, 0, 0);
		nen_options.colBackErr = SendDlgItemMessage(m_hwnd, IDC_COLBACK_ERR, CPM_GETCOLOUR, 0, 0);
		nen_options.colTextErr = SendDlgItemMessage(m_hwnd, IDC_COLTEXT_ERR, CPM_GETCOLOUR, 0, 0);
		g_Settings.crPUBkgColour = SendDlgItemMessage(m_hwnd, IDC_COLBACK_MUC, CPM_GETCOLOUR, 0, 0);
		g_Settings.crPUTextColour = SendDlgItemMessage(m_hwnd, IDC_COLTEXT_MUC, CPM_GETCOLOUR, 0, 0);
	}
};

void Popup_Options(WPARAM wParam)
{
	OPTIONSDIALOGPAGE odp = {};
	odp.flags = ODPF_BOLDGROUPS;
	odp.position = 910000000;
	odp.pDialog = new CPopupOptionsDlg();
	odp.szTitle.a = LPGEN("Event notifications");
	odp.szGroup.a = LPGEN("Popups");
	g_plugin.addOptions(wParam, &odp);
}

int tabSRMM_ShowPopup(MCONTACT hContact, MEVENT hDbEvent, WORD eventType, int windowOpen, TContainerData *pContainer, HWND hwndChild, const char *szProto)
{
	if (nen_options.iDisable) // no popups at all. Period
		return 0;

	PU_CleanUp();

	if (nen_options.bDisableNonMessage && eventType != EVENTTYPE_MESSAGE)
		return 0;

	// check the status mode against the status mask
	if (nen_options.dwStatusMask != -1) {
		if (szProto != nullptr) {
			int dwStatus = Proto_GetStatus(szProto);
			if (!(dwStatus == 0 || dwStatus <= ID_STATUS_OFFLINE || ((1 << (dwStatus - ID_STATUS_ONLINE)) & nen_options.dwStatusMask)))           // should never happen, but...
				return 0;
		}
	}
	if (nen_options.bNoRSS && szProto != nullptr && !strncmp(szProto, "NewsAggregator", 3))
		return 0; // filter out RSS popups

	// message window is open, need to check the container config if we want to see a popup nonetheless
	if (windowOpen && pContainer != nullptr) { 
		if (nen_options.bWindowCheck && windowOpen) // no popups at all for open windows... no exceptions
			return 0;
	
		if (pContainer->m_flags.m_bDontReport && (IsIconic(pContainer->m_hwnd))) // in tray counts as "minimised"
			goto passed;

		if (pContainer->m_flags.m_bDontReportUnfocused)
			if (!IsIconic(pContainer->m_hwnd) && !pContainer->IsActive())
				goto passed;

		if (pContainer->m_flags.m_bAlwaysReportInactive) {
			if (pContainer->m_flags.m_bDontReportFocused)
				goto passed;

			if (pContainer->m_hwndActive != hwndChild)
				goto passed;
		}
		return 0;
	}
passed:
	if (PU_GetByContact(hContact) && nen_options.bMergePopup && eventType == EVENTTYPE_MESSAGE) {
		if (PopupUpdateT(hContact, hDbEvent) != 0)
			PopupShowT(&nen_options, hContact, hDbEvent, eventType, pContainer ? pContainer->m_hwnd : nullptr);
	}
	else PopupShowT(&nen_options, hContact, hDbEvent, eventType, pContainer ? pContainer->m_hwnd : nullptr);

	return 0;
}

// remove all popups for hContact, but only if the mask matches the current "mode"
void TSAPI DeletePopupsForContact(MCONTACT hContact, DWORD dwMask)
{
	if (!(dwMask & nen_options.dwRemoveMask) || nen_options.iDisable)
		return;

	PLUGIN_DATAT *_T = nullptr;
	while ((_T = PU_GetByContact(hContact)) != nullptr) {
		_T->hContact = 0;									// make sure, it never "comes back"
		if (_T->hWnd != nullptr)
			PUDeletePopup(_T->hWnd);
	}
}

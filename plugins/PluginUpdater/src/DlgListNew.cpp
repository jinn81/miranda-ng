/*
Copyright (C) 2010 Mataes

This is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with this file; see the file license.txt. If
not, write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.
*/

#include "stdafx.h"

static HWND hwndDialog;
static HANDLE hListThread;

static void ApplyDownloads(void *param);

bool FILEINFO::IsFiltered(const CMStringW &wszFilter)
{
	if (wszFilter.IsEmpty())
		return false;

	wchar_t pathLwr[MAX_PATH];
	wcsncpy_s(pathLwr, this->tszNewName, _TRUNCATE);
	wcslwr(pathLwr);
	return wcsstr(pathLwr, wszFilter) == 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

static LRESULT CALLBACK PluginListWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_LBUTTONDOWN) {
		LVHITTESTINFO hi;
		hi.pt.x = LOWORD(lParam); hi.pt.y = HIWORD(lParam);
		ListView_SubItemHitTest(hwnd, &hi);
		if ((hi.iSubItem == 0) && (hi.flags & LVHT_ONITEMICON)) {
			LVITEM lvi = { 0 };
			lvi.mask = LVIF_IMAGE | LVIF_PARAM | LVIF_GROUPID;
			lvi.stateMask = -1;
			lvi.iItem = hi.iItem;
			if (ListView_GetItem(hwnd, &lvi) && lvi.iGroupId == 1) {
				FILEINFO *info = (FILEINFO *)lvi.lParam;

				wchar_t tszFileName[MAX_PATH];
				wcscpy(tszFileName, wcsrchr(info->tszNewName, L'\\') + 1);
				wchar_t *p = wcschr(tszFileName, L'.'); *p = 0;

				wchar_t link[MAX_PATH];
				mir_snwprintf(link, PLUGIN_INFO_URL, tszFileName);
				Utils_OpenUrlW(link);
			}
		}
	}

	return mir_callNextSubclass(hwnd, PluginListWndProc, msg, wParam, lParam);
}

int ImageList_AddIconFromIconLib(HIMAGELIST hIml, int i)
{
	HICON icon = IcoLib_GetIconByHandle(iconList[i].hIcolib);
	int res = ImageList_AddIcon(hIml, icon);
	IcoLib_ReleaseIcon(icon);
	return res;
}

class CMissingPLuginsDlg : public CDlgBase
{
	CMStringW m_wszFilter;
	OBJLIST<FILEINFO> *todo;
	CCtrlEdit m_filter;
	CCtrlListView m_list;
	CCtrlButton btnOk, btnNone;

	void FillList()
	{
		m_list.DeleteAllItems();

		LVITEM lvi = { 0 };
		lvi.mask = LVIF_PARAM | LVIF_GROUPID | LVIF_TEXT | LVIF_IMAGE;

		bool enableOk = false;
		for (auto &p : *todo) {
			if (p->IsFiltered(m_wszFilter))
				continue;

			int groupId = 4;
			if (wcschr(p->tszOldName, L'\\') != nullptr)
				groupId = (wcsstr(p->tszOldName, L"Plugins") != nullptr) ? 1 : ((wcsstr(p->tszOldName, L"Languages") != nullptr) ? 3 : 2);

			lvi.iItem = todo->indexOf(&p);
			lvi.lParam = (LPARAM)p;
			lvi.iGroupId = groupId;
			lvi.iImage = ((groupId == 1) ? 0 : -1);
			lvi.pszText = p->tszOldName;
			m_list.InsertItem(&lvi);

			if (p->bEnabled) {
				enableOk = true;
				m_list.SetCheckState(lvi.iItem, 1);
			}
		}

		btnOk.Enable(enableOk);
	}

	void SelectAll(bool bEnable)
	{
		for (int i = 0; i < todo->getCount(); i++)
			m_list.SetCheckState(i, (*todo)[i].bEnabled = bEnable);
	}

public:
	CMissingPLuginsDlg(OBJLIST<FILEINFO> *param) :
		CDlgBase(g_plugin, IDD_MISSINGPLUGINS),
		todo(param),
		btnOk(this, IDOK),
		btnNone(this, IDC_SELNONE),
		m_list(this, IDC_LIST_UPDATES),
		m_filter(this, IDC_SEARCH)
	{
		SetParent(GetDesktopWindow());
		SetMinSize(370, 300);

		btnNone.OnClick = Callback(this, &CMissingPLuginsDlg::onClick_None);

		m_filter.OnChange = Callback(this, &CMissingPLuginsDlg::onChange_Filter);
		m_list.OnItemChanged = Callback(this, &CMissingPLuginsDlg::onItemChanged);
	}

	bool OnInitDialog() override
	{
		hwndDialog = m_hwnd;
		mir_subclassWindow(m_list.GetHwnd(), PluginListWndProc);

		Window_SetIcon_IcoLib(m_hwnd, iconList[2].hIcolib);

		HIMAGELIST hIml = ImageList_Create(16, 16, ILC_MASK | ILC_COLOR32, 4, 0);
		ImageList_AddIconFromIconLib(hIml, 1);
		m_list.SetImageList(hIml, LVSIL_SMALL);

		if (IsWinVer7Plus()) {
			wchar_t szPath[MAX_PATH];
			GetModuleFileNameW(nullptr, szPath, _countof(szPath));
			wchar_t *ext = wcsrchr(szPath, '.');
			if (ext != nullptr)
				*ext = '\0';
			wcscat(szPath, L".test");
			HANDLE hFile = CreateFileW(szPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hFile == INVALID_HANDLE_VALUE)
				Button_SetElevationRequiredState(GetDlgItem(m_hwnd, IDOK), !IsProcessElevated());
			else {
				CloseHandle(hFile);
				DeleteFile(szPath);
			}
		}

		//////////////////////////////////////////////////////////////////////////////////////
		LVCOLUMN lvc = { 0 };
		lvc.mask = LVCF_WIDTH | LVCF_TEXT;

		lvc.pszText = TranslateT("Component Name");
		lvc.cx = 220; // width of column in pixels
		m_list.InsertColumn(0, &lvc);

		lvc.pszText = TranslateT("State");
		lvc.cx = 100; // width of column in pixels
		m_list.InsertColumn(1, &lvc);

		//////////////////////////////////////////////////////////////////////////////////////
		LVGROUP lvg;
		lvg.cbSize = sizeof(LVGROUP);
		lvg.mask = LVGF_HEADER | LVGF_GROUPID;

		lvg.pszHeader = TranslateT("Plugins");
		lvg.iGroupId = 1;
		m_list.InsertGroup(0, &lvg);

		lvg.pszHeader = TranslateT("Icons");
		lvg.iGroupId = 2;
		m_list.InsertGroup(0, &lvg);

		lvg.pszHeader = TranslateT("Languages");
		lvg.iGroupId = 3;
		m_list.InsertGroup(0, &lvg);

		lvg.pszHeader = TranslateT("Other");
		lvg.iGroupId = 4;
		m_list.InsertGroup(0, &lvg);

		m_list.EnableGroupView(true);

		//////////////////////////////////////////////////////////////////////////////////////
		m_list.SendMsg(LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_SUBITEMIMAGES | LVS_EX_CHECKBOXES | LVS_EX_LABELTIP);

		FillList();

		// do this after filling list - enables 'ITEMCHANGED' below
		Utils_RestoreWindowPosition(m_hwnd, 0, MODULENAME, "ListWindow");
		return true;
	}

	bool OnApply() override
	{
		btnOk.Disable();
		btnNone.Disable();
		mir_forkthread(ApplyDownloads, this);
		return false; // do not allow a dialog to close
	}

	void OnDestroy() override
	{
		Utils_SaveWindowPosition(m_hwnd, NULL, MODULENAME, "ListWindow");
		Window_FreeIcon_IcoLib(m_hwnd);
		hwndDialog = nullptr;
		delete (OBJLIST<FILEINFO> *)GetWindowLongPtr(m_hwnd, GWLP_USERDATA);
	}

	int Resizer(UTILRESIZECONTROL *urc) override
	{
		switch (urc->wId) {
		case IDC_SELNONE:
		case IDOK:
			return RD_ANCHORX_RIGHT | RD_ANCHORY_BOTTOM;

		case IDC_SEARCH:
			return RD_ANCHORX_WIDTH | RD_ANCHORY_TOP;

		case IDC_UPDATETEXT:
			return RD_ANCHORX_CENTRE;
		}
		return RD_ANCHORX_LEFT | RD_ANCHORY_TOP | RD_ANCHORX_WIDTH | RD_ANCHORY_HEIGHT;
	}

	void onItemChanged(CCtrlListView::TEventInfo *ev)
	{
		NMLISTVIEW *nmlv = ev->nmlv;
		if ((nmlv->uNewState ^ nmlv->uOldState) & LVIS_STATEIMAGEMASK) {
			LVITEM lvI = { 0 };
			lvI.iItem = nmlv->iItem;
			lvI.iSubItem = 0;
			lvI.mask = LVIF_PARAM;
			m_list.GetItem(&lvI);

			FILEINFO *p = (FILEINFO *)lvI.lParam;
			p->bEnabled = m_list.GetCheckState(nmlv->iItem);

			bool enableOk = false;
			for (auto &it : *todo) {
				if (it->bEnabled) {
					enableOk = true;
					break;
				}
			}

			btnOk.Enable(enableOk);
		}
	}

	void onClick_None(CCtrlButton *)
	{
		SelectAll(false);
	}

	void onChange_Filter(CCtrlEdit *)
	{
		wchar_t wszText[100];
		m_filter.GetText(wszText, _countof(wszText));
		if (wszText[0] == 0)
			m_wszFilter.Empty();
		else {
			wcslwr(wszText);
			m_wszFilter = wszText;
		}
		FillList();
	}

	void Unpack()
	{
		AutoHandle pipe(hPipe);

		// create needed folders after escalating priviledges. Folders creates when we actually install updates
		wchar_t tszFileTemp[MAX_PATH], tszFileBack[MAX_PATH];

		mir_snwprintf(tszFileBack, L"%s\\Backups", g_tszRoot);
		SafeCreateDirectory(tszFileBack);

		mir_snwprintf(tszFileTemp, L"%s\\Temp", g_tszRoot);
		SafeCreateDirectory(tszFileTemp);

		VARSW tszMirandaPath(L"%miranda_path%");

		HNETLIBCONN nlc = nullptr;
		int i = 0;
		for (auto &p : *todo) {
			if (p->IsFiltered(m_wszFilter))
				continue;

			m_list.EnsureVisible(i, FALSE);
			if (p->bEnabled) {
				// download update
				m_list.SetItemText(i, 1, TranslateT("Downloading..."));

				if (DownloadFile(&p->File, nlc)) {
					m_list.SetItemText(i, 1, TranslateT("Succeeded."));
					if (unzip(p->File.tszDiskPath, tszMirandaPath, tszFileBack, false))
						SafeDeleteFile(p->File.tszDiskPath);  // remove .zip after successful update
					db_unset(0, DB_MODULE_NEW_FILES, _T2A(p->tszOldName));
				}
				else m_list.SetItemText(i, 1, TranslateT("Failed!"));
			}
			else m_list.SetItemText(i, 1, TranslateT("Skipped."));
			i++;
		}
		Netlib_CloseHandle(nlc);

		ShowPopup(TranslateT("Plugin Updater"), TranslateT("Download complete"), POPUP_TYPE_INFO);

		int rc = MessageBox(m_hwnd, TranslateT("Download complete. Do you want to go to plugins option page?"), TranslateT("Plugin Updater"), MB_YESNO | MB_ICONQUESTION);
		if (rc == IDYES)
			CallFunctionAsync(OpenPluginOptions, nullptr);

		Close();
	}
};

static void ApplyDownloads(void *param)
{
	Thread_SetName("PluginUpdater: ApplyDownloads");
	auto *pDlg = (CMissingPLuginsDlg *)param;

	// if we need to escalate priviledges, launch a atub
	if (PrepareEscalation())
		pDlg->Unpack();
		
	pDlg->Close();
}

static void __stdcall LaunchListDialog(void *param)
{
	(new CMissingPLuginsDlg((OBJLIST<FILEINFO> *)param))->Show();
}

static FILEINFO* ServerEntryToFileInfo(const ServListEntry &hash, const wchar_t* tszBaseUrl, const wchar_t* tszPath)
{
	FILEINFO *FileInfo = new FILEINFO;
	FileInfo->bDeleteOnly = FALSE;
	// copy the relative old name
	wcsncpy_s(FileInfo->tszOldName, hash.m_name, _TRUNCATE);
	wcsncpy_s(FileInfo->tszNewName, hash.m_name, _TRUNCATE);

	wchar_t tszFileName[MAX_PATH];
	wcsncpy_s(tszFileName, wcsrchr(tszPath, L'\\') + 1, _TRUNCATE);
	if (auto *tp = wcschr(tszFileName, L'.'))
		*tp = 0;

	wchar_t tszRelFileName[MAX_PATH];
	wcsncpy_s(tszRelFileName, hash.m_name, _TRUNCATE);
	if (auto *tp = wcsrchr(tszRelFileName, L'.'))
		*tp = 0;
	if (auto *tp = wcschr(tszRelFileName, L'\\'))
		wcslwr((tp) ? tp+1 : tszRelFileName);

	mir_snwprintf(FileInfo->File.tszDiskPath, L"%s\\Temp\\%s.zip", g_tszRoot, tszFileName);
	mir_snwprintf(FileInfo->File.tszDownloadURL, L"%s/%s.zip", tszBaseUrl, tszRelFileName);
	for (auto *tp = wcschr(FileInfo->File.tszDownloadURL, '\\'); tp != nullptr; tp = wcschr(tp, '\\'))
		*tp++ = '/';
	FileInfo->File.CRCsum = hash.m_crc;
	
	// Load list of checked Plugins from database
	Netlib_LogfW(hNetlibUser, L"File %s found", FileInfo->tszOldName);
	FileInfo->bEnabled = db_get_b(0, DB_MODULE_NEW_FILES, _T2A(FileInfo->tszOldName)) != 0;
	return FileInfo;
}

/////////////////////////////////////////////////////////////////////////////////////////
// building file list in the separate thread

static void GetList(void *)
{
	Thread_SetName("PluginUpdater: GetList");

	wchar_t tszTempPath[MAX_PATH];
	DWORD dwLen = GetTempPath(_countof(tszTempPath), tszTempPath);
	if (tszTempPath[dwLen - 1] == '\\')
		tszTempPath[dwLen - 1] = 0;

	ptrW updateUrl(GetDefaultUrl()), baseUrl;
	SERVLIST hashes(50, CompareHashes);
	if (!ParseHashes(updateUrl, baseUrl, hashes)) {
		hListThread = nullptr;
		return;
	}

	FILELIST *UpdateFiles = new FILELIST(20);
	VARSW dirname(L"%miranda_path%");

	for (auto &it : hashes) {
		wchar_t tszPath[MAX_PATH];
		mir_snwprintf(tszPath, L"%s\\%s", dirname.get(), it->m_name);

		if (GetFileAttributes(tszPath) == INVALID_FILE_ATTRIBUTES) {
			FILEINFO *FileInfo = ServerEntryToFileInfo(*it, baseUrl, tszPath);
			UpdateFiles->insert(FileInfo);
		}
	}

	// Show dialog
	if (UpdateFiles->getCount() == 0) {
		ShowPopup(TranslateT("Plugin Updater"), TranslateT("List is empty."), POPUP_TYPE_INFO);
		delete UpdateFiles;
	}
	else CallFunctionAsync(LaunchListDialog, UpdateFiles);

	hListThread = nullptr;
}

static void DoGetList()
{
	if (hListThread)
		ShowPopup(TranslateT("Plugin Updater"), TranslateT("List loading already started!"), POPUP_TYPE_INFO);
	else if (hwndDialog) {
		ShowWindow(hwndDialog, SW_SHOW);
		SetForegroundWindow(hwndDialog);
		SetFocus(hwndDialog);
	}
	else hListThread = mir_forkthread(GetList);
}

void UninitListNew()
{
	if (hwndDialog != nullptr)
		DestroyWindow(hwndDialog);
}

static INT_PTR ShowListCommand(WPARAM, LPARAM)
{
	DoGetList();
	return 0;
}

void UnloadListNew()
{
	if (hListThread)
		hListThread = nullptr;
}

static INT_PTR ParseUriService(WPARAM, LPARAM lParam)
{
	wchar_t *arg = (wchar_t *)lParam;
	if (arg == nullptr)
		return 1;

	wchar_t uri[1024];
	wcsncpy_s(uri, arg, _TRUNCATE);

	wchar_t *p = wcschr(uri, ':');
	if (p == nullptr)
		return 1;

	wchar_t pluginPath[MAX_PATH];
	mir_wstrcpy(pluginPath, p + 1);
	p = wcschr(pluginPath, '/');
	if (p) *p = '\\';

	if (GetFileAttributes(pluginPath) != INVALID_FILE_ATTRIBUTES)
		return 0;

	ptrW updateUrl(GetDefaultUrl()), baseUrl;
	SERVLIST hashes(50, CompareHashes);
	if (!ParseHashes(updateUrl, baseUrl, hashes)) {
		hListThread = nullptr;
		return 1;
	}

	ServListEntry *hash = hashes.find((ServListEntry*)&pluginPath);
	if (hash == nullptr)
		return 0;

	VARSW dirName(L"%miranda_path%");
	wchar_t tszPath[MAX_PATH];
	mir_snwprintf(tszPath, L"%s\\%s", dirName.get(), hash->m_name);
	FILEINFO *fileInfo = ServerEntryToFileInfo(*hash, baseUrl, tszPath);

	FILELIST *fileList = new FILELIST(1);
	fileList->insert(fileInfo);
	CallFunctionAsync(LaunchListDialog, fileList);

	return 0;
}

void InitListNew()
{
	CreateServiceFunction(MODULENAME "/ParseUri", ParseUriService);
	CreateServiceFunction(MS_PU_SHOWLIST, ShowListCommand);
}

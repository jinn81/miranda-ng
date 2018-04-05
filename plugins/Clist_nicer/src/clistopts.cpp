/*

Miranda NG: the free IM client for Microsoft* Windows*

Copyright (c) 2012-18 Miranda NG team (https://miranda-ng.org),
Copyright (c) 2000-03 Miranda ICQ/IM project,
all portions of this codebase are copyrighted to the people
listed in contributors.txt.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "stdafx.h"

static int opt_gen_opts_changed = 0;

static void __setFlag(DWORD dwFlag, int iMode)
{
	cfg::dat.dwFlags = iMode ? cfg::dat.dwFlags | dwFlag : cfg::dat.dwFlags & ~dwFlag;
}

INT_PTR CALLBACK DlgProcGenOpts(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_USER + 1:
		{
			MCONTACT hContact = wParam;
			DBCONTACTWRITESETTING *ws = (DBCONTACTWRITESETTING *)lParam;
			if (hContact == NULL && ws != nullptr && ws->szModule != nullptr && ws->szSetting != nullptr && strcmp(ws->szModule, "CList") == 0 && strcmp(ws->szSetting, "UseGroups") == 0 && IsWindowVisible(hwndDlg))
				CheckDlgButton(hwndDlg, IDC_DISABLEGROUPS, ws->value.bVal == 0 ? BST_CHECKED : BST_UNCHECKED);
		}
		break;

	case WM_DESTROY:
		UnhookEvent((HANDLE)GetWindowLongPtr(hwndDlg, GWLP_USERDATA));
		break;

	case WM_INITDIALOG:
		opt_gen_opts_changed = 0;
		TranslateDialogDefault(hwndDlg);
		SetWindowLongPtr(hwndDlg, GWLP_USERDATA, (LONG_PTR)HookEventMessage(ME_DB_CONTACT_SETTINGCHANGED, hwndDlg, WM_USER + 1));
		CheckDlgButton(hwndDlg, IDC_HIDEOFFLINE, db_get_b(NULL, "CList", "HideOffline", SETTING_HIDEOFFLINE_DEFAULT) ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_HIDEEMPTYGROUPS, db_get_b(NULL, "CList", "HideEmptyGroups", SETTING_HIDEEMPTYGROUPS_DEFAULT) ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_DISABLEGROUPS, db_get_b(NULL, "CList", "UseGroups", SETTING_USEGROUPS_DEFAULT) ? BST_UNCHECKED : BST_CHECKED);
		CheckDlgButton(hwndDlg, IDC_CONFIRMDELETE, db_get_b(NULL, "CList", "ConfirmDelete", SETTING_CONFIRMDELETE_DEFAULT) ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_SHOWBOTTOMBUTTONS, cfg::dat.dwFlags & CLUI_FRAME_SHOWBOTTOMBUTTONS ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_CLISTSUNKEN, cfg::dat.dwFlags & CLUI_FRAME_CLISTSUNKEN ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_EVENTAREAAUTOHIDE, cfg::dat.dwFlags & CLUI_FRAME_AUTOHIDENOTIFY ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_EVENTAREASUNKEN, (cfg::dat.dwFlags & CLUI_FRAME_EVENTAREASUNKEN) ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_ONECLK, db_get_b(NULL, "CList", "Tray1Click", SETTING_TRAY1CLICK_DEFAULT) ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_ALWAYSSTATUS, db_get_b(NULL, "CList", "AlwaysStatus", SETTING_ALWAYSSTATUS_DEFAULT) ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_ALWAYSMULTI, !db_get_b(NULL, "CList", "AlwaysMulti", SETTING_ALWAYSMULTI_DEFAULT) ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_DONTCYCLE, db_get_b(NULL, "CList", "TrayIcon", SETTING_TRAYICON_DEFAULT) == SETTING_TRAYICON_SINGLE ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_CYCLE, db_get_b(NULL, "CList", "TrayIcon", SETTING_TRAYICON_DEFAULT) == SETTING_TRAYICON_CYCLE ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_MULTITRAY, db_get_b(NULL, "CList", "TrayIcon", SETTING_TRAYICON_DEFAULT) == SETTING_TRAYICON_MULTI ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_DISABLEBLINK, db_get_b(NULL, "CList", "DisableTrayFlash", 0) == 1 ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwndDlg, IDC_ICONBLINK, db_get_b(NULL, "CList", "NoIconBlink", 0) == 1 ? BST_CHECKED : BST_UNCHECKED);
		if (IsDlgButtonChecked(hwndDlg, IDC_DONTCYCLE)) {
			Utils::enableDlgControl(hwndDlg, IDC_CYCLETIMESPIN, FALSE);
			Utils::enableDlgControl(hwndDlg, IDC_CYCLETIME, FALSE);
			Utils::enableDlgControl(hwndDlg, IDC_ALWAYSMULTI, FALSE);
		}
		if (IsDlgButtonChecked(hwndDlg, IDC_CYCLE)) {
			Utils::enableDlgControl(hwndDlg, IDC_PRIMARYSTATUS, FALSE);
			Utils::enableDlgControl(hwndDlg, IDC_ALWAYSMULTI, FALSE);
		}
		if (IsDlgButtonChecked(hwndDlg, IDC_MULTITRAY)) {
			Utils::enableDlgControl(hwndDlg, IDC_CYCLETIMESPIN, FALSE);
			Utils::enableDlgControl(hwndDlg, IDC_CYCLETIME, FALSE);
			Utils::enableDlgControl(hwndDlg, IDC_PRIMARYSTATUS, FALSE);
		}
		SendDlgItemMessage(hwndDlg, IDC_CYCLETIMESPIN, UDM_SETRANGE, 0, MAKELONG(120, 1));
		SendDlgItemMessage(hwndDlg, IDC_CYCLETIMESPIN, UDM_SETPOS, 0, MAKELONG(db_get_w(NULL, "CList", "CycleTime", SETTING_CYCLETIME_DEFAULT), 0));
		{
			ptrA szPrimaryStatus(db_get_sa(NULL, "CList", "PrimaryStatus"));

			int item = SendDlgItemMessage(hwndDlg, IDC_PRIMARYSTATUS, CB_ADDSTRING, 0, (LPARAM)TranslateT("Global"));
			SendDlgItemMessage(hwndDlg, IDC_PRIMARYSTATUS, CB_SETITEMDATA, item, (LPARAM)0);

			for (auto &pa : Accounts()) {
				if (!pa->IsEnabled() || CallProtoService(pa->szModuleName, PS_GETCAPS, PFLAGNUM_2, 0) == 0)
					continue;

				item = SendDlgItemMessage(hwndDlg, IDC_PRIMARYSTATUS, CB_ADDSTRING, 0, (LPARAM)pa->tszAccountName);
				SendDlgItemMessage(hwndDlg, IDC_PRIMARYSTATUS, CB_SETITEMDATA, item, (LPARAM)pa);
				if (!mir_strcmp(szPrimaryStatus, pa->szModuleName))
					SendDlgItemMessage(hwndDlg, IDC_PRIMARYSTATUS, CB_SETCURSEL, item, 0);
			}
		}
		if (CB_ERR == (int)SendDlgItemMessage(hwndDlg, IDC_PRIMARYSTATUS, CB_GETCURSEL, 0, 0))
			SendDlgItemMessage(hwndDlg, IDC_PRIMARYSTATUS, CB_SETCURSEL, 0, 0);
		SendDlgItemMessage(hwndDlg, IDC_BLINKSPIN, UDM_SETBUDDY, (WPARAM)GetDlgItem(hwndDlg, IDC_BLINKTIME), 0);
		SendDlgItemMessage(hwndDlg, IDC_BLINKSPIN, UDM_SETRANGE, 0, MAKELONG(0x3FFF, 250));
		SendDlgItemMessage(hwndDlg, IDC_BLINKSPIN, UDM_SETPOS, 0, MAKELONG(db_get_w(NULL, "CList", "IconFlashTime", 550), 0));
		CheckDlgButton(hwndDlg, IDC_NOTRAYINFOTIPS, cfg::dat.bNoTrayTips ? 1 : 0);
		CheckDlgButton(hwndDlg, IDC_APPLYLASTVIEWMODE, db_get_b(NULL, "CList", "AutoApplyLastViewMode", 0) ? BST_CHECKED : BST_UNCHECKED);
		return TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_DONTCYCLE || LOWORD(wParam) == IDC_CYCLE || LOWORD(wParam) == IDC_MULTITRAY) {
			Utils::enableDlgControl(hwndDlg, IDC_PRIMARYSTATUS, IsDlgButtonChecked(hwndDlg, IDC_DONTCYCLE));
			Utils::enableDlgControl(hwndDlg, IDC_CYCLETIME, IsDlgButtonChecked(hwndDlg, IDC_CYCLE));
			Utils::enableDlgControl(hwndDlg, IDC_CYCLETIMESPIN, IsDlgButtonChecked(hwndDlg, IDC_CYCLE));
			Utils::enableDlgControl(hwndDlg, IDC_ALWAYSMULTI, IsDlgButtonChecked(hwndDlg, IDC_MULTITRAY));
		}
		if (LOWORD(wParam) == IDC_CYCLETIME && HIWORD(wParam) != EN_CHANGE)
			break;
		if (LOWORD(wParam) == IDC_PRIMARYSTATUS && HIWORD(wParam) != CBN_SELCHANGE)
			break;
		if (LOWORD(wParam) == IDC_CYCLETIME && (HIWORD(wParam) != EN_CHANGE || (HWND)lParam != GetFocus()))
			return 0;
		if (LOWORD(wParam) == IDC_BLINKTIME && (HIWORD(wParam) != EN_CHANGE || (HWND)lParam != GetFocus()))
			return 0;
		SendMessage(GetParent(hwndDlg), PSM_CHANGED, 0, 0);
		opt_gen_opts_changed = TRUE;
		break;

	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->idFrom) {
		case 0:
			switch (((LPNMHDR)lParam)->code) {
			case PSN_APPLY:
				if (!opt_gen_opts_changed)
					return TRUE;

				db_set_b(NULL, "CList", "HideOffline", (BYTE)IsDlgButtonChecked(hwndDlg, IDC_HIDEOFFLINE));
				db_set_b(NULL, "CList", "HideEmptyGroups", (BYTE)IsDlgButtonChecked(hwndDlg, IDC_HIDEEMPTYGROUPS));
				db_set_b(NULL, "CList", "UseGroups", (BYTE)BST_UNCHECKED == IsDlgButtonChecked(hwndDlg, IDC_DISABLEGROUPS));
				db_set_b(NULL, "CList", "ConfirmDelete", (BYTE)IsDlgButtonChecked(hwndDlg, IDC_CONFIRMDELETE));
				db_set_b(NULL, "CList", "Tray1Click", (BYTE)IsDlgButtonChecked(hwndDlg, IDC_ONECLK));
				db_set_b(NULL, "CList", "AlwaysStatus", (BYTE)IsDlgButtonChecked(hwndDlg, IDC_ALWAYSSTATUS));
				db_set_b(NULL, "CList", "AlwaysMulti", (BYTE)BST_UNCHECKED == IsDlgButtonChecked(hwndDlg, IDC_ALWAYSMULTI));
				db_set_b(NULL, "CList", "TrayIcon", (BYTE)(IsDlgButtonChecked(hwndDlg, IDC_DONTCYCLE) ? SETTING_TRAYICON_SINGLE : (IsDlgButtonChecked(hwndDlg, IDC_CYCLE) ? SETTING_TRAYICON_CYCLE : SETTING_TRAYICON_MULTI)));
				db_set_w(NULL, "CList", "CycleTime", (WORD)SendDlgItemMessage(hwndDlg, IDC_CYCLETIMESPIN, UDM_GETPOS, 0, 0));
				db_set_w(NULL, "CList", "IconFlashTime", (WORD)SendDlgItemMessage(hwndDlg, IDC_BLINKSPIN, UDM_GETPOS, 0, 0));
				db_set_b(NULL, "CList", "DisableTrayFlash", (BYTE)IsDlgButtonChecked(hwndDlg, IDC_DISABLEBLINK));
				db_set_b(NULL, "CList", "NoIconBlink", (BYTE)IsDlgButtonChecked(hwndDlg, IDC_ICONBLINK));
				db_set_b(NULL, "CList", "AutoApplyLastViewMode", (BYTE)IsDlgButtonChecked(hwndDlg, IDC_APPLYLASTVIEWMODE));

				__setFlag(CLUI_FRAME_EVENTAREASUNKEN, IsDlgButtonChecked(hwndDlg, IDC_EVENTAREASUNKEN));
				__setFlag(CLUI_FRAME_AUTOHIDENOTIFY, IsDlgButtonChecked(hwndDlg, IDC_EVENTAREAAUTOHIDE));

				__setFlag(CLUI_FRAME_SHOWBOTTOMBUTTONS, IsDlgButtonChecked(hwndDlg, IDC_SHOWBOTTOMBUTTONS));
				__setFlag(CLUI_FRAME_CLISTSUNKEN, IsDlgButtonChecked(hwndDlg, IDC_CLISTSUNKEN));

				cfg::dat.bNoTrayTips = IsDlgButtonChecked(hwndDlg, IDC_NOTRAYINFOTIPS) ? 1 : 0;
				db_set_b(NULL, "CList", "NoTrayTips", (BYTE)cfg::dat.bNoTrayTips);
				{
					int cursel = SendDlgItemMessage(hwndDlg, IDC_PRIMARYSTATUS, CB_GETCURSEL, 0, 0);
					PROTOACCOUNT *pa = (PROTOACCOUNT *)SendDlgItemMessage(hwndDlg, IDC_PRIMARYSTATUS, CB_GETITEMDATA, cursel, 0);
					if (!pa)
						db_unset(NULL, "CList", "PrimaryStatus");
					else
						db_set_s(NULL, "CList", "PrimaryStatus", pa->szModuleName);
				}
				Clist_TrayIconIconsChanged();
				db_set_dw(NULL, "CLUI", "Frameflags", cfg::dat.dwFlags);
				ConfigureFrame();
				ConfigureCLUIGeometry(1);
				ConfigureEventArea();
				HideShowNotifyFrame();
				SendMessage(pcli->hwndContactTree, WM_SIZE, 0, 0);
				SendMessage(pcli->hwndContactList, WM_SIZE, 0, 0);
				Clist_LoadContactTree(); /* this won't do job properly since it only really works when changes happen */
				Clist_Broadcast(CLM_AUTOREBUILD, 0, 0);
				PostMessage(pcli->hwndContactList, CLUIINTM_REDRAW, 0, 0);

				opt_gen_opts_changed = 0;
				return TRUE;
			}
			break;
		}
		break;
	}
	return FALSE;
}

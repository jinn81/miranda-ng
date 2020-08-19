/*
Copyright (C) 2005 Ricardo Pescuma Domenecci

This is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with this file; see the file license.txt.  If
not, write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.
*/

#include "stdafx.h"

CDlgBase *pSetNickDialog = nullptr, *pSetStatusMessageDialog = nullptr;

// Set nickname ///////////////////////////////////////////////////////////////////////////////////

class CSetNickDialog : public CDlgBase
{
	int m_protonum;

	CCtrlEdit m_edtNickname;

public:
	CSetNickDialog(int protoparam) :
		CDlgBase(g_plugin, IDD_SETNICKNAME),
		m_edtNickname(this, IDC_NICKNAME)
	{
		m_protonum = protoparam;
	}

	bool OnInitDialog() override
	{
		m_edtNickname.SendMsg(EM_LIMITTEXT, MS_MYDETAILS_GETMYNICKNAME_BUFFER_SIZE - 1, 0);

		if (m_protonum == -1) {
			Window_SetSkinIcon_IcoLib(m_hwnd, SKINICON_OTHER_MIRANDA);

			// All protos have the same nick?
			if (protocols->GetSize() > 0) {
				wchar_t *nick = protocols->Get(0)->nickname;

				bool foundDefNick = true;
				for (int i = 1; foundDefNick && i < protocols->GetSize(); i++) {
					if (mir_wstrcmpi(protocols->Get(i)->nickname, nick) != 0) {
						foundDefNick = false;
						break;
					}
				}

				if (foundDefNick)
					if (mir_wstrcmpi(protocols->default_nick, nick) != 0)
						mir_wstrcpy(protocols->default_nick, nick);
			}

			m_edtNickname.SetText(protocols->default_nick);
			m_edtNickname.SendMsg(EM_LIMITTEXT, MS_MYDETAILS_GETMYNICKNAME_BUFFER_SIZE, 0);
		}
		else {
			Protocol *proto = protocols->Get(m_protonum);

			wchar_t tmp[128];
			mir_snwprintf(tmp, TranslateT("Set my nickname for %s"), proto->description);

			SetWindowText(m_hwnd, tmp);

			HICON hIcon = (HICON)CallProtoService(proto->name, PS_LOADICON, PLI_PROTOCOL, 0);
			if (hIcon != nullptr) {
				SendMessage(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
				DestroyIcon(hIcon);
			}

			m_edtNickname.SetText(proto->nickname);
			m_edtNickname.SendMsg(EM_LIMITTEXT, min(MS_MYDETAILS_GETMYNICKNAME_BUFFER_SIZE, proto->GetNickMaxLength()), 0);
		}
		return true;
	}

	bool OnApply() override
	{
		wchar_t tmp[MS_MYDETAILS_GETMYNICKNAME_BUFFER_SIZE];
		m_edtNickname.GetText(tmp, _countof(tmp));

		if (m_protonum == -1)
			protocols->SetNicks(tmp);
		else
			protocols->Get(m_protonum)->SetNick(tmp);
		return true;
	}

	void OnDestroy() override
	{
		if (pSetNickDialog == this)
			pSetNickDialog = nullptr;
	}
};

INT_PTR PluginCommand_SetMyNicknameUI(WPARAM, LPARAM lParam)
{
	char *proto = (char *)lParam;
	int proto_num = -1;

	if (proto != nullptr) {
		int i;
		for (i = 0; i < protocols->GetSize(); i++) {
			if (mir_strcmpi(protocols->Get(i)->name, proto) == 0) {
				proto_num = i;
				break;
			}
		}

		if (proto_num == -1)
			return -1;

		if (!protocols->Get(i)->CanSetNick())
			return -2;

	}

	if (pSetNickDialog == nullptr) {
		pSetNickDialog = new CSetNickDialog(proto_num);
		pSetNickDialog->Show();
	}
	else {
		SetForegroundWindow(pSetNickDialog->GetHwnd());
		SetFocus(pSetNickDialog->GetHwnd());
	}

	return 0;
}

INT_PTR PluginCommand_SetMyNickname(WPARAM wParam, LPARAM lParam)
{
	char *proto = (char *)wParam;
	if (proto != nullptr) {
		for (int i = 0; i < protocols->GetSize(); i++) {
			if (mir_strcmpi(protocols->Get(i)->name, proto) == 0) {
				if (!protocols->Get(i)->CanSetNick())
					return -2;

				protocols->Get(i)->SetNick((wchar_t *)lParam);
				return 0;
			}
		}

		return -1;
	}

	protocols->SetNicks((wchar_t *)lParam);
	return 0;
}

INT_PTR PluginCommand_GetMyNickname(WPARAM wParam, LPARAM lParam)
{
	wchar_t *ret = (wchar_t *)lParam;
	if (ret == nullptr)
		return -1;

	char *proto = (char *)wParam;
	if (proto == nullptr) {
		mir_wstrncpy(ret, protocols->default_nick, MS_MYDETAILS_GETMYNICKNAME_BUFFER_SIZE);
		return 0;
	}
	else {
		Protocol *protocol = protocols->Get(proto);
		if (protocol != nullptr) {
			mir_wstrncpy(ret, protocol->nickname, MS_MYDETAILS_GETMYNICKNAME_BUFFER_SIZE);
			return 0;
		}

		return -1;
	}
}

// Set avatar /////////////////////////////////////////////////////////////////////////////////////

INT_PTR PluginCommand_SetMyAvatarUI(WPARAM, LPARAM lParam)
{
	char *proto = (char *)lParam;
	int proto_num = -1;

	if (proto != nullptr) {
		int i;
		for (i = 0; i < protocols->GetSize(); i++) {
			if (mir_strcmpi(protocols->Get(i)->name, proto) == 0) {
				proto_num = i;
				break;
			}
		}

		if (proto_num == -1)
			return -1;

		if (!protocols->Get(i)->CanSetAvatar())
			return -2;
	}

	if (proto_num == -1)
		protocols->SetAvatars(nullptr);
	else
		protocols->Get(proto_num)->SetAvatar(nullptr);

	return 0;
}

INT_PTR PluginCommand_SetMyAvatar(WPARAM wParam, LPARAM lParam)
{
	char *proto = (char *)wParam;
	if (proto != nullptr) {
		for (int i = 0; i < protocols->GetSize(); i++) {
			if (mir_strcmpi(protocols->Get(i)->name, proto) == 0) {
				if (!protocols->Get(i)->CanSetAvatar())
					return -2;

				protocols->Get(i)->SetAvatar((wchar_t *)lParam);
				return 0;
			}
		}

		return -1;
	}

	protocols->SetAvatars((wchar_t *)lParam);
	return 0;
}

INT_PTR PluginCommand_GetMyAvatar(WPARAM wParam, LPARAM lParam)
{
	wchar_t *ret = (wchar_t *)lParam;
	char *proto = (char *)wParam;

	if (ret == nullptr)
		return -1;

	if (proto == nullptr) {
		mir_wstrncpy(ret, protocols->default_avatar_file, MS_MYDETAILS_GETMYAVATAR_BUFFER_SIZE);
		return 0;
	}

	for (int i = 0; i < protocols->GetSize(); i++) {
		if (mir_strcmpi(protocols->Get(i)->name, proto) == 0) {
			if (!protocols->Get(i)->CanGetAvatar())
				return -2;

			protocols->Get(i)->GetAvatar();

			if (mir_wstrlen(protocols->Get(i)->avatar_file))
				mir_wstrncpy(ret, protocols->Get(i)->avatar_file, MS_MYDETAILS_GETMYAVATAR_BUFFER_SIZE);
			else
				ret[0] = '\0';

			return 0;
		}
	}

	return -1;
}

static LRESULT CALLBACK StatusMsgEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CHAR:
		if (wParam == 0x0a && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
			PostMessage(GetParent(hwnd), WM_COMMAND, IDOK, 0);
			return 0;
		}
		break;
	}

	return mir_callNextSubclass(hwnd, StatusMsgEditSubclassProc, msg, wParam, lParam);
}

struct SetStatusMessageData
{
	int status;
	int proto_num;
};

class CSetStatusMessageDialog : public CDlgBase
{
	SetStatusMessageData *m_data;

	CCtrlEdit m_edtStatusMessage;

public:
	CSetStatusMessageDialog(int protoparam, int statusparam) :
		CDlgBase(g_plugin, IDD_SETSTATUSMESSAGE),
		m_edtStatusMessage(this, IDC_STATUSMESSAGE)
	{
		m_data = (SetStatusMessageData *)mir_alloc(sizeof(SetStatusMessageData));
		m_data->proto_num = protoparam;
		m_data->status = statusparam;
	}

	bool OnInitDialog() override
	{
		m_edtStatusMessage.SendMsg(EM_LIMITTEXT, MS_MYDETAILS_GETMYSTATUSMESSAGE_BUFFER_SIZE - 1, 0);
		mir_subclassWindow(m_edtStatusMessage.GetHwnd(), StatusMsgEditSubclassProc);

		if (m_data->proto_num >= 0) {
			Protocol *proto = protocols->Get(m_data->proto_num);

			HICON hIcon = (HICON)CallProtoService(proto->name, PS_LOADICON, PLI_PROTOCOL, 0);
			if (hIcon != nullptr) {
				SendMessage(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
				DestroyIcon(hIcon);
			}

			wchar_t title[256];
			mir_snwprintf(title, TranslateT("Set my status message for %s"), proto->description);
			SetWindowText(m_hwnd, title);

			m_edtStatusMessage.SetText(proto->GetStatusMsg());
		}
		else if (m_data->status != 0) {
			Window_SetProtoIcon_IcoLib(m_hwnd, nullptr, m_data->status);

			wchar_t title[256];
			mir_snwprintf(title, TranslateT("Set my status message for %s"), Clist_GetStatusModeDescription(m_data->status, 0));
			SetWindowText(m_hwnd, title);

			m_edtStatusMessage.SetText(protocols->GetDefaultStatusMsg(m_data->status));
		}
		else {
			Window_SetSkinIcon_IcoLib(m_hwnd, SKINICON_OTHER_MIRANDA);

			m_edtStatusMessage.SetText(protocols->GetDefaultStatusMsg());
		}
		return true;
	}

	bool OnApply() override
	{
		wchar_t tmp[MS_MYDETAILS_GETMYSTATUSMESSAGE_BUFFER_SIZE];
		m_edtStatusMessage.GetText(tmp, _countof(tmp));

		if (m_data->proto_num >= 0)
			protocols->Get(m_data->proto_num)->SetStatusMsg(tmp);
		else if (m_data->status == 0)
			protocols->SetStatusMsgs(tmp);
		else
			protocols->SetStatusMsgs(m_data->status, tmp);
		return true;
	}

	void OnDestroy() override
	{
		mir_free(m_data);
		Window_FreeIcon_IcoLib(m_hwnd);
		if (pSetStatusMessageDialog == this)
			pSetStatusMessageDialog = nullptr;
	}
};

INT_PTR PluginCommand_SetMyStatusMessageUI(WPARAM wParam, LPARAM lParam)
{
	int status = (int)wParam;
	char *proto_name = (char *)lParam;
	int proto_num = -1;
	Protocol *proto = nullptr;

	if (status != 0 && (status < ID_STATUS_OFFLINE || status > ID_STATUS_MAX))
		return -10;

	if (proto_name != nullptr) {
		for (int i = 0; i < protocols->GetSize(); i++) {
			proto = protocols->Get(i);

			if (mir_strcmpi(proto->name, proto_name) == 0) {
				proto_num = i;
				break;
			}
		}

		if (proto_num == -1)
			return -1;

		if (protocols->CanSetStatusMsgPerProtocol() && !proto->CanSetStatusMsg())
			return -2;
	}
	else if (ServiceExists(MS_SIMPLESTATUSMSG_CHANGESTATUSMSG)) {
		if (status != 0)
			CallService(MS_SIMPLESTATUSMSG_CHANGESTATUSMSG, status, (LPARAM)proto_name);
		else if (proto != nullptr)
			CallService(MS_SIMPLESTATUSMSG_CHANGESTATUSMSG, proto->status, (LPARAM)proto_name);
		else
			CallService(MS_SIMPLESTATUSMSG_CHANGESTATUSMSG, protocols->GetGlobalStatus(), NULL);
		return 0;
	} // fallthrough

	if (proto == nullptr || proto->status != ID_STATUS_OFFLINE) {
		if (pSetStatusMessageDialog == nullptr) {
			pSetStatusMessageDialog = new CSetStatusMessageDialog(proto_num, status);
			pSetStatusMessageDialog->Show();
		}
		else {
			SetForegroundWindow(pSetStatusMessageDialog->GetHwnd());
			SetFocus(pSetStatusMessageDialog->GetHwnd());
		}
		return 0;
	}

	return -3;
}

INT_PTR PluginCommand_CycleThroughtProtocols(WPARAM wParam, LPARAM)
{
	db_set_b(0, "MyDetails", "CicleThroughtProtocols", (BYTE)wParam);

	LoadOptions();

	return 0;
}

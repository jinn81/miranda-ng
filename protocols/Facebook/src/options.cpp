/*

Facebook plugin for Miranda NG
Copyright © 2019-20 Miranda NG team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "stdafx.h"

class CFacebookAccOptsDlg : public CFBDlgBase
{
	CCtrlEdit edtGroup;
	CCtrlCheck chkEnableChats, chkHideChats, chkKeepUnread;

public:
	CFacebookAccOptsDlg(FacebookProto *pThis) :
		CFBDlgBase(pThis, IDD_OPTIONS),
		edtGroup(this, IDC_GROUP),
		chkHideChats(this, IDC_HIDECHATS),
		chkKeepUnread(this, IDC_KEEP_UNREAD),
		chkEnableChats(this, IDC_ENABLECHATS)
	{
		CreateLink(edtGroup, pThis->m_wszDefaultGroup);
		CreateLink(chkHideChats, pThis->m_bUseGroupchats);
		CreateLink(chkKeepUnread, pThis->m_bKeepUnread);
		CreateLink(chkEnableChats, pThis->m_bHideGroupchats);
	}

	bool OnInitDialog() override
	{
		ptrA login(m_proto->getStringA(DBKEY_LOGIN));
		if (login != nullptr)
			SetDlgItemTextA(m_hwnd, IDC_UN, login);

		ptrA password(m_proto->getStringA(DBKEY_PASS));
		if (password != nullptr)
			SetDlgItemTextA(m_hwnd, IDC_PW, password);
		return true;
	}

	bool OnApply() override
	{
		char str[128];

		GetDlgItemTextA(m_hwnd, IDC_UN, str, _countof(str));
		m_proto->setString(DBKEY_LOGIN, str);

		GetDlgItemTextA(m_hwnd, IDC_PW, str, _countof(str));
		m_proto->setString(DBKEY_PASS, str);
		return true;
	}
};

int FacebookProto::OnOptionsInit(WPARAM wParam, LPARAM)
{
	OPTIONSDIALOGPAGE odp = {};
	odp.position = -790000000;
	odp.szTitle.w = m_tszUserName;
	odp.szGroup.w = LPGENW("Network");
	odp.flags = ODPF_UNICODE;

	odp.szTab.w = LPGENW("Account");
	odp.pDialog = new CFacebookAccOptsDlg(this);
	g_plugin.addOptions(wParam, &odp);
	return 0;
}
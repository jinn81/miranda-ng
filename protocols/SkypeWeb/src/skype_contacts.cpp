/*
Copyright (c) 2015-20 Miranda NG team (https://miranda-ng.org)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "stdafx.h"

WORD CSkypeProto::GetContactStatus(MCONTACT hContact)
{
	return getWord(hContact, "Status", ID_STATUS_OFFLINE);
}

void CSkypeProto::SetContactStatus(MCONTACT hContact, WORD status)
{
	WORD oldStatus = GetContactStatus(hContact);
	if (oldStatus != status) {
		setWord(hContact, "Status", status);
		if (status == ID_STATUS_OFFLINE)
			db_unset(hContact, m_szModuleName, "MirVer");
	}
}

void CSkypeProto::SetChatStatus(MCONTACT hContact, int iStatus)
{
	ptrW tszChatID(getWStringA(hContact, "ChatRoomID"));
	if (tszChatID != NULL)
		Chat_Control(m_szModuleName, tszChatID, (iStatus == ID_STATUS_OFFLINE) ? SESSION_OFFLINE : SESSION_ONLINE);
}

MCONTACT CSkypeProto::GetContactFromAuthEvent(MEVENT hEvent)
{
	DWORD body[3];
	DBEVENTINFO dbei = {};
	dbei.cbBlob = sizeof(DWORD) * 2;
	dbei.pBlob = (PBYTE)&body;

	if (db_event_get(hEvent, &dbei))
		return INVALID_CONTACT_ID;

	if (dbei.eventType != EVENTTYPE_AUTHREQUEST)
		return INVALID_CONTACT_ID;

	if (mir_strcmp(dbei.szModule, m_szModuleName) != 0)
		return INVALID_CONTACT_ID;
	return DbGetAuthEventContact(&dbei);
}

MCONTACT CSkypeProto::FindContact(const char *skypename)
{
	for (auto &hContact : AccContacts())
		if (!mir_strcmpi(skypename, getId(hContact)))
			return hContact;

	return 0;
}

MCONTACT CSkypeProto::AddContact(const char *skypename, bool isTemporary)
{
	MCONTACT hContact = FindContact(skypename);

	if (!hContact) {
		hContact = db_add_contact();
		Proto_AddToContact(hContact, m_szModuleName);

		setString(hContact, SKYPE_SETTINGS_ID, skypename);

		if (m_opts.wstrCListGroup) {
			Clist_GroupCreate(0, m_opts.wstrCListGroup);
			Clist_SetGroup(hContact, m_opts.wstrCListGroup);
		}

		setByte(hContact, "Auth", 1);
		setByte(hContact, "Grant", 1);

		if (isTemporary)
			Contact_RemoveFromList(hContact);
	}
	return hContact;
}

void CSkypeProto::LoadContactsAuth(NETLIBHTTPREQUEST *response, AsyncHttpRequest*)
{
	JsonReply reply(response);
	if (reply.error())
		return;

	auto &root = reply.data();
	for (auto &item : root["invite_list"]) {
		std::string skypename = item["mri"].as_string().erase(0, 2);
		std::string reason = item["greeting"].as_string();

		time_t eventTime = IsoToUnixTime(item["invites"][json_index_t(0)].as_string().c_str());

		MCONTACT hContact = AddContact(skypename.c_str());
		if (hContact) {
			time_t lastEventTime = db_get_dw(hContact, m_szModuleName, "LastAuthRequestTime", 0);

			if (lastEventTime < eventTime) {
				db_set_dw(hContact, m_szModuleName, "LastAuthRequestTime", eventTime);
				delSetting(hContact, "Auth");

				DB::AUTH_BLOB blob(hContact, nullptr, nullptr, nullptr, skypename.c_str(), reason.c_str());

				PROTORECVEVENT pre = { 0 };
				pre.timestamp = time(0);
				pre.lParam = blob.size();
				pre.szMessage = blob;

				ProtoChainRecv(hContact, PSR_AUTH, 0, (LPARAM)&pre);
			}
		}
	}
}

//[{"skypename":"echo123", "authorized" : true, "blocked" : false, ...},...]
// other properties is exists but empty

void CSkypeProto::LoadContactList(NETLIBHTTPREQUEST *response, AsyncHttpRequest*)
{
	JsonReply reply(response);
	if (reply.error())
		return;

	bool loadAll = getBool("LoadAllContacts", false);

	auto &root = reply.data();
	for (auto &item : root["contacts"]) {
		const JSONNode &name = item["name"];

		std::string skypename = item["id"].as_string();
		CMStringW display_name = item["display_name"].as_mstring();
		CMStringW first_name = name["first"].as_mstring();
		CMStringW last_name = name["surname"].as_mstring();
		CMStringW avatar_url = item["avatar_url"].as_mstring();
		std::string type = item["type"].as_string();

		if (type == "skype" || loadAll) {
			MCONTACT hContact = AddContact(skypename.c_str());
			if (hContact) {
				if (item["authorized"].as_bool()) {
					delSetting(hContact, "Auth");
					delSetting(hContact, "Grant");
				}
				else setByte(hContact, "Grant", 1);

				if (item["blocked"].as_bool())
					setByte(hContact, "IsBlocked", 1);
				else
					delSetting(hContact, "IsBlocked");

				ptrW wszGroup(Clist_GetGroup(hContact));
				if (wszGroup == nullptr) {
					if (m_opts.wstrCListGroup) {
						Clist_GroupCreate(0, m_opts.wstrCListGroup);
						Clist_SetGroup(hContact, m_opts.wstrCListGroup);
					}
				}

				setString(hContact, "Type", type.c_str());

				if (display_name)
					setWString(hContact, "Nick", display_name);
				if (first_name)
					setWString(hContact, "FirstName", first_name);
				if (last_name)
					setWString(hContact, "LastName", last_name);

				if (item["mood"])
					db_set_utf(hContact, "CList", "StatusMsg", RemoveHtml(item["mood"].as_string()).c_str());

				SetAvatarUrl(hContact, avatar_url);
				ReloadAvatarInfo(hContact);

				for (auto &phone : item["phones"]) {
					CMStringW number = phone["number"].as_mstring();

					switch (phone["type"].as_int()) {
					case 0:
						setWString(hContact, "Phone", number);
						break;
					case 2:
						setWString(hContact, "Cellular", number);
						break;
					}
				}
			}
		}
	}

	PushRequest(new GetContactsAuthRequest());
}

INT_PTR CSkypeProto::OnRequestAuth(WPARAM hContact, LPARAM)
{
	if (hContact == INVALID_CONTACT_ID)
		return 1;

	PushRequest(new AddContactRequest(getId(hContact)));
	return 0;
}

INT_PTR CSkypeProto::OnGrantAuth(WPARAM hContact, LPARAM)
{
	if (hContact == INVALID_CONTACT_ID)
		return 1;

	PushRequest(new AuthAcceptRequest(getId(hContact)));
	return 0;
}

void CSkypeProto::OnContactDeleted(MCONTACT hContact)
{
	if (IsOnline())
		if (hContact && !isChatRoom(hContact))
			PushRequest(new DeleteContactRequest(getId(hContact)));
}

INT_PTR CSkypeProto::BlockContact(WPARAM hContact, LPARAM)
{
	if (!IsOnline()) return 1;

	if (IDYES == MessageBox(NULL, TranslateT("Are you sure?"), TranslateT("Warning"), MB_YESNO | MB_ICONQUESTION))
		PushRequest(new BlockContactRequest(this, hContact));
	return 0;
}

void CSkypeProto::OnBlockContact(NETLIBHTTPREQUEST *response, AsyncHttpRequest *pRequest)
{
	MCONTACT hContact = (DWORD_PTR)pRequest->pUserInfo;
	if (response != nullptr)
		Contact_Hide(hContact);
}

INT_PTR CSkypeProto::UnblockContact(WPARAM hContact, LPARAM)
{
	PushRequest(new UnblockContactRequest(this, hContact));
	return 0;
}

void CSkypeProto::OnUnblockContact(NETLIBHTTPREQUEST *response, AsyncHttpRequest *pRequest)
{
	if (response == nullptr)
		return;

	MCONTACT hContact = (DWORD_PTR)pRequest->pUserInfo;
	Contact_Hide(hContact, false);
	delSetting(hContact, "IsBlocked");
}

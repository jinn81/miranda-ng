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

void CSkypeProto::InitGroupChatModule()
{
	GCREGISTER gcr = {};
	gcr.iMaxText = 0;
	gcr.ptszDispName = m_tszUserName;
	gcr.pszModule = m_szModuleName;
	Chat_Register(&gcr);

	HookProtoEvent(ME_GC_EVENT, &CSkypeProto::OnGroupChatEventHook);
	HookProtoEvent(ME_GC_BUILDMENU, &CSkypeProto::OnGroupChatMenuHook);

	CreateProtoService(PS_JOINCHAT, &CSkypeProto::OnJoinChatRoom);
	CreateProtoService(PS_LEAVECHAT, &CSkypeProto::OnLeaveChatRoom);
}

MCONTACT CSkypeProto::FindChatRoom(const char *chatname)
{
	SESSION_INFO *si = g_chatApi.SM_FindSession(_A2T(chatname), m_szModuleName);
	return si ? si->hContact : 0;
}

void CSkypeProto::StartChatRoom(const wchar_t *tid, const wchar_t *tname)
{
	// Create the group chat session
	SESSION_INFO *si = Chat_NewSession(GCW_CHATROOM, m_szModuleName, tid, tname);
	if (!si)
		return;

	// Create a user statuses
	Chat_AddGroup(si, TranslateT("Admin"));
	Chat_AddGroup(si, TranslateT("User"));

	// Finish initialization
	Chat_Control(m_szModuleName, tid, (getBool("HideChats", 1) ? WINDOW_HIDDEN : SESSION_INITDONE));
	Chat_Control(m_szModuleName, tid, SESSION_ONLINE);
}

void CSkypeProto::OnLoadChats(NETLIBHTTPREQUEST *response, AsyncHttpRequest*)
{
	JsonReply reply(response);
	if (reply.error())
		return;

	auto &root = reply.data();
	const JSONNode &metadata = root["_metadata"];
	const JSONNode &conversations = root["conversations"].as_array();

	int totalCount = metadata["totalCount"].as_int();
	std::string syncState = metadata["syncState"].as_string();

	if (totalCount >= 99 || conversations.size() >= 99)
		ReadHistoryRest(syncState.c_str());

	for (auto &conversation : conversations) {
		if (!conversation["lastMessage"])
			continue;

		const JSONNode &id = conversation["id"];
		const JSONNode &threadProperties = conversation["threadProperties"];
		CMStringW topic(threadProperties["topic"].as_mstring());
		SendRequest(new GetChatInfoRequest(id.as_string().c_str(), topic));
	}
}

/* Hooks */

int CSkypeProto::OnGroupChatEventHook(WPARAM, LPARAM lParam)
{
	GCHOOK *gch = (GCHOOK*)lParam;
	if (!gch)
		return 0;

	if (mir_strcmp(gch->si->pszModule, m_szModuleName) != 0)
		return 0;

	T2Utf chat_id(gch->si->ptszID), user_id(gch->ptszUID);

	switch (gch->iType) {
	case GC_USER_MESSAGE:
		OnSendChatMessage(chat_id, gch->ptszText);
		break;

	case GC_USER_PRIVMESS:
		{
			MCONTACT hContact = FindContact(user_id);
			if (hContact == NULL) {
				hContact = AddContact(user_id, true);
				setWord(hContact, "Status", ID_STATUS_ONLINE);
				Contact_Hide(hContact);
				setWString(hContact, "Nick", gch->ptszUID);
			}
			CallService(MS_MSG_SENDMESSAGEW, hContact, 0);
		}
		break;

	case GC_USER_LOGMENU:
		switch (gch->dwData) {
		case 10:
			{
				CSkypeInviteDlg dlg(this);
				if (dlg.DoModal())
					if (dlg.m_hContact != NULL)
						PushRequest(new InviteUserToChatRequest(chat_id, getId(dlg.m_hContact), "User"));
			}
			break;

		case 20:
			OnLeaveChatRoom(FindChatRoom(chat_id), NULL);
			break;

		case 30:
			CMStringW newTopic = ChangeTopicForm();
			if (!newTopic.IsEmpty())
				PushRequest(new SetChatPropertiesRequest(chat_id, "topic", T2Utf(newTopic.GetBuffer())));
			break;
		}
		break;

	case GC_USER_NICKLISTMENU:
		switch (gch->dwData) {
		case 10:
			PushRequest(new KickUserRequest(chat_id, user_id));
			break;
		case 30:
			PushRequest(new InviteUserToChatRequest(chat_id, user_id, "Admin"));
			break;
		case 40:
			PushRequest(new InviteUserToChatRequest(chat_id, user_id, "User"));
			break;
		case 50:
			ptrA tnick_old(GetChatContactNick(chat_id, user_id, T2Utf(gch->ptszText)));

			ENTER_STRING pForm = {};
			pForm.type = ESF_COMBO;
			pForm.caption = TranslateT("Enter new nickname");
			pForm.szModuleName = m_szModuleName;
			pForm.szDataPrefix = "renamenick_";

			if (EnterString(&pForm)) {
				MCONTACT hChatContact = FindChatRoom(chat_id);
				if (hChatContact == NULL)
					break; // This probably shouldn't happen, but if chat is NULL for some reason, do nothing

				ptrA tnick_new(mir_utf8encodeW(pForm.ptszResult));
				bool reset = mir_strlen(tnick_new) == 0;
				if (reset) {
					// User fill blank name, which means we reset the custom nick
					db_unset(hChatContact, "UsersNicks", user_id);
					tnick_new = GetChatContactNick(chat_id, user_id, _T2A(gch->ptszText));
				}

				if (!mir_strcmp(tnick_old, tnick_new))
					break; // New nick is same, do nothing

				GCEVENT gce = { m_szModuleName, chat_id, GC_EVENT_NICK };
				gce.dwFlags = GCEF_UTF8 + GCEF_ADDTOLOG;
				gce.pszNick.a = tnick_old;
				gce.bIsMe = IsMe(user_id);
				gce.pszUID.a = user_id;
				gce.pszText.a = tnick_new;
				gce.time = time(0);
				Chat_Event(&gce);

				if (!reset)
					db_set_utf(hChatContact, "UsersNicks", user_id, tnick_new);
			}
			break;
		}
		break;
	}
	return 1;
}

INT_PTR CSkypeProto::OnJoinChatRoom(WPARAM hContact, LPARAM)
{
	if (hContact) {
		ptrW idT(getWStringA(hContact, "ChatRoomID"));
		ptrW nameT(getWStringA(hContact, "Nick"));
		StartChatRoom(idT, nameT != NULL ? nameT : idT);
	}
	return 0;
}

INT_PTR CSkypeProto::OnLeaveChatRoom(WPARAM hContact, LPARAM)
{
	if (!IsOnline())
		return 1;

	if (hContact && IDYES == MessageBox(nullptr, TranslateT("This chat is going to be destroyed forever with all its contents. This action cannot be undone. Are you sure?"), TranslateT("Warning"), MB_YESNO | MB_ICONQUESTION)) {
		ptrW idT(getWStringA(hContact, "ChatRoomID"));
		Chat_Control(m_szModuleName, idT, SESSION_OFFLINE);
		Chat_Terminate(m_szModuleName, idT);

		PushRequest(new KickUserRequest(_T2A(idT), m_szSkypename));

		db_delete_contact(hContact);
	}
	return 0;
}

/* CHAT EVENT */

void CSkypeProto::OnChatEvent(const JSONNode &node)
{
	CMStringA szConversationName(UrlToSkypename(node["conversationLink"].as_string().c_str()));
	CMStringA szFromSkypename(UrlToSkypename(node["from"].as_string().c_str()));

	CMStringW szTopic(node["threadtopic"].as_mstring());

	time_t timestamp = IsoToUnixTime(node["composetime"].as_string().c_str());

	std::string strContent = node["content"].as_string();
	int nEmoteOffset = node["skypeemoteoffset"].as_int();

	if (FindChatRoom(szConversationName) == NULL)
		PushRequest(new GetChatInfoRequest(szConversationName, szTopic));

	std::string messageType = node["messagetype"].as_string();
	if (messageType == "Text" || messageType == "RichText") {
		std::string szClearedContent(messageType == "RichText" ? RemoveHtml(strContent) : strContent);
		AddMessageToChat(szConversationName, szFromSkypename, szClearedContent.c_str(), nEmoteOffset != NULL, nEmoteOffset, timestamp);
	}
	else if (messageType == "ThreadActivity/AddMember") {
		// <addmember><eventtime>1429186229164</eventtime><initiator>8:initiator</initiator><target>8:user</target></addmember>
		TiXmlDocument doc;
		if (0 != doc.Parse(strContent.c_str()))
			return;

		if (auto *pRoot = doc.FirstChildElement("addMember")) {
			CMStringA target = ParseUrl(pRoot->FirstChildElement("target")->Value(), "8:");
			AddChatContact(szConversationName, target, target, "User");
		}
	}
	else if (messageType == "ThreadActivity/DeleteMember") {
		// <deletemember><eventtime>1429186229164</eventtime><initiator>8:initiator</initiator><target>8:user</target></deletemember>
		TiXmlDocument doc;
		if (0 != doc.Parse(strContent.c_str()))
			return;

		if (auto *pRoot = doc.FirstChildElement("deletemember")) {
			CMStringA target = ParseUrl(pRoot->FirstChildElement("target")->Value(), "8:");
			CMStringA initiator = ParseUrl(pRoot->FirstChildElement("initiator")->Value(), "8:");
			RemoveChatContact(szConversationName, target, target, true, initiator);
		}
	}
	else if (messageType == "ThreadActivity/TopicUpdate") {
		// <topicupdate><eventtime>1429532702130</eventtime><initiator>8:user</initiator><value>test topic</value></topicupdate>
		TiXmlDocument doc;
		if (0 != doc.Parse(strContent.c_str()))
			return;

		auto *pRoot = doc.FirstChildElement("topicupdate");
		if (pRoot) {
			CMStringA initiator = ParseUrl(pRoot->FirstChildElement("initiator")->Value(), "8:");
			CMStringA value = pRoot->FirstChildElement("value")->Value();
			RenameChat(szConversationName, value);
			ChangeChatTopic(szConversationName, value, initiator);
		}
	}
	else if (messageType == "ThreadActivity/RoleUpdate") {
		// <roleupdate><eventtime>1429551258363</eventtime><initiator>8:user</initiator><target><id>8:user1</id><role>admin</role></target></roleupdate>
		TiXmlDocument doc;
		if (0 != doc.Parse(strContent.c_str()))
			return;

		auto *pRoot = doc.FirstChildElement("roleupdate");
		if (pRoot) {
			CMStringA initiator = ParseUrl(pRoot->FirstChildElement("initiator")->Value(), "8:");

			auto *pTarget = pRoot->FirstChildElement("target");
			if (pTarget) {
				CMStringA id = ParseUrl(pTarget->FirstChildElement("id")->Value(), "8:");
				const char *role = pTarget->FirstChildElement("role")->Value();

				GCEVENT gce = { m_szModuleName, szConversationName, !mir_strcmpi(role, "Admin") ? GC_EVENT_ADDSTATUS : GC_EVENT_REMOVESTATUS };
				gce.dwFlags = GCEF_ADDTOLOG + GCEF_UTF8;
				gce.pszNick.a = id;
				gce.pszUID.a = id;
				gce.pszText.a = initiator;
				gce.time = time(0);
				gce.bIsMe = IsMe(id);
				gce.pszStatus.a = TranslateU("Admin");
				Chat_Event(&gce);
			}
		}
	}
}

void CSkypeProto::OnSendChatMessage(const char *chat_id, const wchar_t *tszMessage)
{
	if (!IsOnline())
		return;

	wchar_t *buf = NEWWSTR_ALLOCA(tszMessage);
	rtrimw(buf);
	Chat_UnescapeTags(buf);

	ptrA szMessage(mir_utf8encodeW(buf));

	if (strncmp(szMessage, "/me ", 4) == 0)
		PushRequest(new SendChatActionRequest(chat_id, time(0), szMessage));
	else
		PushRequest(new SendChatMessageRequest(chat_id, time(0), szMessage));
}

void CSkypeProto::AddMessageToChat(const char *chat_id, const char *from, const char *content, bool isAction, int emoteOffset, time_t timestamp, bool isLoading)
{
	ptrA tnick(GetChatContactNick(chat_id, from, from));

	GCEVENT gce = { m_szModuleName, chat_id, isAction ? GC_EVENT_ACTION : GC_EVENT_MESSAGE };
	gce.dwFlags = GCEF_UTF8;
	gce.bIsMe = IsMe(from);
	gce.pszNick.a = tnick;
	gce.time = timestamp;
	gce.pszUID.a = from;

	CMStringA szText(content);
	szText.Replace("%", "%%");

	if (!isAction) {
		gce.pszText.a = szText;
		gce.dwFlags |= GCEF_ADDTOLOG;
	}
	else gce.pszText.a = szText.GetBuffer() + emoteOffset;

	if (isLoading)
		gce.dwFlags |= GCEF_NOTNOTIFY;

	Chat_Event(&gce);
}

void CSkypeProto::OnGetChatInfo(NETLIBHTTPREQUEST *response, AsyncHttpRequest *pRequest)
{
	ptrW topic((wchar_t*)pRequest->pUserInfo); // memory must be freed in any case

	JsonReply reply(response);
	if (reply.error())
		return;

	auto &root = reply.data();
	const JSONNode &members = root["members"];
	const JSONNode &properties = root["properties"];
	if (!properties["capabilities"] || properties["capabilities"].empty())
		return;

	CMStringA chatId(UrlToSkypename(root["messages"].as_string().c_str()));
	StartChatRoom(_A2T(chatId), topic);
	for (auto &member : members) {
		CMStringA username(UrlToSkypename(member["userLink"].as_string().c_str()));
		std::string role = member["role"].as_string();
		AddChatContact(chatId, username, username, role.c_str(), true);
	}
	PushRequest(new GetHistoryRequest(chatId, 15, true, 0));
}

void CSkypeProto::RenameChat(const char *chat_id, const char *name)
{
	ptrW tchat_id(mir_a2u(chat_id));
	ptrW tname(mir_utf8decodeW(name));
	Chat_ChangeSessionName(m_szModuleName, tchat_id, tname);
}

void CSkypeProto::ChangeChatTopic(const char *chat_id, const char *topic, const char *initiator)
{
	GCEVENT gce = { m_szModuleName, chat_id, GC_EVENT_TOPIC };
	gce.dwFlags = GCEF_UTF8;
	gce.pszUID.a = initiator;
	gce.pszNick.a = initiator;
	gce.pszText.a = topic;
	Chat_Event(&gce);
}

char* CSkypeProto::GetChatContactNick(const char *chat_id, const char *id, const char *name)
{
	// Check if there is custom nick for this chat contact
	if (chat_id != nullptr) {
		if (char *tname = db_get_utfa(FindChatRoom(chat_id), "UsersNicks", id))
			return tname;
	}

	// Check if we have this contact in database
	if (IsMe(id)) {
		// Return my nick
		if (char *tname = getUStringA("Nick"))
			return tname;
	}
	else {
		MCONTACT hContact = FindContact(id);
		if (hContact != NULL) {
			// Primarily return custom name
			if (char *tname = db_get_utfa(hContact, "CList", "MyHandle"))
				return tname;

			// If not exists, then user nick
			if (char *tname = getUStringA(hContact, "Nick"))
				return tname;
		}
	}

	// Return default value as nick - given name or user id
	if (name != nullptr)
		return mir_strdup(name);
	return mir_strdup(id);
}

void CSkypeProto::AddChatContact(const char *chat_id, const char *id, const char *name, const char *role, bool isChange)
{
	ptrA szNick(GetChatContactNick(chat_id, id, name));

	GCEVENT gce = { m_szModuleName, chat_id, GC_EVENT_JOIN };
	gce.dwFlags = GCEF_UTF8 + GCEF_ADDTOLOG;
	gce.pszNick.a = szNick;
	gce.pszUID.a = id;
	gce.time = !isChange ? time(0) : NULL;
	gce.bIsMe = IsMe(id);
	gce.pszStatus.a = TranslateU(role);
	Chat_Event(&gce);
}

void CSkypeProto::RemoveChatContact(const char *chat_id, const char *id, const char *name, bool isKick, const char *initiator)
{
	if (IsMe(id))
		return;

	ptrA szNick(GetChatContactNick(chat_id, id, name));
	ptrA szInitiator(GetChatContactNick(chat_id, initiator, initiator));
	ptrW tid(mir_a2u(id));

	GCEVENT gce = { m_szModuleName, chat_id, isKick ? GC_EVENT_KICK : GC_EVENT_PART };
	gce.dwFlags = GCEF_UTF8;
	gce.pszNick.a = szNick;
	gce.pszUID.a = id;
	gce.time = time(0);
	if (isKick)
		gce.pszStatus.a = szInitiator;
	else {
		gce.dwFlags += GCEF_ADDTOLOG;
		gce.bIsMe = IsMe(id);
	}

	Chat_Event(&gce);
}

INT_PTR CSkypeProto::SvcCreateChat(WPARAM, LPARAM)
{
	if (IsOnline()) {
		CSkypeGCCreateDlg dlg(this);
		if (dlg.DoModal()) {
			PushRequest(new CreateChatroomRequest(dlg.m_ContactsList, this));
			return 0;
		}
	}
	return 1;
}

/* Menus */

int CSkypeProto::OnGroupChatMenuHook(WPARAM, LPARAM lParam)
{
	GCMENUITEMS *gcmi = (GCMENUITEMS*)lParam;
	if (mir_strcmpi(gcmi->pszModule, m_szModuleName)) return 0;

	if (gcmi->Type == MENU_ON_LOG) {
		static const struct gc_item Items[] =
		{
			{ LPGENW("&Invite user..."),     10, MENU_ITEM, FALSE },
			{ LPGENW("&Leave chat session"), 20, MENU_ITEM, FALSE },
			{ LPGENW("&Change topic..."),    30, MENU_ITEM, FALSE }
		};
		Chat_AddMenuItems(gcmi->hMenu, _countof(Items), Items, &g_plugin);
	}
	else if (gcmi->Type == MENU_ON_NICKLIST) {
		static const struct gc_item Items[] =
		{
			{ LPGENW("Kick &user"),     10, MENU_ITEM      },
			{ nullptr,                     0,  MENU_SEPARATOR },
			{ LPGENW("Set &role"),      20, MENU_NEWPOPUP  },
			{ LPGENW("&Admin"),         30, MENU_POPUPITEM },
			{ LPGENW("&User"),          40, MENU_POPUPITEM },
			{ LPGENW("Change nick..."), 50, MENU_ITEM },
		};
		Chat_AddMenuItems(gcmi->hMenu, _countof(Items), Items, &g_plugin);
	}

	return 0;
}

CMStringW CSkypeProto::ChangeTopicForm()
{
	CMStringW caption(FORMAT, L"[%s] %s", _A2T(m_szModuleName), TranslateT("Enter new chatroom topic"));
	ENTER_STRING pForm = {};
	pForm.type = ESF_MULTILINE;
	pForm.caption = caption;
	pForm.szModuleName = m_szModuleName;
	return (!EnterString(&pForm)) ? CMStringW() : CMStringW(ptrW(pForm.ptszResult));
}

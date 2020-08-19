/*
Copyright © 2016-20 Miranda NG team

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

int compareUsers(const CDiscordUser *p1, const CDiscordUser *p2);

static int compareRoles(const CDiscordRole *p1, const CDiscordRole *p2)
{
	return compareInt64(p1->id, p2->id);
}

static int compareChatUsers(const CDiscordGuildMember *p1, const CDiscordGuildMember *p2)
{
	return compareInt64(p1->userId, p2->userId);
}

CDiscordGuild::CDiscordGuild(SnowFlake _id) :
	id(_id),
	arChannels(10, compareUsers),
	arChatUsers(30, compareChatUsers),
	arRoles(10, compareRoles)
{
}

CDiscordGuild::~CDiscordGuild()
{
}

CDiscordUser::~CDiscordUser()
{
	if (pGuild != nullptr)
		pGuild->arChannels.remove(this);
}

/////////////////////////////////////////////////////////////////////////////////////////
// reads a presence block from json

void CDiscordProto::ProcessPresence(const JSONNode &root)
{
	auto userId = ::getId(root["user"]["id"]);
	CDiscordUser *pUser = FindUser(userId);
	if (pUser == nullptr) {
		debugLogA("Presence from unknown user id %lld ignored", userId);
		return;
	}

	setWord(pUser->hContact, "Status", StrToStatus(root["status"].as_mstring()));

	CheckAvatarChange(pUser->hContact, root["user"]["avatar"].as_mstring());

	for (auto &act : root["activities"]) {
		CMStringW wszStatus(act["state"].as_mstring());
		if (!wszStatus.IsEmpty())
			db_set_ws(pUser->hContact, "CList", "StatusMsg", wszStatus);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// reads a role from json

void CDiscordProto::ProcessRole(CDiscordGuild *guild, const JSONNode &role)
{
	SnowFlake id = ::getId(role["id"]);
	CDiscordRole *p = guild->arRoles.find((CDiscordRole*)&id);
	if (p == nullptr) {
		p = new CDiscordRole();
		p->id = id;
		guild->arRoles.insert(p);
	}

	p->color = role["color"].as_int();
	p->position = role["position"].as_int();
	p->permissions = role["permissions"].as_int();
	p->wszName = role["name"].as_mstring();
}

/////////////////////////////////////////////////////////////////////////////////////////

static void sttSetGroupName(MCONTACT hContact, const wchar_t *pwszGroupName)
{
	ptrW wszOldName(Clist_GetGroup(hContact));
	if (wszOldName != nullptr) {
		ptrW wszChatGroup(Chat_GetGroup());
		if (mir_wstrcmpi(wszOldName, wszChatGroup))
			return; // custom group, don't touch it
	}

	Clist_SetGroup(hContact, pwszGroupName);
}

void CDiscordProto::BatchChatCreate(void *param)
{
	CDiscordGuild *pGuild = (CDiscordGuild*)param;

	for (auto &it : pGuild->arChannels)
		if (!it->bIsPrivate && !it->bIsGroup)
			CreateChat(pGuild, it);
}

void CDiscordProto::CreateChat(CDiscordGuild *pGuild, CDiscordUser *pUser)
{
	SESSION_INFO *si = Chat_NewSession(GCW_CHATROOM, m_szModuleName, pUser->wszUsername, pUser->wszChannelName);
	si->pParent = pGuild->pParentSi;
	pUser->hContact = si->hContact;

	if (m_bUseGuildGroups) {
		if (pUser->parentId) {
			CDiscordUser *pParent = FindUserByChannel(pUser->parentId);
			if (pParent != nullptr)
				sttSetGroupName(pUser->hContact, pParent->wszChannelName);
		}
		else sttSetGroupName(pUser->hContact, Clist_GroupGetName(pGuild->groupId));
	}

	BuildStatusList(pGuild, si);

	Chat_Control(m_szModuleName, pUser->wszUsername, m_bHideGroupchats ? WINDOW_HIDDEN : SESSION_INITDONE);
	Chat_Control(m_szModuleName, pUser->wszUsername, SESSION_ONLINE);

	if (!pUser->wszTopic.IsEmpty()) {
		Chat_SetStatusbarText(m_szModuleName, pUser->wszUsername, pUser->wszTopic);

		GCEVENT gce = { m_szModuleName, 0, GC_EVENT_TOPIC };
		gce.pszID.w = pUser->wszUsername;
		gce.time = time(0);
		gce.pszText.w = pUser->wszTopic;
		Chat_Event(&gce);
	}
}

void CDiscordProto::ProcessGuild(const JSONNode &pRoot)
{
	SnowFlake guildId = ::getId(pRoot["id"]);

	CDiscordGuild *pGuild = FindGuild(guildId);
	if (pGuild == nullptr) {
		pGuild = new CDiscordGuild(guildId);
		arGuilds.insert(pGuild);
	}

	pGuild->ownerId = ::getId(pRoot["owner_id"]);
	pGuild->wszName = pRoot["name"].as_mstring();
	if (m_bUseGuildGroups)
		pGuild->groupId = Clist_GroupCreate(Clist_GroupExists(m_wszDefaultGroup), pGuild->wszName);

	SESSION_INFO *si = Chat_NewSession(GCW_SERVER, m_szModuleName, pGuild->wszName, pGuild->wszName, pGuild);
	pGuild->pParentSi = (SESSION_INFO*)si;
	pGuild->hContact = si->hContact;
	setId(pGuild->hContact, DB_KEY_CHANNELID, guildId);

	if (!pGuild->bSynced && getByte(si->hContact, "EnableSync"))
		LoadGuildInfo(pGuild);

	Chat_Control(m_szModuleName, pGuild->wszName, WINDOW_HIDDEN);
	Chat_Control(m_szModuleName, pGuild->wszName, SESSION_ONLINE);

	for (auto &it : pRoot["roles"])
		ProcessRole(pGuild, it);

	BuildStatusList(pGuild, si);

	// store all guild members
	for (auto &it : pRoot["members"]) {
		CMStringW wszUserId = it["user"]["id"].as_mstring();
		SnowFlake userId = _wtoi64(wszUserId);
		CDiscordGuildMember *pm = pGuild->FindUser(userId);
		if (pm == nullptr) {
			pm = new CDiscordGuildMember(userId);
			pGuild->arChatUsers.insert(pm);
		}

		pm->wszNick = it["nick"].as_mstring();
		if (pm->wszNick.IsEmpty())
			pm->wszNick = it["user"]["username"].as_mstring() + L"#" + it["user"]["discriminator"].as_mstring();

		if (userId == pGuild->ownerId)
			pm->wszRole = L"@owner";
		else {
			CDiscordRole *pRole = nullptr;
			for (auto &itr : it["roles"]) {
				SnowFlake roleId = ::getId(itr);
				if (pRole = pGuild->arRoles.find((CDiscordRole *)&roleId))
					break;
			}
			pm->wszRole = (pRole == nullptr) ? L"@everyone" : pRole->wszName;
		}
		pm->iStatus = ID_STATUS_OFFLINE;
	}

	// parse online statuses
	for (auto &it : pRoot["presences"]) {
		CDiscordGuildMember *gm = pGuild->FindUser(::getId(it["user"]["id"]));
		if (gm != nullptr)
			gm->iStatus = StrToStatus(it["status"].as_mstring());
	}

	for (auto &it : pGuild->arChatUsers)
		AddGuildUser(pGuild, *it);

	for (auto &it : pRoot["channels"])
		ProcessGuildChannel(pGuild, it);

	if (m_bUseGroupchats)
		ForkThread(&CDiscordProto::BatchChatCreate, pGuild);

	// retrieve missing histories
	for (auto &it : pGuild->arChannels) {
		if (it->bIsPrivate)
			continue;

		if (!it->bSynced) {
			it->bSynced = true;
			SnowFlake oldMsgId = getId(it->hContact, DB_KEY_LASTMSGID);
			if (oldMsgId != 0 && it->lastMsgId > oldMsgId)
				RetrieveHistory(it, MSG_AFTER, oldMsgId, 99);
		}
	}

	pGuild->bSynced = true;
}

/////////////////////////////////////////////////////////////////////////////////////////

CDiscordUser* CDiscordProto::ProcessGuildChannel(CDiscordGuild *pGuild, const JSONNode &pch)
{
	CMStringW wszChannelId = pch["id"].as_mstring();
	SnowFlake channelId = _wtoi64(wszChannelId);
	CMStringW wszName = pch["name"].as_mstring();
	CDiscordUser *pUser;

	// filter our all channels but the text ones
	switch (pch["type"].as_int()) {
	case 4: // channel group
		if (!m_bUseGuildGroups) // ignore groups when they aren't enabled
			return nullptr;

		pUser = FindUserByChannel(channelId);
		if (pUser == nullptr) {
			// missing channel - create it
			pUser = new CDiscordUser(channelId);
			pUser->bIsPrivate = false;
			pUser->channelId = channelId;
			pUser->bIsGroup = true;
			arUsers.insert(pUser);

			pGuild->arChannels.insert(pUser);

			MGROUP grpId = Clist_GroupCreate(pGuild->groupId, wszName);
			pUser->wszChannelName = Clist_GroupGetName(grpId);
		}
		return pUser;

	case 0: // text channel
		pUser = FindUserByChannel(channelId);
		if (pUser == nullptr) {
			// missing channel - create it
			pUser = new CDiscordUser(channelId);
			pUser->bIsPrivate = false;
			pUser->channelId = channelId;
			arUsers.insert(pUser);
		}

		if (pGuild->arChannels.find(pUser) == nullptr)
			pGuild->arChannels.insert(pUser);

		pUser->wszUsername = wszChannelId;
		if (m_bUseGuildGroups)
			pUser->wszChannelName = L"#" + wszName;
		else
			pUser->wszChannelName = pGuild->wszName + L"#" + wszName;
		pUser->wszTopic = pch["topic"].as_mstring();
		pUser->pGuild = pGuild;
		pUser->lastMsgId = ::getId(pch["last_message_id"]);
		pUser->parentId = _wtoi64(pch["parent_id"].as_mstring());

		SnowFlake oldMsgId = getId(pUser->hContact, DB_KEY_LASTMSGID);
		if (oldMsgId == 0)
			RetrieveHistory(pUser, MSG_BEFORE, pUser->lastMsgId, 20);
		else if (!pUser->bSynced && pUser->lastMsgId > oldMsgId) {
			pUser->bSynced = true;
			RetrieveHistory(pUser, MSG_AFTER, oldMsgId, 99);
		}

		setId(pUser->hContact, DB_KEY_ID, channelId);
		setId(pUser->hContact, DB_KEY_CHANNELID, channelId);
		return pUser;
	}

	return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////

void CDiscordProto::AddGuildUser(CDiscordGuild *pGuild, const CDiscordGuildMember &pUser)
{
	int flags = 0;
	switch (pUser.iStatus) {
	case ID_STATUS_ONLINE: case ID_STATUS_NA: case ID_STATUS_DND:
		flags = 1;
		break;
	}

	auto *pStatus = g_chatApi.TM_FindStatus(pGuild->pParentSi->pStatuses, pUser.wszRole);

	wchar_t wszUserId[100];
	_i64tow_s(pUser.userId, wszUserId, _countof(wszUserId), 10);
	
	auto *pu = g_chatApi.UM_AddUser(pGuild->pParentSi, wszUserId, pUser.wszNick, (pStatus) ? pStatus->iStatus : 0);
	pu->iStatusEx = flags;
	if (pUser.userId == m_ownId)
		pGuild->pParentSi->pMe = pu;
}

/////////////////////////////////////////////////////////////////////////////////////////

static CMStringW GetCacheFileName(SnowFlake guildId)
{
	VARSW wszCacheDir(L"%miranda_userdata%\\Discord");
	CreateDirectoryTreeW(wszCacheDir);

	return CMStringW(FORMAT, L"%s\\%lld.cache", wszCacheDir.get(), guildId);
}

void CDiscordProto::LoadGuildInfo(CDiscordGuild *pGuild)
{
	CMStringW wszCacheFile(GetCacheFileName(pGuild->id));
	int fileId = _wopen(wszCacheFile, _O_BINARY | _O_RDONLY);
	if (fileId != -1) {
		size_t length = _filelength(fileId);
		ptrA buf((char *)mir_alloc(length+1));
		_read(fileId, buf, (unsigned)length);
		_close(fileId);

		JSONNode root(JSONNode::parse(buf));
		for (auto &cc : root) {
			auto *pUser = new CDiscordGuildMember(_wtoi64(cc["id"].as_mstring()));
			pUser->wszNick = cc["nick"].as_mstring();
			pUser->wszRole = cc["role"].as_mstring();
			pGuild->arChatUsers.insert(pUser);

			AddGuildUser(pGuild, *pUser);
		}
	}
}

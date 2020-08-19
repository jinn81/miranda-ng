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

/* MESSAGE SENDING */

// outcoming message flow
int CSkypeProto::OnSendMessage(MCONTACT hContact, int, const char *szMessage)
{
	if (!IsOnline()) {
		ProtoBroadcastAck(hContact, ACKTYPE_MESSAGE, ACKRESULT_FAILED, NULL, (LPARAM)TranslateT("You cannot send when you are offline."));
		return 0;
	}

	SendMessageParam *param = new SendMessageParam();
	param->hContact = hContact;
	Utils_GetRandom(&param->hMessage, sizeof(param->hMessage));
	param->hMessage &= ~0x80000000;

	ptrA username(getStringA(hContact, "Skypename"));

	AsyncHttpRequest *pReq;
	if (strncmp(szMessage, "/me ", 4) == 0)
		pReq = new SendActionRequest(username, param->hMessage, &szMessage[4], this);
	else
		pReq = new SendMessageRequest(username, param->hMessage, szMessage);
	pReq->pUserInfo = param;
	PushRequest(pReq);
	{
		mir_cslock lck(m_lckOutMessagesList);
		m_OutMessages.insert((void*)param->hMessage);
	}
	return param->hMessage;
}

void CSkypeProto::OnMessageSent(NETLIBHTTPREQUEST *response, AsyncHttpRequest *pRequest)
{
	auto *param = (SendMessageParam*)pRequest->pUserInfo;
	MCONTACT hContact = param->hContact;
	HANDLE hMessage = (HANDLE)param->hMessage;
	delete param;

	if (response != nullptr) {
		if (response->resultCode != 201) {
			std::string strError = Translate("Unknown error!");

			if (response->pData != nullptr) {
				JSONNode jRoot = JSONNode::parse(response->pData);
				const JSONNode &jErr = jRoot["errorCode"];
				if (jErr)
					strError = jErr.as_string();
			}

			ProtoBroadcastAck(hContact, ACKTYPE_MESSAGE, ACKRESULT_FAILED, hMessage, _A2T(strError.c_str()));
		}
	}
	else ProtoBroadcastAck(hContact, ACKTYPE_MESSAGE, ACKRESULT_FAILED, hMessage, (LPARAM)TranslateT("Network error!"));
}

// preparing message/action to be written into db
int CSkypeProto::OnPreCreateMessage(WPARAM, LPARAM lParam)
{
	MessageWindowEvent *evt = (MessageWindowEvent*)lParam;
	if (mir_strcmp(Proto_GetBaseAccountName(evt->hContact), m_szModuleName))
		return 0;

	char *message = (char*)evt->dbei->pBlob;
	if (strncmp(message, "/me ", 4) == 0) {
		evt->dbei->cbBlob = evt->dbei->cbBlob - 4;
		memmove(evt->dbei->pBlob, &evt->dbei->pBlob[4], evt->dbei->cbBlob);
		evt->dbei->eventType = SKYPE_DB_EVENT_TYPE_ACTION;
	}
	return 0;
}

/* MESSAGE EVENT */

void CSkypeProto::OnPrivateMessageEvent(const JSONNode &node)
{
	CMStringA szMessageId = node["clientmessageid"] ? node["clientmessageid"].as_string().c_str() : node["skypeeditedid"].as_string().c_str();
	CMStringA szConversationName(UrlToSkypename(node["conversationLink"].as_string().c_str()));
	CMStringA szFromSkypename(UrlToSkypename(node["from"].as_string().c_str()));

	std::string strMessageType = node["messagetype"].as_string();
	std::string strContent = node["content"].as_string();
	std::string szClearedContent(strMessageType == "RichText" ? RemoveHtml(strContent) : strContent);

	bool bEdited = node["skypeeditedid"];
	time_t timestamp = time(0); // fuck the server time, we need to place events in the order of our local time

	int nEmoteOffset = atoi(node["skypeemoteoffset"].as_string().c_str());

	MCONTACT hContact = AddContact(szConversationName, true);

	if (m_bHistorySynced)
		db_set_dw(hContact, m_szModuleName, "LastMsgTime", (DWORD)timestamp);

	if (strMessageType == "Control/Typing") {
		CallService(MS_PROTO_CONTACTISTYPING, hContact, PROTOTYPE_CONTACTTYPING_INFINITE);
	}
	else if (strMessageType == "Control/ClearTyping") {
		CallService(MS_PROTO_CONTACTISTYPING, hContact, PROTOTYPE_CONTACTTYPING_OFF);
	}
	else if (strMessageType == "Text" || strMessageType == "RichText") {
		if (IsMe(szFromSkypename)) {
			HANDLE hMessage = (HANDLE)atoi(szMessageId);
			if (m_OutMessages.getIndex(hMessage) != -1) {
				ProtoBroadcastAck(hContact, ACKTYPE_MESSAGE, ACKRESULT_SUCCESS, hMessage, (LPARAM)szMessageId.c_str());

				mir_cslock lck(m_lckOutMessagesList);
				m_OutMessages.remove(hMessage);
			}
			else AddDbEvent(nEmoteOffset == 0 ? EVENTTYPE_MESSAGE : SKYPE_DB_EVENT_TYPE_ACTION, hContact, timestamp, DBEF_UTF | DBEF_SENT, &szClearedContent[nEmoteOffset], szMessageId);
		}
		else {
			CallService(MS_PROTO_CONTACTISTYPING, hContact, PROTOTYPE_CONTACTTYPING_OFF);

			MEVENT hDbEvent = GetMessageFromDb(szMessageId);
			if (bEdited && hDbEvent != NULL)
				EditEvent(hContact, hDbEvent, szClearedContent.c_str(), timestamp);
			else {
				PROTORECVEVENT recv = {};
				recv.timestamp = timestamp;
				recv.szMessage = (char*)szClearedContent.c_str();
				recv.lParam = nEmoteOffset;
				recv.szMsgId = szMessageId;
				ProtoChainRecvMsg(hContact, &recv);
			}
		}
	}
	else if (strMessageType == "Event/Call") {
		AddDbEvent(SKYPE_DB_EVENT_TYPE_CALL_INFO, hContact, timestamp, DBEF_UTF, strContent.c_str(), szMessageId);
	}
	else if (strMessageType == "RichText/Files") {
		AddDbEvent(SKYPE_DB_EVENT_TYPE_FILETRANSFER_INFO, hContact, timestamp, DBEF_UTF, strContent.c_str(), szMessageId);
	}
	else if (strMessageType == "RichText/UriObject") {
		AddDbEvent(SKYPE_DB_EVENT_TYPE_URIOBJ, hContact, timestamp, DBEF_UTF, strContent.c_str(), szMessageId);
	}
	else if (strMessageType == "RichText/Contacts") {
		ProcessContactRecv(hContact, timestamp, strContent.c_str(), szMessageId);
	}
	else if (strMessageType == "RichText/Media_FlikMsg") {
		AddDbEvent(SKYPE_DB_EVENT_TYPE_MOJI, hContact, timestamp, DBEF_UTF, strContent.c_str(), szMessageId);
	}
	else if (strMessageType == "RichText/Media_GenericFile") {
		AddDbEvent(SKYPE_DB_EVENT_TYPE_FILE, hContact, timestamp, DBEF_UTF, strContent.c_str(), szMessageId);
	}
	else if (strMessageType == "RichText/Media_Album") {
		// do nothing
	}
	else {
		AddDbEvent(SKYPE_DB_EVENT_TYPE_UNKNOWN, hContact, timestamp, DBEF_UTF, strContent.c_str(), szMessageId);
	}
}

int CSkypeProto::OnDbEventRead(WPARAM hContact, LPARAM hDbEvent)
{
	if (IsOnline() && !isChatRoom(hContact) && !mir_strcmp(Proto_GetBaseAccountName(hContact), m_szModuleName))
		MarkMessagesRead(hContact, hDbEvent);
	return 0;
}

void CSkypeProto::MarkMessagesRead(MCONTACT hContact, MEVENT hDbEvent)
{
	debugLogA(__FUNCTION__);

	DBEVENTINFO dbei = {};
	db_event_get(hDbEvent, &dbei);
	time_t timestamp = dbei.timestamp;

	if (db_get_dw(hContact, m_szModuleName, "LastMsgTime", 0) > (timestamp - 300))
		PushRequest(new MarkMessageReadRequest(getId(hContact), timestamp, timestamp, false));
}

void CSkypeProto::ProcessContactRecv(MCONTACT hContact, time_t timestamp, const char *szContent, const char *szMessageId)
{
	TiXmlDocument doc;
	if (0 != doc.Parse(szContent))
		return;

	auto *xmlNode = doc.FirstChildElement("contacts");
	if (xmlNode == nullptr)
		return;

	int nCount = 0;
	for (auto *it : TiXmlEnum(xmlNode)) {
		UNREFERENCED_PARAMETER(it);
		nCount++;
	}

	PROTOSEARCHRESULT **psr = (PROTOSEARCHRESULT**)mir_calloc(sizeof(PROTOSEARCHRESULT*) * nCount);
	
	nCount = 0;
	for (auto *xmlContact : TiXmlFilter(xmlNode, "c")) {
		psr[nCount] = (PROTOSEARCHRESULT*)mir_calloc(sizeof(PROTOSEARCHRESULT));
		psr[nCount]->cbSize = sizeof(psr);
		psr[nCount]->id.a = mir_strdup(xmlContact->Attribute("s"));
		nCount++;
	}

	if (nCount) {
		PROTORECVEVENT pre = {};
		pre.timestamp = (DWORD)timestamp;
		pre.szMessage = (char*)psr;

		PBYTE b = (PBYTE)mir_calloc(sizeof(DWORD) + mir_strlen(szMessageId) + 1);
		PBYTE pCur = b;
		*((PDWORD)pCur) = nCount;
		pCur += sizeof(DWORD);

		mir_strcpy((char*)pCur, szMessageId);

		pre.lParam = (LPARAM)b;

		ProtoChainRecv(hContact, PSR_CONTACTS, 0, (LPARAM)&pre);
		for (DWORD i = 0; i < *((PDWORD)b); i++) {
			mir_free(psr[i]->id.a);
			mir_free(psr[i]);
		}
		mir_free(b);
	}
	mir_free(psr);
}

/*

Miranda NG: the free IM client for Microsoft* Windows*

Copyright (C) 2012-20 Miranda NG team (https://miranda-ng.org)
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

LONG CDbxMDBX::GetEventCount(MCONTACT contactID)
{
	if (!contactID)
		return m_ccDummy.dbc.dwEventCount;

	DBCachedContact *cc = m_cache->GetCachedContact(contactID);
	return (cc == nullptr) ? 0 : cc->dbc.dwEventCount;
}

/////////////////////////////////////////////////////////////////////////////////////////

MEVENT CDbxMDBX::AddEvent(MCONTACT contactID, const DBEVENTINFO *dbei)
{
	if (dbei == nullptr) return 0;
	if (dbei->timestamp == 0) return 0;

	MEVENT dwEventId = InterlockedIncrement(&m_dwMaxEventId);
	if (!EditEvent(contactID, dwEventId, dbei, true))
		return 0;

	return dwEventId;
}

/////////////////////////////////////////////////////////////////////////////////////////

BOOL CDbxMDBX::DeleteEvent(MEVENT hDbEvent)
{
	DBCachedContact *cc, *cc2;
	DBEvent dbe;
	char *szId = nullptr;
	{
		txn_ptr_ro txn(m_txn_ro);
		MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data;
		if (mdbx_get(txn, m_dbEvents, &key, &data) != MDBX_SUCCESS)
			return 1;

		dbe = *(DBEvent*)data.iov_base;
		if (dbe.flags & DBEF_HAS_ID) {
			char *src = (char *)data.iov_base + sizeof(dbe) + dbe.cbBlob + 1;
			szId = NEWSTR_ALLOCA(src);
		}

		cc = (dbe.dwContactID != 0) ? m_cache->GetCachedContact(dbe.dwContactID) : &m_ccDummy;
		if (cc == nullptr || cc->dbc.dwEventCount == 0)
			return 1;
	}

	if (!CheckEvent(cc, &dbe, cc2))
		return 1;
	{
		txn_ptr trnlck(StartTran());
		DBEventSortingKey key2 = { dbe.dwContactID, hDbEvent, dbe.timestamp };
		MDBX_val key = { &key2, sizeof(key2) }, data;

		if (mdbx_del(trnlck, m_dbEventsSort, &key, nullptr) != MDBX_SUCCESS)
			return 1;

		if (dbe.dwContactID != 0) {
			cc->dbc.dwEventCount--;
			if (cc->dbc.evFirstUnread == hDbEvent)
				FindNextUnread(trnlck, cc, key2);

			MDBX_val keyc = { &dbe.dwContactID, sizeof(MCONTACT) };
			data.iov_len = sizeof(DBContact); data.iov_base = &cc->dbc;
			if (mdbx_put(trnlck, m_dbContacts, &keyc, &data, 0) != MDBX_SUCCESS)
				return 1;
		}
		else {
			m_ccDummy.dbc.dwEventCount--;
			if (m_ccDummy.dbc.evFirstUnread == hDbEvent)
				FindNextUnread(trnlck, &m_ccDummy, key2);

			uint32_t keyVal = 2;
			MDBX_val keyc = { &keyVal, sizeof(keyVal) }, datac = { &m_ccDummy.dbc, sizeof(m_ccDummy.dbc) };
			if (mdbx_put(trnlck, m_dbGlobal, &keyc, &datac, 0) != MDBX_SUCCESS)
				return 0;
		}

		if (cc2) {
			key2.hContact = cc2->contactID;
			if (mdbx_del(trnlck, m_dbEventsSort, &key, nullptr) != MDBX_SUCCESS)
				return 1;

			key.iov_len = sizeof(MCONTACT); key.iov_base = &dbe.dwContactID;
			cc2->dbc.dwEventCount--;
			if (cc2->dbc.evFirstUnread == hDbEvent)
				FindNextUnread(trnlck, cc2, key2);

			MDBX_val keyc = { &cc2->contactID, sizeof(MCONTACT) };
			data.iov_len = sizeof(DBContact); data.iov_base = &cc2->dbc;
			if (mdbx_put(trnlck, m_dbContacts, &keyc, &data, 0) != MDBX_SUCCESS)
				return 1;
		}

		if (szId) {
			DBEventIdKey keyId;
			keyId.iModuleId = dbe.iModuleId;
			strncpy_s(keyId.szEventId, szId, _TRUNCATE);

			MDBX_val keyid = { &keyId, sizeof(MEVENT) + strlen(keyId.szEventId) + 1 };
			mdbx_del(trnlck, m_dbEventIds, &keyid, nullptr);
		}

		// remove an event
		key.iov_len = sizeof(MEVENT); key.iov_base = &hDbEvent;
		if (mdbx_del(trnlck, m_dbEvents, &key, nullptr) != MDBX_SUCCESS)
			return 1;

		if (trnlck.commit() != MDBX_SUCCESS)
			return 1;
	}

	DBFlush();
	NotifyEventHooks(g_hevEventDeleted, dbe.dwContactID, hDbEvent);
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

BOOL CDbxMDBX::EditEvent(MCONTACT contactID, MEVENT hDbEvent, const DBEVENTINFO *dbei)
{
	if (dbei == nullptr) return 1;
	if (dbei->timestamp == 0) return 1;

	DBEVENTINFO tmp = *dbei;
	{
		txn_ptr_ro txn(m_txn_ro);
		MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data;
		if (mdbx_get(txn, m_dbEvents, &key, &data) != MDBX_SUCCESS)
			return 1;

		DBEvent *dbe = (DBEvent*)data.iov_base;
		tmp.timestamp = dbe->timestamp;
	}

	return !EditEvent(contactID, hDbEvent, &tmp, false);
}

bool CDbxMDBX::EditEvent(MCONTACT contactID, MEVENT hDbEvent, const DBEVENTINFO *dbei, bool bNew)
{
	DBEvent dbe;
	dbe.dwContactID = contactID; // store native or subcontact's id
	dbe.iModuleId = GetModuleID(dbei->szModule);

	MCONTACT contactNotifyID = contactID;
	DBCachedContact *cc, *ccSub = nullptr;
	if (contactID != 0) {
		if ((cc = m_cache->GetCachedContact(contactID)) == nullptr)
			return false;

		if (cc->IsSub()) {
			ccSub = cc;
			if ((cc = m_cache->GetCachedContact(cc->parentID)) == nullptr)
				return false;

			// set default sub to the event's source
			if (!(dbei->flags & DBEF_SENT))
				db_mc_setDefault(cc->contactID, contactID, false);
			contactID = cc->contactID; // and add an event to a metahistory
			if (db_mc_isEnabled())
				contactNotifyID = contactID;
		}
	}
	else cc = &m_ccDummy;

	if (bNew && m_safetyMode)
		if (NotifyEventHooks(g_hevEventFiltered, contactNotifyID, (LPARAM)dbei))
			return false;

	dbe.timestamp = dbei->timestamp;
	dbe.flags = dbei->flags;
	dbe.wEventType = dbei->eventType;
	dbe.cbBlob = dbei->cbBlob;
	BYTE *pBlob = dbei->pBlob;

	mir_ptr<BYTE> pCryptBlob;
	if (m_bEncrypted) {
		size_t len;
		BYTE *pResult = m_crypto->encodeBuffer(pBlob, dbe.cbBlob, &len);
		if (pResult != nullptr) {
			pCryptBlob = pBlob = pResult;
			dbe.cbBlob = (uint16_t)len;
			dbe.flags |= DBEF_ENCRYPTED;
		}
	}

	size_t cbSrvId = mir_strlen(dbei->szId);
	if (cbSrvId > 0) {
		cbSrvId++;
		dbe.flags |= DBEF_HAS_ID;
	}

	BYTE *recBuf = (BYTE*)_alloca(sizeof(DBEvent) + dbe.cbBlob + cbSrvId + 1), *p = recBuf;
	memcpy(p, &dbe, sizeof(dbe)); p += sizeof(dbe);
	memcpy(p, pBlob, dbe.cbBlob); p += dbe.cbBlob;
	if (*p != 0)
		*p++ = 0;
	if (cbSrvId) {
		memcpy(p, dbei->szId, cbSrvId);
		p += cbSrvId;
	}

	{
		txn_ptr trnlck(StartTran());
		MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data = { recBuf, size_t(p - recBuf) };
		if (mdbx_put(trnlck, m_dbEvents, &key, &data, 0) != MDBX_SUCCESS)
			return false;

		// add a sorting key
		DBEventSortingKey key2 = { contactID, hDbEvent, dbe.timestamp };
		key.iov_len = sizeof(key2); key.iov_base = &key2;
		data.iov_len = 1; data.iov_base = (char*)("");
		if (mdbx_put(trnlck, m_dbEventsSort, &key, &data, 0) != MDBX_SUCCESS)
			return false;

		cc->Advance(hDbEvent, dbe);
		if (contactID != 0) {
			MDBX_val keyc = { &contactID, sizeof(MCONTACT) }, datac = { &cc->dbc, sizeof(DBContact) };
			if (mdbx_put(trnlck, m_dbContacts, &keyc, &datac, 0) != MDBX_SUCCESS)
				return false;

			// insert an event into a sub's history too
			if (ccSub != nullptr) {
				key2.hContact = ccSub->contactID;
				if (mdbx_put(trnlck, m_dbEventsSort, &key, &data, 0) != MDBX_SUCCESS)
					return false;

				ccSub->Advance(hDbEvent, dbe);
				datac.iov_base = &ccSub->dbc;
				keyc.iov_base = &ccSub->contactID;
				if (mdbx_put(trnlck, m_dbContacts, &keyc, &datac, 0) != MDBX_SUCCESS)
					return false;
			}
		}
		else {
			uint32_t keyVal = 2;
			MDBX_val keyc = { &keyVal, sizeof(keyVal) }, datac = { &m_ccDummy.dbc, sizeof(m_ccDummy.dbc) };
			if (mdbx_put(trnlck, m_dbGlobal, &keyc, &datac, 0) != MDBX_SUCCESS)
				return false;
		}

		if (dbei->szId) {
			DBEventIdKey keyId;
			keyId.iModuleId = dbe.iModuleId;
			strncpy_s(keyId.szEventId, dbei->szId, _TRUNCATE);

			MDBX_val keyid = { &keyId, sizeof(MEVENT) + strlen(keyId.szEventId) + 1 }, dataid = { &hDbEvent, sizeof(hDbEvent) };
			if (mdbx_put(trnlck, m_dbEventIds, &keyid, &dataid, 0) != MDBX_SUCCESS)
				return false;
		}

		if (trnlck.commit() != MDBX_SUCCESS)
			return false;
	}

	DBFlush();

	// Notify only in safe mode or on really new events
	if (m_safetyMode && !(dbei->flags & DBEF_TEMPORARY))
		NotifyEventHooks(bNew ? g_hevEventAdded : g_hevEventEdited, contactNotifyID, hDbEvent);

	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////

LONG CDbxMDBX::GetBlobSize(MEVENT hDbEvent)
{
	txn_ptr_ro txn(m_txn_ro);

	MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data;
	if (mdbx_get(txn, m_dbEvents, &key, &data) != MDBX_SUCCESS)
		return -1;
	return ((const DBEvent*)data.iov_base)->cbBlob;
}

/////////////////////////////////////////////////////////////////////////////////////////

BOOL CDbxMDBX::GetEvent(MEVENT hDbEvent, DBEVENTINFO *dbei)
{
	if (dbei == nullptr) return 1;
	if (dbei->cbBlob > 0 && dbei->pBlob == nullptr) {
		dbei->cbBlob = 0;
		return 1;
	}

	size_t cbBlob;
	const DBEvent *dbe;
	{
		txn_ptr_ro txn(m_txn_ro);

		MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data;
		if (mdbx_get(txn, m_dbEvents, &key, &data) != MDBX_SUCCESS)
			return 1;

		dbe = (const DBEvent*)data.iov_base;
		cbBlob = data.iov_len - sizeof(DBEvent);
	}

	dbei->szModule = GetModuleName(dbe->iModuleId);
	dbei->timestamp = dbe->timestamp;
	dbei->flags = dbe->flags;
	dbei->eventType = dbe->wEventType;
	size_t bytesToCopy = min(dbei->cbBlob, cbBlob);
	dbei->cbBlob = dbe->cbBlob;
	if (bytesToCopy && dbei->pBlob) {
		BYTE *pSrc = (BYTE*)dbe + sizeof(DBEvent);
		if (dbe->flags & DBEF_ENCRYPTED) {
			dbei->flags &= ~DBEF_ENCRYPTED;
			size_t len;
			BYTE* pBlob = (BYTE*)m_crypto->decodeBuffer(pSrc, dbe->cbBlob, &len);
			if (pBlob == nullptr)
				return 1;

			memcpy(dbei->pBlob, pBlob, bytesToCopy);
			if (bytesToCopy > len)
				memset(dbei->pBlob + len, 0, bytesToCopy - len);

			mir_free(pBlob);
		}
		else memcpy(dbei->pBlob, pSrc, bytesToCopy);

		if (dbei->flags & DBEF_HAS_ID)
			dbei->szId = (char *)pSrc + dbei->cbBlob + 1;
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

void CDbxMDBX::FindNextUnread(const txn_ptr &txn, DBCachedContact *cc, DBEventSortingKey &key2)
{
	cursor_ptr cursor(txn, m_dbEventsSort);

	MDBX_val key = { &key2, sizeof(key2) }, data;

	for (int res = mdbx_cursor_get(cursor, &key, &data, MDBX_SET_KEY); res == MDBX_SUCCESS; res = mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT)) {
		const DBEvent *dbe = (const DBEvent*)data.iov_base;
		if (dbe->dwContactID != cc->contactID)
			break;
		if (!dbe->markedRead()) {
			cc->dbc.evFirstUnread = key2.hEvent;
			cc->dbc.tsFirstUnread = key2.ts;
			return;
		}
	}

	cc->dbc.evFirstUnread = cc->dbc.tsFirstUnread = 0;
}

bool CDbxMDBX::CheckEvent(DBCachedContact *cc, const DBEvent *cdbe, DBCachedContact *&cc2)
{
	// if cc is a sub, cdbe should contain its id
	if (cc->IsSub()) {
		if (cc->contactID != cdbe->dwContactID)
			return false;
		
		cc2 = m_cache->GetCachedContact(cc->parentID);
		return true;
	}

	// if cc is a meta, cdbe should be its sub
	if (cc->IsMeta()) {
		cc2 = m_cache->GetCachedContact(cdbe->dwContactID);
		if (cc2 == nullptr)
			return false;

		return (cc2->parentID == cc->contactID);
	}

	// neither a sub, nor a meta. a usual contact. Contact IDs must match one another
	cc2 = nullptr;
	return cc->contactID == cdbe->dwContactID;
}

/////////////////////////////////////////////////////////////////////////////////////////

BOOL CDbxMDBX::MarkEventRead(MCONTACT contactID, MEVENT hDbEvent)
{
	if (hDbEvent == 0) return -1;

	DBCachedContact *cc = m_cache->GetCachedContact(contactID), *cc2 = nullptr;
	if (cc == nullptr)
		return -1;

	uint32_t wRetVal = -1;
	{
		txn_ptr trnlck(StartTran());
		MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data;
		if (mdbx_get(trnlck, m_dbEvents, &key, &data) != MDBX_SUCCESS)
			return -1;

		DBEvent cdbe = *(DBEvent*)data.iov_base;
		if (cdbe.markedRead())
			return cdbe.flags;

		// check subs & metas
		if (!CheckEvent(cc, &cdbe, cc2))
			return -2;

		void *recBuf = _alloca(data.iov_len);
		memcpy(recBuf, data.iov_base, data.iov_len);
		data.iov_base = recBuf;

		DBEvent *pNewEvent = (DBEvent*)data.iov_base;
		wRetVal = (pNewEvent->flags |= DBEF_READ);
		if (mdbx_put(trnlck, m_dbEvents, &key, &data, 0) != MDBX_SUCCESS)
			return -1;

		DBEventSortingKey keyVal = { contactID, hDbEvent, cdbe.timestamp };
		FindNextUnread(trnlck, cc, keyVal);
		key.iov_len = sizeof(MCONTACT); key.iov_base = &contactID;
		data.iov_base = &cc->dbc; data.iov_len = sizeof(cc->dbc);
		if (mdbx_put(trnlck, m_dbContacts, &key, &data, 0) != MDBX_SUCCESS)
			return -1;

		if (cc2 != nullptr) {
			DBEventSortingKey keyVal2 = { cc2->contactID, hDbEvent, cdbe.timestamp };
			FindNextUnread(trnlck, cc2, keyVal2);
			key.iov_len = sizeof(MCONTACT); key.iov_base = &cc2->contactID;
			data.iov_base = &cc2->dbc; data.iov_len = sizeof(cc2->dbc);
			if (mdbx_put(trnlck, m_dbContacts, &key, &data, 0) != MDBX_SUCCESS)
				return -1;
		}

		if (trnlck.commit() != MDBX_SUCCESS)
			return -1;
	}

	DBFlush();
	NotifyEventHooks(g_hevMarkedRead, contactID, (LPARAM)hDbEvent);
	return wRetVal;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

MEVENT CDbxMDBX::GetEventById(LPCSTR szModule, LPCSTR szId)
{
	if (szModule == nullptr || szId == nullptr)
		return 0;
	
	DBEventIdKey keyId;
	keyId.iModuleId = GetModuleID(szModule);
	strncpy_s(keyId.szEventId, szId, _TRUNCATE);

	MDBX_val key = { &keyId, sizeof(MEVENT) + strlen(keyId.szEventId) + 1 }, data;
	txn_ptr_ro txn(m_txn_ro);
	if (mdbx_get(txn, m_dbEventIds, &key, &data) != MDBX_SUCCESS)
		return 0;

	MEVENT hDbEvent = *(MEVENT *)data.iov_base;
	MDBX_val key2 = { &hDbEvent, sizeof(MEVENT) }, data2;
	if (mdbx_get(txn, m_dbEvents, &key2, &data2) != MDBX_SUCCESS)
		return 0;

	return hDbEvent;
}

/////////////////////////////////////////////////////////////////////////////////////////

MCONTACT CDbxMDBX::GetEventContact(MEVENT hDbEvent)
{
	if (hDbEvent == 0)
		return INVALID_CONTACT_ID;

	txn_ptr_ro txn(m_txn_ro);

	MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data;
	if (mdbx_get(txn, m_dbEvents, &key, &data) != MDBX_SUCCESS)
		return INVALID_CONTACT_ID;

	return ((const DBEvent*)data.iov_base)->dwContactID;
}

/////////////////////////////////////////////////////////////////////////////////////////

MEVENT CDbxMDBX::FindFirstEvent(MCONTACT contactID)
{
	DBCachedContact *cc;
	if (contactID != 0) {
		cc = m_cache->GetCachedContact(contactID);
		if (cc == nullptr)
			return 0;
	}
	else cc = &m_ccDummy;

	DBEventSortingKey keyVal = { contactID, 0, 0 };
	MDBX_val key = { &keyVal, sizeof(keyVal) }, data;

	txn_ptr_ro txn(m_txn_ro);

	cursor_ptr_ro cursor(m_curEventsSort);
	if (mdbx_cursor_get(cursor, &key, &data, MDBX_SET_RANGE) != MDBX_SUCCESS)
		return cc->t_evLast = 0;

	const DBEventSortingKey *pKey = (const DBEventSortingKey*)key.iov_base;
	cc->t_tsLast = pKey->ts;
	return cc->t_evLast = (pKey->hContact == contactID) ? pKey->hEvent : 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

MEVENT CDbxMDBX::FindFirstUnreadEvent(MCONTACT contactID)
{
	DBCachedContact *cc = m_cache->GetCachedContact(contactID);
	return (cc == nullptr) ? 0 : cc->dbc.evFirstUnread;
}

/////////////////////////////////////////////////////////////////////////////////////////

MEVENT CDbxMDBX::FindLastEvent(MCONTACT contactID)
{
	DBCachedContact *cc;
	if (contactID != 0) {
		cc = m_cache->GetCachedContact(contactID);
		if (cc == nullptr)
			return 0;
	}
	else cc = &m_ccDummy;

	DBEventSortingKey keyVal = { contactID, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF };
	MDBX_val key = { &keyVal, sizeof(keyVal) }, data;

	txn_ptr_ro txn(m_txn_ro);
	cursor_ptr_ro cursor(m_curEventsSort);

	if (mdbx_cursor_get(cursor, &key, &data, MDBX_SET_RANGE) != MDBX_SUCCESS) {
		if (mdbx_cursor_get(cursor, &key, &data, MDBX_LAST) != MDBX_SUCCESS)
			return cc->t_evLast = 0;
	}
	else {
		if (mdbx_cursor_get(cursor, &key, &data, MDBX_PREV) != MDBX_SUCCESS)
			return cc->t_evLast = 0;
	}

	const DBEventSortingKey *pKey = (const DBEventSortingKey*)key.iov_base;
	cc->t_tsLast = pKey->ts;
	return cc->t_evLast = (pKey->hContact == contactID) ? pKey->hEvent : 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

MEVENT CDbxMDBX::FindNextEvent(MCONTACT contactID, MEVENT hDbEvent)
{
	DBCachedContact *cc;
	if (contactID != 0) {
		cc = m_cache->GetCachedContact(contactID);
		if (cc == nullptr)
			return 0;
	}
	else cc = &m_ccDummy;

	if (hDbEvent == 0)
		return cc->t_evLast = 0;

	txn_ptr_ro txn(m_txn_ro);

	if (cc->t_evLast != hDbEvent) {
		MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data;
		if (mdbx_get(txn, m_dbEvents, &key, &data) != MDBX_SUCCESS)
			return 0;
		cc->t_tsLast = ((DBEvent*)data.iov_base)->timestamp;
	}

	DBEventSortingKey keyVal = { contactID, hDbEvent, cc->t_tsLast };
	MDBX_val key = { &keyVal, sizeof(keyVal) }, data;

	cursor_ptr_ro cursor(m_curEventsSort);
	if (mdbx_cursor_get(cursor, &key, nullptr, MDBX_SET) != MDBX_SUCCESS)
		return cc->t_evLast = 0;

	if (mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT) != MDBX_SUCCESS)
		return cc->t_evLast = 0;

	const DBEventSortingKey *pKey = (const DBEventSortingKey*)key.iov_base;
	cc->t_tsLast = pKey->ts;
	return cc->t_evLast = (pKey->hContact == contactID) ? pKey->hEvent : 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

MEVENT CDbxMDBX::FindPrevEvent(MCONTACT contactID, MEVENT hDbEvent)
{
	DBCachedContact *cc;
	if (contactID != 0) {
		cc = m_cache->GetCachedContact(contactID);
		if (cc == nullptr)
			return 0;
	}
	else cc = &m_ccDummy;

	if (hDbEvent == 0)
		return cc->t_evLast = 0;

	MDBX_val data;

	txn_ptr_ro txn(m_txn_ro);

	if (cc->t_evLast != hDbEvent) {
		MDBX_val key = { &hDbEvent, sizeof(MEVENT) };
		if (mdbx_get(txn, m_dbEvents, &key, &data) != MDBX_SUCCESS)
			return 0;
		cc->t_tsLast = ((DBEvent*)data.iov_base)->timestamp;
	}

	DBEventSortingKey keyVal = { contactID, hDbEvent, cc->t_tsLast };
	MDBX_val key = { &keyVal, sizeof(keyVal) };

	cursor_ptr_ro cursor(m_curEventsSort);
	if (mdbx_cursor_get(cursor, &key, nullptr, MDBX_SET) != MDBX_SUCCESS)
		return cc->t_evLast = 0;

	if (mdbx_cursor_get(cursor, &key, &data, MDBX_PREV) != MDBX_SUCCESS)
		return cc->t_evLast = 0;

	const DBEventSortingKey *pKey = (const DBEventSortingKey*)key.iov_base;
	cc->t_tsLast = pKey->ts;
	return cc->t_evLast = (pKey->hContact == contactID) ? pKey->hEvent : 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Event cursors

class CMdbxEventCursor : public DB::EventCursor
{
	friend class CDbxMDBX;
	CDbxMDBX *m_pOwner;

	bool m_bForward, m_bFirst = true;
	DBCachedContact *m_cc;
	DBEventSortingKey m_key;

public:
	CMdbxEventCursor(class CDbxMDBX *pDb, DBCachedContact *cc, bool bForward) :
		EventCursor(cc->contactID),
		m_pOwner(pDb),
		m_bForward(bForward),
		m_cc(cc)
	{
		m_key.hContact = hContact;
		if (bForward) {
			m_key.hEvent = 0;
			m_key.ts = 0;
		}
		else {
			m_key.hEvent = 0xFFFFFFFF;
			m_key.ts = 0xFFFFFFFFFFFFFFFF;
		}
	}

	MEVENT FetchNext() override
	{
		MDBX_val key = { &m_key, sizeof(m_key) }, data;
		DBEventSortingKey dbKey;
		{
			txn_ptr_ro txn(m_pOwner->m_txn_ro);
			cursor_ptr_ro cursor(m_pOwner->m_curEventsSort);

			if (m_bFirst) {
				m_bFirst = false;

				if (mdbx_cursor_get(cursor, &key, &data, MDBX_SET_RANGE) != MDBX_SUCCESS)
					return 0;
				dbKey = *(const DBEventSortingKey *)key.iov_base;
			}
			else {
				if (mdbx_cursor_get(cursor, &key, &data, MDBX_SET) != MDBX_SUCCESS)
					return 0;

				if (mdbx_cursor_get(cursor, &key, &data, (m_bForward) ? MDBX_NEXT : MDBX_PREV) != MDBX_SUCCESS)
					return 0;

				dbKey = *(const DBEventSortingKey *)key.iov_base;
			}
		}

		if (dbKey.hContact != hContact)
			return 0;
		
		m_key = dbKey;
		return dbKey.hEvent;
	}
};

DB::EventCursor* CDbxMDBX::EventCursor(MCONTACT hContact, MEVENT)
{
	DBCachedContact *cc;
	if (hContact != 0) {
		cc = m_cache->GetCachedContact(hContact);
		if (cc == nullptr)
			return nullptr;
	}
	else cc = &m_ccDummy;

	return new CMdbxEventCursor(this, cc, true);
}

DB::EventCursor* CDbxMDBX::EventCursorRev(MCONTACT hContact, MEVENT)
{
	DBCachedContact *cc;
	if (hContact != 0) {
		cc = m_cache->GetCachedContact(hContact);
		if (cc == nullptr)
			return nullptr;
	}
	else cc = &m_ccDummy;

	return new CMdbxEventCursor(this, cc, false);
}

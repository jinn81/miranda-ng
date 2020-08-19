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

LONG CDbxMDBX::GetContactCount(void)
{
	MDBX_stat st;
	txn_ptr_ro trnlck(m_txn_ro);
	mdbx_dbi_stat(trnlck, m_dbContacts, &st, sizeof(st));
	return st.ms_entries;
}

LONG CDbxMDBX::GetContactSize(void)
{
	return sizeof(DBCachedContact);
}

LONG CDbxMDBX::DeleteContact(MCONTACT contactID)
{
	if (contactID == 0) // global contact cannot be removed
		return 1;

	DBCachedContact *cc = m_cache->GetCachedContact(contactID);
	if (cc == nullptr)
		return 1;

	NotifyEventHooks(g_hevContactDeleted, contactID, 0);

	// remove event sorting keys owned by contact
	Netlib_Log(0, "Started wipe history");
	{
		DBEventSortingKey keyS = { contactID, 0, 0 };
		MDBX_val key = { &keyS, sizeof(keyS) }, data;

		txn_ptr trnlck(StartTran());
		cursor_ptr cursor(trnlck, m_dbEventsSort);

		for (int res = mdbx_cursor_get(cursor, &key, &data, MDBX_SET_RANGE); res == MDBX_SUCCESS; res = mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT)) {
			auto *pKey = (DBEventSortingKey *)key.iov_base;
			if (pKey->hContact != contactID)
				break;

			if (mdbx_cursor_del(cursor, 0) != MDBX_SUCCESS)
				return 1;

			if (!cc->IsMeta() && !cc->IsSub()) {
				MDBX_val key2 = { &pKey->hEvent, sizeof(MEVENT) };
				mdbx_del(trnlck, m_dbEvents, &key2, nullptr);
			}
		}

		if (trnlck.commit() != MDBX_SUCCESS)
			return 1;
	}

	// remove all contact's settings
	Netlib_Log(0, "Started wipe settings");
	{
		DBSettingKey keyS = { contactID, 0, 0 };
		MDBX_val key = { &keyS, sizeof(keyS) }, data;

		txn_ptr trnlck(StartTran());
		cursor_ptr cursor(trnlck, m_dbSettings);

		for (int res = mdbx_cursor_get(cursor, &key, &data, MDBX_SET_RANGE); res == MDBX_SUCCESS; res = mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT)) {
			const DBSettingKey *pKey = (const DBSettingKey*)key.iov_base;
			if (pKey->hContact != contactID)
				break;

			if (mdbx_cursor_del(cursor, 0) != MDBX_SUCCESS)
				return 1;
		}

		if (trnlck.commit() != MDBX_SUCCESS)
			return 1;
	}

	// finally remove the contact itself
	Netlib_Log(0, "Started wipe contact itself");
	MDBX_val key = { &contactID, sizeof(MCONTACT) };
	{
		txn_ptr trnlck(StartTran());
		if (mdbx_del(trnlck, m_dbContacts, &key, nullptr) != MDBX_SUCCESS)
			return 1;
		if (trnlck.commit() != MDBX_SUCCESS)
			return 1;
	}

	// free cache item
	m_cache->FreeCachedContact(contactID);
	DBFlush();
	return 0;
}

MCONTACT CDbxMDBX::AddContact()
{
	MCONTACT dwContactId = InterlockedIncrement(&m_maxContactId);

	DBCachedContact *cc = m_cache->AddContactToCache(dwContactId);
	{
		MDBX_val key = { &dwContactId, sizeof(MCONTACT) };
		MDBX_val data = { &cc->dbc, sizeof(cc->dbc) };

		txn_ptr trnlck(StartTran());
		if (mdbx_put(trnlck, m_dbContacts, &key, &data, 0) != MDBX_SUCCESS)
			return 0;
		if (trnlck.commit() != MDBX_SUCCESS)
			return 0;
	}

	DBFlush();

	NotifyEventHooks(g_hevContactAdded, dwContactId, 0);
	return dwContactId;
}

STDMETHODIMP_(BOOL) CDbxMDBX::IsDbContact(MCONTACT contactID)
{
	DBCachedContact *cc = m_cache->GetCachedContact(contactID);
	return (cc != nullptr);
}

/////////////////////////////////////////////////////////////////////////////////////////

void CDbxMDBX::GatherContactHistory(MCONTACT hContact, OBJLIST<EventItem> &list)
{
	DBEventSortingKey keyVal = { hContact, 0, 0 };
	MDBX_val key = { &keyVal, sizeof(keyVal) }, data;

	txn_ptr_ro trnlck(m_txn_ro);
	cursor_ptr_ro cursor(m_curEventsSort);

	for (int res = mdbx_cursor_get(cursor, &key, &data, MDBX_SET_RANGE); res == MDBX_SUCCESS; res = mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT)) {
		const DBEventSortingKey *pKey = (const DBEventSortingKey*)key.iov_base;
		if (pKey->hContact != hContact)
			return;

		list.insert(new EventItem(pKey->ts, pKey->hEvent));
	}
}

BOOL CDbxMDBX::MetaMergeHistory(DBCachedContact *ccMeta, DBCachedContact *ccSub)
{
	OBJLIST<EventItem> list(1000);
	GatherContactHistory(ccSub->contactID, list);

	for (auto &EI : list) {
		txn_ptr trnlck(StartTran());

		DBEventSortingKey insVal = { ccMeta->contactID, EI->eventId, EI->ts };
		MDBX_val key = { &insVal, sizeof(insVal) }, data = { (void*)"", 1 };
		if (mdbx_put(trnlck, m_dbEventsSort, &key, &data, 0) != MDBX_SUCCESS)
			return 1;

		if (trnlck.commit() != MDBX_SUCCESS)
			return 1;

		ccMeta->dbc.dwEventCount++;
	}

	MDBX_val keyc = { &ccMeta->contactID, sizeof(MCONTACT) }, datac = { &ccMeta->dbc, sizeof(ccMeta->dbc) };
	txn_ptr trnlck(StartTran());
	if (mdbx_put(trnlck, m_dbContacts, &keyc, &datac, 0) != MDBX_SUCCESS)
		return 1;
	if (trnlck.commit() != MDBX_SUCCESS)
		return 1;

	DBFlush();
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

BOOL CDbxMDBX::MetaSplitHistory(DBCachedContact *ccMeta, DBCachedContact *ccSub)
{
	OBJLIST<EventItem> list(1000);
	GatherContactHistory(ccSub->contactID, list);

	for (auto &EI : list) {
		txn_ptr trnlck(StartTran());
		DBEventSortingKey insVal = { ccMeta->contactID, EI->eventId, EI->ts };
		MDBX_val key = { &insVal, sizeof(insVal) };
		if (mdbx_del(trnlck, m_dbEventsSort, &key, nullptr) != MDBX_SUCCESS)
			return 1;
		if (trnlck.commit() != MDBX_SUCCESS)
			return 1;

		ccMeta->dbc.dwEventCount--;
	}

	txn_ptr trnlck(StartTran());
	MDBX_val keyc = { &ccMeta->contactID, sizeof(MCONTACT) }, datac = { &ccMeta->dbc, sizeof(ccMeta->dbc) };
	if (mdbx_put(trnlck, m_dbContacts, &keyc, &datac, 0) != MDBX_SUCCESS)
		return 1;
	if (trnlck.commit() != MDBX_SUCCESS)
		return 1;
	
	DBFlush();
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

BOOL CDbxMDBX::MetaRemoveSubHistory(DBCachedContact *ccSub)
{
	OBJLIST<EventItem> list(1000);
	GatherContactHistory(ccSub->contactID, list);

	for (auto &EI : list) {
		txn_ptr trnlck(StartTran());
		{
			MDBX_val key = { &EI->eventId, sizeof(MEVENT) }, data;
			if (mdbx_get(trnlck, m_dbEvents, &key, &data) == MDBX_SUCCESS) {
				DBEvent *pEvent = (DBEvent*)data.iov_base;
				pEvent->dwContactID = ccSub->parentID;
				if (mdbx_put(trnlck, m_dbEvents, &key, &data, 0) != MDBX_SUCCESS)
					return 1;
			}
		}

		DBEventSortingKey sortKey = { ccSub->contactID, EI->eventId, EI->ts };
		{
			MDBX_val key = { &sortKey, sizeof(sortKey) };
			if (mdbx_del(trnlck, m_dbEventsSort, &key, nullptr) != MDBX_SUCCESS)
				return 1;
		}
		
		if (trnlck.commit() != MDBX_SUCCESS)
			return 1;
	}

	DBFlush();
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

void DBCachedContact::Advance(MEVENT id, DBEvent &dbe)
{
	dbc.dwEventCount++;

	if (dbe.flags & (DBEF_READ | DBEF_SENT))
		return;

	if (dbe.timestamp < dbc.tsFirstUnread || dbc.tsFirstUnread == 0) {
		dbc.tsFirstUnread = dbe.timestamp;
		dbc.evFirstUnread = id;
	}
}

void DBCachedContact::Snapshot()
{
	tmp_dbc = dbc;
}

void DBCachedContact::Revert()
{
	dbc = tmp_dbc;
}

/////////////////////////////////////////////////////////////////////////////////////////
// initial cycle to fill the contacts' cache

void CDbxMDBX::FillContacts()
{
	{
		txn_ptr_ro trnlck(m_txn_ro);
		cursor_ptr_ro cursor(m_curContacts);

		MDBX_val key, data;
		while (mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT) == MDBX_SUCCESS) {
			DBCachedContact *cc = m_cache->AddContactToCache(*(MCONTACT*)key.iov_base);
			cc->dbc = *(DBContact*)data.iov_base;
		}
	}

	for (DBCachedContact *cc = m_cache->GetFirstContact(); cc; cc = m_cache->GetNextContact(cc->contactID)) {
		CheckProto(cc, "");

		DBVARIANT dbv; dbv.type = DBVT_DWORD;
		cc->nSubs = (0 != GetContactSetting(cc->contactID, META_PROTO, "NumContacts", &dbv)) ? -1 : dbv.dVal;
		if (cc->nSubs != -1) {
			cc->pSubs = (MCONTACT*)mir_alloc(cc->nSubs * sizeof(MCONTACT));
			for (int k = 0; k < cc->nSubs; k++) {
				char setting[100];
				mir_snprintf(setting, _countof(setting), "Handle%d", k);
				cc->pSubs[k] = (0 != GetContactSetting(cc->contactID, META_PROTO, setting, &dbv)) ? 0 : dbv.dVal;
			}
		}
		cc->nDefault = (0 != GetContactSetting(cc->contactID, META_PROTO, "Default", &dbv)) ? -1 : dbv.dVal;
		cc->parentID = (0 != GetContactSetting(cc->contactID, META_PROTO, "ParentMeta", &dbv)) ? 0 : dbv.dVal;
	}
}

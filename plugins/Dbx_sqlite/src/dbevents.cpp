#include "stdafx.h"

enum {
	SQL_EVT_STMT_COUNT = 0,
	SQL_EVT_STMT_ADDEVENT,
	SQL_EVT_STMT_DELETE,
	SQL_EVT_STMT_EDIT,
	SQL_EVT_STMT_BLOBSIZE,
	SQL_EVT_STMT_GET,
	SQL_EVT_STMT_GETFLAGS,
	SQL_EVT_STMT_SETFLAGS,
	SQL_EVT_STMT_GETCONTACT,
	SQL_EVT_STMT_FINDFIRST,
	SQL_EVT_STMT_FINDFIRSTUNREAD,
	SQL_EVT_STMT_FINDLAST,
	SQL_EVT_STMT_GETIDBYSRVID,
	SQL_EVT_STMT_ADDEVENT_SRT,
	SQL_EVT_STMT_DELETE_SRT,
	SQL_EVT_STMT_META_SPLIT,
	SQL_EVT_STMT_META_MERGE_SELECT,
	SQL_EVT_STMT_NUM
};

//TODO: hide it inside cursor class
static const char* normal_order_query =
"select id from events_srt where contact_id = ? order by timestamp;";
static const char* normal_order_pos_query =
"select id from events_srt where contact_id = ? and id >= ? order by timestamp;";

static const char* reverse_order_query =
"select id from events_srt where contact_id = ? order by timestamp desc, id desc;";

static const char* reverse_order_pos_query =
"select id from events_srt where contact_id = ? and id <= ? order by timestamp desc, id desc;";


static const char *evt_stmts[SQL_EVT_STMT_NUM] = {
	"select count(1) from events where contact_id = ? limit 1;",
	"insert into events(contact_id, module, timestamp, type, flags, data, server_id) values (?, ?, ?, ?, ?, ?, ?);",
	"delete from events where id = ?;",
	"update events set module = ?, timestamp = ?, type = ?, flags = ?, blob = ? where id = ?;",
	"select length(data) from events where id = ? limit 1;",
	"select module, timestamp, type, flags, length(data), data from events where id = ? limit 1;",
	"select flags from events where id = ? limit 1;",
	"update events set flags = ? where id = ?;",
	"select contact_id from events where id = ? limit 1;",
	normal_order_query,
	"select id, timestamp from events where contact_id = ? and (flags & ?) = 0 order by timestamp, id limit 1;",
	reverse_order_query,
	"select id, timestamp from events where module = ? and server_id = ? limit 1;",
	"insert into events_srt(id, contact_id, timestamp) values (?, ?, ?);",
	"delete from events_srt where id = ?;",
	"delete from events_srt where contact_id = ?;",
	"select id, timestamp from events where contact_id = ?;",
};

static sqlite3_stmt *evt_stmts_prep[SQL_EVT_STMT_NUM] = { 0 };

void CDbxSQLite::InitEvents()
{
	for (size_t i = 0; i < SQL_EVT_STMT_NUM; i++)
		sqlite3_prepare_v3(m_db, evt_stmts[i], -1, SQLITE_PREPARE_PERSISTENT, &evt_stmts_prep[i], nullptr);

	sqlite3_stmt *stmt = nullptr;
	sqlite3_prepare_v2(m_db, "select distinct module from events;", -1, &stmt, nullptr);
	int rc = 0;
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		const char *module = (char*)sqlite3_column_text(stmt, 0);
		if (mir_strlen(module) > 0)
			m_modules.insert(mir_strdup(module));
	}
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	sqlite3_finalize(stmt);
}

void CDbxSQLite::UninitEvents()
{
	for (auto module : m_modules.rev_iter()) {
		m_modules.removeItem(&module);
		mir_free(module);
	}

	for (size_t i = 0; i < SQL_EVT_STMT_NUM; i++)
		sqlite3_finalize(evt_stmts_prep[i]);
}

LONG CDbxSQLite::GetEventCount(MCONTACT hContact)
{
	DBCachedContact *cc = (hContact)
		? m_cache->GetCachedContact(hContact)
		: &m_system;

	if (cc->HasCount())
		return cc->m_count;

	mir_cslock lock(m_csDbAccess);

	if (cc->IsMeta()) {
		if (cc->nSubs == 0) {
			cc->m_count = 0;
			return 0;
		}

		CMStringA query = "select count(1) from events where contact_id in (";
		for (int k = 0; k < cc->nSubs; k++)
			query.AppendFormat("%lu, ", cc->pSubs[k]);
		query.Delete(query.GetLength() - 2, 2);
		query.Append(") limit 1;");

		sqlite3_stmt *stmt = nullptr;
		sqlite3_prepare_v2(m_db, query, -1, &stmt, nullptr);
		int rc = sqlite3_step(stmt);
		assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			return 0;
		}
		cc->m_count = sqlite3_column_int64(stmt, 0);
		sqlite3_finalize(stmt);
		return cc->m_count;
	}

	sqlite3_stmt *stmt = evt_stmts_prep[SQL_EVT_STMT_COUNT];
	sqlite3_bind_int64(stmt, 1, hContact);
	int rc = sqlite3_step(stmt);
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	if (rc != SQLITE_ROW) {
		sqlite3_reset(stmt);
		return 0;
	}
	cc->m_count = sqlite3_column_int64(stmt, 0);
	sqlite3_reset(stmt);

	return cc->m_count;
}

MEVENT CDbxSQLite::AddEvent(MCONTACT hContact, const DBEVENTINFO *dbei)
{
	if (dbei == nullptr)
		return 0;

	if (dbei->timestamp == 0)
		return 0;

	MCONTACT hNotifyContact = hContact;
	DBCachedContact *cc, *ccSub = nullptr;
	if (hContact != 0) {
		if ((cc = m_cache->GetCachedContact(hContact)) == nullptr)
			return 0;

		if (cc->IsSub()) {
			ccSub = cc;
			if ((cc = m_cache->GetCachedContact(cc->parentID)) == nullptr)
				return 0;

			// set default sub to the event's source
			if (!(dbei->flags & DBEF_SENT))
				db_mc_setDefault(cc->contactID, hContact, false);
			if (db_mc_isEnabled())
				hNotifyContact = cc->contactID; // and add an event to a metahistory
		}
	}
	else cc = &m_system;

	if (cc == nullptr)
		return 0;

	if (m_safetyMode)
		if (NotifyEventHooks(g_hevEventFiltered, hNotifyContact, (LPARAM)dbei))
			return 0;

	MEVENT hDbEvent = 0;
	{
		const char *szEventId;
		DWORD dwFlags = dbei->flags;
		if (dbei->szId != nullptr) {
			dwFlags |= DBEF_HAS_ID;
			szEventId = dbei->szId;
		}
		else szEventId = "";

		mir_cslock lock(m_csDbAccess);
		sqlite3_stmt *stmt = evt_stmts_prep[SQL_EVT_STMT_ADDEVENT];
		sqlite3_bind_int64(stmt, 1, hContact);
		sqlite3_bind_text(stmt, 2, dbei->szModule, (int)mir_strlen(dbei->szModule), nullptr);
		sqlite3_bind_int64(stmt, 3, dbei->timestamp);
		sqlite3_bind_int(stmt, 4, dbei->eventType);
		sqlite3_bind_int64(stmt, 5, dwFlags);
		sqlite3_bind_blob(stmt, 6, dbei->pBlob, dbei->cbBlob, nullptr);
		sqlite3_bind_text(stmt, 7, szEventId, (int)mir_strlen(szEventId), nullptr);
		int rc = sqlite3_step(stmt);
		assert(rc == SQLITE_DONE);
		sqlite3_reset(stmt);
		
		hDbEvent = sqlite3_last_insert_rowid(m_db);

		stmt = evt_stmts_prep[SQL_EVT_STMT_ADDEVENT_SRT];
		sqlite3_bind_int64(stmt, 1, hDbEvent);
		sqlite3_bind_int64(stmt, 2, cc->contactID);
		sqlite3_bind_int64(stmt, 3, dbei->timestamp);
		rc = sqlite3_step(stmt);
		assert(rc == SQLITE_DONE);
		sqlite3_reset(stmt);

		cc->AddEvent(hDbEvent, dbei->timestamp, !dbei->markedRead());
		if (ccSub != nullptr)
		{
			stmt = evt_stmts_prep[SQL_EVT_STMT_ADDEVENT_SRT];
			sqlite3_bind_int64(stmt, 1, hDbEvent);
			sqlite3_bind_int64(stmt, 2, ccSub->contactID);
			sqlite3_bind_int64(stmt, 3, dbei->timestamp);
			rc = sqlite3_step(stmt);
			assert(rc == SQLITE_DONE);
			sqlite3_reset(stmt); //is this necessary ?

			ccSub->AddEvent(hDbEvent, dbei->timestamp, !dbei->markedRead());
		}

		char *module = m_modules.find((char*)dbei->szModule);
		if (module == nullptr)
			m_modules.insert(mir_strdup(dbei->szModule));
	}

	if (m_safetyMode && !(dbei->flags & DBEF_TEMPORARY))
		NotifyEventHooks(g_hevEventAdded, hNotifyContact, (LPARAM)hDbEvent);

	return hDbEvent;
}

BOOL CDbxSQLite::DeleteEvent(MEVENT hDbEvent)
{
	if (hDbEvent == 0)
		return 1;

	MEVENT hContact = GetEventContact(hDbEvent);
	DBCachedContact *cc = (hContact) ? m_cache->GetCachedContact(hContact) : &m_system;
	if (cc == nullptr)
		return 1;

	{
		mir_cslock lock(m_csDbAccess);
		sqlite3_stmt *stmt = evt_stmts_prep[SQL_EVT_STMT_DELETE];
		sqlite3_bind_int64(stmt, 1, hDbEvent);
		int rc = sqlite3_step(stmt);
		assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
		sqlite3_reset(stmt);
		if (rc != SQLITE_DONE)
			return 1;

		stmt = evt_stmts_prep[SQL_EVT_STMT_DELETE_SRT];
		sqlite3_bind_int64(stmt, 1, hDbEvent);
		rc = sqlite3_step(stmt);
		assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
		sqlite3_reset(stmt);
		if (rc != SQLITE_DONE)
			return 1;

		cc->DeleteEvent(hDbEvent);
		if (cc->IsSub() && (cc = m_cache->GetCachedContact(cc->parentID)))
			cc->DeleteEvent(hDbEvent);
	}

	NotifyEventHooks(g_hevEventDeleted, hContact, hDbEvent);

	return 0;
}

BOOL CDbxSQLite::EditEvent(MCONTACT hContact, MEVENT hDbEvent, const DBEVENTINFO *dbei)
{
	if (dbei == nullptr)
		return 1;

	if (dbei->timestamp == 0)
		return 1;

	DBCachedContact *cc = (hContact)
		? m_cache->GetCachedContact(hContact)
		: &m_system;
	if (cc == nullptr)
		return 1;

	{
		mir_cslock lock(m_csDbAccess);
		sqlite3_stmt *stmt = evt_stmts_prep[SQL_EVT_STMT_EDIT];
		sqlite3_bind_text(stmt, 1, dbei->szModule, (int)mir_strlen(dbei->szModule), nullptr);
		sqlite3_bind_int64(stmt, 2, dbei->timestamp);
		sqlite3_bind_int(stmt, 3, dbei->eventType);
		sqlite3_bind_int64(stmt, 4, dbei->flags);
		sqlite3_bind_blob(stmt, 5, dbei->pBlob, dbei->cbBlob, nullptr);
		sqlite3_bind_int64(stmt, 6, hDbEvent);
		int rc = sqlite3_step(stmt);
		assert(rc == SQLITE_DONE);
		sqlite3_reset(stmt);

		cc->EditEvent(hDbEvent, dbei->timestamp, !dbei->markedRead());
		if (cc->IsSub() && (cc = m_cache->GetCachedContact(cc->parentID)))
			cc->EditEvent(hDbEvent, dbei->timestamp, !dbei->markedRead());

		char *module = m_modules.find((char*)dbei->szModule);
		if (module == nullptr)
			m_modules.insert(mir_strdup(dbei->szModule));
	}

	NotifyEventHooks(g_hevEventEdited, hContact, (LPARAM)hDbEvent);
	return 0;
}

LONG CDbxSQLite::GetBlobSize(MEVENT hDbEvent)
{
	if (hDbEvent == 0)
		return -1;

	mir_cslock lock(m_csDbAccess);
	sqlite3_stmt *stmt = evt_stmts_prep[SQL_EVT_STMT_BLOBSIZE];
	sqlite3_bind_int(stmt, 1, hDbEvent);
	int rc = sqlite3_step(stmt);
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	if (rc != SQLITE_ROW) {
		sqlite3_reset(stmt);
		return -1;
	}
	LONG res = sqlite3_column_int64(stmt, 0);
	sqlite3_reset(stmt);
	return res;
}

BOOL CDbxSQLite::GetEvent(MEVENT hDbEvent, DBEVENTINFO *dbei)
{
	if (hDbEvent == 0)
		return 1;

	if (dbei == nullptr)
		return 1;

	if (dbei->cbBlob > 0 && dbei->pBlob == nullptr) {
		dbei->cbBlob = 0;
		return 1;
	}

	mir_cslock lock(m_csDbAccess);
	sqlite3_stmt *stmt = evt_stmts_prep[SQL_EVT_STMT_GET];
	sqlite3_bind_int64(stmt, 1, hDbEvent);
	int rc = sqlite3_step(stmt);
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	if (rc != SQLITE_ROW) {
		sqlite3_reset(stmt);
		return 1;
	}

	char *module = (char*)sqlite3_column_text(stmt, 0);
	dbei->szModule = m_modules.find(module);
	if (dbei->szModule == nullptr)
		return 1;

	dbei->timestamp = sqlite3_column_int64(stmt, 1);
	dbei->eventType = sqlite3_column_int(stmt, 2);
	dbei->flags = sqlite3_column_int64(stmt, 3);

	DWORD cbBlob = sqlite3_column_int64(stmt, 4);
	int bytesToCopy = (dbei->cbBlob < cbBlob) ? dbei->cbBlob : cbBlob;
	dbei->cbBlob = cbBlob;
	if (bytesToCopy && dbei->pBlob) {
		BYTE *data = (BYTE*)sqlite3_column_blob(stmt, 5);
		memcpy(dbei->pBlob, data, bytesToCopy);
	}
	sqlite3_reset(stmt);
	return 0;
}

BOOL CDbxSQLite::MarkEventRead(MCONTACT hContact, MEVENT hDbEvent)
{
	if (hDbEvent == 0)
		return -1;

	DBCachedContact *cc = (hContact)
		? m_cache->GetCachedContact(hContact)
		: &m_system;
	if (cc == nullptr)
		return -1;

	DWORD flags = 0;
	{
		mir_cslock lock(m_csDbAccess);
		sqlite3_stmt *stmt = evt_stmts_prep[SQL_EVT_STMT_GETFLAGS];
		sqlite3_bind_int64(stmt, 1, hDbEvent);
		int rc = sqlite3_step(stmt);
		assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
		if (rc != SQLITE_ROW) {
			sqlite3_reset(stmt);
			return -1;
		}
		flags = sqlite3_column_int64(stmt, 0);
		sqlite3_reset(stmt);
	}

	if ((flags & DBEF_READ) == DBEF_READ)
		return flags;

	flags |= DBEF_READ;
	{
		mir_cslock lock(m_csDbAccess);
		sqlite3_stmt *stmt = evt_stmts_prep[SQL_EVT_STMT_SETFLAGS];
		sqlite3_bind_int(stmt, 1, flags);
		sqlite3_bind_int64(stmt, 2, hDbEvent);
		int rc = sqlite3_step(stmt);
		assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
		sqlite3_reset(stmt);
		if (rc != SQLITE_DONE)
			return -1;

		cc->MarkRead(hDbEvent);
		if (cc->IsSub() && (cc = m_cache->GetCachedContact(cc->parentID)))
			cc->MarkRead(hDbEvent);
	}

	NotifyEventHooks(g_hevMarkedRead, hContact, (LPARAM)hDbEvent);

	return flags;
}

MCONTACT CDbxSQLite::GetEventContact(MEVENT hDbEvent)
{
	if (hDbEvent == 0)
		return INVALID_CONTACT_ID;

	mir_cslock lock(m_csDbAccess);
	sqlite3_stmt *stmt = evt_stmts_prep[SQL_EVT_STMT_GETCONTACT];
	sqlite3_bind_int64(stmt, 1, hDbEvent);
	int rc = sqlite3_step(stmt);
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	if (rc != SQLITE_ROW) {
		sqlite3_reset(stmt);
		return INVALID_CONTACT_ID;
	}
	MCONTACT hContact = sqlite3_column_int64(stmt, 0);
	sqlite3_reset(stmt);
	return hContact;
}

MEVENT CDbxSQLite::FindFirstEvent(MCONTACT hContact)
{
	DBCachedContact *cc = (hContact)
		? m_cache->GetCachedContact(hContact)
		: &m_system;
	if (cc == nullptr)
		return 0;


	evt_cnt_fwd = hContact;

	mir_cslock lock(m_csDbAccess);

	if (evt_cur_fwd)
	{
		sqlite3_reset(evt_cur_fwd);
	}

	evt_cur_fwd = evt_stmts_prep[SQL_EVT_STMT_FINDFIRST];
	sqlite3_bind_int64(evt_cur_fwd, 1, hContact);
	
	int rc = sqlite3_step(evt_cur_fwd);
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	if (rc != SQLITE_ROW) {
		//empty response
		//reset sql cursor
		sqlite3_reset(evt_cur_fwd);
		evt_cur_fwd = 0;
		//reset current contact
		evt_cnt_fwd = 0;
		return 0;
	}
	return sqlite3_column_int64(evt_cur_fwd, 0);
}

MEVENT CDbxSQLite::FindFirstUnreadEvent(MCONTACT hContact)
{
	DBCachedContact *cc = (hContact)
		? m_cache->GetCachedContact(hContact)
		: &m_system;
	if (cc == nullptr)
		return 0;

	if (cc->m_unread)
		return cc->m_unread;

	mir_cslock lock(m_csDbAccess);

	if (cc->IsMeta()) {
		if (cc->nSubs == 0) {
			cc->m_unread = 0;
			cc->m_unreadTimestamp = 0;
			return 0;
		}

		CMStringA query(FORMAT, "select id from events where (flags & %d) = 0 and contact_id in (", DBEF_READ | DBEF_SENT);
		for (int k = 0; k < cc->nSubs; k++)
			query.AppendFormat("%lu, ", cc->pSubs[k]);
		query.Delete(query.GetLength() - 2, 2);
		query.Append(") order by timestamp, id limit 1;");

		sqlite3_stmt *stmt;
		sqlite3_prepare_v2(m_db, query, -1, &stmt, nullptr);
		int rc = sqlite3_step(stmt);
		assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			return 0;
		}
		cc->m_unread = sqlite3_column_int64(stmt, 0);
		cc->m_unreadTimestamp = sqlite3_column_int64(stmt, 1);
		sqlite3_finalize(stmt);
		return cc->m_unread;
	}

	sqlite3_stmt *stmt = evt_stmts_prep[SQL_EVT_STMT_FINDFIRSTUNREAD];
	sqlite3_bind_int64(stmt, 1, hContact);
	sqlite3_bind_int(stmt, 2, DBEF_READ | DBEF_SENT);
	int rc = sqlite3_step(stmt);
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	if (rc != SQLITE_ROW) {
		sqlite3_reset(stmt);
		return 0;
	}
	cc->m_unread = sqlite3_column_int64(stmt, 0);
	cc->m_unreadTimestamp = sqlite3_column_int64(stmt, 1);
	sqlite3_reset(stmt);
	return cc->m_unread;
}

MEVENT CDbxSQLite::FindLastEvent(MCONTACT hContact)
{
	DBCachedContact *cc = (hContact)
		? m_cache->GetCachedContact(hContact)
		: &m_system;
	if (cc == nullptr)
		return 0;

	evt_cnt_backwd = hContact;

	mir_cslock lock(m_csDbAccess);

	if (evt_cur_backwd)
	{
		sqlite3_reset(evt_cur_backwd);
	}

	evt_cur_backwd = evt_stmts_prep[SQL_EVT_STMT_FINDLAST];
	sqlite3_bind_int64(evt_cur_backwd, 1, hContact);
	int rc = sqlite3_step(evt_cur_backwd);
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	if (rc != SQLITE_ROW) {
		//empty response
		//reset sql cursor
		sqlite3_reset(evt_cur_backwd);
		evt_cur_backwd = 0;
		//reset current contact
		evt_cnt_backwd = 0;
		return 0;
	}
	return sqlite3_column_int64(evt_cur_backwd, 0);
}

MEVENT CDbxSQLite::FindNextEvent(MCONTACT hContact, MEVENT hDbEvent)
{
	if (hDbEvent == 0)
		return 0;

	DBCachedContact *cc = m_cache->GetCachedContact(hContact);
	if (cc == nullptr)
		return 0;

	if (!evt_cur_fwd)
	{
		return 0;
	}
	if (hContact != evt_cnt_fwd)
	{
		return 0;
	}

	while (hDbEvent !=  sqlite3_column_int64(evt_cur_fwd, 0))
	{
		int rc = sqlite3_step(evt_cur_fwd);
		assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
		if (rc == SQLITE_DONE)
		{
			//reset sql cursor
			sqlite3_reset(evt_cur_fwd);
			evt_cur_fwd = 0;
			//reset current contact
			evt_cnt_fwd = 0;
			return 0;
		}
	}

	int rc = sqlite3_step(evt_cur_fwd);
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	if (rc != SQLITE_ROW) {
		//reset sql cursor
		sqlite3_reset(evt_cur_fwd);
		evt_cur_fwd = 0;
		//reset current contact
		evt_cnt_fwd = 0;
		return 0;
	} 
	hDbEvent = sqlite3_column_int64(evt_cur_fwd, 0);

	return hDbEvent;
}

MEVENT CDbxSQLite::FindPrevEvent(MCONTACT hContact, MEVENT hDbEvent)
{
	if (hDbEvent == 0)
		return 0;

	DBCachedContact *cc = m_cache->GetCachedContact(hContact);
	if (cc == nullptr)
		return 0;

	if (!evt_cur_backwd)
	{
		return 0;
	}
	if (hContact != evt_cnt_backwd)
	{
		return 0;
	}

	while (hDbEvent != sqlite3_column_int64(evt_cur_backwd, 0))
	{
		int rc = sqlite3_step(evt_cur_backwd);
		assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
		if (rc == SQLITE_DONE)
		{
			//reset sql cursor
			sqlite3_reset(evt_cur_backwd);
			evt_cur_backwd = 0;
			//reset current contact
			evt_cnt_backwd = 0;
			return 0;
		}
	}

	int rc = sqlite3_step(evt_cur_backwd);
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	if (rc != SQLITE_ROW) {
		//reset sql cursor
		sqlite3_reset(evt_cur_backwd);
		evt_cur_backwd = 0;
		//reset current contact
		evt_cnt_backwd = 0;
		return 0;
	}
	hDbEvent = sqlite3_column_int64(evt_cur_backwd, 0);

	return hDbEvent;
}

MEVENT CDbxSQLite::GetEventById(LPCSTR szModule, LPCSTR szId)
{
	if (szModule == nullptr || szId == nullptr)
		return 0;

	mir_cslock lock(m_csDbAccess);
	sqlite3_stmt *stmt = evt_stmts_prep[SQL_EVT_STMT_GETIDBYSRVID];
	sqlite3_bind_text(stmt, 1, szModule, (int)mir_strlen(szModule), nullptr);
	sqlite3_bind_text(stmt, 2, szId, (int)mir_strlen(szId), nullptr);
	int rc = sqlite3_step(stmt);
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	if (rc != SQLITE_ROW) {
		sqlite3_reset(stmt);
		return 0;
	}
	MEVENT hDbEvent = sqlite3_column_int64(stmt, 0);
	sqlite3_reset(stmt);
	return hDbEvent;
}

BOOL CDbxSQLite::MetaMergeHistory(DBCachedContact *ccMeta, DBCachedContact *ccSub)
{
	//TODO: test this
	mir_cslock lock(m_csDbAccess);
	sqlite3_stmt *stmt = evt_stmts_prep[SQL_EVT_STMT_META_MERGE_SELECT];
	sqlite3_bind_int64(stmt, 1, ccSub->contactID);
	int rc = sqlite3_step(stmt);
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	while (rc == SQLITE_ROW)
	{
		sqlite3_stmt *stmt2 = evt_stmts_prep[SQL_EVT_STMT_ADDEVENT_SRT];
		sqlite3_bind_int64(stmt2, 1, sqlite3_column_int64(stmt, 0));
		sqlite3_bind_int64(stmt2, 2, ccMeta->contactID);
		sqlite3_bind_int64(stmt2, 3, sqlite3_column_int64(stmt, 1));
		int rc2 = sqlite3_step(stmt2);
		assert(rc2 == SQLITE_ROW || rc == SQLITE_DONE);
		sqlite3_reset(stmt2);
		rc = sqlite3_step(stmt);
		assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	}
	sqlite3_reset(stmt);


	return TRUE;
}

BOOL CDbxSQLite::MetaSplitHistory(DBCachedContact *ccMeta, DBCachedContact*)
{
	mir_cslock lock(m_csDbAccess);
	sqlite3_stmt *stmt = evt_stmts_prep[SQL_EVT_STMT_META_SPLIT];
	sqlite3_bind_int64(stmt, 1, ccMeta->contactID);
	int rc = sqlite3_step(stmt);
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	sqlite3_reset(stmt);
	if (rc != SQLITE_DONE)
		return 1;

	return TRUE;
}

STDMETHODIMP_(DB::EventCursor*) CDbxSQLite::EventCursor(MCONTACT hContact, MEVENT hDbEvent)
{
	return new CDbxSQLiteEventCursor(hContact, m_db, hDbEvent);
}

STDMETHODIMP_(DB::EventCursor*) CDbxSQLite::EventCursorRev(MCONTACT hContact, MEVENT hDbEvent)
{
	return new CDbxSQLiteEventCursor(hContact, m_db, hDbEvent, true);
}

CDbxSQLiteEventCursor::CDbxSQLiteEventCursor(MCONTACT _1, sqlite3* _db, MEVENT hDbEvent, bool reverse)
	: EventCursor(_1), m_db(_db)
{
	if (reverse)
	{
		if (!hDbEvent)
			sqlite3_prepare_v2(m_db, reverse_order_query, -1, &cursor, nullptr);
		else
			sqlite3_prepare_v2(m_db, reverse_order_pos_query, -1, &cursor, nullptr);
	}
	else
	{
		if (!hDbEvent)
			sqlite3_prepare_v2(m_db, normal_order_query, -1, &cursor, nullptr);
		else
			sqlite3_prepare_v2(m_db, normal_order_pos_query, -1, &cursor, nullptr);
	}
	sqlite3_bind_int64(cursor, 1, hContact);
	if (hDbEvent)
		sqlite3_bind_int64(cursor, 2, hDbEvent);
}

CDbxSQLiteEventCursor::~CDbxSQLiteEventCursor()
{
	if (cursor)
		sqlite3_reset(cursor);
}

MEVENT CDbxSQLiteEventCursor::FetchNext()
{
	if (!cursor)
		return 0;
	int rc = sqlite3_step(cursor);
	assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
	if (rc != SQLITE_ROW) {
		//empty response
		//reset sql cursor
		sqlite3_reset(cursor);
		cursor = nullptr;
		return 0;
	}
	return sqlite3_column_int64(cursor, 0);
}

#include "..\stdafx.h"
#include "dropbox_api.h"

CDropboxService::CDropboxService(const char *protoName, const wchar_t *userName)
	: CCloudService(protoName, userName)
{
	m_hProtoIcon = GetIconHandle(IDI_DROPBOX);
}

CDropboxService* CDropboxService::Init(const char *moduleName, const wchar_t *userName)
{
	CDropboxService *proto = new CDropboxService(moduleName, userName);
	Services.insert(proto);
	return proto;
}

int CDropboxService::UnInit(CDropboxService *proto)
{
	Services.remove(proto);
	delete proto;
	return 0;
}

const char* CDropboxService::GetModuleName() const
{
	return "Dropbox";
}

int CDropboxService::GetIconId() const
{
	return IDI_DROPBOX;
}

bool CDropboxService::IsLoggedIn()
{
	ptrA token(db_get_sa(NULL, GetAccountName(), "TokenSecret"));
	if (!token || token[0] == 0)
		return false;
	return true;
}

void CDropboxService::Login()
{
	COAuthDlg(this, DROPBOX_API_AUTH, RequestAccessTokenThread).DoModal();
}

void CDropboxService::Logout()
{
	mir_forkthreadex(RevokeAccessTokenThread, this);
}

unsigned CDropboxService::RequestAccessTokenThread(void *owner, void *param)
{
	HWND hwndDlg = (HWND)param;
	CDropboxService *service = (CDropboxService*)owner;

	if (service->IsLoggedIn())
		service->Logout();

	char requestToken[128];
	GetDlgItemTextA(hwndDlg, IDC_OAUTH_CODE, requestToken, _countof(requestToken));

	DropboxAPI::GetAccessTokenRequest request(requestToken);
	NLHR_PTR response(request.Send(service->m_hConnection));

	if (response == nullptr || response->resultCode != HTTP_CODE_OK) {
		Netlib_Logf(service->m_hConnection, "%s: %s", service->GetAccountName(), service->HttpStatusToError());
		//ShowNotification(TranslateT("server does not respond"), MB_ICONERROR);
		return 0;
	}

	JSONNode root = JSONNode::parse(response->pData);
	if (root.empty()) {
		Netlib_Logf(service->m_hConnection, "%s: %s", service->GetAccountName(), service->HttpStatusToError(response->resultCode));
		//ShowNotification((wchar_t*)error_description, MB_ICONERROR);
		return 0;
	}

	JSONNode node = root.at("error_description");
	if (!node.isnull()) {
		ptrW error_description(mir_a2u_cp(node.as_string().c_str(), CP_UTF8));
		Netlib_Logf(service->m_hConnection, "%s: %s", service->GetAccountName(), service->HttpStatusToError(response->resultCode));
		//ShowNotification((wchar_t*)error_description, MB_ICONERROR);
		return 0;
	}

	node = root.at("access_token");
	db_set_s(NULL, service->GetAccountName(), "TokenSecret", node.as_string().c_str());
	//ProtoBroadcastAck(MODULE, NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, (HANDLE)ID_STATUS_OFFLINE, (WPARAM)ID_STATUS_ONLINE);

	SetDlgItemTextA(hwndDlg, IDC_OAUTH_CODE, "");

	EndDialog(hwndDlg, 1);
	return 0;
}

unsigned CDropboxService::RevokeAccessTokenThread(void *param)
{
	CDropboxService *service = (CDropboxService*)param;

	ptrA token(db_get_sa(NULL, service->GetAccountName(), "TokenSecret"));
	DropboxAPI::RevokeAccessTokenRequest request(token);
	NLHR_PTR response(request.Send(service->m_hConnection));

	return 0;
}

void CDropboxService::HandleJsonError(JSONNode &node)
{
	JSONNode error = node.at("error");
	if (!error.isnull()) {
		json_string tag = error.at(".tag").as_string();
		throw Exception(tag.c_str());
	}
}

void CDropboxService::UploadFile(const char *data, size_t size, CMStringA &path)
{
	ptrA token(db_get_sa(NULL, GetAccountName(), "TokenSecret"));
	BYTE strategy = db_get_b(NULL, MODULE, "ConflictStrategy", OnConflict::REPLACE);
	DropboxAPI::UploadFileRequest request(token, path, data, size, (OnConflict)strategy);
	NLHR_PTR response(request.Send(m_hConnection));

	JSONNode root = GetJsonResponse(response);
	if (root)
		path = root["path_lower"].as_string().c_str();
}

void CDropboxService::CreateUploadSession(const char *chunk, size_t chunkSize, CMStringA &sessionId)
{
	ptrA token(db_get_sa(NULL, GetAccountName(), "TokenSecret"));
	DropboxAPI::CreateUploadSessionRequest request(token, chunk, chunkSize);
	NLHR_PTR response(request.Send(m_hConnection));

	JSONNode root = GetJsonResponse(response);
	if (root)
		sessionId = root["session_id"].as_string().c_str();
}

void CDropboxService::UploadFileChunk(const char *chunk, size_t chunkSize, const char *sessionId, size_t offset)
{
	ptrA token(db_get_sa(NULL, GetAccountName(), "TokenSecret"));
	DropboxAPI::UploadFileChunkRequest request(token, sessionId, offset, chunk, chunkSize);
	NLHR_PTR response(request.Send(m_hConnection));
	HandleHttpError(response);
}

void CDropboxService::CommitUploadSession(const char *data, size_t size, const char *sessionId, size_t offset, CMStringA &path)
{
	ptrA token(db_get_sa(NULL, GetAccountName(), "TokenSecret"));
	BYTE strategy = db_get_b(NULL, MODULE, "ConflictStrategy", OnConflict::REPLACE);
	DropboxAPI::CommitUploadSessionRequest request(token, sessionId, offset, path, data, size, (OnConflict)strategy);
	NLHR_PTR response(request.Send(m_hConnection));

	JSONNode root = GetJsonResponse(response);
	if (root)
		path = root["path_lower"].as_string().c_str();
}

void CDropboxService::CreateFolder(const char *path)
{
	ptrA token(db_get_sa(NULL, GetAccountName(), "TokenSecret"));
	DropboxAPI::CreateFolderRequest request(token, path);
	NLHR_PTR response(request.Send(m_hConnection));

	HandleHttpError(response);

	// forder exists on server 
	if (response->resultCode == HTTP_CODE_FORBIDDEN)
		return;

	GetJsonResponse(response);
}

void CDropboxService::CreateSharedLink(const char *path, CMStringA &url)
{
	ptrA token(db_get_sa(NULL, GetAccountName(), "TokenSecret"));
	DropboxAPI::CreateSharedLinkRequest shareRequest(token, path);
	NLHR_PTR response(shareRequest.Send(m_hConnection));

	if (response == nullptr)
		throw Exception(HttpStatusToError());

	if (!HTTP_CODE_SUCCESS(response->resultCode) &&
		response->resultCode != HTTP_CODE_CONFLICT) {
		if (response->dataLength)
			throw Exception(response->pData);
		throw Exception(HttpStatusToError(response->resultCode));
	}

	JSONNode root = JSONNode::parse(response->pData);
	if (root.isnull())
		throw Exception(HttpStatusToError());

	JSONNode error = root.at("error");
	if (error.isnull()) {
		JSONNode link = root.at("url");
		url = link.as_string().c_str();
		return;
	}

	json_string tag = error.at(".tag").as_string();
	if (tag != "shared_link_already_exists")
		throw Exception(tag.c_str());

	DropboxAPI::GetSharedLinkRequest getRequest(token, path);
	response = getRequest.Send(m_hConnection);

	root = GetJsonResponse(response);

	JSONNode links = root.at("links").as_array();
	JSONNode link = links[(size_t)0].at("url");
	url = link.as_string().c_str();
}

UINT CDropboxService::Upload(FileTransferParam *ftp)
{
	if (!IsLoggedIn())
		Login();

	try {
		if (ftp->IsFolder()) {
			T2Utf folderName(ftp->GetFolderName());

			CMStringA path;
			PreparePath(folderName, path);
			CreateFolder(path);

			CMStringA link;
			CreateSharedLink(path, link);
			ftp->AppendFormatData(L"%s\r\n", ptrW(mir_utf8decodeW(link)));
			ftp->AddSharedLink(link);
		}

		ftp->FirstFile();
		do
		{
			T2Utf fileName(ftp->GetCurrentRelativeFilePath());
			uint64_t fileSize = ftp->GetCurrentFileSize();

			size_t chunkSize = ftp->GetCurrentFileChunkSize();
			mir_ptr<char>chunk((char*)mir_calloc(chunkSize));

			CMStringA path;
			const wchar_t *serverFolder = ftp->GetServerFolder();
			if (serverFolder) {
				char serverPath[MAX_PATH] = { 0 };
				mir_snprintf(serverPath, "%s\\%s", T2Utf(serverFolder), fileName);
				PreparePath(serverPath, path);
			}
			else PreparePath(fileName, path);

			if (chunkSize == fileSize)
			{
				ftp->CheckCurrentFile();
				size_t size = ftp->ReadCurrentFile(chunk, chunkSize);

				UploadFile(chunk, size, path);

				ftp->Progress(size);
			}
			else
			{
				ftp->CheckCurrentFile();
				size_t size = ftp->ReadCurrentFile(chunk, chunkSize);

				CMStringA sessionId;
				CreateUploadSession(chunk, size, sessionId);

				ftp->Progress(size);

				size_t offset = size;
				double chunkCount = ceil(double(fileSize) / chunkSize) - 2;
				while (chunkCount > 0) {
					ftp->CheckCurrentFile();

					size = ftp->ReadCurrentFile(chunk, chunkSize);
					UploadFileChunk(chunk, size, sessionId, offset);

					offset += size;
					ftp->Progress(size);
				}

				ftp->CheckCurrentFile();
				size = offset < fileSize
					? ftp->ReadCurrentFile(chunk, fileSize - offset)
					: 0;

				CommitUploadSession(chunk, size, sessionId, offset, path);

				ftp->Progress(size);
			}

			if (!ftp->IsFolder()) {
				CMStringA link;
				CreateSharedLink(path, link);
				ftp->AppendFormatData(L"%s\r\n", ptrW(mir_utf8decodeW(link)));
				ftp->AddSharedLink(link);
			}
		} while (ftp->NextFile());
	}
	catch (Exception &ex) {
		Netlib_Logf(m_hConnection, "%s: %s", MODULE, ex.what());
		ftp->SetStatus(ACKRESULT_FAILED);
		return ACKRESULT_FAILED;
	}

	ftp->SetStatus(ACKRESULT_SUCCESS);
	return ACKRESULT_SUCCESS;
}

/*
 * This code implements window handling (connection error)
 *
 * (c) majvan 2002,2004
 */

#include "../stdafx.h"

#define BADCONNECTTITLE LPGEN("%s - connection error")
#define BADCONNECTMSG LPGEN("An error occurred. Error code: %d")//is in use?

//--------------------------------------------------------------------------------------------------

LRESULT CALLBACK BadConnectPopupProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	DWORD PluginParam;
	switch (msg) {
	case WM_COMMAND:
		// if clicked and it's new mail popup window
		if ((HIWORD(wParam) == STN_CLICKED) && (CallService(MS_POPUP_GETPLUGINDATA, (WPARAM)hWnd, (LPARAM)&PluginParam))) {
			PROCESS_INFORMATION pi;
			STARTUPINFOW si;
			memset(&si, 0, sizeof(si));
			si.cb = sizeof(si);
			HACCOUNT ActualAccount = (HACCOUNT)CallService(MS_POPUP_GETPLUGINDATA, (WPARAM)hWnd, 0);
#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"PopupProc:LEFTCLICK:ActualAccountSO-read wait\n");
#endif
			if (WAIT_OBJECT_0 == WaitToReadFcn(ActualAccount->AccountAccessSO)) {
#ifdef DEBUG_SYNCHRO
				DebugLog(SynchroFile,"PopupProc:LEFTCLICK:ActualAccountSO-read enter\n");
#endif
				if (ActualAccount->BadConnectN.App != NULL) {
					WCHAR *Command;
					if (ActualAccount->BadConnectN.AppParam != NULL)
						Command = new WCHAR[mir_wstrlen(ActualAccount->BadConnectN.App) + mir_wstrlen(ActualAccount->BadConnectN.AppParam) + 6];
					else
						Command = new WCHAR[mir_wstrlen(ActualAccount->BadConnectN.App) + 6];

					if (Command != NULL) {
						mir_wstrcpy(Command, L"\"");
						mir_wstrcat(Command, ActualAccount->BadConnectN.App);
						mir_wstrcat(Command, L"\" ");
						if (ActualAccount->BadConnectN.AppParam != NULL)
							mir_wstrcat(Command, ActualAccount->BadConnectN.AppParam);
						CreateProcessW(NULL, Command, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi);
						delete[] Command;
					}
				}
#ifdef DEBUG_SYNCHRO
				DebugLog(SynchroFile,"PopupProc:LEFTCLICK:ActualAccountSO-read done\n");
#endif
				ReadDoneFcn(ActualAccount->AccountAccessSO);
			}
#ifdef DEBUG_SYNCHRO
			else
				DebugLog(SynchroFile,"PopupProc:LEFTCLICK:ActualAccountSO-read enter failed\n");
#endif
			PUDeletePopup(hWnd);
		}
		break;

	case UM_FREEPLUGINDATA:
		//Here we'd free our own data, if we had it.
		return FALSE;

	case UM_INITPOPUP:
		//This is the equivalent to WM_INITDIALOG you'd get if you were the maker of dialog popups.
		break;
	case WM_CONTEXTMENU:
		PUDeletePopup(hWnd);
		break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

INT_PTR CALLBACK DlgProcYAMNBadConnection(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_INITDIALOG:
	{
		BOOL ShowPopup, ShowMsg, ShowIco;
		HACCOUNT ActualAccount;
		DWORD  ErrorCode;
		char* TitleStrA;
		char *Message1A = NULL;
		wchar_t *Message1W = NULL;
		POPUPDATAT BadConnectPopup;

		ActualAccount = ((struct BadConnectionParam *)lParam)->account;
		ErrorCode = ((struct BadConnectionParam *)lParam)->errcode;
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"BadConnect:ActualAccountSO-read wait\n");
#endif
		if (WAIT_OBJECT_0 != WaitToReadFcn(ActualAccount->AccountAccessSO))
		{
#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"BadConnect:ActualAccountSO-read wait failed\n");
#endif
			return FALSE;
		}
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"BadConnect:ActualAccountSO-read enter\n");
#endif
		int size = (int)(mir_strlen(ActualAccount->Name) + mir_strlen(Translate(BADCONNECTTITLE)));
		TitleStrA = new char[size];
		mir_snprintf(TitleStrA, size, Translate(BADCONNECTTITLE), ActualAccount->Name);

		ShowPopup = ActualAccount->BadConnectN.Flags & YAMN_ACC_POP;
		ShowMsg = ActualAccount->BadConnectN.Flags & YAMN_ACC_MSG;
		ShowIco = ActualAccount->BadConnectN.Flags & YAMN_ACC_ICO;

		if (ShowPopup) {
			BadConnectPopup.lchIcon = g_LoadIconEx(3);
			BadConnectPopup.colorBack = ActualAccount->BadConnectN.Flags & YAMN_ACC_POPC ? ActualAccount->BadConnectN.PopupB : GetSysColor(COLOR_BTNFACE);
			BadConnectPopup.colorText = ActualAccount->BadConnectN.Flags & YAMN_ACC_POPC ? ActualAccount->BadConnectN.PopupT : GetSysColor(COLOR_WINDOWTEXT);
			BadConnectPopup.iSeconds = ActualAccount->BadConnectN.PopupTime;

			BadConnectPopup.PluginWindowProc = BadConnectPopupProc;
			BadConnectPopup.PluginData = ActualAccount;
			mir_wstrncpy(BadConnectPopup.lptzContactName, _A2T(ActualAccount->Name), _countof(BadConnectPopup.lptzContactName));
		}

		if (ActualAccount->Plugin->Fcn != NULL && ActualAccount->Plugin->Fcn->GetErrorStringWFcnPtr != NULL) {
			Message1W = ActualAccount->Plugin->Fcn->GetErrorStringWFcnPtr(ErrorCode);
			SetDlgItemText(hDlg, IDC_STATICMSG, Message1W);
			wcsncpy_s(BadConnectPopup.lptzText, Message1W, _TRUNCATE);
			if (ShowPopup)
				PUAddPopupT(&BadConnectPopup);
		}
		else if (ActualAccount->Plugin->Fcn != NULL && ActualAccount->Plugin->Fcn->GetErrorStringAFcnPtr != NULL)
		{
			Message1W = ActualAccount->Plugin->Fcn->GetErrorStringWFcnPtr(ErrorCode);
			SetDlgItemText(hDlg, IDC_STATICMSG, Message1W);
			wcsncpy_s(BadConnectPopup.lptzText, Message1W, _TRUNCATE);
			if (ShowPopup)
				PUAddPopupT(&BadConnectPopup);
		}
		else
		{
			Message1W = TranslateT("Unknown error");
			SetDlgItemText(hDlg, IDC_STATICMSG, Message1W);
			wcsncpy_s(BadConnectPopup.lptzText, Message1W, _TRUNCATE);
			if (ShowPopup)
				PUAddPopupT(&BadConnectPopup);
		}

		if (!ShowMsg && !ShowIco)
			DestroyWindow(hDlg);
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"BadConnect:ActualAccountSO-read done\n");
#endif
		ReadDoneFcn(ActualAccount->AccountAccessSO);

		SetWindowTextA(hDlg, TitleStrA);
		delete[] TitleStrA;
		if (Message1A != NULL)
			delete[] Message1A;
		if (ActualAccount->Plugin->Fcn != NULL && ActualAccount->Plugin->Fcn->DeleteErrorStringFcnPtr != NULL && Message1A != NULL)
			ActualAccount->Plugin->Fcn->DeleteErrorStringFcnPtr(Message1A);
		if (ActualAccount->Plugin->Fcn != NULL && ActualAccount->Plugin->Fcn->DeleteErrorStringFcnPtr != NULL && Message1W != NULL)
			ActualAccount->Plugin->Fcn->DeleteErrorStringFcnPtr(Message1W);
		return 0;
	}
	case WM_DESTROY:
	{
		NOTIFYICONDATA nid;

		memset(&nid, 0, sizeof(NOTIFYICONDATA));
		nid.cbSize = sizeof(NOTIFYICONDATA);
		nid.hWnd = hDlg;
		nid.uID = 0;
		Shell_NotifyIcon(NIM_DELETE, &nid);
		PostQuitMessage(0);
		break;
	}
	case WM_YAMN_NOTIFYICON:
		switch (lParam)
		{
		case WM_LBUTTONDBLCLK:
			ShowWindow(hDlg, SW_SHOWNORMAL);
			SetForegroundWindow(hDlg);
			break;
		}
		return 0;
	case WM_CHAR:
		switch ((wchar_t)wParam)
		{
		case 27:
		case 13:
			DestroyWindow(hDlg);
			break;
		}
		break;
	case WM_SYSCOMMAND:
		switch (wParam)
		{
		case SC_CLOSE:
			DestroyWindow(hDlg);
		}
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_BTNOK:
			DestroyWindow(hDlg);
		}
		break;
	}
	return 0;
}

void __cdecl BadConnection(void *Param)
{
	MSG msg;
	HWND hBadConnect;
	HACCOUNT ActualAccount;

	struct BadConnectionParam MyParam = *(struct BadConnectionParam *)Param;
	ActualAccount = MyParam.account;
#ifdef DEBUG_SYNCHRO
	DebugLog(SynchroFile,"BadConnect:Incrementing \"using threads\" %x (account %x)\n",ActualAccount->UsingThreads,ActualAccount);
#endif
	SCIncFcn(ActualAccount->UsingThreads);

	//	we will not use params in stack anymore
	SetEvent(MyParam.ThreadRunningEV);

	__try
	{
		hBadConnect = CreateDialogParam(YAMNVar.hInst, MAKEINTRESOURCE(IDD_DLGBADCONNECT), NULL, DlgProcYAMNBadConnection, (LPARAM)&MyParam);
		Window_SetIcon_IcoLib(hBadConnect, g_GetIconHandle(3));

#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"BadConnect:ActualAccountSO-read wait\n");
#endif
		if (WAIT_OBJECT_0 != WaitToReadFcn(ActualAccount->AccountAccessSO)) {
#ifdef DEBUG_SYNCHRO
			DebugLog(SynchroFile,"BadConnect:ActualAccountSO-read wait failed\n");
#endif
			return;
		}
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"BadConnect:ActualAccountSO-read enter\n");
#endif
		if (ActualAccount->BadConnectN.Flags & YAMN_ACC_SND)
			CallService(MS_SKIN_PLAYSOUND, 0, (LPARAM)YAMN_CONNECTFAILSOUND);

		if (ActualAccount->BadConnectN.Flags & YAMN_ACC_MSG)
			ShowWindow(hBadConnect, SW_SHOWNORMAL);

		if (ActualAccount->BadConnectN.Flags & YAMN_ACC_ICO) {
			NOTIFYICONDATA nid = {};
			nid.cbSize = sizeof(nid);
			nid.hWnd = hBadConnect;
			nid.hIcon = g_LoadIconEx(3);
			nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
			nid.uCallbackMessage = WM_YAMN_NOTIFYICON;
			mir_snwprintf(nid.szTip, L"%S%s", ActualAccount->Name, TranslateT(" - connection error"));
			Shell_NotifyIcon(NIM_ADD, &nid);
		}

#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"BadConnect:ActualAccountSO-read done\n");
#endif
		ReadDoneFcn(ActualAccount->AccountAccessSO);

		UpdateWindow(hBadConnect);
		while (GetMessage(&msg, NULL, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		//	now, write to file. Why? Because we want to write when was new mail last checked
		if ((ActualAccount->Plugin->Fcn != NULL) && (ActualAccount->Plugin->Fcn->WriteAccountsFcnPtr != NULL) && ActualAccount->AbleToWork)
			ActualAccount->Plugin->Fcn->WriteAccountsFcnPtr();
	}
	__finally
	{
#ifdef DEBUG_SYNCHRO
		DebugLog(SynchroFile,"BadConnect:Decrementing \"using threads\" %x (account %x)\n",ActualAccount->UsingThreads,ActualAccount);
#endif
		SCDecFcn(ActualAccount->UsingThreads);
	}
}


INT_PTR RunBadConnectionSvc(WPARAM wParam, LPARAM lParam)
{
	//an event for successfull copy parameters to which point a pointer in stack for new thread
	HANDLE ThreadRunningEV;
	PYAMN_BADCONNECTIONPARAM Param = (PYAMN_BADCONNECTIONPARAM)wParam;

	if ((DWORD)lParam != YAMN_BADCONNECTIONVERSION)
		return 0;

	if (NULL != (ThreadRunningEV = CreateEvent(NULL, FALSE, FALSE, NULL)))
	{
		HANDLE NewThread;

		Param->ThreadRunningEV = ThreadRunningEV;
		if (NULL != (NewThread = mir_forkthread(BadConnection, (void*)Param)))
		{
			WaitForSingleObject(ThreadRunningEV, INFINITE);
			CloseHandle(NewThread);
		}
		CloseHandle(ThreadRunningEV);

		return 1;
	}
	return 0;
}

#include "commonheaders.h"

static void sttWaitForExchange(LPVOID param)
{
	MCONTACT hContact = (UINT_PTR)param;
	pUinKey ptr = getUinKey(hContact);
	if (!ptr)
		return;

	for (int i = 0; i < g_plugin.getWord("ket", 10) * 10; i++) {
		Sleep(100);
		if (ptr->waitForExchange != 1)
			break;
	}

	Sent_NetLog("sttWaitForExchange: %d", ptr->waitForExchange);

	// if keyexchange failed or timeout
	if (ptr->waitForExchange == 1 || ptr->waitForExchange == 3) { // протухло - отправляем незашифрованно, если надо
		if (ptr->msgQueue && msgbox(nullptr, LPGEN("User has not answered to key exchange!\nYour messages are still in SecureIM queue, do you want to send them unencrypted now?"), MODULENAME, MB_YESNO | MB_ICONQUESTION) == IDYES) {
			mir_cslock lck(localQueueMutex);
			ptr->sendQueue = true;
			pWM ptrMessage = ptr->msgQueue;
			while (ptrMessage) {
				Sent_NetLog("Sent (unencrypted) message from queue: %s", ptrMessage->Message);

				// send unencrypted messages
				ProtoChainSend(ptr->hContact, PSS_MESSAGE, (WPARAM)ptrMessage->wParam | PREF_METANODB, (LPARAM)ptrMessage->Message);
				mir_free(ptrMessage->Message);
				pWM tmp = ptrMessage;
				ptrMessage = ptrMessage->nextMessage;
				mir_free(tmp);
			}
			ptr->msgQueue = nullptr;
			ptr->sendQueue = false;
		}
		ptr->waitForExchange = 0;
		ShowStatusIconNotify(ptr->hContact);
	}
	else if (ptr->waitForExchange == 2) { // дослать очередь через установленное соединение
		mir_cslock lck(localQueueMutex);
		// we need to resend last send back message with new crypto Key
		pWM ptrMessage = ptr->msgQueue;
		while (ptrMessage) {
			Sent_NetLog("Sent (encrypted) message from queue: %s", ptrMessage->Message);

			// send unencrypted messages
			ProtoChainSend(ptr->hContact, PSS_MESSAGE, (WPARAM)ptrMessage->wParam | PREF_METANODB, (LPARAM)ptrMessage->Message);
			mir_free(ptrMessage->Message);
			pWM tmp = ptrMessage;
			ptrMessage = ptrMessage->nextMessage;
			mir_free(tmp);
		}
		ptr->msgQueue = nullptr;
		ptr->waitForExchange = 0;
	}
	else if (ptr->waitForExchange == 0) { // очистить очередь
		mir_cslock lck(localQueueMutex);
		// we need to resend last send back message with new crypto Key
		pWM ptrMessage = ptr->msgQueue;
		while (ptrMessage) {
			mir_free(ptrMessage->Message);
			pWM tmp = ptrMessage;
			ptrMessage = ptrMessage->nextMessage;
			mir_free(tmp);
		}
		ptr->msgQueue = nullptr;
	}
}

// set wait flag and run thread
void waitForExchange(pUinKey ptr, int flag)
{
	switch (flag) {
	case 0: // reset
	case 2: // send secure
	case 3: // send unsecure
		if (ptr->waitForExchange)
			ptr->waitForExchange = flag;
		break;
	case 1: // launch
		if (ptr->waitForExchange)
			break;

		ptr->waitForExchange = 1;
		mir_forkthread(sttWaitForExchange, (void*)ptr->hContact);
		break;
	}
}

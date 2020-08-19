#include "StdAfx.h"
#include "CurrencyRatesProviderCurrencyConverter.h"

#define WINDOW_PREFIX "CurrenyConverter_"

#define DB_STR_CC_CURRENCYRATE_FROM_ID "CurrencyConverter_FromID"
#define DB_STR_CC_CURRENCYRATE_TO_ID "CurrencyConverter_ToID"
#define DB_STR_CC_AMOUNT "CurrencyConverter_Amount"

static CCurrencyRatesProviderCurrencyConverter *get_currency_converter_provider()
{
	for (auto &it : g_apProviders)
		if (auto p = dynamic_cast<CCurrencyRatesProviderCurrencyConverter*>(it))
			return p;

	assert(!"We should never get here!");
	return nullptr;
}

CCurrencyRateSection get_currencyrates(const CCurrencyRatesProviderCurrencyConverter* pProvider = nullptr)
{
	if (nullptr == pProvider)
		pProvider = get_currency_converter_provider();

	if (pProvider) {
		const auto& rCurrencyRates = pProvider->GetCurrencyRates();
		if (rCurrencyRates.GetSectionCount() > 0)
			return rCurrencyRates.GetSection(0);
	}

	return CCurrencyRateSection();
}

inline tstring make_currencyrate_name(const CCurrencyRate &rCurrencyRate)
{
	const tstring &rsDesc = rCurrencyRate.GetName();
	return((false == rsDesc.empty()) ? rsDesc : rCurrencyRate.GetSymbol());
}

inline void update_convert_button(HWND hDlg)
{
	int nFrom = static_cast<int>(::SendDlgItemMessage(hDlg, IDC_COMBO_CONVERT_FROM, CB_GETCURSEL, 0, 0));
	int nTo = static_cast<int>(::SendDlgItemMessage(hDlg, IDC_COMBO_CONVERT_INTO, CB_GETCURSEL, 0, 0));
	bool bEnableButton = ((CB_ERR != nFrom)
		&& (CB_ERR != nTo)
		&& (nFrom != nTo)
		&& (GetWindowTextLength(GetDlgItem(hDlg, IDC_EDIT_VALUE)) > 0));
	EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CONVERT), bEnableButton);
}

inline void update_swap_button(HWND hDlg)
{
	int nFrom = static_cast<int>(::SendDlgItemMessage(hDlg, IDC_COMBO_CONVERT_FROM, CB_GETCURSEL, 0, 0));
	int nTo = static_cast<int>(::SendDlgItemMessage(hDlg, IDC_COMBO_CONVERT_INTO, CB_GETCURSEL, 0, 0));
	bool bEnableButton = ((CB_ERR != nFrom)
		&& (CB_ERR != nTo)
		&& (nFrom != nTo));
	EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SWAP), bEnableButton);
}

inline tstring double2str(double dValue)
{
	tostringstream output;
	output.imbue(GetSystemLocale());
	output << std::fixed << std::setprecision(2) << dValue;
	return output.str();
}

inline bool str2double(const tstring& s, double& d)
{
	tistringstream input(s);
	input.imbue(GetSystemLocale());
	input >> d;
	return ((false == input.bad()) && (false == input.fail()));
}


INT_PTR CALLBACK CurrencyConverterDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_INITDIALOG:
		TranslateDialogDefault(hDlg);
		{
			MWindowList hWL = CModuleInfo::GetWindowList(WINDOW_PREFIX, false);
			assert(hWL);
			WindowList_Add(hWL, hDlg);

			Window_SetIcon_IcoLib(hDlg, CurrencyRates_GetIconHandle(IDI_ICON_CURRENCY_CONVERTER));

			HWND hcbxFrom = ::GetDlgItem(hDlg, IDC_COMBO_CONVERT_FROM);
			HWND hcbxTo = ::GetDlgItem(hDlg, IDC_COMBO_CONVERT_INTO);

			tstring sFromCurrencyRateID = CurrencyRates_DBGetStringW(NULL, MODULENAME, DB_STR_CC_CURRENCYRATE_FROM_ID);
			tstring sToCurrencyRateID = CurrencyRates_DBGetStringW(NULL, MODULENAME, DB_STR_CC_CURRENCYRATE_TO_ID);

			const auto pProvider = get_currency_converter_provider();
			const auto& rSection = get_currencyrates(pProvider);
			auto cCurrencyRates = rSection.GetCurrencyRateCount();
			for (auto i = 0u; i < cCurrencyRates; ++i) {
				const auto& rCurrencyRate = rSection.GetCurrencyRate(i);
				tstring sName = make_currencyrate_name(rCurrencyRate);
				LPCTSTR pszName = sName.c_str();
				LRESULT nFrom = ::SendMessage(hcbxFrom, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pszName));
				LRESULT nTo = ::SendMessage(hcbxTo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pszName));

				if (0 == mir_wstrcmpi(rCurrencyRate.GetID().c_str(), sFromCurrencyRateID.c_str())) {
					::SendMessage(hcbxFrom, CB_SETCURSEL, nFrom, 0);
				}

				if (0 == mir_wstrcmpi(rCurrencyRate.GetID().c_str(), sToCurrencyRateID.c_str())) {
					::SendMessage(hcbxTo, CB_SETCURSEL, nTo, 0);
				}
			}

			double dAmount = 1.0;
			CurrencyRates_DBReadDouble(NULL, MODULENAME, DB_STR_CC_AMOUNT, dAmount);
			::SetDlgItemText(hDlg, IDC_EDIT_VALUE, double2str(dAmount).c_str());

			const ICurrencyRatesProvider::CProviderInfo& pi = pProvider->GetInfo();
			tostringstream o;
			o << TranslateT("Info provided by") << L" <a href=\"" << pi.m_sURL << L"\">" << pi.m_sName << L"</a>";

			::SetDlgItemText(hDlg, IDC_SYSLINK_PROVIDER, o.str().c_str());

			::SendDlgItemMessage(hDlg, IDC_BUTTON_SWAP, BM_SETIMAGE, IMAGE_ICON, LPARAM(CurrencyRates_LoadIconEx(IDI_ICON_SWAP)));

			update_convert_button(hDlg);
			update_swap_button(hDlg);

			Utils_RestoreWindowPositionNoSize(hDlg, NULL, MODULENAME, WINDOW_PREFIX);
			::ShowWindow(hDlg, SW_SHOW);
		}
		return TRUE;

	case WM_CLOSE:
		{
			MWindowList hWL = CModuleInfo::GetWindowList(WINDOW_PREFIX, false);
			assert(hWL);
			WindowList_Remove(hWL, hDlg);
			Utils_SaveWindowPosition(hDlg, NULL, MODULENAME, WINDOW_PREFIX);
			EndDialog(hDlg, 0);
		}
		return TRUE;

	case WM_DESTROY:
		Window_FreeIcon_IcoLib(hDlg);
		break;

	case WM_COMMAND:
		switch (LOWORD(wp)) {
		case IDC_COMBO_CONVERT_FROM:
		case IDC_COMBO_CONVERT_INTO:
			if (CBN_SELCHANGE == HIWORD(wp)) {
				update_convert_button(hDlg);
				update_swap_button(hDlg);
			}
			return TRUE;
		
		case IDC_EDIT_VALUE:
			if (EN_CHANGE == HIWORD(wp))
				update_convert_button(hDlg);
			return TRUE;

		case IDCANCEL:
			SendMessage(hDlg, WM_CLOSE, 0, 0);
			return TRUE;

		case IDC_BUTTON_SWAP:
			{
				HWND wndFrom = ::GetDlgItem(hDlg, IDC_COMBO_CONVERT_FROM);
				HWND wndTo = ::GetDlgItem(hDlg, IDC_COMBO_CONVERT_INTO);
				WPARAM nFrom = ::SendMessage(wndFrom, CB_GETCURSEL, 0, 0);
				WPARAM nTo = ::SendMessage(wndTo, CB_GETCURSEL, 0, 0);

				::SendMessage(wndFrom, CB_SETCURSEL, nTo, 0);
				::SendMessage(wndTo, CB_SETCURSEL, nFrom, 0);
			}
			return TRUE;

		case IDC_BUTTON_CONVERT:
			{
				HWND hwndAmount = GetDlgItem(hDlg, IDC_EDIT_VALUE);
				tstring sText = get_window_text(hwndAmount);

				double dAmount = 1.0;
				if ((true == str2double(sText, dAmount)) && (dAmount > 0.0)) {
					CurrencyRates_DBWriteDouble(NULL, MODULENAME, DB_STR_CC_AMOUNT, dAmount);

					size_t nFrom = static_cast<size_t>(::SendDlgItemMessage(hDlg, IDC_COMBO_CONVERT_FROM, CB_GETCURSEL, 0, 0));
					size_t nTo = static_cast<size_t>(::SendDlgItemMessage(hDlg, IDC_COMBO_CONVERT_INTO, CB_GETCURSEL, 0, 0));
					if ((CB_ERR != nFrom) && (CB_ERR != nTo) && (nFrom != nTo)) {
						const auto& rSection = get_currencyrates();
						size_t cCurrencyRates = rSection.GetCurrencyRateCount();
						if ((nFrom < cCurrencyRates) && (nTo < cCurrencyRates)) {
							auto from = rSection.GetCurrencyRate(nFrom);
							auto to = rSection.GetCurrencyRate(nTo);

							g_plugin.setWString(DB_STR_CC_CURRENCYRATE_FROM_ID, from.GetID().c_str());
							g_plugin.setWString(DB_STR_CC_CURRENCYRATE_TO_ID, to.GetID().c_str());

							const auto pProvider = get_currency_converter_provider();
							assert(pProvider);
							if (pProvider) {
								tstring sResult;
								std::string sError;
								try {
									double dResult = pProvider->Convert(dAmount, from, to);
									tostringstream ss;
									ss.imbue(GetSystemLocale());
									ss << std::fixed << std::setprecision(2) << dAmount << " " << from.GetName() << " = " << dResult << " " << to.GetName();
									sResult = ss.str();
								}
								catch (std::exception& e) {
									sError = e.what();
								}

								if (false == sError.empty())
									sResult = currencyrates_a2t(sError.c_str());//A2T(sError.c_str());

								SetDlgItemText(hDlg, IDC_EDIT_RESULT, sResult.c_str());
							}
						}
					}
				}
				else {
					CurrencyRates_MessageBox(hDlg, TranslateT("Enter positive number."), MB_OK | MB_ICONERROR);
					prepare_edit_ctrl_for_error(GetDlgItem(hDlg, IDC_EDIT_VALUE));
				}
			}
			return TRUE;
		}
		break;

	case WM_NOTIFY:
		LPNMHDR pNMHDR = reinterpret_cast<LPNMHDR>(lp);
		switch (pNMHDR->code) {
		case NM_CLICK:
			if (IDC_SYSLINK_PROVIDER == wp) {
				PNMLINK pNMLink = reinterpret_cast<PNMLINK>(pNMHDR);
				::ShellExecute(hDlg, L"open", pNMLink->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
			}
			break;
		}
		break;
	}
	return (FALSE);
}

INT_PTR CurrencyRatesMenu_CurrencyConverter(WPARAM, LPARAM)
{
	MWindowList hWL = CModuleInfo::GetWindowList(WINDOW_PREFIX, true);
	HWND hWnd = WindowList_Find(hWL, NULL);
	if (nullptr != hWnd) {
		SetForegroundWindow(hWnd);
		SetFocus(hWnd);
	}
	else CreateDialogParam(g_plugin.getInst(), MAKEINTRESOURCE(IDD_CURRENCY_CONVERTER), nullptr, CurrencyConverterDlgProc, 0);

	return 0;
}

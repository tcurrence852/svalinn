// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include <NTSecAPI.h> // PUNICODE_STRING
#include <string> // string operations
#include <iostream> // logging operations
#include <fstream> // logging operations
#include <winhttp.h> // http actions

#pragma comment(lib, "winhttp.lib") // required for winhttp

// SUBKEY is where passfilt configuration is stored
const std::wstring SUBKEY = L"SOFTWARE\\passfilt";
const char *ERRLOG_PATH = (char *)"C:\\Windows\\temp\\pfilterr.log"; 

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

// config_t represents the configuration read from the registry
struct config_t {
	std::wstring srv;
	DWORD port;
	DWORD enableTLS;
	DWORD disableTLSValidation;
};


// readConfig parses the configuration from the registry and passes
// it back in the provided config_t struct
 LONG readConfig(config_t *conf) {
	 LONG rCode;
	 DWORD dataSize{};

	 std::ofstream errlog; // error log
	 errlog.open(ERRLOG_PATH, std::ios::app);

	 std::wstring value = L"server";

	 // Get the server address...
	 rCode = RegGetValue(HKEY_LOCAL_MACHINE, 
		 SUBKEY.c_str(), 
		 value.c_str(), 
		 RRF_RT_REG_SZ, 
		 nullptr, 
		 nullptr, 
		 &dataSize);

	 if (rCode != ERROR_SUCCESS) {
		 errlog << "Failed to read the size of server with errcode: " << rCode << "\n";
		 return rCode;
	 }

	 // change the size of srv to receive the value of the registry key
	 conf->srv.resize(dataSize / sizeof(wchar_t));
	 rCode = RegGetValue(
		 HKEY_LOCAL_MACHINE,
		 SUBKEY.c_str(),
		 value.c_str(),
		 RRF_RT_REG_SZ,
		 nullptr,
		 &conf->srv[0],
		 &dataSize
	 );

	 if (rCode != ERROR_SUCCESS) {
		 errlog << "Failed to read server value with errcode: " << rCode << "\n";
		 return rCode;
	 }

	 value = L"port";
	 // retrieve the port to access
	 dataSize = sizeof(conf->port);
	 rCode = RegGetValue(
		 HKEY_LOCAL_MACHINE,
		 SUBKEY.c_str(),
		 value.c_str(),
		 RRF_RT_REG_DWORD,
		 nullptr,
		 &conf->port,
		 &dataSize
	 );

	 if (rCode != ERROR_SUCCESS) {
		 errlog << "Failed to read port value with errcode: " << rCode << "\n";
		 return rCode;
	 }

	 value = L"enable tls";
	 // retrieve whether or not to enable TLS
	 dataSize = sizeof(conf->enableTLS);
	 rCode = RegGetValue(
		 HKEY_LOCAL_MACHINE,
		 SUBKEY.c_str(),
		 value.c_str(),
		 RRF_RT_REG_DWORD,
		 nullptr,
		 &conf->enableTLS,
		 &dataSize
	 );

	 if (rCode != ERROR_SUCCESS) {
		 errlog << "Failed to read tls configuration with errcode: " << rCode << "\n";
		 return rCode;
	 }

	 value = L"disable tls validation";
	 // retrieve whether or not to enable TLS
	 dataSize = sizeof(conf->disableTLSValidation);
	 rCode = RegGetValue(
		 HKEY_LOCAL_MACHINE,
		 SUBKEY.c_str(),
		 value.c_str(),
		 RRF_RT_REG_DWORD,
		 nullptr,
		 &conf->disableTLSValidation,
		 &dataSize
	 );

	 errlog.close();
	 
	 return rCode;
}

//
// InitializeChangeNotify is used to determine if the password filter is ready
// for use. In this case, we simply return TRUE always.
//
extern "C" __declspec(dllexport) BOOLEAN __stdcall InitializeChangeNotify(void) {
	return TRUE;
}

// 
// PasswordChangeNotify notifies that a password has been changed.
// 
extern "C" __declspec(dllexport) int __stdcall
PasswordChangeNotify(PUNICODE_STRING *UserName,
	ULONG RelativeId,
	PUNICODE_STRING *NewPassword) {
	return 0;
}

//
// PasswordFilter is called during the password change process. It returns TRUE to
// permit a password change and FALSE to reject one.
//
extern "C" __declspec(dllexport) BOOLEAN __stdcall PasswordFilter(PUNICODE_STRING AccountName,
	PUNICODE_STRING FullName,
	PUNICODE_STRING Password,
	BOOLEAN SetOperation) {

	config_t conf;
	LONG rCode = readConfig(&conf);

	std::ofstream errlog; // error log
	errlog.open(ERRLOG_PATH, std::ios::app);

	if (rCode != ERROR_SUCCESS) {
		errlog << "Error occurred while trying to read registry configuration.\n";
		errlog.close();
		return TRUE;
	}

	HINTERNET hSession = NULL, // winhttp handlers
		hConnect = NULL,
		hRequest = NULL;
	DWORD dwFlags = 0; // flags for winhttp function calls
	BOOL bResults = FALSE; // winhttp results

	hSession = WinHttpOpen(L"svalinndll", 
		WINHTTP_ACCESS_TYPE_NO_PROXY, 
		WINHTTP_NO_PROXY_NAME, 
		WINHTTP_NO_PROXY_BYPASS, 0);

	if (hSession) {
		hConnect = WinHttpConnect(hSession, (LPCWSTR)conf.srv.c_str(), conf.port, 0);
	}
	else {
		errlog << "WinHttpOpen: " << GetLastError() << "\n";
	}

	// enable TLS when requested
	if (conf.enableTLS == 1) {
		dwFlags = WINHTTP_FLAG_SECURE;
	}

	if (hConnect) {
		hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/", NULL, 
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, dwFlags);
	}
	else {
		errlog << "WinHttpConnect: " << GetLastError() << "\n";
	}

	if (hRequest) {
		// specify the credential to test
		WinHttpSetCredentials(hRequest,
			WINHTTP_AUTH_TARGET_SERVER,
			WINHTTP_AUTH_SCHEME_BASIC,
			AccountName->Buffer,
			Password->Buffer, NULL);

		if (conf.enableTLS == 1 & conf.disableTLSValidation == 1) {
			// use flags in winhttp to disable certificate validation
			dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
				SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
				SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
				SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;

			WinHttpSetOption(
				hRequest,
				WINHTTP_OPTION_SECURITY_FLAGS,
				&dwFlags,
				sizeof(dwFlags));
		}

		char data = (char)"svalinndll password check";
		bResults = WinHttpSendRequest(hRequest,
			WINHTTP_NO_ADDITIONAL_HEADERS, 0, &data, 
			strlen(&data), strlen(&data), NULL);


		if (bResults) {
			// finalize the request
			bResults = WinHttpReceiveResponse(hRequest, NULL);
		}
	}
	else {
		errlog << "WinHttpOpenRequest: " << GetLastError() << "\n";
	}

	DWORD dwStatusCode = 0;
	DWORD dwStatusCodeSize = sizeof(dwStatusCode);
	if (bResults) {
		WinHttpQueryHeaders(hRequest,
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX,
			&dwStatusCode, &dwStatusCodeSize, WINHTTP_NO_HEADER_INDEX);
	}
	else {
		errlog << "WinHttpSendRequest: " << GetLastError() << "\n";
	}

	// Close any open handles.
	if (hRequest) WinHttpCloseHandle(hRequest);
	if (hConnect) WinHttpCloseHandle(hConnect);
	if (hSession) WinHttpCloseHandle(hSession);

	switch (dwStatusCode) {
		case HTTP_STATUS_OK:
			errlog.close();
			return TRUE;
		case HTTP_STATUS_FORBIDDEN:
			errlog.close();
			return FALSE;
		default:
			errlog << "An error occurred! Arrived at default return with dwStatusCode " << dwStatusCode << "\n";
			errlog.close();
			return TRUE;
	}
}
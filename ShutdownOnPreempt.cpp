#include <Windows.h>
#include <WinHTTP.h>
#include <cstring>
#include <cwchar>

#include "Libraries/Windows_MinimalVersionInfo.hpp"

#pragma warning (disable:6053) // _snwprintf may not NUL-terminate
#pragma warning (disable:28159) // shutdown should be reconsidered

SERVICE_STATUS_HANDLE handle = NULL;
SERVICE_STATUS status = {
    SERVICE_WIN32_OWN_PROCESS, SERVICE_START_PENDING,
    SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE | SERVICE_ACCEPT_SHUTDOWN,
    0, 0, 0, 255
};

wchar_t statusbuffer [4];
std::size_t received = 0;

Windows::MinimalVersionInfo version;

HANDLE run = NULL;
HANDLE quit = NULL;
HANDLE preempt = NULL;
HINTERNET connection = NULL;

void WINAPI ServiceMain (DWORD argc, LPWSTR * argw);
DWORD WINAPI ServiceCtrlHandler (DWORD code, DWORD event, LPVOID data, LPVOID context);
void WINAPI HttpCallback (HINTERNET, DWORD_PTR, DWORD, LPVOID, DWORD);

static const SERVICE_TABLE_ENTRY services [] = {
    { const_cast <LPWSTR> (L""), ServiceMain },
    { NULL, NULL }
};

bool InitInternet () {
    wchar_t agent [128];
    _snwprintf (agent, 128, L"%s/%u.%u (%s)",
                version [L"InternalName"],
                HIWORD (version->dwProductVersionMS), LOWORD (version->dwProductVersionMS),
                version [L"URL"]);

    if (HINTERNET internet = WinHttpOpen (agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                          NULL, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)) {
        WinHttpSetStatusCallback (internet, HttpCallback, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS, NULL);
        connection = WinHttpConnect (internet, L"169.254.169.254", INTERNET_DEFAULT_HTTP_PORT, 0);
        if (connection)
            return true;
    }
    return false;
}

bool EnablePrivilege (LPCTSTR lpszPrivilege) {
    HANDLE hToken;
    if (OpenProcessToken (GetCurrentProcess (), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LUID luid;
        if (LookupPrivilegeValue (NULL, lpszPrivilege, &luid)) {
            TOKEN_PRIVILEGES tp = { 1, { luid, SE_PRIVILEGE_ENABLED } };
            if (AdjustTokenPrivileges (hToken, FALSE, &tp, sizeof (TOKEN_PRIVILEGES), NULL, NULL)) {
                return GetLastError () != ERROR_NOT_ALL_ASSIGNED;
            }
        }
    }
    return false;
}

void WINAPI ServiceMain (DWORD argc, LPWSTR * argw) {
    handle = RegisterServiceCtrlHandlerEx (version [L"InternalName"], ServiceCtrlHandler, NULL);
    if (!handle) {
        status.dwWin32ExitCode = GetLastError ();
        return;
    }

    preempt = CreateEvent (NULL, TRUE, FALSE, L"Global\\AzurePreempt");

    if (   (run = CreateEvent (NULL, TRUE, TRUE, NULL))
        && (quit = CreateEvent (NULL, FALSE, FALSE, NULL))
        && EnablePrivilege (SE_SHUTDOWN_NAME)
        && InitInternet ()) {

        while (WaitForSingleObject (run, INFINITE) == WAIT_OBJECT_0) {
            switch (WaitForSingleObject (quit, 1000)) {

                case WAIT_TIMEOUT:
                    status.dwCurrentState = SERVICE_RUNNING;
                    SetServiceStatus (handle, &status);

                    if (received == 0) {
                        if (auto request = WinHttpOpenRequest (connection, NULL, L"metadata/scheduledevents?api-version=2020-07-01", NULL,
                                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)) {
                            if (!WinHttpSendRequest (request, L"Metadata:true", 13, WINHTTP_NO_REQUEST_DATA, 0, 0, NULL)) {
                                WinHttpCloseHandle (request);
                            }
                        }
                    }
                    break;

                case WAIT_FAILED:
                    goto failed;
                case WAIT_OBJECT_0:
                    goto finished;
            }

            if (WaitForSingleObject (run, 0) == WAIT_TIMEOUT) {
                status.dwCurrentState = SERVICE_PAUSED;
                SetServiceStatus (handle, &status);
            }
        }
    }

failed:
    status.dwWin32ExitCode = GetLastError ();
finished:
    status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus (handle, &status);
}

char buffer [3072];

void WINAPI HttpCallback (HINTERNET request, DWORD_PTR context, DWORD code, LPVOID data, DWORD size) {
    switch (code) {

        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        case WINHTTP_CALLBACK_STATUS_CLOSING_CONNECTION:
            break;

        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            if (WinHttpReceiveResponse (request, NULL))
                return;

            break;

        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            size = sizeof statusbuffer;
            if (WinHttpQueryHeaders (request, WINHTTP_QUERY_STATUS_CODE, NULL, statusbuffer, &size, WINHTTP_NO_HEADER_INDEX)) {
                if (std::wcscmp (statusbuffer, L"200") == 0) {
                    if (WinHttpQueryDataAvailable (request, NULL))
                        return;
                }
            }
            break;

        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            if (size) {
                received += size;
                buffer [received] = '\0';

                [[ fallthrough ]];

        case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
                if (WinHttpReadData (request, buffer + received, (DWORD) (sizeof buffer - received - 1), NULL))
                    return;
            }
    }

    WinHttpCloseHandle (request);

    if (received) {

        // TODO: Use proper JSON parser

        if (std::strstr (buffer, "\"EventType\":\"Preempt\"") != nullptr) {
            if (preempt) {
                SetEvent (preempt);
            }

            // TODO: Configurable timeout?

            InitiateSystemShutdownEx (NULL, const_cast <LPWSTR> (version [L"FileDescription"]),
                                      10, TRUE, FALSE, SHTDN_REASON_FLAG_USER_DEFINED | 0x00AA0101);
            return;
        }

        // done, free resources
        HeapSetInformation (NULL, HeapOptimizeResources, NULL, 0);
        SetProcessWorkingSetSize (GetCurrentProcess (), (SIZE_T) -1, (SIZE_T) -1);

        // allow next request
        received = 0;
    }
}


DWORD WINAPI ServiceCtrlHandler (DWORD code, DWORD event, LPVOID data, LPVOID context) {
    switch (code) {
        case SERVICE_CONTROL_SHUTDOWN:
        case SERVICE_CONTROL_STOP:
            status.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus (handle, &status);
            SetEvent (quit);
            SetEvent (run);
            break;

        case SERVICE_CONTROL_INTERROGATE:
            SetServiceStatus (handle, &status);
            break;

        case SERVICE_CONTROL_PAUSE:
            status.dwCurrentState = SERVICE_PAUSE_PENDING;
            SetServiceStatus (handle, &status);
            ResetEvent (run);
            break;
        case SERVICE_CONTROL_CONTINUE:
            status.dwCurrentState = SERVICE_CONTINUE_PENDING;
            SetServiceStatus (handle, &status);
            SetEvent (run);
            break;

        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
    return NO_ERROR;
}

void Main () {
    if (!version.initialize (NULL))
        ExitProcess (ERROR_FILE_CORRUPT);

    if (StartServiceCtrlDispatcher (services)) {
        ExitProcess (status.dwWin32ExitCode);
    } else {
        ExitProcess (GetLastError ());
    }
}

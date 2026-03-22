#include <Windows.h>
#include <CommCtrl.h>
#include <ShellAPI.h>
#include <cstring>
#include <cwchar>

#include "..\Libraries\Windows_MinimalVersionInfo.hpp"

#pragma warning (disable:6053) // _snwprintf may not NUL-terminate

extern "C" IMAGE_DOS_HEADER __ImageBase;
Windows::MinimalVersionInfo version;
const auto szServiceName = L"ShutdownOnPreempt";

DWORD Report (DWORD error = GetLastError ());
HRESULT CALLBACK Callback (_In_ HWND, _In_ UINT, _In_ WPARAM, _In_ LPARAM, _In_ LONG_PTR);

void Main () {
    if (!version.initialize (NULL))
        ExitProcess (ERROR_FILE_CORRUPT);

    TASKDIALOG_BUTTON buttons [] = {
        { IDCONTINUE, MAKEINTRESOURCE (4) },
        { IDYES,      MAKEINTRESOURCE (7) },
        { IDRETRY,    MAKEINTRESOURCE (8) },
    };
    TASKDIALOGCONFIG dialog {
        sizeof (TASKDIALOGCONFIG),
        NULL, NULL,
        TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION |
        TDF_ENABLE_HYPERLINKS | TDF_EXPAND_FOOTER_AREA,
        TDCBF_CLOSE_BUTTON,
        NULL,
        NULL,
        NULL,
        MAKEINTRESOURCE (1),
        sizeof buttons / sizeof buttons [0], buttons,
        IDCONTINUE,
        0, NULL,
        NULL,
        NULL,
        NULL, NULL,
        NULL, NULL,
        MAKEINTRESOURCE (2), // footer
        Callback, NULL, 0
    };

    dialog.pszWindowTitle = version [L"FileDescription"];

    while (true) {
        bool installed;

        if (SC_HANDLE scm = OpenSCManager (NULL, NULL, SC_MANAGER_CONNECT)) {
            if (SC_HANDLE svc = OpenService (scm, szServiceName, SERVICE_QUERY_STATUS)) {
                installed = true;
                CloseServiceHandle (svc);
            } else {
                auto error = GetLastError ();
                if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
                    installed = false;
                } else {
                    ExitProcess (Report (error));
                }
            }

            CloseServiceHandle (scm);
        } else
            ExitProcess (Report ());

        dialog.pszMainInstruction = MAKEINTRESOURCE (installed ? 5 : 3);
        buttons [0].pszButtonText = MAKEINTRESOURCE (installed ? 6 : 4);

        int action = 0;
        HRESULT hr;
        if (SUCCEEDED (hr = TaskDialogIndirect (&dialog, &action, NULL, NULL))) {
            switch (action) {

                case IDCLOSE:
                case IDCANCEL:
                    ExitProcess (ERROR_SUCCESS);

                case IDRETRY:
                    break;

                case IDCONTINUE:
                    if (SC_HANDLE scm = OpenSCManager (NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE)) {
                        SC_HANDLE svc = NULL;

                        if (installed) {

                            // remove the service

                            svc = OpenService (scm, szServiceName, DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
                            if (svc) {
                                bool success = true;

                                union {
                                    QUERY_SERVICE_CONFIG config;
                                    char buffer [1024];
                                };
                                DWORD n;
                                if (!QueryServiceConfig (svc, &config, sizeof buffer, &n)) {
                                    config.lpBinaryPathName = NULL;
                                }

                                // stop if running

                                SERVICE_STATUS status;
                                if (!ControlService (svc, SERVICE_CONTROL_STOP, &status)) {
                                    auto error = GetLastError ();
                                    if (error != ERROR_SERVICE_NOT_ACTIVE) {
                                        Report (error);
                                        success = false;
                                    }
                                }

                                // delete service

                                if (!DeleteService (svc)) {
                                    Report ();
                                    success = false;
                                }
                                CloseServiceHandle (svc);

                                // delete the file

                                if (config.lpBinaryPathName) {
                                    Sleep (100);
                                    if (!DeleteFile (config.lpBinaryPathName)) {
                                        Report ();
                                        success = false;
                                    }
                                }

                                if (success) {
                                    MSGBOXPARAMS box = {
                                            sizeof (MSGBOXPARAMS), NULL,
                                            reinterpret_cast <HINSTANCE> (&__ImageBase),
                                            MAKEINTRESOURCE (0x000D),
                                            MAKEINTRESOURCE (0x000B),
                                            MB_ICONASTERISK,
                                            NULL, 0, NULL, 0
                                    };
                                    MessageBoxIndirect (&box);
                                }
                            } else {
                                Report ();
                            }

                        } else {
                            wchar_t target [MAX_PATH];
                            GetSystemDirectory (target, MAX_PATH);

                            std::wcscat (target, L"\\");
                            std::wcscat (target, szServiceName);
                            std::wcscat (target, L".exe");

                            wchar_t source [MAX_PATH];
                            GetModuleFileName (NULL, source, MAX_PATH);
                            std::wcsrchr (source, L'\\') [1] = L'\0';
                            std::wcscat (source, szServiceName);
                            std::wcscat (source, L".exe");

                            if (CopyFileEx (source, target, NULL, NULL, NULL, 0)) {
                                svc = CreateService (scm, szServiceName, version [L"ProductName"], SERVICE_ALL_ACCESS,
                                                     SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                                                     target, NULL, NULL, NULL, NULL, NULL);
                                if (svc) {
                                    if (LoadString (NULL, 0x0009, source, MAX_PATH)) {
                                        SERVICE_DESCRIPTION info = { source };
                                        if (!ChangeServiceConfig2 (svc, SERVICE_CONFIG_DESCRIPTION, &info)) {
                                            Report ();
                                        }
                                    }

                                    if (StartService (svc, 0, NULL)) {
                                        MSGBOXPARAMS box = {
                                            sizeof (MSGBOXPARAMS), NULL,
                                            reinterpret_cast <HINSTANCE> (&__ImageBase),
                                            MAKEINTRESOURCE (0x000C),
                                            MAKEINTRESOURCE (0x000B),
                                            MB_ICONASTERISK,
                                            NULL, 0, NULL, 0
                                        };
                                        MessageBoxIndirect (&box);
                                    } else {
                                        Report ();
                                    }
                                    CloseServiceHandle (svc);
                                } else {
                                    Report ();
                                }
                            } else {
                                Report ();
                            }
                        }
                        CloseServiceHandle (scm);

                        // custom shutdown code

                        HKEY hKeyUserDefined = NULL;
                        if (RegCreateKeyEx (HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Reliability\\UserDefined",
                                            0, NULL, 0, KEY_SET_VALUE, NULL, &hKeyUserDefined, NULL) == ERROR_SUCCESS) {
                            if (installed) {
                                RegDeleteValue (hKeyUserDefined, L"D;170;257");

                            } else {
                                wchar_t value [162];
                                auto length = LoadString (NULL, 0x000A, value, 162);
                                for (auto & c : value) {
                                    if (c == L'\1')
                                        c = L'\0';
                                }
                                ++length;

                                RegSetValueEx (hKeyUserDefined, L"D;170;257", NULL, REG_MULTI_SZ,
                                               (const BYTE *) value, length * sizeof (wchar_t));
                            }
                            RegCloseKey (hKeyUserDefined);
                        }

                    } else {
                        Report ();
                    }
                    break;
            }
        } else
            ExitProcess (Report (hr));
    }
}


HRESULT CALLBACK Callback (_In_ HWND hwnd, _In_ UINT notification, _In_ WPARAM wParam, _In_ LPARAM lParam, _In_ LONG_PTR) {
    switch (notification) {
        case TDN_BUTTON_CLICKED:
            switch (wParam) {
                case IDYES:
                    ShellExecute (NULL, NULL, L"services.msc", NULL, NULL, SW_SHOWNORMAL);
                    return S_FALSE;
            }
            break;

        case TDN_HELP:
        case TDN_HYPERLINK_CLICKED:
            ShellExecute (NULL, NULL, L"https://github.com/tringi/shutdown-on-preempt", NULL, NULL, SW_SHOWNORMAL);
            break;
    }
    return S_OK;
}

DWORD Report (DWORD error) {
    wchar_t code [24];
    LPTSTR text = NULL;
    if (!FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
                        NULL, error, 0, (LPTSTR) &text, 0, NULL)) {
        _snwprintf (code, 24, L"Error 0x%08X", error);
        text = code;
    }
    MessageBox (NULL, text, version [L"FileDescription"], MB_ICONSTOP);
    return error;
}

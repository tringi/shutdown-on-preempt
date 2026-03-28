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

bool Test (bool * installed);
void Action (bool install);
void Install (SC_HANDLE);
void Remove (SC_HANDLE);

DWORD Report (DWORD error = GetLastError ());

VOID CALLBACK Help (LPHELPINFO lpHelpInfo);
HRESULT CALLBACK Callback (_In_ HWND, _In_ UINT, _In_ WPARAM, _In_ LPARAM, _In_ LONG_PTR);
HRESULT (WINAPI * ptrTaskDialogIndirect) (_In_ const TASKDIALOGCONFIG * pTaskConfig, _Out_opt_ int * pnButton,
                                          _Out_opt_ int * pnRadioButton, _Out_opt_ BOOL * pfVerificationFlagChecked) = NULL;

template <typename P>
bool Symbol (HMODULE h, P & pointer, const char * name) {
    if (P p = reinterpret_cast <P> (GetProcAddress (h, name))) {
        pointer = p;
        return true;
    } else
        return false;
}

const wchar_t * GetString (UINT id) {
    LPWSTR p = NULL;
    if (LoadString (NULL, id, (LPWSTR) &p, 0)) { // NOTE: we rely on /n option (NUL-terminated resource strings)
        return p;
    } else
        return nullptr;
}

void Main () {
    InitCommonControls ();

    if (!version.initialize (NULL))
        ExitProcess (ERROR_FILE_CORRUPT);

    if (auto hKernel32 = GetModuleHandle (L"COMCTL32")) {
        Symbol (hKernel32, ptrTaskDialogIndirect, "TaskDialogIndirect");
    }
    
    while (true) {
        bool installed;
        if (!Test (&installed))
            ExitProcess (Report ());

        int action = 0;
        if (ptrTaskDialogIndirect) {
            TASKDIALOG_BUTTON buttons [] = {
                { IDCONTINUE, NULL },
                { IDYES,      MAKEINTRESOURCE (7) },
                { IDTRYAGAIN, MAKEINTRESOURCE (8) },
            };
            TASKDIALOGCONFIG dialog {
                sizeof (TASKDIALOGCONFIG), NULL, NULL,
                TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | 
                TDF_ENABLE_HYPERLINKS | TDF_EXPAND_FOOTER_AREA,
                TDCBF_CLOSE_BUTTON,
                NULL, NULL, NULL,
                MAKEINTRESOURCE (1),
                sizeof buttons / sizeof buttons [0], buttons,
                IDCONTINUE,
                0, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                MAKEINTRESOURCE (2),
                Callback, NULL, 0
            };

            dialog.pszWindowTitle = version [L"FileDescription"];
            dialog.pszMainInstruction = MAKEINTRESOURCE (installed ? 5 : 3);
            buttons [0].pszButtonText = MAKEINTRESOURCE (installed ? 6 : 4);

            HRESULT hr;
            if (!SUCCEEDED (hr = ptrTaskDialogIndirect (&dialog, &action, NULL, NULL)))
                ExitProcess (Report (hr));

        } else {
            MSGBOXPARAMS box = {
                sizeof (MSGBOXPARAMS),
                NULL, NULL, NULL, NULL,
                MB_CANCELTRYCONTINUE | MB_HELP | MB_DEFBUTTON3 | MB_USERICON,
                IDI_APPLICATION,
                NULL, Help, 0
            };

            wchar_t content [1536];
            _snwprintf (content, 1536, GetString (0x0010),
                        GetString (1),
                        GetString (installed ? 5 : 3), GetString (installed ? 0x0012 : 0x0011),
                        GetString (0x000F));

            box.lpszCaption = version [L"FileDescription"];
            box.lpszText = content;

            action = MessageBoxIndirect (&box);
            if (!action)
                ExitProcess (Report ());
        }

        switch (action) {
            case IDCLOSE:
            case IDCANCEL:
                ExitProcess (ERROR_SUCCESS);

            case IDCONTINUE:
                Action (installed ? false : true);
        }
    }
}

bool Test (bool * installed) {
    bool success = false;
    if (SC_HANDLE scm = OpenSCManager (NULL, NULL, SC_MANAGER_CONNECT)) {

        if (SC_HANDLE svc = OpenService (scm, szServiceName, SERVICE_QUERY_STATUS)) {
            *installed = true;
            success = true;
            CloseServiceHandle (svc);

        } else
        if (GetLastError () == ERROR_SERVICE_DOES_NOT_EXIST) {
            *installed = false;
            success = true;
        }
        
        CloseServiceHandle (scm);
    }
    return success;
}

void Install (SC_HANDLE scm) {
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
        if (auto svc = CreateService (scm, szServiceName, version [L"ProductName"], SERVICE_ALL_ACCESS,
                                      SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                                      target, NULL, NULL, NULL, NULL, NULL)) {
            if (auto p = GetString (0x0009)) {
                SERVICE_DESCRIPTION info = { (LPWSTR) p };
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

void Remove (SC_HANDLE scm) {
    if (auto svc = OpenService (scm, szServiceName, DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG)) {
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
            Sleep (50);

            bool deleted = false;
            for (auto i = 0u; i != 10u; ++i) {
                if (i) {
                    Sleep (250);
                }
                if (DeleteFile (config.lpBinaryPathName)) {
                    deleted = true;
                    break;
                }
            }
            if (!deleted) {
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
}

void Action (bool install) {
    if (SC_HANDLE scm = OpenSCManager (NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE)) {
        if (install) {
            Install (scm);
        } else {
            Remove (scm);
        }
        CloseServiceHandle (scm);

        // custom shutdown code

        HKEY hKeyUserDefined = NULL;
        if (RegCreateKeyEx (HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Reliability\\UserDefined",
                            0, NULL, 0, KEY_SET_VALUE, NULL, &hKeyUserDefined, NULL) == ERROR_SUCCESS) {
            if (install) {
                wchar_t value [162];
                auto length = LoadString (NULL, 0x000A, value, 162);
                for (auto & c : value) {
                    if (c == L'\1')
                        c = L'\0';
                }
                ++length;

                RegSetValueEx (hKeyUserDefined, L"D;170;257", NULL, REG_MULTI_SZ,
                               (const BYTE *) value, length * sizeof (wchar_t));
            } else {
                RegDeleteValue (hKeyUserDefined, L"D;170;257");
            }
            RegCloseKey (hKeyUserDefined);
        }

    } else {
        Report ();
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
            Help (NULL);
            break;
    }
    return S_OK;
}

VOID CALLBACK Help (LPHELPINFO lpHelpInfo) {
    ShellExecute (NULL, NULL, GetString (0x000F), NULL, NULL, SW_SHOWNORMAL);
}

DWORD Report (DWORD error) {
    wchar_t code [256];
    LPTSTR text = NULL;
    if (!FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
                        NULL, error, 0, (LPTSTR) &text, 0, NULL)) {
        _snwprintf (code, 256, GetString (0x000E), error);
        text = code;
    }
    MessageBox (NULL, text, version [L"FileDescription"], MB_ICONSTOP);
    return error;
}

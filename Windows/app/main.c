#include "autorelease.h"
#include "common.h"
#include "resource.h"
#include "osdep.h"
#include "nodeapi.h"
#include "jansson.h"
#include "msg_proxy.h"
#include "version.h"
#include "mainwnd.h"

#include "winsparkle.h"

#include <windows.h>
#include <windowsx.h>
#include <io.h>
#include <ole2.h>
#include <commctrl.h>
#include <ShlObj.h>
#include <ShellAPI.h>
#include <shlwapi.h>
#include <malloc.h>
#include <time.h>

#include <assert.h>


void C_app__failed_to_start(json_t *arg) {
    MessageBox(NULL, U2W(json_string_value(json_object_get(arg, "message"))), L"LiveReload failed to start", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

BOOL InitApp(void) {
    INITCOMMONCONTROLSEX sex;
    sex.dwSize = sizeof(sex);
    sex.dwICC = ICC_WIN95_CLASSES | ICC_LINK_CLASS;
    InitCommonControlsEx(&sex);

    return TRUE;
}

DWORD g_dwMainThreadId;
enum { AM_INVOKE = WM_APP + 1 };
void invoke_on_main_thread(INVOKE_LATER_FUNC func, void *context) {
  PostThreadMessage(g_dwMainThreadId, AM_INVOKE, (WPARAM)func, (LPARAM) context);
}

// node.js side gets stuck reading from stdin (a bug in Win32 code, I guess), these pings help to unstuck it
void CALLBACK SendRegularPing(HWND, UINT, UINT_PTR, DWORD) {
    S_app_ping(json_object());
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hinstPrev,
                   LPSTR lpCmdLine, int nShowCmd)
{
    MSG msg;
    HWND hwnd;

    if (time(NULL) > 1338508800 /* June 1, 2012 UTC */) {
        DWORD result = MessageBox(NULL, L"Sorry, this beta version of LiveReload has expired and cannot be launched.\n\nDo you want to visit http://livereload.com/ to get an updated version?",
            L"LiveReload 2 beta expired", MB_YESNO | MB_ICONERROR);
        if (result == IDYES) {
            ShellExecute(NULL, L"open", L"http://livereload.com/", NULL, NULL, SW_SHOWNORMAL);
        }
        return 1;
    }

    // SHBrowseForFolder needs this, and says it's better to use OleInitialize than ComInitialize
    HRESULT result = OleInitialize(NULL);
    assert(SUCCEEDED(result));

    g_dwMainThreadId = GetCurrentThreadId();

    // create message queue
    PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    win_sparkle_set_app_details(L"Andrey Tarantsov", L"LiveReload", TEXT(LIVERELOAD_VERSION));
    win_sparkle_set_appcast_url("http://download.livereload.com/LiveReload-Windows-appcast.xml");
    win_sparkle_set_registry_path("Software\\LiveReload\\Updates");

    if (!InitApp()) return 0;

    mainwnd_init();

    BOOL outputToConsole = !!strstr(lpCmdLine, "--console");

    os_init(); // to fill in paths before opening log files

    if (outputToConsole) {
        AllocConsole();
        freopen("CONOUT$", "wb", stdout);
        freopen("CONOUT$", "wb", stderr);
    } else {
        WCHAR buf[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, os_log_path, -1, buf, MAX_PATH);
        wcscat(buf, L"\\log.txt");
        _wfreopen(buf, L"w", stderr);
        HANDLE hLogFile = (HANDLE) _get_osfhandle(_fileno(stderr));
        //HANDLE hLogFile = CreateFile(buf, FILE_ALL_ACCESS, FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, FILE_FLAG_WRITE_THROUGH, NULL);
        //SetStdHandle(STD_OUTPUT_HANDLE, hLogFile);
        SetStdHandle(STD_ERROR_HANDLE, hLogFile);
    }
    time_t startup_time = time(NULL);
    struct tm *startup_tm = gmtime(&startup_time);
    fprintf(stderr, "%04d-%02d-%02d %02d:%02d:%02d LiveReload " LIVERELOAD_VERSION " launched\n", 1900 + startup_tm->tm_year,
        1 + startup_tm->tm_mon, startup_tm->tm_mday, startup_tm->tm_hour, startup_tm->tm_min, startup_tm->tm_sec);
    fflush(stderr);

    node_init();

    mainwnd_show();

    win_sparkle_init();

    SetTimer(NULL, 0, 1000, SendRegularPing);

    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == AM_INVOKE && msg.hwnd == NULL) {
          INVOKE_LATER_FUNC func = (INVOKE_LATER_FUNC)msg.wParam;
          void *context = (void *)msg.lParam;
          func(context);
        } else {
          TranslateMessage(&msg);
          DispatchMessage(&msg);
        }
        autorelease_cleanup();
    }

    node_shutdown();

    OleUninitialize();

    return 0;
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#pragma comment(lib, "user32.lib")

static BYTE  g_map[256];
static int   g_log;
static DWORD g_tid;

static LRESULT CALLBACK kbd_proc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION) {
        KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lp;
        BYTE t = (k->vkCode < 256) ? g_map[k->vkCode] : 0;
        if (t) {
            INPUT in;
            if (g_log)
                printf("vk %02lX -> INS %s\n", k->vkCode,
                       (k->flags & LLKHF_UP) ? "up" : "down");
            memset(&in, 0, sizeof in);
            in.type       = INPUT_KEYBOARD;
            in.ki.wVk     = t;
            in.ki.wScan   = 0x52;
            in.ki.dwFlags = KEYEVENTF_EXTENDEDKEY |
                            ((k->flags & LLKHF_UP) ? KEYEVENTF_KEYUP : 0);
            SendInput(1, &in, sizeof in);
            return 1;
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

static void pump(void)
{
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

static void send_key(UINT vk, WORD scan, BOOL up)
{
    INPUT in;
    memset(&in, 0, sizeof in);
    in.type       = INPUT_KEYBOARD;
    in.ki.wVk     = (WORD)vk;
    in.ki.wScan   = scan;
    in.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | (up ? KEYEVENTF_KEYUP : 0);
    SendInput(1, &in, sizeof in);
}

static volatile LONG g_raw_last_vk;
static volatile LONG g_raw_orig;

static LRESULT CALLBACK sink_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INPUT) {
        RAWINPUT ri;
        UINT size = sizeof ri;
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &size,
                            sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEKEYBOARD) {
            USHORT vk = ri.data.keyboard.VKey;
            if (vk == VK_DELETE || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_END)
                InterlockedExchange(&g_raw_orig, 1);
            InterlockedExchange(&g_raw_last_vk, vk);
        }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void make_sink(void)
{
    WNDCLASSW wc;
    RAWINPUTDEVICE rid;
    HWND h;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc   = sink_proc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.lpszClassName = L"ins_sink";
    RegisterClassW(&wc);
    h = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0,
                        NULL, NULL, wc.hInstance, NULL);
    rid.usUsagePage = 1;
    rid.usUsage     = 6;
    rid.dwFlags     = RIDEV_INPUTSINK;
    rid.hwndTarget  = h;
    RegisterRawInputDevices(&rid, 1, sizeof rid);
}

static int wait_raw(UINT expect)
{
    int i;
    for (i = 0; i < 200; i++) {
        pump();
        if ((UINT)g_raw_last_vk == expect)
            return 1;
        Sleep(5);
    }
    return 0;
}

static int test_mode(void)
{
    static const struct { UINT vk; WORD scan; const char *name; } src[] = {
        { VK_DELETE, 0x53, "DEL"  },
        { VK_PRIOR,  0x49, "PGUP" },
        { VK_NEXT,   0x51, "PGDN" },
        { VK_END,    0x4F, "END"  },
    };
    int pass = 1, ok, i;

    make_sink();
    for (i = 0; i < 4; i++) {
        int leak;
        InterlockedExchange(&g_raw_last_vk, 0);
        InterlockedExchange(&g_raw_orig, 0);
        send_key(src[i].vk, src[i].scan, FALSE);
        ok = wait_raw(VK_INSERT);
        send_key(src[i].vk, src[i].scan, TRUE);
        pump(); Sleep(30); pump();
        leak = g_raw_orig;
        printf("%-4s -> INS : %s", src[i].name, ok ? "OK" : "FAIL");
        if (!ok)
            printf("  (raw input saw vk %04lX)", (DWORD)g_raw_last_vk);
        if (leak)
            printf("  (original leaked to raw input)");
        printf("\n");
        pass &= ok;
    }

    InterlockedExchange(&g_raw_last_vk, 0);
    send_key(VK_F24, 0x64, FALSE);
    ok = wait_raw(VK_F24);
    send_key(VK_F24, 0x64, TRUE);
    pump(); Sleep(30); pump();
    printf("F24 pass-through : %s\n", ok ? "OK" : "FAIL");
    pass &= ok;

    return pass ? 0 : 1;
}

static BOOL WINAPI on_ctrl(DWORD ev)
{
    if (ev == CTRL_C_EVENT || ev == CTRL_BREAK_EVENT || ev == CTRL_CLOSE_EVENT ||
        ev == CTRL_LOGOFF_EVENT || ev == CTRL_SHUTDOWN_EVENT)
        PostThreadMessageW(g_tid, WM_QUIT, 0, 0);
    return TRUE;
}

static void disable_quickedit(void)
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode & ~ENABLE_QUICK_EDIT_MODE & ~ENABLE_MOUSE_INPUT);
}

int main(int argc, char **argv)
{
    HHOOK hook;
    MSG m;
    int i, n = 0, want_test = 0;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!_stricmp(a, "-log"))        g_log = 1;
        else if (!_stricmp(a, "-test"))  want_test = 1;
        else if (!_stricmp(a, "del"))  { g_map[VK_DELETE] = VK_INSERT; n++; }
        else if (!_stricmp(a, "pgup")) { g_map[VK_PRIOR]  = VK_INSERT; n++; }
        else if (!_stricmp(a, "pgdn")) { g_map[VK_NEXT]   = VK_INSERT; n++; }
        else if (!_stricmp(a, "end"))  { g_map[VK_END]    = VK_INSERT; n++; }
        else {
            printf("unknown arg: %s\n", a);
            n = 0;
            break;
        }
    }

    if (!n) {
        printf("usage: remap.exe [del] [pgup] [pgdn] [end] [-log] [-test]\n");
        return 2;
    }

    g_tid = GetCurrentThreadId();
    disable_quickedit();
    SetConsoleCtrlHandler(on_ctrl, TRUE);

    hook = SetWindowsHookExW(WH_KEYBOARD_LL, kbd_proc, GetModuleHandleW(NULL), 0);
    if (!hook) {
        printf("SetWindowsHookEx failed: %lu\n", GetLastError());
        return 1;
    }

    printf("active: ");
    if (g_map[VK_DELETE]) printf("DEL ");
    if (g_map[VK_PRIOR])  printf("PGUP ");
    if (g_map[VK_NEXT])   printf("PGDN ");
    if (g_map[VK_END])    printf("END ");
    printf("-> INSERT   (Ctrl+C to stop)\n");
    fflush(stdout);

    if (want_test) {
        i = test_mode();
        UnhookWindowsHookEx(hook);
        return i;
    }

    while (GetMessageW(&m, NULL, 0, 0) > 0)
        ;

    UnhookWindowsHookEx(hook);
    printf("stopped\n");
    return 0;
}

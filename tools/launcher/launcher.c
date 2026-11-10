/* TokyoJungle.exe -- launcher with a settings screen (Windows, and SteamOS via Proton).
 *
 * Shows a small window to pick the resolution (1280x720 or 2560x1440) and
 * window/fullscreen, remembers the choice in launcher.ini, then does what
 * `scripts\launch_tokyojungle.ps1 -Run -Recommended` does without PowerShell:
 * sets the same environment, starts the recompiled game and waits for it (so
 * Steam sees the game as running until it closes).
 *
 * Folder layout, relative to this launcher:
 *   NPUA80523\                  extracted game (USRDIR, PARAM.SFO)
 *   engine\  or build\tokyojungle\
 *       tokyojungle.exe  or build-clang\tokyojungle.exe (+ ffmpeg.exe for movies)
 *       input\EBOOT.ELF, input\PARAM.SFO, controls.ini
 *   logs\                        created; stdout.log / stderr.log
 *   launcher.ini                 created; the saved choice
 *
 * Command line: -play starts with the saved choice without the window,
 * -2k forces 2560x1440, -fullscreen forces full screen. Holding Shift while
 * starting shows the window even when "don't ask again" was ticked.
 *
 * For RecompLauncher (recomplauncher.json), only used when passed:
 *   --game <folder>   the extracted game folder, anywhere (instead of [paths] game=)
 *   --prepare         runs BUILD.bat on that folder with no window and passes its
 *                     "progress 0.4 Text" lines through on stdout
 */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdlib.h>
#include <wchar.h>

#define MAXP 1024

enum { ID_RES_720 = 101, ID_RES_2K, ID_WINDOWED, ID_FULLSCREEN, ID_SKIP, ID_PLAY, ID_QUIT };

typedef struct {
    int two_k;       /* 0: 1280x720, 1: 2560x1440 */
    int fullscreen;  /* 0: window, 1: borderless full screen */
    int skip;        /* 1: start directly next time */
} Settings;

static wchar_t g_base[MAXP];
static wchar_t g_ini[MAXP];
static Settings g_settings;
static int g_play;
static HFONT g_font;

static int exists(const wchar_t* p) {
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

static void join(wchar_t* out, const wchar_t* a, const wchar_t* b) {
    swprintf(out, MAXP, L"%ls\\%ls", a, b);
}

static int is_absolute(const wchar_t* p) {
    return (p[0] && p[1] == L':') || (p[0] == L'\\' && p[1] == L'\\');
}

static void strip_trailing_slashes(wchar_t* p) {
    size_t n = wcslen(p);
    while (n > 3 && (p[n - 1] == L'\\' || p[n - 1] == L'/')) p[--n] = 0;
}

static int fail(const wchar_t* msg, const wchar_t* path) {
    wchar_t text[2 * MAXP];
    swprintf(text, 2 * MAXP, L"%ls\n\n%ls", msg, path ? path : L"");
    MessageBoxW(NULL, text, L"Tokyo Jungle", MB_OK | MB_ICONERROR);
    return 1;
}

static void load_settings(void) {
    g_settings.two_k = GetPrivateProfileIntW(L"video", L"resolution_2k", 0, g_ini) != 0;
    g_settings.fullscreen = GetPrivateProfileIntW(L"video", L"fullscreen", 0, g_ini) != 0;
    g_settings.skip = GetPrivateProfileIntW(L"launcher", L"dont_ask", 0, g_ini) != 0;
}

static void save_settings(void) {
    WritePrivateProfileStringW(L"video", L"resolution_2k", g_settings.two_k ? L"1" : L"0", g_ini);
    WritePrivateProfileStringW(L"video", L"fullscreen", g_settings.fullscreen ? L"1" : L"0", g_ini);
    WritePrivateProfileStringW(L"launcher", L"dont_ask", g_settings.skip ? L"1" : L"0", g_ini);
}

/* ---- settings window ---------------------------------------------------- */

static HWND add(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
                int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                             parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static LRESULT CALLBACK settings_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        add(hwnd, L"BUTTON", L"Resolução", BS_GROUPBOX, 12, 8, 316, 78, 0);
        add(hwnd, L"BUTTON", L"1280 x 720  (original do PS3, mais leve)",
            BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 24, 30, 296, 22, ID_RES_720);
        add(hwnd, L"BUTTON", L"2560 x 1440  (2K, mais nítido)",
            BS_AUTORADIOBUTTON, 24, 56, 296, 22, ID_RES_2K);
        add(hwnd, L"BUTTON", L"Tela", BS_GROUPBOX, 12, 94, 316, 78, 0);
        add(hwnd, L"BUTTON", L"Janela",
            BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 24, 116, 296, 22, ID_WINDOWED);
        add(hwnd, L"BUTTON", L"Tela cheia",
            BS_AUTORADIOBUTTON, 24, 142, 296, 22, ID_FULLSCREEN);
        add(hwnd, L"BUTTON", L"Não perguntar de novo (segure Shift ao abrir para mudar)",
            BS_AUTOCHECKBOX | WS_GROUP | WS_TABSTOP, 14, 180, 316, 22, ID_SKIP);
        add(hwnd, L"BUTTON", L"Jogar", BS_DEFPUSHBUTTON | WS_GROUP | WS_TABSTOP,
            146, 214, 88, 30, ID_PLAY);
        add(hwnd, L"BUTTON", L"Sair", BS_PUSHBUTTON | WS_TABSTOP, 240, 214, 88, 30, ID_QUIT);
        CheckRadioButton(hwnd, ID_RES_720, ID_RES_2K, g_settings.two_k ? ID_RES_2K : ID_RES_720);
        CheckRadioButton(hwnd, ID_WINDOWED, ID_FULLSCREEN,
                         g_settings.fullscreen ? ID_FULLSCREEN : ID_WINDOWED);
        CheckDlgButton(hwnd, ID_SKIP, g_settings.skip ? BST_CHECKED : BST_UNCHECKED);
        SetFocus(GetDlgItem(hwnd, ID_PLAY));
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_PLAY || LOWORD(wp) == IDOK) {
            g_settings.two_k = IsDlgButtonChecked(hwnd, ID_RES_2K) == BST_CHECKED;
            g_settings.fullscreen = IsDlgButtonChecked(hwnd, ID_FULLSCREEN) == BST_CHECKED;
            g_settings.skip = IsDlgButtonChecked(hwnd, ID_SKIP) == BST_CHECKED;
            g_play = 1;
            DestroyWindow(hwnd);
        } else if (LOWORD(wp) == ID_QUIT || LOWORD(wp) == IDCANCEL) {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* Returns 1 to start the game, 0 if the player chose Sair. */
static int show_settings(HINSTANCE inst) {
    NONCLIENTMETRICSW ncm = { sizeof ncm };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = settings_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.lpszClassName = L"TokyoJungleLauncher";
    RegisterClassW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = {0, 0, 340, 256};
    AdjustWindowRect(&r, style, FALSE);
    const int w = r.right - r.left, h = r.bottom - r.top;
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Tokyo Jungle", style,
                              (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                              (GetSystemMetrics(SM_CYSCREEN) - h) / 2,
                              w, h, NULL, NULL, inst, NULL);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {  /* Tab, Enter = Jogar, Esc = Sair */
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DeleteObject(g_font);
    return g_play;
}

/* ---- RecompLauncher setup step (--prepare) --------------------------------- */

/* Runs BUILD.bat next to this launcher on the game folder, without a window.
 * Its output (with tj_build_all.ps1's "progress" lines) goes to our stdout,
 * which RecompLauncher reads. Returns BUILD.bat's exit code. */
static int prepare(const wchar_t* game) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    wchar_t bat[MAXP];
    join(bat, g_base, L"BUILD.bat");
    if (!exists(bat)) {
        static const char msg[] = "progress 0 BUILD.bat is missing next to TokyoJungle.exe\n";
        DWORD n;
        if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, msg, sizeof msg - 1, &n, NULL);
        return 2;
    }

    wchar_t g[MAXP] = L"";
    if (game) {
        wcsncpy(g, game, MAXP - 1);
        g[MAXP - 1] = 0;
        strip_trailing_slashes(g);  /* "C:\x\" would end the quoted argument early */
    }
    wchar_t cmd[3 * MAXP];
    swprintf(cmd, 3 * MAXP, L"cmd.exe /d /s /c \"\"%ls\" -Progress%ls%ls%ls\"", bat,
             g[0] ? L" -Game \"" : L"", g, g[0] ? L"\"" : L"");

    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                            OPEN_EXISTING, 0, NULL);
    if (out && out != INVALID_HANDLE_VALUE) SetHandleInformation(out, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    if (err && err != INVALID_HANDLE_VALUE) SetHandleInformation(err, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    STARTUPINFOW si = { sizeof si };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = out;
    si.hStdError = (err && err != INVALID_HANDLE_VALUE) ? err : out;

    SetEnvironmentVariableW(L"TJ_NO_PAUSE", L"1");  /* BUILD.bat ends without "pause" */
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, g_base, &si, &pi))
        return 3;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
    return (int)code;
}

/* ---- game start ------------------------------------------------------------ */

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show) {
    (void)prev; (void)show; (void)cmdline;

    wchar_t self[MAXP];
    GetModuleFileNameW(NULL, self, MAXP);
    wcscpy(g_base, self);
    wchar_t* slash = wcsrchr(g_base, L'\\');
    if (slash) *slash = 0;
    const wchar_t* selfname = slash ? slash + 1 : self;
    join(g_ini, g_base, L"launcher.ini");
    load_settings();

    /* Arguments one by one, so a --game path can't be read as an option. */
    int arg_play = 0, arg_2k = 0, arg_full = 0, arg_prepare = 0;
    const wchar_t* arg_game = NULL;
    for (int i = 1; i < __argc; i++) {
        const wchar_t* a = __wargv[i];
        if (!_wcsicmp(a, L"--game") && i + 1 < __argc) arg_game = __wargv[++i];
        else if (!_wcsicmp(a, L"--prepare")) arg_prepare = 1;
        else if (wcsstr(a, L"-play")) arg_play = 1;
        else if (wcsstr(a, L"-2k") || wcsstr(a, L"-2K")) arg_2k = 1;
        else if (wcsstr(a, L"-fullscreen")) arg_full = 1;
    }
    if (wcsstr(selfname, L"2K") || wcsstr(selfname, L"2k")) arg_2k = 1;
    if (arg_prepare) return prepare(arg_game);
    const int shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    if (!arg_play && !arg_2k && !arg_full && (!g_settings.skip || shift)) {
        if (!show_settings(inst)) return 0;
        save_settings();
    }
    if (arg_2k) g_settings.two_k = 1;
    if (arg_full) g_settings.fullscreen = 1;

    /* Locate the engine folder and the recompiled executable. */
    wchar_t engine[MAXP], exe[MAXP], tmp[MAXP];
    const wchar_t* engine_dirs[] = { L"engine", L"build\\tokyojungle" };
    const wchar_t* exe_names[]   = { L"tokyojungle.exe", L"build-clang\\tokyojungle.exe" };
    int found = 0;
    for (int i = 0; i < 2 && !found; i++) {
        join(engine, g_base, engine_dirs[i]);
        for (int j = 0; j < 2 && !found; j++) {
            join(exe, engine, exe_names[j]);
            if (exists(exe)) found = 1;
        }
    }
    if (!found)
        return fail(L"Não encontrei o jogo (tokyojungle.exe) em engine\\ ou build\\tokyojungle\\ ao lado deste launcher.", g_base);

    join(tmp, engine, L"input\\EBOOT.ELF");
    if (!exists(tmp)) return fail(L"Falta o EBOOT.ELF descriptografado:", tmp);
    join(tmp, engine, L"input\\PARAM.SFO");
    if (!exists(tmp)) return fail(L"Falta o PARAM.SFO:", tmp);

    /* Game folder: --game, else [paths] game= in launcher.ini (written by
     * BUILD.bat; a name next to this launcher or a full path), NPUA80523 by default. */
    wchar_t game_dir[MAXP], game[MAXP];
    if (arg_game) {
        wcsncpy(game_dir, arg_game, MAXP - 1);
        game_dir[MAXP - 1] = 0;
    } else {
        GetPrivateProfileStringW(L"paths", L"game", L"NPUA80523", game_dir, MAXP, g_ini);
    }
    strip_trailing_slashes(game_dir);
    if (is_absolute(game_dir)) wcscpy(game, game_dir);
    else join(game, g_base, game_dir);
    join(tmp, game, L"USRDIR");
    if (!exists(tmp)) return fail(L"Falta a pasta do jogo extraído (NPUA80523 com USRDIR dentro):", game);

    wchar_t saves[MAXP], cache[MAXP], controls[MAXP], logs[MAXP], out_log[MAXP], err_log[MAXP];
    join(saves, engine, L"savedata");
    join(cache, engine, L"shader-cache");
    join(controls, engine, L"controls.ini");
    join(logs, g_base, L"logs");
    CreateDirectoryW(saves, NULL);
    CreateDirectoryW(cache, NULL);
    CreateDirectoryW(logs, NULL);
    join(out_log, logs, L"stdout.log");
    join(err_log, logs, L"stderr.log");

    /* Same environment as launch_tokyojungle.ps1 -Run -Recommended. */
    SetEnvironmentVariableW(L"PS3_VFS_ROOT", game);
    SetEnvironmentVariableW(L"PS3_HDD_GAME_ROOT", game);
    SetEnvironmentVariableW(L"PS3_VFS_READONLY", L"1");
    SetEnvironmentVariableW(L"PS3_TITLE", L"Tokyo Jungle");
    SetEnvironmentVariableW(L"PS3_SAVEDATA_ROOT", saves);
    SetEnvironmentVariableW(L"TJ_SHADER_CACHE_DIR", cache);
    SetEnvironmentVariableW(L"TJ_CONTROLS_FILE", controls);
    SetEnvironmentVariableW(L"TJ_STDOUT_FILE", out_log);
    SetEnvironmentVariableW(L"TJ_STDERR_FILE", err_log);

    static const wchar_t* recommended[][2] = {
        { L"TJ_AUDIO_BUFFER_MS", L"60" },       { L"TJ_AUDIO_MMCSS", L"1" },
        { L"TJ_BLIT_CLIP_ABSOLUTE", L"1" },     { L"TJ_BLIT_SNAPSHOT_POOL", L"1" },
        { L"TJ_DISABLE_MLAA", L"1" },           { L"TJ_EDGE_NOWAIT", L"1" },
        { L"TJ_FIFO_KICK_WAIT", L"1" },         { L"TJ_FLIP30_LIMIT", L"9" },
        { L"TJ_FLIP_PROMOTE", L"1" },           { L"TJ_FPS_TRACE", L"1" },
        { L"TJ_GCM_INTERRUPT_WORKER", L"1" },   { L"TJ_GPU_COMPLETION_REUSE", L"1" },
        { L"TJ_HIGH_QOS", L"1" },               { L"TJ_LABEL_EARLY_MEMO", L"1" },
        { L"TJ_LOOKUP_FASTPATH", L"1" },        { L"TJ_PREFIX_OVERLAP", L"1" },
        { L"TJ_PREFIX_OWNERSHIP", L"1" },       { L"TJ_PREFIX_QUEUED_PAIR", L"1" },
        { L"TJ_PREFIX_QUEUE_AHEAD", L"1" },     { L"TJ_PREFIX_REPLAY_PREPARE", L"1" },
        { L"TJ_SHADER_PREFETCH", L"1" },        { L"TJ_SNAPSHOT_LATE_PROOF", L"1" },
        { L"TJ_SPU_CTX_REUSE", L"1" },          { L"TJ_TEXTURE_LABEL_BATCH", L"1" },
        { L"TJ_TEXTURE_LABEL_CAP", L"32" },     { L"TJ_TEXTURE_LABEL_EARLY", L"3" },
        { L"TJ_TEXTURE_SNAPSHOT_HINT_RETARGET", L"1" },
        { L"TJ_TEXTURE_SNAPSHOT_RETAIN", L"1" },{ L"TJ_TEXTURE_WRITE_WATCH", L"1" },
        { L"TJ_VERTEX_PARALLEL", L"8" },        { L"TJ_VERTEX_PLAN", L"1" },
        { L"TJ_VP_UCODE_HASH_REUSE", L"1" },
    };
    for (size_t i = 0; i < sizeof recommended / sizeof recommended[0]; i++)
        SetEnvironmentVariableW(recommended[i][0], recommended[i][1]);

    if (g_settings.two_k) {
        SetEnvironmentVariableW(L"TJ_RENDER_SCALE", L"2");
        SetEnvironmentVariableW(L"TJ_VBLANK_SOURCE", L"display");
        SetEnvironmentVariableW(L"TJ_PRESENT_PHASE", L"1");
    }
    if (g_settings.fullscreen)
        SetEnvironmentVariableW(L"TJ_WINDOW_MODE", L"borderless");

    /* Start the game with the engine folder as working directory and wait. */
    wchar_t cmd[MAXP + 4];
    swprintf(cmd, MAXP + 4, L"\"%ls\"", exe);
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, engine, &si, &pi))
        return fail(L"Não consegui iniciar o jogo:", exe);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    /* A crash ends the game with an NTSTATUS error code (0xC0000005 access
     * violation, 0xC0000409 fail-fast, 0xC00000FD stack overflow...). Say so
     * and point at the files a bug report needs. */
    if (code >= 0xC0000000u || code == 0x80000003u) {
        wchar_t text[MAXP + 768];
        swprintf(text, MAXP + 768,
                 L"Tokyo Jungle closed unexpectedly (error 0x%08lX).\n\n"
                 L"To report it, attach stderr.log, stdout.log and any crash-*.dmp from:\n%ls\n\n"
                 L"O jogo fechou inesperadamente. Para reportar, anexe esses arquivos da pasta acima.\n\n"
                 L"Open the logs folder? / Abrir a pasta?",
                 (unsigned long)code, logs);
        if (MessageBoxW(NULL, text, L"Tokyo Jungle", MB_YESNO | MB_ICONERROR) == IDYES) {
            wchar_t open[MAXP + 32];
            swprintf(open, MAXP + 32, L"explorer.exe \"%ls\"", logs);
            STARTUPINFOW esi = { sizeof esi };
            PROCESS_INFORMATION epi;
            if (CreateProcessW(NULL, open, NULL, NULL, FALSE, 0, NULL, NULL, &esi, &epi)) {
                CloseHandle(epi.hThread);
                CloseHandle(epi.hProcess);
            }
        }
    }
    return (int)code;
}

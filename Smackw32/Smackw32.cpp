// ============================================================
//  SmackIT - general purpose Smacker (smackw32.dll) replacement
//
//  Games call the Smacker API as usual. Instead of decoding .smk files this DLL
//  plays a modern video with the same name (MOVIE.SMK -> MOVIE.mp4 by default)
//  through Media Foundation in a borderless overlay on top of the game window,
//  then tells the game the movie has ended.
//
//  Nothing here is tied to one game. The game window, its size and whether it is
//  fullscreen are detected at runtime, and everything can be overridden in
//  SmackIT.ini placed next to this DLL (see the example SmackIT.ini).
//
//  Based on the WoWRevived Smackw32.dll.
// ============================================================

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfplay.h>
#include <mferror.h>
#include <mfidl.h>
#include <evr.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <atomic>

#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "user32.lib")

// ============================================================
//  Settings (SmackIT.ini, section [SmackIT])
// ============================================================

struct Settings {
    bool        debug = false;          // Debug=1 writes Smackw32_log.txt
    std::string extension = "mp4";      // VideoExtension=mp4
    std::string folder;                 // VideoFolder= (optional, checked before the movie's own folder)
    std::string windowTitle;            // WindowTitle= (optional, otherwise the largest visible window of the game)
    std::string windowClass;            // WindowClass= (optional)
    int         fullscreen = -1;        // Fullscreen=-1 auto, 0 windowed, 1 fullscreen
    bool        stretch = false;        // Stretch=1 fills the area instead of keeping the video's aspect ratio
    bool        skipKeys = false;       // SkipKeys=1 lets Escape / Space / Enter / left click end a movie
    bool        hideCursor = true;      // HideCursor=1 hides the mouse cursor while a movie plays
    int         volume = 100;           // Volume=0..100
    int         x = 0, y = 0;           // X= Y= offset of the video area inside the game's client area
    int         width = 0, height = 0;  // Width= Height= size of the video area, 0 = whole client area
};

Settings            cfg;
HMODULE             thisModule = NULL;
FILE*               logFile = nullptr;
CRITICAL_SECTION    logLock;

void Log(const char* fmt, ...)
{
    if (!cfg.debug) return;
    EnterCriticalSection(&logLock);
    if (!logFile) {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(thisModule, path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        if (slash) *(slash + 1) = '\0';
        strcat(path, "Smackw32_log.txt");
        logFile = fopen(path, "w");
    }
    if (logFile) {
        va_list args;
        va_start(args, fmt);
        fprintf(logFile, "[%08lu] ", GetTickCount());
        vfprintf(logFile, fmt, args);
        fprintf(logFile, "\n");
        fflush(logFile);
        va_end(args);
    }
    LeaveCriticalSection(&logLock);
}

// folder this DLL lives in (normally the game folder), with a trailing backslash
std::string ModuleFolder()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(thisModule, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) *(slash + 1) = '\0';
    return path;
}

std::string IniString(const char* ini, const char* key, const char* def)
{
    char buffer[MAX_PATH] = {};
    GetPrivateProfileStringA("SmackIT", key, def, buffer, sizeof(buffer), ini);
    return buffer;
}

int IniInt(const char* ini, const char* key, int def)
{
    return (int)GetPrivateProfileIntA("SmackIT", key, def, ini);
}

void LoadSettings()
{
    std::string ini = ModuleFolder() + "SmackIT.ini";
    const char* path = ini.c_str();
    cfg.debug       = IniInt(path, "Debug", 0) != 0;
    cfg.extension   = IniString(path, "VideoExtension", "mp4");
    cfg.folder      = IniString(path, "VideoFolder", "");
    cfg.windowTitle = IniString(path, "WindowTitle", "");
    cfg.windowClass = IniString(path, "WindowClass", "");
    cfg.fullscreen  = IniInt(path, "Fullscreen", -1);
    cfg.stretch     = IniInt(path, "Stretch", 0) != 0;
    cfg.skipKeys    = IniInt(path, "SkipKeys", 0) != 0;
    cfg.hideCursor  = IniInt(path, "HideCursor", 1) != 0;
    cfg.volume      = IniInt(path, "Volume", 100);
    cfg.x           = IniInt(path, "X", 0);
    cfg.y           = IniInt(path, "Y", 0);
    cfg.width       = IniInt(path, "Width", 0);
    cfg.height      = IniInt(path, "Height", 0);

    if (!cfg.extension.empty() && cfg.extension[0] == '.') cfg.extension.erase(0, 1);
    if (cfg.extension.empty()) cfg.extension = "mp4";
    if (!cfg.folder.empty() && cfg.folder.back() != '\\' && cfg.folder.back() != '/') cfg.folder += '\\';
    if (cfg.volume < 0) cfg.volume = 0;
    if (cfg.volume > 100) cfg.volume = 100;

    char exe[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    Log("SmackIT loaded into %s", exe);
    Log("Settings: ini=%s extension=%s folder=%s title=%s class=%s fullscreen=%d stretch=%d skip=%d hideCursor=%d volume=%d area=%d,%d %dx%d",
        path, cfg.extension.c_str(), cfg.folder.c_str(), cfg.windowTitle.c_str(), cfg.windowClass.c_str(), cfg.fullscreen,
        cfg.stretch, cfg.skipKeys, cfg.hideCursor, cfg.volume, cfg.x, cfg.y, cfg.width, cfg.height);
}

// Settings, Media Foundation and the log are set up on first use rather than in DllMain:
// Media Foundation loads DLLs and creates threads, which must not happen under the loader lock.
INIT_ONCE initOnce = INIT_ONCE_STATIC_INIT;
BOOL CALLBACK InitOnce(PINIT_ONCE, PVOID, PVOID*)
{
    LoadSettings();
    HRESULT hr = MFStartup(MF_VERSION);
    Log("MFStartup: %08X", (unsigned)hr);
    return TRUE;
}
void EnsureInit() { InitOnceExecuteOnce(&initOnce, InitOnce, NULL, NULL); }

// ============================================================
//  Finding the game window
// ============================================================

struct FindData { DWORD pid; HWND best; LONGLONG bestArea; };

BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM lParam)
{
    FindData* data = (FindData*)lParam;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != data->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    char cls[64] = {};
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (strcmp(cls, "SmackITOverlay") == 0) return TRUE;
    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return TRUE;
    LONGLONG area = (LONGLONG)(rc.right - rc.left) * (rc.bottom - rc.top);
    if (area > data->bestArea) { data->best = hwnd; data->bestArea = area; }
    return TRUE;
}

HWND FindGameWindow()
{
    if (!cfg.windowTitle.empty() || !cfg.windowClass.empty()) {
        HWND hwnd = FindWindowA(cfg.windowClass.empty() ? NULL : cfg.windowClass.c_str(),
                                cfg.windowTitle.empty() ? NULL : cfg.windowTitle.c_str());
        if (hwnd) return hwnd;
    }
    // the largest visible top level window that belongs to this process
    FindData data = { GetCurrentProcessId(), NULL, -1 };
    EnumWindows(FindGameWindowProc, (LPARAM)&data);
    if (data.best) return data.best;
    // a game that hasn't shown its window yet: its active / foreground window, if it is ours
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId() ? fg : NULL;
}

// The screen rectangle the video should cover, and whether the game is fullscreen.
bool GetVideoArea(HWND gameWnd, RECT& area, bool& fullscreen)
{
    RECT client = {};
    if (gameWnd && GetClientRect(gameWnd, &client) && client.right > 0 && client.bottom > 0) {
        POINT origin = { 0, 0 };
        ClientToScreen(gameWnd, &origin);
        area = { origin.x, origin.y, origin.x + client.right, origin.y + client.bottom };
    }
    else {
        // no usable game window: the primary monitor
        area = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        gameWnd = NULL;
    }

    if (cfg.fullscreen >= 0) {
        fullscreen = cfg.fullscreen != 0;
    }
    else {
        // auto: the client area covers the whole monitor it is on
        HMONITOR monitor = MonitorFromRect(&area, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        fullscreen = !gameWnd || (GetMonitorInfoA(monitor, &mi) &&
            area.left <= mi.rcMonitor.left && area.top <= mi.rcMonitor.top &&
            area.right >= mi.rcMonitor.right && area.bottom >= mi.rcMonitor.bottom);
    }

    // manual area inside the client area
    area.left += cfg.x;
    area.top += cfg.y;
    if (cfg.width > 0) area.right = area.left + cfg.width;
    if (cfg.height > 0) area.bottom = area.top + cfg.height;
    return gameWnd != NULL;
}

// ============================================================
//  Smacker structures
//
//  Games read fields of the structure SmackOpen returns, most importantly Frames and
//  FrameNum to decide when a movie has ended, so the real layout (smack.h, v3/v4) is used.
// ============================================================

#pragma pack(push, 4)
struct Smack {
    uint32_t Version;               // 0x000 'SMK2'
    uint32_t Width;                 // 0x004
    uint32_t Height;                // 0x008
    uint32_t Frames;                // 0x00C
    uint32_t MSPerFrame;            // 0x010
    uint32_t SmackerType;           // 0x014
    uint32_t LargestInTrack[7];     // 0x018
    uint32_t tablesize;             // 0x034
    uint32_t codesize;              // 0x038
    uint32_t absize;                // 0x03C
    uint32_t detailsize;            // 0x040
    uint32_t typesize;              // 0x044
    uint32_t TrackType[7];          // 0x048
    uint32_t extra;                 // 0x064
    uint32_t NewPalette;            // 0x068
    uint8_t  Palette[772];          // 0x06C
    uint32_t PalType;               // 0x370
    uint32_t FrameNum;              // 0x374
    uint32_t FrameSize;             // 0x378
    uint32_t SndSize;               // 0x37C
    int32_t  LastRectx;             // 0x380
    int32_t  LastRecty;             // 0x384
    int32_t  LastRectw;             // 0x388
    int32_t  LastRecth;             // 0x38C
    uint32_t OpenFlags;             // 0x390
    uint32_t LeftOfs;               // 0x394
    uint32_t TopOfs;                // 0x398
    uint32_t LargeWriteSize;        // 0x39C
    uint32_t Highest1SecRate;       // 0x3A0
    uint32_t Highest1SecFrame;      // 0x3A4
    uint32_t ReadError;             // 0x3A8
    uint32_t addr32;                // 0x3AC
};
#pragma pack(pop)
static_assert(offsetof(Smack, FrameNum) == 0x374, "Smack layout");

// Some games look past the documented structure, so each handle gets spare zeroed space.
struct SmackHandle {
    Smack    smk;
    uint8_t  spare[1024];
};

#define SMACKFILEHANDLE     0x01000
#define SMACK_VERSION       0x324B4D53  // 'SMK2'
#define PLAYING_FRAMES      0x00FFFFFF  // reported while a movie plays, so loops keep running

// ============================================================
//  Playback state (game thread unless noted)
// ============================================================

class MediaPlayerCallback;

SmackHandle*            current = nullptr;          // the movie being played
IMFPMediaPlayer*        pMediaPlayer = NULL;
MediaPlayerCallback*    pCallback = nullptr;
std::atomic<HWND>       overlayWindow{ NULL };      // also read by the watch thread
std::atomic<HWND>       gameWindow{ NULL };         // also read by the watch thread
std::atomic<bool>       videoFinished{ false };     // set by the player callback, the watch thread or a skip
std::atomic<LONG>       movieId{ 0 };               // stops a late "ended" event of an old movie ending a new one
std::atomic<bool>       watchThreadRunning{ false };
bool                    overlayFullscreen = false;
bool                    movieHidCursor = false;
bool                    skipArmed = false;          // the skip keys were all released since the movie started

// ============================================================
//  Media Foundation callback (runs on the game thread, from the message pump in SmackNextFrame / SmackWait)
// ============================================================

class MediaPlayerCallback : public IMFPMediaPlayerCallback {
    LONG refCount = 1;
    LONG id;
public:
    explicit MediaPlayerCallback(LONG movie) : id(movie) {}

    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* pEventHeader) override {
        bool mine = (id == movieId);
        switch (pEventHeader->eEventType) {
        case MFP_EVENT_TYPE_MEDIAITEM_CREATED:
            if (SUCCEEDED(pEventHeader->hrEvent) && mine && pMediaPlayer) {
                MFP_MEDIAITEM_CREATED_EVENT* pEvent = MFP_GET_MEDIAITEM_CREATED_EVENT(pEventHeader);
                pMediaPlayer->SetMediaItem(pEvent->pMediaItem);
            }
            else {
                Log("Media item could not be opened: %08X", (unsigned)pEventHeader->hrEvent);
                if (mine) videoFinished = true;
            }
            break;
        case MFP_EVENT_TYPE_MEDIAITEM_SET:
            if (mine && pMediaPlayer && SUCCEEDED(pEventHeader->hrEvent)) {
                pMediaPlayer->SetAspectRatioMode(cfg.stretch ? MFVideoARMode_None : MFVideoARMode_PreservePicture);
                pMediaPlayer->SetBorderColor(RGB(0, 0, 0));
                pMediaPlayer->SetVolume(cfg.volume / 100.0f);
                SIZE video = {}, arSize = {};
                if (SUCCEEDED(pMediaPlayer->GetNativeVideoSize(&video, &arSize)))
                    Log("Video size %dx%d", video.cx, video.cy);
                pMediaPlayer->Play();
                Log("Playback started");
            }
            else if (mine) {
                Log("SetMediaItem failed: %08X", (unsigned)pEventHeader->hrEvent);
                videoFinished = true;
            }
            break;
        case MFP_EVENT_TYPE_PLAYBACK_ENDED:
            if (mine) videoFinished = true;
            Log("Video playback ended");
            break;
        case MFP_EVENT_TYPE_ERROR:
            if (mine) videoFinished = true;
            Log("Playback error: %08X", (unsigned)pEventHeader->hrEvent);
            break;
        default:
            break;
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == __uuidof(IMFPMediaPlayerCallback)) {
            *ppv = static_cast<IMFPMediaPlayerCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    virtual ~MediaPlayerCallback() {}
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&refCount);
        if (count == 0) delete this;
        return count;
    }
};

// ============================================================
//  Overlay window
// ============================================================

#define WM_CLOSE_OVERLAY (WM_USER + 1)

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CLOSE_OVERLAY:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (overlayWindow == hwnd) overlayWindow = NULL;
        return 0;
    case WM_SETCURSOR:
        if (cfg.hideCursor) { SetCursor(NULL); return TRUE; }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (pMediaPlayer) pMediaPlayer->UpdateVideo();
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// Keeps the overlay over the game: follows it when it moves or resizes, hides it while
// another program is in front, and ends the movie if the game window goes away.
DWORD WINAPI WatchThread(LPVOID)
{
    for (;;) {
        RECT lastArea = {};
        while (HWND overlay = overlayWindow) {
            HWND game = gameWindow;
            if (game && !IsWindow(game)) {
                Log("Game window closed during a movie");
                videoFinished = true;
                gameWindow = NULL;
                game = NULL;
            }
            HWND foreground = GetForegroundWindow();
            DWORD pid = 0;
            if (foreground) GetWindowThreadProcessId(foreground, &pid);
            bool focused = !foreground || pid == GetCurrentProcessId();
            UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS;
            if (focused && game && !IsIconic(game)) {
                RECT area; bool fs;
                GetVideoArea(game, area, fs);
                if (!EqualRect(&area, &lastArea)) {
                    SetWindowPos(overlay, NULL, area.left, area.top, area.right - area.left, area.bottom - area.top, flags | SWP_NOZORDER);
                    lastArea = area;
                }
                if (!IsWindowVisible(overlay)) SetWindowPos(overlay, NULL, 0, 0, 0, 0, flags | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            }
            else if (!focused || (game && IsIconic(game))) {
                if (IsWindowVisible(overlay)) SetWindowPos(overlay, NULL, 0, 0, 0, 0, flags | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_HIDEWINDOW);
            }
            Sleep(30);
        }
        // stand down, unless an overlay appeared while leaving and nobody else has taken over
        watchThreadRunning = false;
        bool expected = false;
        if (!overlayWindow || !watchThreadRunning.compare_exchange_strong(expected, true)) return 0;
    }
}

void StartWatchThread()
{
    bool expected = false;
    if (!watchThreadRunning.compare_exchange_strong(expected, true)) return;
    HANDLE thread = CreateThread(NULL, 0, WatchThread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    else watchThreadRunning = false;
}

void CreateOverlayWindow()
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = thisModule;
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = "SmackITOverlay";
        registered = RegisterClassExA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    HWND game = FindGameWindow();
    RECT area; bool fullscreen;
    bool haveGame = GetVideoArea(game, area, fullscreen);
    gameWindow = haveGame ? game : NULL;
    overlayFullscreen = fullscreen;

    // Owned by the game window it stays above it; fullscreen it must be topmost to show over the game.
    // Layered + transparent makes it click-through, so mouse and keyboard input still reach the game
    // (the same window styles the WoWRevived build uses).
    HWND overlay = CreateWindowExA(
        WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED | (fullscreen ? WS_EX_TOPMOST : 0) | (haveGame ? 0 : WS_EX_TOOLWINDOW),
        "SmackITOverlay", NULL, WS_POPUP,
        area.left, area.top, area.right - area.left, area.bottom - area.top,
        (haveGame && !fullscreen) ? game : NULL, NULL, thisModule, NULL);
    overlayWindow = overlay;
    if (!overlay) {
        Log("Overlay window could not be created: %lu", GetLastError());
        return;
    }
    ShowWindow(overlay, SW_SHOWNA);
    StartWatchThread();

    char title[128] = {};
    if (game) GetWindowTextA(game, title, sizeof(title));
    Log("Overlay %dx%d at %d,%d, game window %p \"%s\", %s", area.right - area.left, area.bottom - area.top,
        area.left, area.top, (void*)game, title, fullscreen ? "fullscreen" : "windowed");
}

void ClosePlayback()
{
    movieId++; // anything still in flight belongs to an old movie now
    if (pMediaPlayer) {
        pMediaPlayer->Stop();
        pMediaPlayer->Shutdown();
        pMediaPlayer->Release();
        pMediaPlayer = NULL;
    }
    if (pCallback) {
        pCallback->Release();
        pCallback = nullptr;
    }
    HWND overlay = overlayWindow.exchange(NULL);
    if (overlay) {
        // destroyed by the thread that owns it (normally this one)
        if (GetWindowThreadProcessId(overlay, NULL) == GetCurrentThreadId()) DestroyWindow(overlay);
        else PostMessageA(overlay, WM_CLOSE_OVERLAY, 0, 0);
    }
    gameWindow = NULL;
}

// ============================================================
//  Movie names
// ============================================================

bool FileExists(const std::string& path)
{
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::string FullPath(const std::string& path)
{
    char full[MAX_PATH] = {};
    DWORD len = GetFullPathNameA(path.c_str(), MAX_PATH, full, NULL);
    return (len > 0 && len < MAX_PATH) ? std::string(full) : path;
}

// the name the game asked for, also when it passed an open file handle instead of a name
std::string SmackerName(const char* name, DWORD flags)
{
    if (flags & SMACKFILEHANDLE) {
        char path[MAX_PATH] = {};
        DWORD len = GetFinalPathNameByHandleA((HANDLE)name, path, MAX_PATH, FILE_NAME_NORMALIZED);
        if (len == 0 || len >= MAX_PATH) return "";
        std::string result = path;
        if (result.compare(0, 4, "\\\\?\\") == 0) result.erase(0, 4);
        return result;
    }
    return name ? name : "";
}

// MOVIES\INTRO.SMK -> the first of VideoFolder\INTRO.mp4 and MOVIES\INTRO.mp4 that exists
std::string FindReplacement(const std::string& smacker)
{
    size_t slash = smacker.find_last_of("\\/:");
    size_t dot = smacker.find_last_of('.');
    std::string stem = (dot != std::string::npos && (slash == std::string::npos || dot > slash)) ? smacker.substr(0, dot) : smacker;
    std::string fileName = (slash != std::string::npos) ? stem.substr(slash + 1) : stem;

    std::string candidates[2];
    int count = 0;
    if (!cfg.folder.empty()) {
        std::string folder = cfg.folder;
        if (folder.find(':') == std::string::npos && folder.compare(0, 2, "\\\\") != 0) folder = ModuleFolder() + folder;
        candidates[count++] = folder + fileName + "." + cfg.extension;
    }
    candidates[count++] = stem + "." + cfg.extension;

    for (int i = 0; i < count; i++) {
        std::string full = FullPath(candidates[i]);
        Log("Looking for %s", full.c_str());
        if (FileExists(full)) return full;
    }
    return "";
}

// ============================================================
//  Skip keys
// ============================================================

bool SkipPressed()
{
    static const int keys[] = { VK_ESCAPE, VK_SPACE, VK_RETURN, VK_LBUTTON };
    bool down = false;
    for (int key : keys) if (GetAsyncKeyState(key) & 0x8000) down = true;
    if (!down) { skipArmed = true; return false; }
    if (!skipArmed) return false; // still held from before the movie started
    HWND foreground = GetForegroundWindow();
    DWORD pid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &pid);
    return pid == GetCurrentProcessId();
}

// ============================================================
//  Per frame work
// ============================================================

void PumpMessages()
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

void UpdateMovie(Smack* smk)
{
    PumpMessages(); // Media Foundation delivers its events through this thread's message queue
    if (!smk) return;
    bool isCurrent = current && smk == &current->smk;
    if (cfg.skipKeys && isCurrent && !videoFinished && SkipPressed()) {
        Log("Movie skipped");
        videoFinished = true;
    }
    if (videoFinished && smk->Frames == PLAYING_FRAMES) {
        // Report the last frame. Loops that check FrameNum == Frames - 1, FrameNum >= Frames - 1
        // or count their own frames up to Frames all end here.
        smk->Frames = smk->FrameNum + 1;
        Log("Movie finished at frame %u", smk->FrameNum);
        if (isCurrent) ClosePlayback(); // the game may draw again before it calls SmackClose
    }
    if (cfg.hideCursor && movieHidCursor) SetCursor(NULL);
}

// ============================================================
//  Smacker API
//
//  Every export uses the real stdcall signature (the @N in the .def file), so the stack stays
//  balanced whatever the game calls. Functions that only matter for real decoding do nothing.
// ============================================================

extern "C" {

Smack* WINAPI SmackOpen(const char* name, DWORD flags, DWORD extrabuf)
{
    EnsureInit();
    std::string smacker = SmackerName(name, flags);
    Log("SmackOpen: %s flags=%08X", smacker.c_str(), flags);

    // a movie that was never closed must not leak its player or overlay
    if (pMediaPlayer || overlayWindow) ClosePlayback();

    SmackHandle* handle = (SmackHandle*)calloc(1, sizeof(SmackHandle));
    if (!handle) return nullptr;
    Smack* smk = &handle->smk;
    smk->Version = SMACK_VERSION;
    smk->Frames = PLAYING_FRAMES;
    smk->MSPerFrame = 33;
    smk->OpenFlags = flags;

    HWND game = FindGameWindow();
    RECT client = {};
    if (game && GetClientRect(game, &client) && client.right > 0 && client.bottom > 0) {
        smk->Width = client.right;
        smk->Height = client.bottom;
    }
    else {
        smk->Width = 640;
        smk->Height = 480;
    }

    current = handle;
    long id = ++movieId;
    videoFinished = false;
    skipArmed = false;

    std::string movie = FindReplacement(smacker);
    if (movie.empty()) {
        Log("No replacement video found, the movie ends straight away");
        smk->Frames = 1;
        videoFinished = true;
        return smk;
    }
    Log("Playing %s", movie.c_str());

    if (cfg.hideCursor && !movieHidCursor) {
        ShowCursor(FALSE); // once per movie, undone in SmackClose
        movieHidCursor = true;
    }

    CreateOverlayWindow();
    HWND overlay = overlayWindow;
    if (!overlay) {
        smk->Frames = 1;
        videoFinished = true;
        return smk;
    }

    pCallback = new MediaPlayerCallback(id);
    HRESULT hr = MFPCreateMediaPlayer(NULL, FALSE, 0, pCallback, overlay, &pMediaPlayer);
    if (SUCCEEDED(hr)) {
        int len = MultiByteToWideChar(CP_ACP, 0, movie.c_str(), -1, NULL, 0);
        std::wstring wMovie(len, L'\0');
        MultiByteToWideChar(CP_ACP, 0, movie.c_str(), -1, &wMovie[0], len);
        hr = pMediaPlayer->CreateMediaItemFromURL(wMovie.c_str(), FALSE, 0, NULL);
        Log(SUCCEEDED(hr) ? "Media item requested" : "CreateMediaItemFromURL failed: %08X", (unsigned)hr);
    }
    else {
        Log("MFPCreateMediaPlayer failed: %08X", (unsigned)hr);
    }
    if (FAILED(hr)) videoFinished = true;
    return smk;
}

void WINAPI SmackClose(Smack* smk)
{
    Log("SmackClose");
    if (!smk) return;
    if (current && smk == &current->smk) {
        ClosePlayback();
        current = nullptr;
        videoFinished = false;
        if (movieHidCursor) {
            ShowCursor(TRUE); // undo SmackOpen's single ShowCursor(FALSE)
            movieHidCursor = false;
        }
    }
    free(smk); // SmackHandle starts with the Smack, so this is the calloc'd block
}

DWORD WINAPI SmackDoFrame(Smack* smk)               { UpdateMovie(smk); return 0; }
void  WINAPI SmackNextFrame(Smack* smk)
{
    UpdateMovie(smk);
    if (smk && smk->FrameNum + 2 < smk->Frames) smk->FrameNum++;
}
DWORD WINAPI SmackWait(Smack* smk)                  { UpdateMovie(smk); Sleep(1); return 0; } // 0 = the next frame is due now
void  WINAPI SmackGoto(Smack* smk, DWORD frame)     { UpdateMovie(smk); }
DWORD WINAPI SmackSoundOnOff(Smack* smk, DWORD on)  { return 1; }
DWORD WINAPI SmackSoundInTrack(Smack* smk, DWORD trackflags) { return 0; }
void  WINAPI SmackVolumePan(Smack* smk, DWORD trackflag, DWORD volume, DWORD pan) {}
void  WINAPI SmackSummary(Smack* smk, void* sum)    {}
DWORD WINAPI SmackGetTrackData(Smack* smk, void* dest, DWORD trackflag) { return 0; }
DWORD WINAPI SmackGetKeyFrame(Smack* smk, DWORD frame, DWORD flags) { return 0; }
void  WINAPI SmackToBuffer(Smack* smk, DWORD left, DWORD top, DWORD pitch, DWORD destheight, void* buf, DWORD flags) {}
DWORD WINAPI SmackToBufferRect(Smack* smk, DWORD surface) { return 0; } // 0 = no changed rectangles to copy
void  WINAPI SmackToScreen(Smack* smk, DWORD left, DWORD top, DWORD bytesPerScanline, const void* winTbl, void* setBank, DWORD flags) {}
void  WINAPI SmackColorRemap(Smack* smk, const void* pal, DWORD numcolors, DWORD paltype) {}
void  WINAPI SmackColorRemapWithTrans(Smack* smk, const void* pal, DWORD numcolors, DWORD paltype, DWORD transindex) {}
void  WINAPI SmackColorTrans(Smack* smk, const void* trans) {}
void  WINAPI SmackFrameRate(DWORD forcerate)        {}
void  WINAPI SmackSimulate(DWORD sim)               {}
DWORD WINAPI SmackUseMMX(DWORD flag)                { return 0; }
DWORD WINAPI SmackDDSurfaceType(void* lpDDS)        { return 0; }
DWORD WINAPI SmackSetSystemRes(DWORD mode)          { return 1; }
DWORD WINAPI SmackIsSoftwareCursor(const void* lpDDSP, HCURSOR cursor) { return 0; }
DWORD WINAPI SmackCheckCursor(HWND wnd, DWORD x, DWORD y, DWORD w, DWORD h) { return 0; }
void  WINAPI SmackRestoreCursor(DWORD checkcount)   {}
void  WINAPI RADSetMemory(void* alloc, void* freeFn) {}

// Sound setup: report success so games carry on (the replacement video plays its own audio)
BYTE  WINAPI SmackSoundUseMSS(void* digDriver)      { return 1; }
BYTE  WINAPI SmackSoundUseDirectSound(void* dsound) { return 1; }
BYTE  WINAPI SmackSoundUseDW(DWORD freq, DWORD bits, DWORD chans) { return 1; }
BYTE  WINAPI SmackSoundUseWin()                     { return 1; }
void  WINAPI SmackSoundCheck()                      {}

// Blitter: games check the handle isn't NULL, nothing is ever drawn with it
static uint32_t dummyBlit[64];
void* WINAPI SmackBlitOpen(DWORD flags)             { return dummyBlit; }
void  WINAPI SmackBlitClose(void* blit)             {}
void  WINAPI SmackBlitSetPalette(void* blit, void* palette, DWORD paltype) {}
DWORD WINAPI SmackBlitSetFlags(void* blit, DWORD flags) { return flags; }
void  WINAPI SmackBlit(void* blit, void* dest, DWORD destpitch, DWORD destx, DWORD desty, void* src, DWORD srcpitch, DWORD srcx, DWORD srcy, DWORD srcw, DWORD srch) {}
void  WINAPI SmackBlitClear(void* blit, void* dest, DWORD destpitch, DWORD destx, DWORD desty, DWORD destw, DWORD desth, LONG color) {}
void  WINAPI SmackBlitTrans(void* blit, void* dest, DWORD destpitch, DWORD destx, DWORD desty, void* src, DWORD srcpitch, DWORD srcx, DWORD srcy, DWORD srcw, DWORD srch, DWORD trans) {}
void  WINAPI SmackBlitMask(void* blit, void* dest, DWORD destpitch, DWORD destx, DWORD desty, void* src, DWORD srcpitch, DWORD srcx, DWORD srcy, DWORD srcw, DWORD srch, void* mask) {}
void  WINAPI SmackBlitMerge(void* blit, void* dest, DWORD destpitch, DWORD destx, DWORD desty, void* src, DWORD srcpitch, DWORD srcx, DWORD srcy, void* back, DWORD backpitch, DWORD backx, DWORD backy, DWORD srcw, DWORD srch) {}
char* WINAPI SmackBlitString(void* blit, char* dest) { if (dest) strcpy(dest, "SmackIT"); return dest; }

// SmackBuffer (GDI output). The returned block follows the real SmackBuf layout for the fields
// games commonly read (size, pitch and the pixel buffer), the rest stays zero.
#pragma pack(push, 4)
struct SmackBuf {
    uint32_t Reversed, SurfaceType, BlitType, FullScreen;
    uint32_t Width, Height, Pitch, Zoomed, ZWidth, ZHeight;
    uint32_t DispColors, MaxPalColors, PalColorsInUse, StartPalColor, EndPalColor;
    RGBQUAD  Palette[256];
    uint32_t PalType, forceredraw, didapalette;
    void*    Buffer;
    uint8_t  rest[1024];
};
#pragma pack(pop)

void* WINAPI SmackBufferOpen(HWND wnd, DWORD blitType, DWORD width, DWORD height, DWORD zoomW, DWORD zoomH)
{
    EnsureInit();
    SmackBuf* sbuf = (SmackBuf*)calloc(1, sizeof(SmackBuf));
    if (!sbuf) return nullptr;
    sbuf->BlitType = blitType;
    sbuf->Width = width;
    sbuf->Height = height;
    sbuf->Pitch = (width + 3) & ~3u;
    sbuf->ZWidth = zoomW ? zoomW : width;
    sbuf->ZHeight = zoomH ? zoomH : height;
    sbuf->DispColors = sbuf->MaxPalColors = 256;
    sbuf->EndPalColor = 255;
    sbuf->Buffer = calloc(1, (size_t)sbuf->Pitch * (height ? height : 1));
    Log("SmackBufferOpen %ux%u", width, height);
    return sbuf;
}
void  WINAPI SmackBufferClose(void* sbuf)
{
    if (!sbuf) return;
    free(((SmackBuf*)sbuf)->Buffer);
    free(sbuf);
}
DWORD WINAPI SmackBufferBlit(void* sbuf, HDC dc, LONG hwndx, LONG hwndy, LONG subx, LONG suby, LONG subw, LONG subh) { return 0; }
void  WINAPI SmackBufferNewPalette(void* sbuf, const void* pal, DWORD paltype) {}
DWORD WINAPI SmackBufferSetPalette(void* sbuf)      { return 0; }
void  WINAPI SmackBufferClear(void* sbuf, DWORD color) {}
void  WINAPI SmackBufferToBuffer(void* dest, LONG destx, LONG desty, const void* src, LONG srcx, LONG srcy, LONG srcw, LONG srch) {}
void  WINAPI SmackBufferToBufferTrans(void* dest, LONG destx, LONG desty, const void* src, LONG srcx, LONG srcy, LONG srcw, LONG srch, DWORD trans) {}
void  WINAPI SmackBufferToBufferMask(void* dest, LONG destx, LONG desty, const void* src, LONG srcx, LONG srcy, LONG srcw, LONG srch, DWORD trans, const void* mask) {}
void  WINAPI SmackBufferCopyPalette(void* dest, void* src, DWORD remap) {}
DWORD WINAPI SmackBufferFocused(void* sbuf)         { return 1; }
void  WINAPI SmackBufferStartMultipleBlits(void* sbuf) {}
void  WINAPI SmackBufferEndMultipleBlits(void* sbuf) {}
DWORD WINAPI SmackBufferFromScreen(void* sbuf, LONG x, LONG y) { return 0; }
void  WINAPI SmackBufferString(void* sbuf, char* dest) { if (dest) strcpy(dest, "SmackIT"); }

} // extern "C"

// ============================================================
//  DllMain
// ============================================================

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        thisModule = module;
        InitializeCriticalSection(&logLock);
        DisableThreadLibraryCalls(module);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        // Nothing is shut down here: the loader lock is held, so stopping Media Foundation or waiting for
        // threads could deadlock. Games only unload this DLL when they exit, and Windows cleans up then.
        overlayWindow = NULL; // lets the watch thread stop
        if (logFile && !reserved) { fclose(logFile); logFile = nullptr; }
    }
    return TRUE;
}

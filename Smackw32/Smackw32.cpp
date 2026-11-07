#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <mfapi.h>
#include <mfplay.h>
#include <mferror.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <mfidl.h>

#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfplat.lib")

// ============================================================
//  Globals
// ============================================================

FILE*               logFile         = nullptr;
bool                debug           = false;
int                 regWidth        = 640;
int                 regHeight       = 480;
int                 offsetY         = 0;
bool                videoFinished   = false;
bool                isFullscreen    = false;
HWND                overlayWindow   = NULL;
IMFPMediaPlayer*    pMediaPlayer    = NULL;
std::string         window          = "game_window_name";

// ============================================================
//  Logging
// ============================================================

void Log(const char* fmt, ...)
{
    if (!debug) return;
    if (!logFile) logFile = fopen("Smackw32_log.txt", "a");
    if (!logFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(logFile, fmt, args);
    fprintf(logFile, "\n");
    fflush(logFile);
    va_end(args);
}

DWORD WINAPI FocusWatchThread(LPVOID) {
    while (overlayWindow) {
        HWND foreground = GetForegroundWindow();
        HWND gameWnd = FindWindowA(NULL, window.c_str());
        bool gameFocused = (foreground == gameWnd || foreground == overlayWindow);
        if (overlayWindow) {
            if (!gameFocused) ShowWindow(overlayWindow, SW_HIDE);
            else ShowWindow(overlayWindow, SW_SHOWNA);
        }
        Sleep(100);
    }
    return 0;
}

// ============================================================
//  Media Foundation callback
// ============================================================

class MediaPlayerCallback : public IMFPMediaPlayerCallback {
    LONG refCount = 1;
public:
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* pEventHeader) override {
        if (pEventHeader->eEventType == MFP_EVENT_TYPE_MEDIAITEM_CREATED) {
            MFP_MEDIAITEM_CREATED_EVENT* pEvent = MFP_GET_MEDIAITEM_CREATED_EVENT(pEventHeader);
            if (SUCCEEDED(pEventHeader->hrEvent)) {
                pMediaPlayer->SetMediaItem(pEvent->pMediaItem);
                //Log("MFP_EVENT_TYPE_MEDIAITEM_CREATED SUCCEEDED"); logged
            }
        }
        else if (pEventHeader->eEventType == MFP_EVENT_TYPE_MEDIAITEM_SET) {
            pMediaPlayer->Play();
            Log("Playback started");
        }
        else if (pEventHeader->eEventType == MFP_EVENT_TYPE_PLAYBACK_ENDED) {
            videoFinished = true;
            Log("Video playback ended");
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == __uuidof(IMFPMediaPlayerCallback)) {
            *ppv = this;
            AddRef();
            Log("AddRef Called"); // this hasn't been logged yet
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&refCount);
        //Log("Release Happens"); // logged
        if (count == 0) delete this;
        return count;
    }
};

MediaPlayerCallback* pCallback = nullptr;

// ============================================================
//  SMK movie overlay
// ============================================================

#define WM_CLOSE_OVERLAY (WM_USER + 1)

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) { overlayWindow = NULL; }
    if (msg == WM_CLOSE_OVERLAY) { DestroyWindow(hwnd); }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void CloseOverlayWindow() {
    if (pMediaPlayer) {
        pMediaPlayer->Stop();
        pMediaPlayer->Shutdown();
        pMediaPlayer->Release();
        pMediaPlayer = NULL;
    }
    if (pCallback) {
        pCallback->Release();
        pCallback = NULL;
    }
    if (overlayWindow) {
        PostMessage(overlayWindow, WM_CLOSE_OVERLAY, 0, 0);
        overlayWindow = NULL;
    }
}

WNDPROC origGameProc = NULL;

LRESULT CALLBACK GameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_MOVE) {
        POINT clientPos = { 0, 0 };
        ClientToScreen(hwnd, &clientPos);
        if (overlayWindow) {
            SetWindowPos(overlayWindow, HWND_TOP, clientPos.x, clientPos.y + offsetY, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    return CallWindowProc(origGameProc, hwnd, msg, wParam, lParam);
}

void CreateOverlayWindow() {
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "SmackOverlay";
    RegisterClassExA(&wc);

    // Get game window position
    HWND gameWnd = FindWindowA(NULL, window.c_str());
    RECT gameRect = {};
    RECT clientRect = {};
    POINT clientPos = { 0, 0 };
    if (gameWnd) {
        GetClientRect(gameWnd, &clientRect);
        ClientToScreen(gameWnd, &clientPos);
    }
    // Video display area: full width, half height (540 for 1080p source), offset by letterbox
    int videoHeight = regHeight - (offsetY * 2);

    overlayWindow = CreateWindowExA(
        (isFullscreen ? (WS_EX_TOPMOST | WS_EX_NOACTIVATE) : WS_EX_NOACTIVATE) | WS_EX_TRANSPARENT | WS_EX_LAYERED,
        "SmackOverlay", NULL,
        WS_POPUP,
        clientPos.x, clientPos.y + offsetY,
        regWidth, videoHeight,
        isFullscreen ? NULL : gameWnd,
        NULL,
        GetModuleHandleA(NULL), NULL
    );
    ShowWindow(overlayWindow, SW_SHOWNA);
    CreateThread(NULL, 0, FocusWatchThread, NULL, 0, NULL);
    if (gameWnd && !origGameProc) {
        origGameProc = (WNDPROC)SetWindowLongPtr(gameWnd, GWLP_WNDPROC, (LONG_PTR)GameWndProc);
    }
    Log("Overlay window created: %dx%d at x=%d y=%d", regWidth, videoHeight, gameRect.left, gameRect.top + offsetY); // logged
}

// ============================================================
//  Smacker stubs
// ============================================================

struct FakeSmack { uint32_t Width = 0, Height = 0; int OffsetY = 0; };
FakeSmack dummy;

extern "C" {
    void WINAPI SmackClose(void* smk) {
        uint32_t* smkData = (uint32_t*)smk;
        if (smkData) {
            smkData[2] = 1;
            smkData[3] = 0;
        }
        Log("SmackClose");
        ShowCursor(TRUE);
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        CloseOverlayWindow();
    }
    void WINAPI SmackBlitClose(void* smk) { Sleep(1); /*Log("SmackBlitClose");*/ }
    void WINAPI SmackWait(void* smk) { Sleep(1); /*Log("SmackWait");*/ }
    void WINAPI SmackBlitOpen(void* smk) { Sleep(1); /*Log("SmackBlitOpen");*/ }
    void WINAPI SmackDDSurfaceType(void* lpDDS) { Sleep(1); /*Log("SmackDDSurfaceType");*/ }
    void* WINAPI SmackOpen(const char* name, DWORD flags, DWORD extra) {
        Log("SmackOpen: %s", name);
        //fix incoming name string .SMK = .mp4
        std::string movie = name;
        movie.resize(movie.length() - 3);
        movie.append("mp4");
        Log("Updated String: %s", movie.c_str());

        CreateOverlayWindow();

        if (overlayWindow) {
            pCallback = new MediaPlayerCallback();
            HRESULT hr = MFPCreateMediaPlayer(nullptr, FALSE, 0, pCallback, overlayWindow, &pMediaPlayer);
            if (SUCCEEDED(hr)) {
                // Convert to wide string for MFPlay
                int len = MultiByteToWideChar(CP_ACP, 0, movie.c_str(), -1, NULL, 0);
                std::wstring wMovie(len, 0);
                MultiByteToWideChar(CP_ACP, 0, movie.c_str(), -1, &wMovie[0], len);

                hr = pMediaPlayer->CreateMediaItemFromURL(wMovie.c_str(), FALSE, 0, NULL);
                Log(SUCCEEDED(hr) ? "Media item created" : "Failed to create media item"); // logged
            }
            else {
                Log("MFPCreateMediaPlayer failed: %08X", hr); // hasn't been logged happening yet
            }
        }

        dummy.Width = regWidth;
        dummy.Height = regHeight;
        dummy.OffsetY = offsetY;

        return &dummy;
    }
    void WINAPI SmackUseMMX(DWORD flag) { Sleep(1); /*Log("SmackUseMMX: %d", flag);*/ }
    void WINAPI SmackSoundUseDirectSound(void* ds) { Sleep(1); /*Log("SmackSoundUseDirectSound");*/ }
    void WINAPI SmackNextFrame(void* smk) {
        if (videoFinished) {
            // force finish movie with dud frame count
            uint32_t* smkData = (uint32_t*)smk;
            if (smkData) {
                smkData[2] = 1;     // Total Frames = 1
                smkData[3] = 1;     // Current Frame = 1
                videoFinished = false;
            }
        }
        SetCursor(NULL);
        ShowCursor(FALSE);
        // pump messages so the overlay window stays responsive
        //Log("NEXT FRAME"); // logged
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    int WINAPI SmackDoFrame(void* smk) { return  0; /* //Log("SmackDoFrame"); // logged */ }
    void WINAPI SmackToBuffer(void* smk, DWORD l, DWORD t, DWORD p, DWORD h, void* buf, DWORD f) { Sleep(1); /*Log("SmackToBuffer"); // logged */ }
	// Expendable
    void WINAPI SmackVolumePan(void* smk, int volume, int pan, int unknown) { Sleep(1); /*Log("SmackVolumePan");*/ }
    void WINAPI SmackSoundUseMSS(DWORD flag) { Sleep(1); /*Log("SmackSoundUseMSS");*/ }
}

int ReadConfigValue(const char* filePath, const char* key) {
    FILE* f = fopen(filePath, "r");
    if (!f) return -1;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, key)) {
            char* start = strchr(line, '[');
            char* end = strchr(line, ']');
            if (start && end && end > start) {
                fclose(f);
                return atoi(start + 1);
            }
        }
    }
    fclose(f);
    return -1;
}

// ============================================================
//  Keyboard hook
// ============================================================
// Somehow this empty keyboard proc effects the compiled file and allows RAGELOGO to play during the human campaign...
// WARNING: This empty function must remain in the compiled binary.
// Its presence changes the DLL's compiled size and memory layout,
// which alters thread scheduling timing enough to prevent a race condition
// Removing this function causes RAGELOGO.mp4 to be skipped on human launch.
// The root cause is a timing race that has not been fully isolated.
// Do not remove this function. Do not move it. Do not add code to it.
// See: https://en.wikipedia.org/wiki/Heisenbug

HHOOK kbHook = NULL;

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    return CallNextHookEx(kbHook, nCode, wParam, lParam);
}

// ============================================================
//  DllMain
// ============================================================

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        if (debug) DeleteFileA("Smackw32_log.txt");
        // detect game, setup window string and other stuff
        if (GetFileAttributesA("Expendable.exe") != INVALID_FILE_ATTRIBUTES)
        {
            // GoG version
            window = "go";
            regWidth = ReadConfigValue("config.txt", "DisplayScreenXres");
            regHeight = ReadConfigValue("config.txt", "DisplayScreenYres");
            //DisplayScreenXres   [1920]
            //DisplayScreenYres   [1080]
            Log("Expendable detected. Initializing SmackIT hooks.");
        }
        else
        {
            Log("No known game executable found. Running in passthrough mode.");
        }



        // Read cnc-ddraw target resolution from ddraw.ini
        if (GetFileAttributesA("ddraw.dll") != INVALID_FILE_ATTRIBUTES) // only parse if ddraw is enabled
        {
            if (regWidth > 1920 || regHeight > 1080) // only parse .ini if resolution is being scaled
            {
                char iniPath[MAX_PATH];
                GetModuleFileNameA(NULL, iniPath, MAX_PATH);
                char* lastSlash = strrchr(iniPath, '\\');
                if (lastSlash) *(lastSlash + 1) = '\0';
                strcat(iniPath, "ddraw.ini");
                regWidth = GetPrivateProfileIntA("ddraw", "width", 0, iniPath);     // update regWidth
                regHeight = GetPrivateProfileIntA("ddraw", "height", 0, iniPath);   // update regHeight
            }
        }

        // Calculate the Aspect Ratio of the screen
        double screenAspect = (double)regWidth / regHeight;
        // Use source video aspect ratio
        double videoAspect = 1920.0 / 1080.0; // 1.777...

        if (screenAspect < videoAspect) {
            double targetVideoHeight = (regWidth * 1080.0) / 1920.0;
            offsetY = (int)((regHeight - targetVideoHeight) / 2.0);
        }

        MFStartup(MF_VERSION);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        CloseOverlayWindow();
        if (pMediaPlayer) {
            pMediaPlayer->Shutdown();
            pMediaPlayer->Release();
            pMediaPlayer = NULL;
        }
        MFShutdown();
    }
    return TRUE;
}
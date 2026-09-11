#include <Windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <wrl.h>
#include <WebView2.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

// This minimal ABI mirrors the Nexus API v6 field order through Quick Access.
// Unused fields remain pointer-sized placeholders so the functions we call
// have the same offsets as the official Nexus API definition.
using NexusOpaqueFn = void (*)();
using NexusKeybindHandlerFn = void (*)(const char*, bool);
using NexusRegisterKeybindFn = void (*)(
    const char*, NexusKeybindHandlerFn, const char*);
using NexusDeregisterKeybindFn = void (*)(const char*);
using NexusQuickAccessAddFn = void (*)(
    const char*, const char*, const char*, const char*, const char*);
using NexusQuickAccessRemoveFn = void (*)(const char*);
struct NexusTexture_t
{
    uint32_t Width;
    uint32_t Height;
    void* Resource;
};
using NexusTextureGetOrCreateFromFileFn = NexusTexture_t* (*)(
    const char*, const char*);

struct AddonAPI_t
{
    void* SwapChain;
    void* ImguiContext;
    void* ImguiMalloc;
    void* ImguiFree;
    NexusOpaqueFn GUI_Register;
    NexusOpaqueFn GUI_Deregister;
    NexusOpaqueFn RequestUpdate;
    NexusOpaqueFn Log;
    NexusOpaqueFn GUI_SendAlert;
    NexusOpaqueFn GUI_RegisterCloseOnEscape;
    NexusOpaqueFn GUI_DeregisterCloseOnEscape;
    NexusOpaqueFn Paths_GetGameDirectory;
    NexusOpaqueFn Paths_GetAddonDirectory;
    NexusOpaqueFn Paths_GetCommonDirectory;
    NexusOpaqueFn MinHook_Create;
    NexusOpaqueFn MinHook_Remove;
    NexusOpaqueFn MinHook_Enable;
    NexusOpaqueFn MinHook_Disable;
    NexusOpaqueFn Events_Raise;
    NexusOpaqueFn Events_RaiseNotification;
    NexusOpaqueFn Events_RaiseTargeted;
    NexusOpaqueFn Events_RaiseNotificationTargeted;
    NexusOpaqueFn Events_Subscribe;
    NexusOpaqueFn Events_Unsubscribe;
    NexusOpaqueFn WndProc_Register;
    NexusOpaqueFn WndProc_Deregister;
    NexusOpaqueFn WndProc_SendToGameOnly;
    NexusOpaqueFn InputBinds_Invoke;
    NexusRegisterKeybindFn InputBinds_RegisterWithString;
    NexusOpaqueFn InputBinds_RegisterWithStruct;
    NexusDeregisterKeybindFn InputBinds_Deregister;
    NexusOpaqueFn GameBinds_PressAsync;
    NexusOpaqueFn GameBinds_ReleaseAsync;
    NexusOpaqueFn GameBinds_InvokeAsync;
    NexusOpaqueFn GameBinds_Press;
    NexusOpaqueFn GameBinds_Release;
    NexusOpaqueFn GameBinds_IsBound;
    NexusOpaqueFn DataLink_Get;
    NexusOpaqueFn DataLink_Share;
    NexusOpaqueFn Textures_Get;
    NexusTextureGetOrCreateFromFileFn Textures_GetOrCreateFromFile;
    NexusOpaqueFn Textures_GetOrCreateFromResource;
    NexusOpaqueFn Textures_GetOrCreateFromURL;
    NexusOpaqueFn Textures_GetOrCreateFromMemory;
    NexusOpaqueFn Textures_LoadFromFile;
    NexusOpaqueFn Textures_LoadFromResource;
    NexusOpaqueFn Textures_LoadFromURL;
    NexusOpaqueFn Textures_LoadFromMemory;
    NexusQuickAccessAddFn QuickAccess_Add;
    NexusQuickAccessRemoveFn QuickAccess_Remove;
    NexusOpaqueFn QuickAccess_Notify;
    NexusOpaqueFn QuickAccess_AddContextMenu;
    NexusOpaqueFn QuickAccess_RemoveContextMenu;
};

static_assert(
    offsetof(AddonAPI_t, QuickAccess_Add) == sizeof(void*) * 48,
    "Nexus API field order changed before QuickAccess_Add");

using AddonLoadFn = void (*)(AddonAPI_t*);
using AddonUnloadFn = void (*)();

enum EAddonFlags
{
    AF_None = 0,
};

enum EUpdateProvider
{
    UP_None = 0,
};

struct AddonVersion_t
{
    uint16_t Major;
    uint16_t Minor;
    uint16_t Build;
    uint16_t Revision;
};

struct AddonDefinition_t
{
    uint32_t Signature;
    uint32_t APIVersion;
    const char* Name;
    AddonVersion_t Version;
    const char* Author;
    const char* Description;
    AddonLoadFn Load;
    AddonUnloadFn Unload;
    EAddonFlags Flags;
    EUpdateProvider Provider;
    const char* UpdateLink;
};

namespace
{
constexpr UINT kShutdownMessage = WM_APP + 1;
constexpr wchar_t kWindowClassName[] = L"YaoZhuGuideWindowClass";
constexpr wchar_t kAddonFileBaseName[] = L"YaoZhuGuide";
constexpr wchar_t kAddonIconFileName[] = L"YaoZhuGuideIcon.png";
constexpr wchar_t kAddonDisplayName[] = L"小夭竺宝典";
constexpr wchar_t kDefaultUrl[] = L"https://v2.gw2.org.cn/guides/bilibili-1846648930-c-8136102";
constexpr int kDefaultWindowWidth = 1000;
constexpr int kDefaultWindowHeight = 640;
// These offsets reproduce the placement in the supplied 1920x1080 screenshot.
constexpr int kDefaultWindowOffsetX = 48;
constexpr int kDefaultWindowOffsetY = -86;
constexpr int kTitleBarHeight = 36;
constexpr int kOpacityControlHeight = 24;
constexpr int kOpacitySliderControlId = 1001;
constexpr int kOpacitySliderWidth = 180;
constexpr int kMinimizeButtonId = 1002;
constexpr int kMaximizeButtonId = 1003;
constexpr int kCloseButtonId = 1004;
constexpr int kTitleTextWidth = 104;
constexpr int kTitleButtonWidth = 34;
constexpr int kTitleButtonCount = 3;
constexpr int kMinOpacityPercent = 30;
constexpr int kMaxOpacityPercent = 100;
constexpr int kDefaultOpacityPercent = 100;
constexpr uint32_t kNexusApiVersion = 6;
constexpr char kQuickAccessIdentifier[] = "QAS_YAOZHUGUIDE";
constexpr char kKeybindIdentifier[] = "KB_YAOZHUGUIDE_TOGGLE";
constexpr char kNexusTextureIdentifier[] = "YAOZHUGUIDE_ICON";
constexpr char kNexusIconIdentifier[] = "ICON_NEXUS";

HMODULE g_module = nullptr;
AddonAPI_t* g_nexusApi = nullptr;
std::atomic_bool g_shutdownRequested = false;
std::atomic_bool g_addonUnloadRequested = false;
std::atomic_bool g_initializationComplete = false;
std::atomic_int g_opacityPercent = kDefaultOpacityPercent;
std::atomic<HWND> g_browserWindow = nullptr;
std::atomic<DWORD> g_browserThreadId = 0;
std::thread g_browserThread;
std::mutex g_browserThreadMutex;
std::atomic_bool g_browserThreadRunning = false;

HWND g_opacityLabel = nullptr;
HWND g_opacitySlider = nullptr;
HWND g_opacityValueLabel = nullptr;
HWND g_minimizeButton = nullptr;
HWND g_maximizeButton = nullptr;
HWND g_closeButton = nullptr;
std::wstring g_windowStatus = kAddonDisplayName;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webView;
EventRegistrationToken g_newWindowToken{};
bool g_newWindowHandlerRegistered = false;

// The Quick Access callback can arrive while the old STA thread is returning
// from a manual close. This guard gives the restart path one reliable state
// bit for every success and early-error exit without duplicating cleanup code.
struct BrowserThreadRunningGuard
{
    BrowserThreadRunningGuard()
    {
        g_browserThreadRunning.store(true);
    }

    ~BrowserThreadRunningGuard()
    {
        g_browserThreadRunning.store(false);
    }
};

std::wstring GetModuleDirectory()
{
    if (!g_module)
    {
        return {};
    }

    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(g_module, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath))
    {
        return {};
    }

    std::wstring directory(modulePath, length);
    const std::wstring::size_type slash = directory.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
    {
        return {};
    }

    directory.resize(slash + 1);
    return directory;
}

/*
 * Nexus texture paths use its narrow UTF-8 API while Win32 module paths are
 * wide strings. Convert at this boundary so the bundled icon also works when
 * the addon is installed under a non-ASCII directory; invalid input fails
 * closed and lets the caller select its documented fallback texture.
 */
std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0)
    {
        return {};
    }

    std::string result(static_cast<size_t>(required), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(value.size()),
        result.data(),
        required,
        nullptr,
        nullptr);
    if (written != required)
    {
        return {};
    }

    return result;
}

std::wstring GetConfigPath()
{
    return GetModuleDirectory() + kAddonFileBaseName + std::wstring(L".ini");
}

int ClampOpacityPercent(int value)
{
    return std::clamp(value, kMinOpacityPercent, kMaxOpacityPercent);
}

/*
 * Read and validate the user-controlled opacity at the configuration boundary.
 * Invalid text falls back to full opacity, while valid out-of-range values are
 * clamped so a malformed INI cannot make the window unusably faint.
 */
int ReadConfiguredOpacity()
{
    wchar_t value[32]{};
    const std::wstring configPath = GetConfigPath();
    GetPrivateProfileStringW(
        L"Browser",
        L"Opacity",
        L"100",
        value,
        ARRAYSIZE(value),
        configPath.c_str());

    const std::wstring text(value);
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(text.c_str(), &end, 10);
    if (end == text.c_str())
    {
        return kDefaultOpacityPercent;
    }

    while (*end == L' ' || *end == L'\t')
    {
        ++end;
    }

    if (*end != L'\0')
    {
        return kDefaultOpacityPercent;
    }

    if (parsed < kMinOpacityPercent)
    {
        return kMinOpacityPercent;
    }
    if (parsed > kMaxOpacityPercent)
    {
        return kMaxOpacityPercent;
    }
    return static_cast<int>(parsed);
}

/*
 * Persist only committed slider changes and window shutdown, avoiding an INI
 * write for every intermediate thumb position while the user is dragging.
 */
void PersistConfiguredOpacity()
{
    const std::wstring configPath = GetConfigPath();
    if (configPath.empty())
    {
        return;
    }

    const std::wstring value = std::to_wstring(g_opacityPercent.load());
    if (!WritePrivateProfileStringW(
            L"Browser",
            L"Opacity",
            value.c_str(),
            configPath.c_str()))
    {
        OutputDebugStringW(L"小夭竺宝典 - opacity configuration write failed\n");
    }
}

void UpdateOpacityValueLabel(int opacityPercent)
{
    if (g_opacityValueLabel)
    {
        const std::wstring value = std::to_wstring(opacityPercent) + L"%";
        SetWindowTextW(g_opacityValueLabel, value.c_str());
    }
}

/*
 * Paint only the custom non-client replacement strip. The title text lives in
 * the parent so empty title-bar space remains draggable instead of becoming a
 * separate child row above the page.
 */
void PaintTitleBar(HWND window, HDC deviceContext)
{
    RECT clientBounds{};
    if (!GetClientRect(window, &clientBounds))
    {
        return;
    }

    RECT titleBounds{
        0,
        0,
        clientBounds.right,
        std::min<LONG>(
            static_cast<LONG>(kTitleBarHeight),
            clientBounds.bottom),
    };
    FillRect(deviceContext, &titleBounds, GetSysColorBrush(COLOR_BTNFACE));

    SetBkMode(deviceContext, TRANSPARENT);
    SetTextColor(deviceContext, GetSysColor(COLOR_BTNTEXT));
    RECT titleTextBounds{
        6,
        0,
        6 + kTitleTextWidth,
        kTitleBarHeight,
    };
    DrawTextW(
        deviceContext,
        g_windowStatus.c_str(),
        -1,
        &titleTextBounds,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

/*
 * Draw flat caption glyphs inside owner-draw buttons. Native themed buttons
 * add a raised frame that clashes with the custom title bar, while owner-draw
 * keeps keyboard activation and button hit testing without that extra chrome.
 */
void DrawTitleButton(const DRAWITEMSTRUCT& item)
{
    if (!item.hDC)
    {
        return;
    }

    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const COLORREF backgroundColor =
        pressed ? RGB(220, 220, 220) : GetSysColor(COLOR_BTNFACE);
    const COLORREF glyphColor =
        (item.CtlID == kCloseButtonId && pressed)
            ? RGB(255, 255, 255)
            : GetSysColor(COLOR_BTNTEXT);

    HBRUSH background = CreateSolidBrush(backgroundColor);
    if (background)
    {
        FillRect(item.hDC, &item.rcItem, background);
        DeleteObject(background);
    }

    HPEN glyphPen = CreatePen(PS_SOLID, 1, glyphColor);
    if (!glyphPen)
    {
        return;
    }

    const int centerX = (item.rcItem.left + item.rcItem.right) / 2;
    const int centerY = (item.rcItem.top + item.rcItem.bottom) / 2
        + (pressed ? 1 : 0);
    const int halfGlyph = 6;
    HGDIOBJ previousPen = SelectObject(item.hDC, glyphPen);

    switch (item.CtlID)
    {
    case kMinimizeButtonId:
        MoveToEx(item.hDC, centerX - halfGlyph, centerY, nullptr);
        LineTo(item.hDC, centerX + halfGlyph, centerY);
        break;
    case kMaximizeButtonId:
        Rectangle(
            item.hDC,
            centerX - halfGlyph,
            centerY - halfGlyph,
            centerX + halfGlyph + 1,
            centerY + halfGlyph + 1);
        break;
    case kCloseButtonId:
        MoveToEx(
            item.hDC,
            centerX - halfGlyph,
            centerY - halfGlyph,
            nullptr);
        LineTo(
            item.hDC,
            centerX + halfGlyph,
            centerY + halfGlyph);
        MoveToEx(
            item.hDC,
            centerX + halfGlyph,
            centerY - halfGlyph,
            nullptr);
        LineTo(
            item.hDC,
            centerX - halfGlyph,
            centerY + halfGlyph);
        break;
    default:
        break;
    }

    SelectObject(item.hDC, previousPen);
    DeleteObject(glyphPen);
}

/*
 * Apply alpha to the plugin-owned top-level window. The slider never enables
 * click-through, so even a translucent window remains fully interactive.
 */
void ApplyWindowOpacity(HWND window, int opacityPercent)
{
    if (!window)
    {
        return;
    }

    const int clampedOpacity = ClampOpacityPercent(opacityPercent);
    g_opacityPercent.store(clampedOpacity);
    const BYTE alpha = static_cast<BYTE>((clampedOpacity * 255 + 50) / 100);
    if (!SetLayeredWindowAttributes(window, 0, alpha, LWA_ALPHA))
    {
        OutputDebugStringW(L"小夭竺宝典 - window opacity update failed\n");
    }
    UpdateOpacityValueLabel(clampedOpacity);
}

/*
 * Keep the native toolbar above the WebView2 child window and reserve its
 * height from the browser bounds on every resize, including initial creation.
 */
void LayoutWindow(HWND window)
{
    RECT clientBounds{};
    if (!GetClientRect(window, &clientBounds))
    {
        return;
    }

    const int clientWidth = static_cast<int>(
        std::max(0L, clientBounds.right - clientBounds.left));
    constexpr int margin = 6;
    constexpr int labelWidth = 52;
    constexpr int valueWidth = 48;
    const int labelX = margin + kTitleTextWidth;
    const int sliderX = labelX + labelWidth + margin;
    const int buttonAreaWidth = kTitleButtonWidth * kTitleButtonCount;
    const int availableSliderWidth = std::max(
        80,
        clientWidth
            - sliderX
            - valueWidth
            - buttonAreaWidth
            - (margin * 3));
    const int sliderWidth = std::min(kOpacitySliderWidth, availableSliderWidth);
    const int valueX = sliderX + sliderWidth + margin;
    const int controlHeight = kOpacityControlHeight;

    if (g_opacityLabel)
    {
        SetWindowPos(
            g_opacityLabel,
            HWND_TOP,
            labelX,
            margin,
            labelWidth,
            controlHeight,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (g_opacitySlider)
    {
        SetWindowPos(
            g_opacitySlider,
            HWND_TOP,
            sliderX,
            margin,
            sliderWidth,
            controlHeight,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (g_opacityValueLabel)
    {
        SetWindowPos(
            g_opacityValueLabel,
            HWND_TOP,
            valueX,
            margin,
            valueWidth,
            controlHeight,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    if (g_minimizeButton)
    {
        SetWindowPos(
            g_minimizeButton,
            HWND_TOP,
            clientWidth - (kTitleButtonWidth * 3),
            0,
            kTitleButtonWidth,
            kTitleBarHeight,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (g_maximizeButton)
    {
        SetWindowPos(
            g_maximizeButton,
            HWND_TOP,
            clientWidth - (kTitleButtonWidth * 2),
            0,
            kTitleButtonWidth,
            kTitleBarHeight,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (g_closeButton)
    {
        SetWindowPos(
            g_closeButton,
            HWND_TOP,
            clientWidth - kTitleButtonWidth,
            0,
            kTitleButtonWidth,
            kTitleBarHeight,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    if (g_controller)
    {
        RECT webViewBounds = clientBounds;
        webViewBounds.top = std::min<LONG>(
            static_cast<LONG>(kTitleBarHeight),
            webViewBounds.bottom);
        g_controller->put_Bounds(webViewBounds);
    }
}

/*
 * Build the toolbar as native controls instead of injecting page markup. This
 * keeps opacity available when navigation fails and prevents the loaded site
 * from changing the control's behavior or appearance.
 */
bool InitializeOpacityControls(HWND window, int opacityPercent)
{
    INITCOMMONCONTROLSEX commonControls{
        sizeof(INITCOMMONCONTROLSEX),
        ICC_BAR_CLASSES,
    };
    if (!InitCommonControlsEx(&commonControls))
    {
        return false;
    }

    g_opacityLabel = CreateWindowExW(
        0,
        L"STATIC",
        L"透明度",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0,
        0,
        0,
        0,
        window,
        nullptr,
        g_module,
        nullptr);

    g_opacitySlider = CreateWindowExW(
        0,
        TRACKBAR_CLASSW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
        0,
        0,
        0,
        0,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpacitySliderControlId)),
        g_module,
        nullptr);

    g_opacityValueLabel = CreateWindowExW(
        0,
        L"STATIC",
        L"100%",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_RIGHT,
        0,
        0,
        0,
        0,
        window,
        nullptr,
        g_module,
        nullptr);

    g_minimizeButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMinimizeButtonId)),
        g_module,
        nullptr);

    g_maximizeButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMaximizeButtonId)),
        g_module,
        nullptr);

    g_closeButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseButtonId)),
        g_module,
        nullptr);

    // Roll back partial creation so resize messages never target stale handles.
    if (!g_opacityLabel
        || !g_opacitySlider
        || !g_opacityValueLabel
        || !g_minimizeButton
        || !g_maximizeButton
        || !g_closeButton)
    {
        if (g_closeButton)
        {
            DestroyWindow(g_closeButton);
            g_closeButton = nullptr;
        }
        if (g_maximizeButton)
        {
            DestroyWindow(g_maximizeButton);
            g_maximizeButton = nullptr;
        }
        if (g_minimizeButton)
        {
            DestroyWindow(g_minimizeButton);
            g_minimizeButton = nullptr;
        }
        if (g_opacityValueLabel)
        {
            DestroyWindow(g_opacityValueLabel);
            g_opacityValueLabel = nullptr;
        }
        if (g_opacitySlider)
        {
            DestroyWindow(g_opacitySlider);
            g_opacitySlider = nullptr;
        }
        if (g_opacityLabel)
        {
            DestroyWindow(g_opacityLabel);
            g_opacityLabel = nullptr;
        }
        return false;
    }

    SendMessageW(
        g_opacitySlider,
        TBM_SETRANGE,
        TRUE,
        MAKELPARAM(kMinOpacityPercent, kMaxOpacityPercent));
    SendMessageW(g_opacitySlider, TBM_SETTICFREQ, 10, 0);
    SendMessageW(g_opacitySlider, TBM_SETPOS, TRUE, opacityPercent);
    UpdateOpacityValueLabel(opacityPercent);
    return true;
}

std::wstring GetWebView2UserDataPath()
{
    PWSTR localAppDataPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_DEFAULT,
            nullptr,
            &localAppDataPath))
        || !localAppDataPath)
    {
        return {};
    }

    const std::wstring addonDirectory =
        std::wstring(localAppDataPath) + L"\\" + kAddonFileBaseName;
    CoTaskMemFree(localAppDataPath);

    if (!CreateDirectoryW(addonDirectory.c_str(), nullptr)
        && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        return {};
    }

    const std::wstring userDataDirectory = addonDirectory + L"\\WebView2";
    if (!CreateDirectoryW(userDataDirectory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        return {};
    }

    return userDataDirectory;
}

RECT GetDefaultWindowBounds()
{
    RECT monitorBounds{
        0,
        0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
    };

    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    if (const HMONITOR monitor = MonitorFromPoint(
            POINT{0, 0},
            MONITOR_DEFAULTTOPRIMARY);
        monitor && GetMonitorInfoW(monitor, &monitorInfo))
    {
        monitorBounds = monitorInfo.rcMonitor;
    }

    const LONG monitorWidth = monitorBounds.right - monitorBounds.left;
    const LONG monitorHeight = monitorBounds.bottom - monitorBounds.top;
    const LONG centeredX = monitorBounds.left
        + (monitorWidth - kDefaultWindowWidth) / 2
        + kDefaultWindowOffsetX;
    const LONG centeredY = monitorBounds.top
        + (monitorHeight - kDefaultWindowHeight) / 2
        + kDefaultWindowOffsetY;
    const LONG maxX = std::max<LONG>(
        monitorBounds.left,
        monitorBounds.right - kDefaultWindowWidth);
    const LONG maxY = std::max<LONG>(
        monitorBounds.top,
        monitorBounds.bottom - kDefaultWindowHeight);

    const LONG left = std::clamp(centeredX, monitorBounds.left, maxX);
    const LONG top = std::clamp(centeredY, monitorBounds.top, maxY);
    return RECT{
        left,
        top,
        left + kDefaultWindowWidth,
        top + kDefaultWindowHeight,
    };
}

bool IsHttpUrl(const std::wstring& value)
{
    return _wcsnicmp(value.c_str(), L"http://", 7) == 0
        || _wcsnicmp(value.c_str(), L"https://", 8) == 0;
}

std::wstring ReadConfiguredUrl()
{
    wchar_t value[2048]{};
    const std::wstring configPath = GetConfigPath();
    GetPrivateProfileStringW(
        L"Browser",
        L"Url",
        kDefaultUrl,
        value,
        ARRAYSIZE(value),
        configPath.c_str());

    const std::wstring configuredUrl(value);
    return IsHttpUrl(configuredUrl) ? configuredUrl : kDefaultUrl;
}

void SetBrowserStatus(HWND window, const wchar_t* status)
{
    if (window)
    {
        g_windowStatus = status;
        SetWindowTextW(window, status);
        InvalidateRect(window, nullptr, FALSE);
    }

    OutputDebugStringW(status);
    OutputDebugStringW(L"\n");
}

void StartBrowserThread();

// Reuse a live window only when it is still in the current browsing session.
// A manual close destroys that session, so the fallback starts a fresh STA
// thread; this is what stops media playback and navigates the configured URL
// again instead of merely unhiding stale WebView2 state.
void ShowBrowserWindowFromQuickAccess()
{
    if (g_addonUnloadRequested.load())
    {
        return;
    }

    HWND window = g_browserWindow.load();
    if (!window || !IsWindow(window) || g_shutdownRequested.load())
    {
        StartBrowserThread();
        return;
    }

    ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
    SetWindowPos(
        window,
        HWND_TOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(window);
    SetForegroundWindow(window);
}

void HandleQuickAccessKeybind(const char* identifier, bool isRelease)
{
    if (isRelease || !identifier
        || std::strcmp(identifier, kKeybindIdentifier) != 0)
    {
        return;
    }

    ShowBrowserWindowFromQuickAccess();
}

void RegisterQuickAccess()
{
    if (!g_nexusApi)
    {
        return;
    }

    if (!g_nexusApi->InputBinds_RegisterWithString
        || !g_nexusApi->InputBinds_Deregister
        || !g_nexusApi->QuickAccess_Add
        || !g_nexusApi->QuickAccess_Remove)
    {
        OutputDebugStringW(L"小夭竺宝典 - Nexus Quick Access API unavailable\n");
        return;
    }

    const std::string iconPath = WideToUtf8(
        GetModuleDirectory() + kAddonIconFileName);
    const char* textureIdentifier = kNexusIconIdentifier;
    if (g_nexusApi->Textures_GetOrCreateFromFile && !iconPath.empty()
        && g_nexusApi->Textures_GetOrCreateFromFile(
               kNexusTextureIdentifier,
               iconPath.c_str()))
    {
        textureIdentifier = kNexusTextureIdentifier;
    }
    else
    {
        // Keep the addon reachable if an installation omitted the optional
        // PNG or an older Nexus build cannot load file textures; normal
        // packaging uses the custom icon path above.
        OutputDebugStringW(
            L"小夭竺宝典 - custom Quick Access icon unavailable; using Nexus fallback\n");
    }

    // Quick Access invokes the registered keybind when the shortcut is
    // clicked. "(null)" deliberately leaves the action without a default
    // keyboard binding while preserving icon activation.
    g_nexusApi->InputBinds_RegisterWithString(
        kKeybindIdentifier,
        HandleQuickAccessKeybind,
        "(null)");
    g_nexusApi->QuickAccess_Add(
        kQuickAccessIdentifier,
        textureIdentifier,
        textureIdentifier,
        kKeybindIdentifier,
        "小夭竺宝典");
}

void UnregisterQuickAccess()
{
    if (!g_nexusApi)
    {
        return;
    }

    if (g_nexusApi->QuickAccess_Remove)
    {
        g_nexusApi->QuickAccess_Remove(kQuickAccessIdentifier);
    }
    if (g_nexusApi->InputBinds_Deregister)
    {
        g_nexusApi->InputBinds_Deregister(kKeybindIdentifier);
    }
}

void FinishInitialization()
{
    g_initializationComplete.store(true);

    if (g_shutdownRequested.load())
    {
        const DWORD threadId = g_browserThreadId.load();
        if (threadId != 0)
        {
            PostThreadMessageW(threadId, kShutdownMessage, 0, 0);
        }
    }
}

HRESULT HandleNewWindowRequested(
    ICoreWebView2*,
    ICoreWebView2NewWindowRequestedEventArgs* arguments)
{
    // Mark the request handled so WebView2 does not create a child window; the
    // explicit ShellExecuteW call below delegates it to Windows' association.
    arguments->put_Handled(TRUE);

    LPWSTR uri = nullptr;
    if (FAILED(arguments->get_Uri(&uri)) || !uri || !*uri)
    {
        // A missing URI cannot be delegated; release any returned allocation
        // and leave the current embedded page unchanged.
        if (uri)
        {
            CoTaskMemFree(uri);
        }
        return S_OK;
    }

    const HINSTANCE launchResult = ShellExecuteW(
        nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(launchResult) <= 32)
    {
        // ShellExecuteW reports launch failures through values <= 32 rather
        // than HRESULTs; keep that failure visible without changing the page.
        SetBrowserStatus(g_browserWindow.load(), L"小夭竺宝典 - 无法打开默认浏览器");
    }

    // WebView2 allocates the URI returned by get_Uri with the task allocator.
    if (uri)
    {
        CoTaskMemFree(uri);
    }

    return S_OK;
}

void CloseWebView(HWND window)
{
    if (g_webView && g_newWindowHandlerRegistered)
    {
        g_webView->remove_NewWindowRequested(g_newWindowToken);
        g_newWindowHandlerRegistered = false;
    }

    g_webView.Reset();

    if (g_controller)
    {
        g_controller->Close();
        g_controller.Reset();
    }

    if (window && IsWindow(window))
    {
        DestroyWindow(window);
    }
}

LRESULT CALLBACK BrowserWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        const HDC deviceContext = BeginPaint(window, &paint);
        PaintTitleBar(window, deviceContext);
        EndPaint(window, &paint);
        return 0;
    }

    case WM_NCHITTEST:
    {
        const LRESULT defaultHit = DefWindowProcW(
            window,
            message,
            wParam,
            lParam);
        if (defaultHit == HTCLIENT)
        {
            POINT point{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam),
            };
            ScreenToClient(window, &point);
            // Empty space in the custom title bar behaves like a native caption.
            if (point.y >= 0 && point.y < kTitleBarHeight)
            {
                return HTCAPTION;
            }
        }
        return defaultHit;
    }

    case WM_SIZE:
        LayoutWindow(window);
        return 0;

    case WM_GETMINMAXINFO:
        if (auto* limits = reinterpret_cast<LPMINMAXINFO>(lParam))
        {
            // Preserve enough room for title controls and a usable WebView2 area.
            limits->ptMinTrackSize.x = 480;
            limits->ptMinTrackSize.y = 240;
        }
        return 0;

    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == g_opacitySlider)
        {
            // TBM_GETPOS is authoritative for both mouse dragging and keyboard steps.
            const int opacityPercent = static_cast<int>(
                SendMessageW(g_opacitySlider, TBM_GETPOS, 0, 0));
            ApplyWindowOpacity(window, opacityPercent);
            if (LOWORD(wParam) == TB_ENDTRACK)
            {
                PersistConfiguredOpacity();
            }
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);

    case WM_DRAWITEM:
        if (auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam))
        {
            if (item->CtlID == kMinimizeButtonId
                || item->CtlID == kMaximizeButtonId
                || item->CtlID == kCloseButtonId)
            {
                DrawTitleButton(*item);
                return TRUE;
            }
        }
        return FALSE;

    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED)
        {
            switch (LOWORD(wParam))
            {
            case kMinimizeButtonId:
                ShowWindow(window, SW_MINIMIZE);
                return 0;
            case kMaximizeButtonId:
                ShowWindow(
                    window,
                    IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
                return 0;
            case kCloseButtonId:
                PostMessageW(window, WM_CLOSE, 0, 0);
                return 0;
            default:
                break;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);

    case WM_CLOSE:
        PersistConfiguredOpacity();
        // Manual close and addon unload both end this WebView session. That
        // releases the media surface so video cannot keep playing while the
        // window is gone; Quick Access creates a new session when requested.
        g_shutdownRequested.store(true);
        if (!g_initializationComplete.load())
        {
            // Let an in-flight WebView2 callback finish its COM cleanup before
            // the thread exits. FinishInitialization will post shutdown again.
            ShowWindow(window, SW_HIDE);
            return 0;
        }

        CloseWebView(window);
        return 0;

    case WM_DESTROY:
        g_opacityValueLabel = nullptr;
        g_opacitySlider = nullptr;
        g_opacityLabel = nullptr;
        g_minimizeButton = nullptr;
        g_maximizeButton = nullptr;
        g_closeButton = nullptr;
        g_browserWindow.store(nullptr);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

HRESULT InitializeWebView(HWND window, const std::wstring& url)
{
    const std::wstring userDataPath = GetWebView2UserDataPath();
    if (userDataPath.empty())
    {
        SetBrowserStatus(window, L"小夭竺宝典 - cannot create WebView2 data directory");
        FinishInitialization();
        return E_FAIL;
    }

    return CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userDataPath.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [window, url](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT
            {
                if (FAILED(result) || !environment)
                {
                    SetBrowserStatus(window, L"小夭竺宝典 - WebView2 Runtime is unavailable");
                    FinishInitialization();
                    return S_OK;
                }

                if (g_shutdownRequested.load())
                {
                    FinishInitialization();
                    return S_OK;
                }

                const HRESULT controllerResult = environment->CreateCoreWebView2Controller(
                    window,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [window, url](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT
                        {
                            if (FAILED(result) || !controller)
                            {
                                SetBrowserStatus(window, L"小夭竺宝典 - cannot create embedded browser");
                                FinishInitialization();
                                return S_OK;
                            }

                            if (g_shutdownRequested.load())
                            {
                                controller->Close();
                                FinishInitialization();
                                return S_OK;
                            }

                            g_controller = controller;
                            HRESULT hr = g_controller->get_CoreWebView2(&g_webView);
                            if (FAILED(hr) || !g_webView)
                            {
                                SetBrowserStatus(window, L"小夭竺宝典 - cannot access WebView2 control");
                                g_controller->Close();
                                g_controller.Reset();
                                FinishInitialization();
                                return S_OK;
                            }

                            g_controller->put_IsVisible(TRUE);
                            // Raise title controls after WebView2 creates its child HWND.
                            LayoutWindow(window);

                            hr = g_webView->add_NewWindowRequested(
                                Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* arguments) -> HRESULT
                                    {
                                        return HandleNewWindowRequested(sender, arguments);
                                    }).Get(),
                                &g_newWindowToken);
                            if (FAILED(hr))
                            {
                                SetBrowserStatus(window, L"小夭竺宝典 - failed to bind navigation handler");
                                g_webView.Reset();
                                g_controller->Close();
                                g_controller.Reset();
                                FinishInitialization();
                                return S_OK;
                            }
                            g_newWindowHandlerRegistered = true;

                            hr = g_webView->Navigate(url.c_str());
                            if (FAILED(hr))
                            {
                                SetBrowserStatus(window, L"小夭竺宝典 - navigation failed");
                            }

                            FinishInitialization();
                            return S_OK;
                        }).Get());

                if (FAILED(controllerResult))
                {
                    SetBrowserStatus(window, L"小夭竺宝典 - controller creation failed");
                    FinishInitialization();
                }
                return S_OK;
            }).Get());
}

void BrowserThreadMain(std::wstring url)
{
    BrowserThreadRunningGuard runningGuard;
    g_browserThreadId.store(GetCurrentThreadId());

    MSG queuedMessage{};
    PeekMessageW(&queuedMessage, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult))
    {
        SetBrowserStatus(nullptr, L"小夭竺宝典 - COM initialization failed");
        g_browserThreadId.store(0);
        return;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = BrowserWindowProc;
    windowClass.hInstance = g_module;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClassName;

    const ATOM registeredClass = RegisterClassExW(&windowClass);
    if (!registeredClass && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        SetBrowserStatus(nullptr, L"小夭竺宝典 - window class registration failed");
        CoUninitialize();
        g_browserThreadId.store(0);
        return;
    }

    const RECT defaultBounds = GetDefaultWindowBounds();
    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_LAYERED,
        kWindowClassName,
        kAddonDisplayName,
        WS_POPUP
            | WS_THICKFRAME
            | WS_MINIMIZEBOX
            | WS_MAXIMIZEBOX
            | WS_SYSMENU
            | WS_CLIPCHILDREN
            | WS_CLIPSIBLINGS,
        defaultBounds.left,
        defaultBounds.top,
        kDefaultWindowWidth,
        kDefaultWindowHeight,
        nullptr,
        nullptr,
        g_module,
        nullptr);

    if (!window)
    {
        SetBrowserStatus(nullptr, L"小夭竺宝典 - window creation failed");
        if (registeredClass)
        {
            UnregisterClassW(kWindowClassName, g_module);
        }
        CoUninitialize();
        g_browserThreadId.store(0);
        return;
    }

    g_browserWindow.store(window);

    // Promote the created HWND into the topmost z-order band; this keeps the
    // plugin above ordinary windows even after focus moves to another app.
    if (!SetWindowPos(
            window,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
    {
        // A z-order failure must not abort the browser, but it is useful in
        // diagnostics because the requested always-on-top invariant was lost.
        OutputDebugStringW(L"小夭竺宝典 - topmost window setup failed\n");
    }

    if (g_shutdownRequested.load())
    {
        g_initializationComplete.store(true);
        DestroyWindow(window);
    }
    else
    {
        const int configuredOpacity = ReadConfiguredOpacity();
        // The browser remains usable if the optional native toolbar cannot load.
        if (!InitializeOpacityControls(window, configuredOpacity))
        {
            OutputDebugStringW(L"小夭竺宝典 - opacity control creation failed\n");
        }
        ApplyWindowOpacity(window, configuredOpacity);
        LayoutWindow(window);
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);

        const HRESULT initializationResult = InitializeWebView(window, url);
        if (FAILED(initializationResult))
        {
            SetBrowserStatus(window, L"小夭竺宝典 - initialization failed");
        }
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (message.message == kShutdownMessage)
        {
            if (HWND currentWindow = g_browserWindow.load())
            {
                PostMessageW(currentWindow, WM_CLOSE, 0, 0);
            }
            continue;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CloseWebView(g_browserWindow.load());
    if (registeredClass)
    {
        UnregisterClassW(kWindowClassName, g_module);
    }

    CoUninitialize();
    g_browserThreadId.store(0);
}

void StartBrowserThread()
{
    std::lock_guard<std::mutex> lock(g_browserThreadMutex);

    if (g_addonUnloadRequested.load())
    {
        return;
    }

    if (HWND window = g_browserWindow.load();
        window && IsWindow(window) && !g_shutdownRequested.load())
    {
        ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
        SetWindowPos(
            window,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        BringWindowToTop(window);
        SetForegroundWindow(window);
        return;
    }

    if (g_browserThread.joinable())
    {
        // A thread that has finished is still joinable. Reclaim it before
        // assigning the replacement; if it is still unwinding, the current
        // click is intentionally ignored and the next click retries safely.
        if (g_browserThreadRunning.load())
        {
            return;
        }
        g_browserThread.join();
    }

    g_shutdownRequested.store(false);
    g_initializationComplete.store(false);
    g_browserThreadRunning.store(true);

    try
    {
        g_browserThread = std::thread(BrowserThreadMain, ReadConfiguredUrl());
    }
    catch (...)
    {
        g_browserThreadRunning.store(false);
        OutputDebugStringW(L"小夭竺宝典 - browser thread creation failed\n");
    }
}

void AddonLoad(AddonAPI_t* api)
{
    g_nexusApi = api;
    g_shutdownRequested.store(false);
    g_addonUnloadRequested.store(false);
    g_initializationComplete.store(false);
    RegisterQuickAccess();
    StartBrowserThread();
}

void AddonUnload()
{
    g_addonUnloadRequested.store(true);
    UnregisterQuickAccess();
    g_nexusApi = nullptr;

    std::lock_guard<std::mutex> lock(g_browserThreadMutex);
    g_shutdownRequested.store(true);

    if (HWND window = g_browserWindow.load())
    {
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    else if (const DWORD threadId = g_browserThreadId.load())
    {
        PostThreadMessageW(threadId, kShutdownMessage, 0, 0);
    }

    if (g_browserThread.joinable())
    {
        g_browserThread.join();
    }
}
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    static AddonDefinition_t definition{
        static_cast<uint32_t>(-20260910),
        kNexusApiVersion,
        "小夭竺宝典",
        {1, 0, 0, 0},
        "协同学院",
        "【激战2的小夭竺】视频攻略大全",
        AddonLoad,
        AddonUnload,
        AF_None,
        UP_None,
        nullptr,
    };

    return &definition;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

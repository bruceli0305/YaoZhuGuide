#include <Windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <dbghelp.h>
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
constexpr int kRuntimeErrorTitleId = 1005;
constexpr int kRuntimeErrorDetailId = 1006;
constexpr int kRuntimeOpenButtonId = 1007;
constexpr int kRuntimeRetryButtonId = 1008;
constexpr int kRuntimeCloseButtonId = 1009;
constexpr int kRuntimeErrorPanelWidth = 760;
constexpr int kRuntimeErrorPanelHeight = 146;
constexpr int kRuntimeErrorTitleHeight = 32;
constexpr int kRuntimeErrorDetailHeight = 56;
constexpr int kRuntimeErrorButtonWidth = 150;
constexpr int kRuntimeErrorButtonHeight = 30;
constexpr int kRuntimeErrorButtonGap = 12;
constexpr int kTitleTextWidth = 104;
constexpr int kTitleButtonWidth = 34;
constexpr int kTitleButtonCount = 3;
constexpr int kMinOpacityPercent = 30;
constexpr int kMaxOpacityPercent = 100;
constexpr int kDefaultOpacityPercent = 100;
constexpr wchar_t kWebView2DownloadUrl[] =
    L"https://developer.microsoft.com/microsoft-edge/webview2/";
constexpr wchar_t kLogFileName[] = L"YaoZhuGuide.log";
constexpr wchar_t kCrashDumpDirectoryName[] = L"CrashDumps";
constexpr std::uint64_t kMaxLogFileBytes = 2u * 1024u * 1024u;
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
std::mutex g_logMutex;
std::atomic_bool g_crashDumpInProgress = false;
LPTOP_LEVEL_EXCEPTION_FILTER g_previousUnhandledExceptionFilter = nullptr;
bool g_crashHandlerInstalled = false;

HWND g_opacityLabel = nullptr;
HWND g_opacitySlider = nullptr;
HWND g_opacityValueLabel = nullptr;
HWND g_minimizeButton = nullptr;
HWND g_maximizeButton = nullptr;
HWND g_closeButton = nullptr;
HWND g_runtimeErrorTitle = nullptr;
HWND g_runtimeErrorDetail = nullptr;
HWND g_runtimeOpenButton = nullptr;
HWND g_runtimeRetryButton = nullptr;
HWND g_runtimeCloseButton = nullptr;
bool g_runtimeErrorVisible = false;
std::wstring g_browserUrl;
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

/*
 * Keep diagnostics outside the addon installation directory. The same
 * per-user directory is shared by the WebView2 profile, the text log, and
 * crash dumps so a read-only or updated addon folder cannot lose diagnostics.
 */
std::wstring GetAddonLocalDataDirectory()
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
    return addonDirectory;
}

std::wstring GetLogPath()
{
    const std::wstring addonDirectory = GetAddonLocalDataDirectory();
    return addonDirectory.empty()
        ? std::wstring{}
        : addonDirectory + L"\\" + kLogFileName;
}

std::wstring GetCrashDumpPath()
{
    const std::wstring addonDirectory = GetAddonLocalDataDirectory();
    if (addonDirectory.empty())
    {
        return {};
    }

    const std::wstring crashDirectory =
        addonDirectory + L"\\" + kCrashDumpDirectoryName;
    if (!CreateDirectoryW(crashDirectory.c_str(), nullptr)
        && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        return {};
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t fileName[128]{};
    _snwprintf_s(
        fileName,
        ARRAYSIZE(fileName),
        _TRUNCATE,
        L"YaoZhuGuide-%04u%02u%02u-%02u%02u%02u-%lu.dmp",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        static_cast<unsigned long>(GetCurrentProcessId()));
    return crashDirectory + L"\\" + fileName;
}

/*
 * Append one UTF-8 line and mirror it to the debugger. The mutex serializes
 * browser-thread and Nexus-callback writes; a small size cap prevents a
 * repeatedly failing addon from consuming unbounded AppData storage.
 */
void LogMessage(const wchar_t* level, const wchar_t* message)
{
    const wchar_t* safeLevel = level ? level : L"INFO";
    const wchar_t* safeMessage = message ? message : L"";
    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t line[2048]{};
    _snwprintf_s(
        line,
        ARRAYSIZE(line),
        _TRUNCATE,
        L"[%04u-%02u-%02u %02u:%02u:%02u.%03u][pid=%lu tid=%lu][%ls] %ls\r\n",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds,
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()),
        safeLevel,
        safeMessage);
    OutputDebugStringW(line);

    try
    {
        const std::string utf8Line = WideToUtf8(line);
        const std::wstring logPath = GetLogPath();
        if (utf8Line.empty() || logPath.empty())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(g_logMutex);
        HANDLE file = CreateFileW(
            logPath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            OutputDebugStringW(L"小夭竺宝典 - 无法打开日志文件\n");
            return;
        }

        LARGE_INTEGER fileSize{};
        if (GetFileSizeEx(file, &fileSize)
            && fileSize.QuadPart > static_cast<LONGLONG>(kMaxLogFileBytes))
        {
            LARGE_INTEGER beginning{};
            if (SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN)
                && SetEndOfFile(file))
            {
                static constexpr char resetLine[] =
                    "--- log truncated after reaching 2 MiB ---\r\n";
                DWORD resetBytes = 0;
                WriteFile(
                    file,
                    resetLine,
                    static_cast<DWORD>(sizeof(resetLine) - 1),
                    &resetBytes,
                    nullptr);
            }
        }

        LARGE_INTEGER end{};
        SetFilePointerEx(file, end, nullptr, FILE_END);
        DWORD written = 0;
        const BOOL writeResult = WriteFile(
            file,
            utf8Line.data(),
            static_cast<DWORD>(utf8Line.size()),
            &written,
            nullptr);
        if (writeResult && written == utf8Line.size())
        {
            // Flush each short diagnostic line so a sudden host crash keeps
            // the latest completed event whenever the filesystem permits it.
            FlushFileBuffers(file);
        }
        CloseHandle(file);
    }
    catch (...)
    {
        // Diagnostics must never turn an allocation or path failure into a
        // second plugin failure; OutputDebugString remains the fallback.
        OutputDebugStringW(L"小夭竺宝典 - 日志写入异常\n");
    }
}

void LogHresult(const wchar_t* operation, HRESULT result)
{
    wchar_t message[256]{};
    _snwprintf_s(
        message,
        ARRAYSIZE(message),
        _TRUNCATE,
        L"%ls failed (HRESULT=0x%08lX)",
        operation ? operation : L"operation",
        static_cast<unsigned long>(result));
    LogMessage(L"ERROR", message);
}

/*
 * Write a best-effort normal minidump from the process-level exception hook.
 * The hook returns the previous filter's result afterward, so Nexus or the
 * game retains its existing crash policy instead of being swallowed here.
 */
void WriteCrashDump(EXCEPTION_POINTERS* exceptionPointers)
{
    if (g_crashDumpInProgress.exchange(true))
    {
        return;
    }

    try
    {
        const std::wstring dumpPath = GetCrashDumpPath();
        if (dumpPath.empty())
        {
            OutputDebugStringW(L"小夭竺宝典 - 无法创建崩溃转储路径\n");
            g_crashDumpInProgress.store(false);
            return;
        }

        HANDLE file = CreateFileW(
            dumpPath.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            OutputDebugStringW(L"小夭竺宝典 - 无法创建崩溃转储文件\n");
            g_crashDumpInProgress.store(false);
            return;
        }

        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
        exceptionInfo.ThreadId = GetCurrentThreadId();
        exceptionInfo.ExceptionPointers = exceptionPointers;
        exceptionInfo.ClientPointers = FALSE;
        const BOOL dumpResult = MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            file,
            MiniDumpNormal,
            exceptionPointers ? &exceptionInfo : nullptr,
            nullptr,
            nullptr);
        CloseHandle(file);
        OutputDebugStringW(
            dumpResult
                ? L"小夭竺宝典 - 崩溃转储已写入\n"
                : L"小夭竺宝典 - 崩溃转储写入失败\n");
        g_crashDumpInProgress.store(false);
    }
    catch (...)
    {
        OutputDebugStringW(L"小夭竺宝典 - 崩溃转储处理异常\n");
        g_crashDumpInProgress.store(false);
    }
}

LONG WINAPI HandleUnhandledException(EXCEPTION_POINTERS* exceptionPointers)
{
    WriteCrashDump(exceptionPointers);
    if (g_previousUnhandledExceptionFilter
        && g_previousUnhandledExceptionFilter != &HandleUnhandledException)
    {
        return g_previousUnhandledExceptionFilter(exceptionPointers);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/*
 * SetUnhandledExceptionFilter is process-wide, so install it only while this
 * addon is loaded and restore the prior callback during unload. It is used
 * solely to capture a dump; it never converts a crash into a handled event.
 */
void InstallCrashHandler()
{
    if (g_crashHandlerInstalled)
    {
        return;
    }
    g_previousUnhandledExceptionFilter =
        SetUnhandledExceptionFilter(&HandleUnhandledException);
    g_crashHandlerInstalled = true;
    LogMessage(L"INFO", L"崩溃转储处理器已安装");
}

void UninstallCrashHandler()
{
    if (!g_crashHandlerInstalled)
    {
        return;
    }
    SetUnhandledExceptionFilter(g_previousUnhandledExceptionFilter);
    g_previousUnhandledExceptionFilter = nullptr;
    g_crashHandlerInstalled = false;
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
        LogMessage(L"ERROR", L"透明度配置写入失败");
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
        LogMessage(L"ERROR", L"窗口透明度更新失败");
    }
    UpdateOpacityValueLabel(clampedOpacity);
}

/*
 * Place the Runtime recovery panel in the same client area that WebView2 uses.
 * It is a native fallback surface, so it remains available even when the
 * browser environment cannot be created and no WebView2 child window exists.
 */
void LayoutRuntimeErrorControls(HWND window)
{
    if (!window)
    {
        return;
    }

    RECT clientBounds{};
    if (!GetClientRect(window, &clientBounds))
    {
        return;
    }

    const int clientWidth = static_cast<int>(
        std::max(0L, clientBounds.right - clientBounds.left));
    const int clientHeight = static_cast<int>(
        std::max(0L, clientBounds.bottom - clientBounds.top));
    const int panelWidth = std::min(
        kRuntimeErrorPanelWidth,
        std::max(240, clientWidth - 32));
    const int panelLeft = std::max(16, (clientWidth - panelWidth) / 2);
    const int panelTop = std::max(
        kTitleBarHeight + 24,
        (clientHeight - kRuntimeErrorPanelHeight) / 2);
    const int buttonGroupWidth = (kRuntimeErrorButtonWidth * 3)
        + (kRuntimeErrorButtonGap * 2);
    const int buttonLeft = panelLeft
        + std::max(0, (panelWidth - buttonGroupWidth) / 2);
    const int buttonTop = panelTop + kRuntimeErrorTitleHeight + 8
        + kRuntimeErrorDetailHeight + 16;
    const UINT visibilityFlags = SWP_NOACTIVATE
        | (g_runtimeErrorVisible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);

    const auto placeControl = [visibilityFlags](
                                  HWND control,
                                  int x,
                                  int y,
                                  int width,
                                  int height) {
        if (control)
        {
            SetWindowPos(
                control,
                HWND_TOP,
                x,
                y,
                width,
                height,
                visibilityFlags);
        }
    };

    placeControl(
        g_runtimeErrorTitle,
        panelLeft,
        panelTop,
        panelWidth,
        kRuntimeErrorTitleHeight);
    placeControl(
        g_runtimeErrorDetail,
        panelLeft,
        panelTop + kRuntimeErrorTitleHeight + 8,
        panelWidth,
        kRuntimeErrorDetailHeight);
    placeControl(
        g_runtimeOpenButton,
        buttonLeft,
        buttonTop,
        kRuntimeErrorButtonWidth,
        kRuntimeErrorButtonHeight);
    placeControl(
        g_runtimeRetryButton,
        buttonLeft + kRuntimeErrorButtonWidth + kRuntimeErrorButtonGap,
        buttonTop,
        kRuntimeErrorButtonWidth,
        kRuntimeErrorButtonHeight);
    placeControl(
        g_runtimeCloseButton,
        buttonLeft + ((kRuntimeErrorButtonWidth + kRuntimeErrorButtonGap) * 2),
        buttonTop,
        kRuntimeErrorButtonWidth,
        kRuntimeErrorButtonHeight);
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

    LayoutRuntimeErrorControls(window);

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

/*
 * Create the Runtime recovery controls as ordinary Win32 children. Keeping
 * this panel outside WebView2 makes the missing-runtime path actionable while
 * still avoiding a bundled Evergreen or Fixed Version runtime.
 */
bool InitializeRuntimeErrorControls(HWND window)
{
    g_runtimeErrorTitle = CreateWindowExW(
        0,
        L"STATIC",
        L"未检测到 Microsoft Edge WebView2 Runtime",
        WS_CHILD | SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX,
        0,
        0,
        0,
        0,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRuntimeErrorTitleId)),
        g_module,
        nullptr);

    g_runtimeErrorDetail = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | SS_CENTER | SS_NOPREFIX,
        0,
        0,
        0,
        0,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRuntimeErrorDetailId)),
        g_module,
        nullptr);

    g_runtimeOpenButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"打开官方下载页面",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRuntimeOpenButtonId)),
        g_module,
        nullptr);

    g_runtimeRetryButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"重新检测",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRuntimeRetryButtonId)),
        g_module,
        nullptr);

    g_runtimeCloseButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"关闭",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRuntimeCloseButtonId)),
        g_module,
        nullptr);

    // Roll back partial creation so a failed fallback setup cannot leave
    // dangling child handles in the resize and command paths.
    if (!g_runtimeErrorTitle
        || !g_runtimeErrorDetail
        || !g_runtimeOpenButton
        || !g_runtimeRetryButton
        || !g_runtimeCloseButton)
    {
        if (g_runtimeCloseButton)
        {
            DestroyWindow(g_runtimeCloseButton);
            g_runtimeCloseButton = nullptr;
        }
        if (g_runtimeRetryButton)
        {
            DestroyWindow(g_runtimeRetryButton);
            g_runtimeRetryButton = nullptr;
        }
        if (g_runtimeOpenButton)
        {
            DestroyWindow(g_runtimeOpenButton);
            g_runtimeOpenButton = nullptr;
        }
        if (g_runtimeErrorDetail)
        {
            DestroyWindow(g_runtimeErrorDetail);
            g_runtimeErrorDetail = nullptr;
        }
        if (g_runtimeErrorTitle)
        {
            DestroyWindow(g_runtimeErrorTitle);
            g_runtimeErrorTitle = nullptr;
        }
        return false;
    }

    g_runtimeErrorVisible = false;
    LayoutRuntimeErrorControls(window);
    return true;
}

std::wstring GetWebView2UserDataPath()
{
    const std::wstring addonDirectory = GetAddonLocalDataDirectory();
    if (addonDirectory.empty())
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

    LogMessage(L"STATUS", status);
}

/*
 * Leave the embedded surface only for an explicit external-navigation or
 * download action. ShellExecuteW uses the user's registered browser and
 * reports launch failures through values at or below 32 rather than HRESULTs.
 */
bool OpenExternalUrl(const wchar_t* url)
{
    if (!url || !*url)
    {
        return false;
    }

    const HINSTANCE launchResult = ShellExecuteW(
        nullptr,
        L"open",
        url,
        nullptr,
        nullptr,
        SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(launchResult) > 32;
}

/*
 * WebView2 exposes this native probe before environment creation. The version
 * string is allocated by the task allocator, so every non-null result is
 * released here; a missing or empty version means the Evergreen Runtime is
 * not available to this process.
 */
bool IsWebView2RuntimeAvailable()
{
    LPWSTR versionInfo = nullptr;
    const HRESULT result = GetAvailableCoreWebView2BrowserVersionString(
        nullptr,
        &versionInfo);
    const bool available = SUCCEEDED(result) && versionInfo && *versionInfo;
    if (versionInfo)
    {
        CoTaskMemFree(versionInfo);
    }
    return available;
}

void HideWebView2RuntimeError(HWND window)
{
    g_runtimeErrorVisible = false;
    if (g_runtimeOpenButton)
    {
        EnableWindow(g_runtimeOpenButton, FALSE);
    }
    if (g_runtimeRetryButton)
    {
        EnableWindow(g_runtimeRetryButton, FALSE);
    }
    if (g_runtimeCloseButton)
    {
        EnableWindow(g_runtimeCloseButton, FALSE);
    }
    LayoutRuntimeErrorControls(window);
}

/*
 * Show a self-contained recovery path for the only dependency that cannot be
 * shipped inside this small addon. The installer is deliberately not started
 * by the plugin; the user chooses the official page and then retries.
 */
void ShowWebView2RuntimeMissing(HWND window)
{
    SetBrowserStatus(window, L"小夭竺宝典 - 未检测到 WebView2 Runtime");
    if (g_runtimeErrorTitle)
    {
        SetWindowTextW(
            g_runtimeErrorTitle,
            L"未检测到 Microsoft Edge WebView2 Runtime");
    }
    if (g_runtimeErrorDetail)
    {
        SetWindowTextW(
            g_runtimeErrorDetail,
            L"当前电脑没有检测到 Microsoft Edge WebView2 Runtime。\r\n"
            L"请安装后点击“重新检测”。插件不会自动下载或执行安装程序。");
    }
    g_runtimeErrorVisible = true;
    if (g_runtimeOpenButton)
    {
        EnableWindow(g_runtimeOpenButton, TRUE);
    }
    if (g_runtimeRetryButton)
    {
        EnableWindow(g_runtimeRetryButton, TRUE);
    }
    if (g_runtimeCloseButton)
    {
        EnableWindow(g_runtimeCloseButton, TRUE);
    }
    LayoutRuntimeErrorControls(window);
    InvalidateRect(window, nullptr, FALSE);
}

void StartBrowserThread();
HRESULT InitializeWebView(HWND window, const std::wstring& url);
void RetryWebView2Initialization(HWND window);

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
        LogMessage(L"ERROR", L"Nexus Quick Access API 不可用");
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
        LogMessage(L"WARN", L"自定义 Quick Access 图标不可用，改用 Nexus 默认图标");
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

    if (!OpenExternalUrl(uri))
    {
        // Keep the embedded page unchanged when the system association cannot
        // launch the requested external page.
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
            case kRuntimeOpenButtonId:
                if (OpenExternalUrl(kWebView2DownloadUrl))
                {
                    SetBrowserStatus(
                        window,
                        L"小夭竺宝典 - 已打开 WebView2 官方下载页面");
                    if (g_runtimeErrorDetail)
                    {
                        SetWindowTextW(
                            g_runtimeErrorDetail,
                            L"安装完成后回到游戏，点击“重新检测”即可继续。\r\n"
                            L"插件不会自动下载或执行安装程序。");
                    }
                }
                else
                {
                    SetBrowserStatus(window, L"小夭竺宝典 - 无法打开官方下载页面");
                    if (g_runtimeErrorDetail)
                    {
                        SetWindowTextW(
                            g_runtimeErrorDetail,
                            L"无法打开系统默认浏览器，请手动访问：\r\n"
                            L"https://developer.microsoft.com/microsoft-edge/webview2/");
                    }
                }
                return 0;
            case kRuntimeRetryButtonId:
                RetryWebView2Initialization(window);
                return 0;
            case kRuntimeCloseButtonId:
                PostMessageW(window, WM_CLOSE, 0, 0);
                return 0;
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
        g_runtimeErrorTitle = nullptr;
        g_runtimeErrorDetail = nullptr;
        g_runtimeOpenButton = nullptr;
        g_runtimeRetryButton = nullptr;
        g_runtimeCloseButton = nullptr;
        g_runtimeErrorVisible = false;
        g_browserWindow.store(nullptr);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

HRESULT InitializeWebView(HWND window, const std::wstring& url)
{
    LogMessage(L"INFO", L"开始初始化 WebView2");
    HideWebView2RuntimeError(window);
    if (!IsWebView2RuntimeAvailable())
    {
        LogMessage(L"ERROR", L"未检测到 WebView2 Runtime");
        ShowWebView2RuntimeMissing(window);
        FinishInitialization();
        // S_FALSE is handled by the caller without replacing the actionable
        // Chinese Runtime panel with a generic initialization error.
        return S_FALSE;
    }

    const std::wstring userDataPath = GetWebView2UserDataPath();
    if (userDataPath.empty())
    {
        LogMessage(L"ERROR", L"无法创建 WebView2 数据目录");
        SetBrowserStatus(window, L"小夭竺宝典 - 无法创建 WebView2 数据目录");
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
                    LogHresult(L"WebView2 环境创建", result);
                    ShowWebView2RuntimeMissing(window);
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
                                LogHresult(L"WebView2 控制器创建", result);
                                SetBrowserStatus(window, L"小夭竺宝典 - 无法创建内嵌浏览器");
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
                                LogHresult(L"获取 WebView2 控件", hr);
                                SetBrowserStatus(window, L"小夭竺宝典 - 无法访问 WebView2 控件");
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
                                LogHresult(L"绑定新窗口处理器", hr);
                                SetBrowserStatus(window, L"小夭竺宝典 - 无法绑定新窗口处理器");
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
                                LogHresult(L"页面导航", hr);
                                SetBrowserStatus(window, L"小夭竺宝典 - 页面导航失败");
                            }

                            FinishInitialization();
                            return S_OK;
                        }).Get());

                if (FAILED(controllerResult))
                {
                    LogHresult(L"请求 WebView2 控制器", controllerResult);
                    SetBrowserStatus(window, L"小夭竺宝典 - 无法创建浏览器控制器");
                    FinishInitialization();
                }
                return S_OK;
            }).Get());
}

/*
 * Retry runs on the WebView2 STA thread after the user has installed the
 * dependency. One guard prevents a double-click from starting overlapping
 * environment callbacks against the same window and COM apartment.
 */
void RetryWebView2Initialization(HWND window)
{
    if (!window
        || g_shutdownRequested.load()
        || !g_initializationComplete.load())
    {
        return;
    }

    g_initializationComplete.store(false);
    SetBrowserStatus(window, L"小夭竺宝典 - 正在重新检测 WebView2 Runtime");
    const HRESULT result = InitializeWebView(window, g_browserUrl);
    if (FAILED(result))
    {
        SetBrowserStatus(window, L"小夭竺宝典 - WebView2 初始化失败");
        FinishInitialization();
    }
}

void BrowserThreadMain(std::wstring url)
{
    BrowserThreadRunningGuard runningGuard;
    g_browserThreadId.store(GetCurrentThreadId());
    g_browserUrl = url;
    LogMessage(L"INFO", L"浏览器线程启动");

    MSG queuedMessage{};
    PeekMessageW(&queuedMessage, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult))
    {
        LogHresult(L"COM 初始化", comResult);
        SetBrowserStatus(nullptr, L"小夭竺宝典 - COM 初始化失败");
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
        SetBrowserStatus(nullptr, L"小夭竺宝典 - 窗口类注册失败");
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
        SetBrowserStatus(nullptr, L"小夭竺宝典 - 窗口创建失败");
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
        LogMessage(L"WARN", L"窗口置顶设置失败");
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
            LogMessage(L"ERROR", L"透明度控件创建失败");
        }
        if (!InitializeRuntimeErrorControls(window))
        {
            LogMessage(L"ERROR", L"Runtime 提示控件创建失败");
        }
        ApplyWindowOpacity(window, configuredOpacity);
        LayoutWindow(window);
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);

        const HRESULT initializationResult = InitializeWebView(window, url);
        if (FAILED(initializationResult))
        {
            SetBrowserStatus(window, L"小夭竺宝典 - WebView2 初始化失败");
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
    LogMessage(L"INFO", L"浏览器线程退出");
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
        LogMessage(L"ERROR", L"浏览器线程创建失败");
    }
}

void AddonLoad(AddonAPI_t* api)
{
    g_nexusApi = api;
    g_shutdownRequested.store(false);
    g_addonUnloadRequested.store(false);
    g_initializationComplete.store(false);
    InstallCrashHandler();
    LogMessage(L"INFO", L"插件加载");
    RegisterQuickAccess();
    StartBrowserThread();
}

void AddonUnload()
{
    g_addonUnloadRequested.store(true);
    LogMessage(L"INFO", L"插件开始卸载");
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
    LogMessage(L"INFO", L"插件卸载完成");
    UninstallCrashHandler();
}
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    static AddonDefinition_t definition{
        static_cast<uint32_t>(-20260910),
        kNexusApiVersion,
        "小夭竺宝典",
        {1, 0, 1, 0},
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

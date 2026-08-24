// MT4_AutoTrading_Emergency_Manager.cpp
//
// Combined emergency manager:
//   1) Finds running MT4 terminals.
//   2) Turns AutoTrading OFF if it can safely identify it as ON.
//   3) Uses an EXTERNAL trade-execution adapter to close positions.
//   4) Waits 5 seconds between successful position-close requests.
//   5) Verifies that the external adapter reports zero open positions.
//
// IMPORTANT:
// MT4 does NOT expose a universal documented retail API that lets an arbitrary
// EXE enumerate/close account positions. Turning AutoTrading OFF also prevents
// normal MQL4 OrderClose() calls. Therefore this program intentionally does
// NOT fake the close operation.
//
// The code includes a compile-ready adapter interface and a DEMO adapter that
// refuses to claim a close. To perform real closes, replace the adapter with
// your broker's authorized external trading API/SDK implementation.
//
// The AutoTrading OFF portion uses the MT4 UI/window command approach.
//
// Build: Visual Studio 2022, C++17, x64.
// Usage:
//   MT4_AutoTrading_Emergency_Manager.exe --once
//   MT4_AutoTrading_Emergency_Manager.exe --watch
//
// Exit code:
//   0 = sequence completed according to the configured adapter.
//   2 = no MT4 terminal found.
//   3 = AutoTrading could not be safely changed.
//   4 = external trade adapter unavailable/failed.

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <functional>
#include <memory>
#include <algorithm>
#include <cstdint>

static constexpr UINT WM_COMMAND_MT4 = 0x0111;

// Common MT4 command used for Expert Advisors/AutoTrading in many builds.
// We never blindly toggle when state cannot be determined.
static constexpr UINT MT4_EXPERTS_COMMAND = 33020;

struct Mt4Terminal {
    HWND hwnd{};
    std::wstring title;
};

// ---------------------------------------------------------------------------
// External trade adapter
// ---------------------------------------------------------------------------

struct Position {
    std::string ticket;
    std::string symbol;
    std::string side;
    double lots{};
};

class ITradeAdapter {
public:
    virtual ~ITradeAdapter() = default;

    // Must return true and populate positions from the broker/account.
    virtual bool GetOpenPositions(std::vector<Position>& positions) = 0;

    // Must close exactly one position by its broker ticket/ID.
    virtual bool ClosePosition(const Position& position) = 0;
};

// Compile-ready placeholder. It deliberately refuses to pretend that it can
// trade. Replace this class with your broker's authorized API implementation.
class UnconfiguredTradeAdapter final : public ITradeAdapter {
public:
    bool GetOpenPositions(std::vector<Position>& positions) override {
        positions.clear();
        std::cerr
            << "[TRADE API] No external trading adapter configured.\n"
            << "[TRADE API] Refusing to invent/open/close account data.\n";
        return false;
    }

    bool ClosePosition(const Position&) override {
        std::cerr
            << "[TRADE API] No external trading adapter configured; close refused.\n";
        return false;
    }
};

// ---------------------------------------------------------------------------
// MT4 window discovery
// ---------------------------------------------------------------------------

static bool IsLikelyMT4(HWND hwnd)
{
    wchar_t cls[256]{};
    wchar_t title[512]{};

    GetClassNameW(hwnd, cls, 255);
    GetWindowTextW(hwnd, title, 511);

    std::wstring c(cls);
    std::wstring t(title);

    return
        c.find(L"MetaTrader") != std::wstring::npos ||
        c.find(L"MetaQuotes") != std::wstring::npos ||
        t.find(L"MetaTrader 4") != std::wstring::npos ||
        t.find(L"MetaTrader4") != std::wstring::npos;
}

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    if (!IsWindowVisible(hwnd))
        return TRUE;

    if (IsLikelyMT4(hwnd)) {
        auto* list =
            reinterpret_cast<std::vector<Mt4Terminal>*>(lParam);

        wchar_t title[512]{};
        GetWindowTextW(hwnd, title, 511);

        list->push_back({hwnd, title});
    }

    return TRUE;
}

static std::vector<Mt4Terminal> FindMT4Terminals()
{
    std::vector<Mt4Terminal> result;
    EnumWindows(EnumWindowsProc,
                reinterpret_cast<LPARAM>(&result));
    return result;
}

// ---------------------------------------------------------------------------
// AutoTrading state detection
// ---------------------------------------------------------------------------
//
// MT4 broker builds can have different toolbar/control implementations.
// We search the window hierarchy for a text-bearing control. If it cannot be
// identified, we refuse to toggle. This is deliberately fail-safe.

struct ButtonSearch {
    HWND found{};
};

static BOOL CALLBACK FindButtonProc(HWND hwnd, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<ButtonSearch*>(lParam);

    wchar_t text[512]{};
    GetWindowTextW(hwnd, text, 511);

    std::wstring s(text);

    if (s.find(L"AutoTrading") != std::wstring::npos ||
        s.find(L"Auto Trading") != std::wstring::npos ||
        s.find(L"Expert Advisors") != std::wstring::npos)
    {
        ctx->found = hwnd;
        return FALSE;
    }

    return TRUE;
}

static HWND FindAutoTradingControl(HWND root)
{
    // First try common control classes.
    const wchar_t* classes[] = {
        L"Button",
        L"ToolbarWindow32",
        nullptr
    };

    for (int i = 0; classes[i]; ++i) {
        HWND h = nullptr;

        while ((h = FindWindowExW(root, h, classes[i], nullptr)) != nullptr) {
            wchar_t text[512]{};
            GetWindowTextW(h, text, 511);

            std::wstring s(text);

            if (s.find(L"AutoTrading") != std::wstring::npos ||
                s.find(L"Auto Trading") != std::wstring::npos ||
                s.find(L"Expert Advisors") != std::wstring::npos)
                return h;
        }
    }

    return nullptr;
}

static bool IsChecked(HWND h)
{
    if (!h)
        return false;

    LRESULT state = SendMessageW(h, BM_GETCHECK, 0, 0);

    return state == BST_CHECKED ||
           state == BST_INDETERMINATE;
}

static bool DisableAutoTrading(Mt4Terminal& terminal)
{
    HWND control = FindAutoTradingControl(terminal.hwnd);

    if (!control) {
        std::wcerr
            << L"[MT4] " << terminal.title
            << L": AutoTrading state could not be identified.\n"
            << L"[MT4] Refusing to blindly toggle it.\n";
        return false;
    }

    bool on = IsChecked(control);

    std::wcout
        << L"[MT4] " << terminal.title
        << L": AutoTrading = "
        << (on ? L"ON" : L"OFF") << L"\n";

    if (!on) {
        std::wcout << L"[MT4] Already OFF.\n";
        return true;
    }

    if (!PostMessageW(terminal.hwnd,
                      WM_COMMAND,
                      MT4_EXPERTS_COMMAND,
                      0))
    {
        std::wcerr
            << L"[MT4] Failed to send AutoTrading OFF command.\n";
        return false;
    }

    // Give MT4 time to process the UI command.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    HWND verifyControl = FindAutoTradingControl(terminal.hwnd);

    if (!verifyControl) {
        std::wcerr
            << L"[MT4] Could not verify AutoTrading state after command.\n";
        return false;
    }

    bool stillOn = IsChecked(verifyControl);

    if (stillOn) {
        std::wcerr
            << L"[MT4] AutoTrading still appears ON.\n";
        return false;
    }

    std::wcout << L"[MT4] AutoTrading successfully turned OFF.\n";
    return true;
}

// ---------------------------------------------------------------------------
// Emergency close sequence
// ---------------------------------------------------------------------------

static bool EmergencyCloseAll(ITradeAdapter& adapter)
{
    std::vector<Position> positions;

    if (!adapter.GetOpenPositions(positions)) {
        std::cerr
            << "[MANAGER] Cannot enumerate positions because no external "
               "trade adapter is configured.\n";
        return false;
    }

    if (positions.empty()) {
        std::cout << "[MANAGER] No open positions.\n";
        return true;
    }

    std::cout
        << "[MANAGER] Found "
        << positions.size()
        << " open position(s).\n";

    size_t index = 0;

    while (true) {
        positions.clear();

        if (!adapter.GetOpenPositions(positions)) {
            std::cerr
                << "[MANAGER] Position refresh failed.\n";
            return false;
        }

        if (positions.empty()) {
            std::cout
                << "[MANAGER] ZERO open positions confirmed.\n";
            return true;
        }

        // Always take the first currently-open position. This avoids using a
        // stale vector after a successful close.
        Position p = positions.front();

        std::cout
            << "[MANAGER] Closing position "
            << (index + 1)
            << " / currently open: "
            << positions.size()
            << " | ticket=" << p.ticket
            << " | symbol=" << p.symbol
            << " | side=" << p.side
            << " | lots=" << p.lots
            << "\n";

        if (!adapter.ClosePosition(p)) {
            std::cerr
                << "[MANAGER] Close failed for ticket "
                << p.ticket << ".\n";
            return false;
        }

        ++index;

        // Exact requested interval between successful close operations.
        if (!positions.empty()) {
            std::cout
                << "[MANAGER] Waiting 5 seconds before the next position...\n";

            std::this_thread::sleep_for(
                std::chrono::seconds(5));
        }
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int wmain(int argc, wchar_t** argv)
{
    bool once = false;
    bool watch = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring a(argv[i]);

        if (a == L"--once")
            once = true;

        if (a == L"--watch")
            watch = true;
    }

    std::wcout
        << L"MT4 AutoTrading Emergency Manager\n"
        << L"==================================\n\n"
        << L"Sequence:\n"
        << L"  1. Detect AutoTrading.\n"
        << L"  2. If ON, turn it OFF.\n"
        << L"  3. Enumerate positions through an external trade adapter.\n"
        << L"  4. Close one position.\n"
        << L"  5. Wait 5 seconds.\n"
        << L"  6. Repeat until zero positions remain.\n\n";

    auto terminals = FindMT4Terminals();

    if (terminals.empty()) {
        std::wcerr << L"No visible MT4 terminal found.\n";
        return 2;
    }

    // For safety, process the first discovered MT4 terminal.
    // This can be changed to account/terminal selection if required.
    Mt4Terminal terminal = terminals.front();

    if (!DisableAutoTrading(terminal))
        return 3;

    // This is intentionally not a fake MT4 OrderClose() bridge.
    UnconfiguredTradeAdapter adapter;

    if (!EmergencyCloseAll(adapter))
        return 4;

    if (watch) {
        std::wcout
            << L"\nWatch mode requested, but the emergency sequence completed.\n";
    }

    if (!once && !watch) {
        std::wcout
            << L"\nCompleted. Press ENTER to exit.\n";
        std::wstring dummy;
        std::getline(std::wcin, dummy);
    }

    return 0;
}

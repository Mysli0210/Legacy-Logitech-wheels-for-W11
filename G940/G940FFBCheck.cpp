// G940FFBCheck: confirms that DirectInput sees the G940 with force feedback.
//   G940FFBCheck.exe           read-only report, sends nothing to the device
//   G940FFBCheck.exe --spring  interactive centering-spring test (asks first)
//   G940FFBCheck.exe --plan    validate and print the suite without acquiring (no effect downloaded)
//   G940FFBCheck.exe --suite   one-shot timed run of every effect, logs stick position to CSV
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <conio.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const DWORD kVidPid = static_cast<DWORD>(MAKELONG(0x046D, 0xC287));

struct Found { GUID instance{}; wchar_t name[MAX_PATH]{}; };

static BOOL CALLBACK OnDevice(LPCDIDEVICEINSTANCEW d, LPVOID ctx) {
    // guidProduct.Data1 holds MAKELONG(VID, PID) for HID game controllers.
    if (d->guidProduct.Data1 != kVidPid) return DIENUM_CONTINUE;
    auto f = static_cast<Found*>(ctx);
    f->instance = d->guidInstance;
    wcsncpy_s(f->name, d->tszProductName, _TRUNCATE);
    return DIENUM_STOP;
}

static BOOL CALLBACK OnEffect(LPCDIEFFECTINFOW e, LPVOID count) {
    wprintf(L"    - %s\n", e->tszName);
    ++*static_cast<int*>(count);
    return DIENUM_CONTINUE;
}

static IDirectInputDevice8W* g_dev = nullptr;

static BOOL CALLBACK OnActuator(LPCDIDEVICEOBJECTINSTANCEW o, LPVOID axes) {
    // Logical range tells the main stick (10-bit, -512..511) from the mini-stick.
    DIPROPRANGE range{ { sizeof(DIPROPRANGE), sizeof(DIPROPHEADER), o->dwType, DIPH_BYID } };
    g_dev->GetProperty(DIPROP_LOGICALRANGE, &range.diph);
    wprintf(L"    - %s (instance %lu, HID usage %02X:%02X, logical %ld..%ld)\n", o->tszName,
            DIDFT_GETINSTANCE(o->dwType), o->wUsagePage, o->wUsage, range.lMin, range.lMax);
    static_cast<std::vector<DWORD>*>(axes)->push_back(o->dwOfs);
    return DIENUM_CONTINUE;
}

static int SpringTest(IDirectInputDevice8W* dev, const std::vector<DWORD>& axes) {
    wprintf(L"\nSpring test: the stick will be pulled to the centre with a moderate spring.\n"
            L"Keep a hand on the stick and your motor power switch within reach.\n"
            L"Type YES and press Enter to start: ");
    wchar_t answer[16]{};
    if (!fgetws(answer, 16, stdin) || wcsncmp(answer, L"YES", 3) != 0) { wprintf(L"Cancelled.\n"); return 0; }

    // DirectInput needs a top-level window for exclusive access.
    HWND wnd = CreateWindowExW(0, L"STATIC", L"G940FFBCheck", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    HRESULT hr = dev->SetCooperativeLevel(wnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
    if (SUCCEEDED(hr)) hr = dev->SetDataFormat(&c_dfDIJoystick2);
    if (SUCCEEDED(hr)) hr = dev->Acquire();
    if (FAILED(hr)) { wprintf(L"Could not acquire device exclusively: 0x%08lX\n", hr); DestroyWindow(wnd); return 1; }

    DICONDITION cond[2]{};
    for (auto& c : cond) { c.lPositiveCoefficient = c.lNegativeCoefficient = 5000; c.dwPositiveSaturation = c.dwNegativeSaturation = 10000; }
    DWORD axisOfs[2] = { DIJOFS_X, DIJOFS_Y };
    LONG dir[2]{};
    DIEFFECT eff{ sizeof(DIEFFECT) };
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE;
    eff.dwGain = DI_FFNOMINALMAX;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.cAxes = axes.size() >= 2 ? 2 : 1;
    eff.rgdwAxes = axisOfs;
    eff.rglDirection = dir;
    eff.cbTypeSpecificParams = sizeof(DICONDITION) * eff.cAxes;
    eff.lpvTypeSpecificParams = cond;

    IDirectInputEffect* spring = nullptr;
    hr = dev->CreateEffect(GUID_Spring, &eff, &spring, nullptr);
    if (SUCCEEDED(hr)) hr = spring->Start(1, 0);
    if (FAILED(hr)) {
        wprintf(L"Spring effect failed: 0x%08lX\n", hr);
    } else {
        wprintf(L"Spring running. Move the stick: it should pull back to centre. Press Enter to stop.\n");
        fgetws(answer, 16, stdin);
    }
    if (spring) { spring->Stop(); spring->Release(); }
    dev->SendForceFeedbackCommand(DISFFC_STOPALL);
    dev->SendForceFeedbackCommand(DISFFC_RESET);
    dev->Unacquire();
    DestroyWindow(wnd);
    wprintf(L"Stopped.\n");
    return FAILED(hr) ? 1 : 0;
}

// ---- One-shot suite ---------------------------------------------------------

enum class Kind { Pause, Condition, Constant, Sine };

struct Step {
    const wchar_t* name;
    const wchar_t* todo;   // what the person at the stick should do
    Kind kind;
    GUID guid;             // condition effects only
    LONG dx, dy;           // constant: direction; sine: which axes (1 = active)
    LONG level;            // coefficient or magnitude, 0..10000
    DWORD ms;
};

static const Step kSuite[] = {
    { L"baseline",      L"hands on grip, stick still",          Kind::Pause,     {},            0, 0,    0, 3000 },
    { L"spring",        L"move stick in big circles",           Kind::Condition, GUID_Spring,   0, 0, 6000, 5000 },
    { L"pause",         L"",                                    Kind::Pause,     {},            0, 0,    0, 1500 },
    { L"damper",        L"move stick fast, back and forth",     Kind::Condition, GUID_Damper,   0, 0, 6000, 5000 },
    { L"pause",         L"",                                    Kind::Pause,     {},            0, 0,    0, 1500 },
    { L"friction",      L"move stick slowly in circles",        Kind::Condition, GUID_Friction, 0, 0, 6000, 5000 },
    { L"pause",         L"",                                    Kind::Pause,     {},            0, 0,    0, 1500 },
    { L"const +X",      L"hold grip loosely, let it push",      Kind::Constant,  {},            1, 0, 4000, 2000 },
    { L"pause",         L"",                                    Kind::Pause,     {},            0, 0,    0, 1000 },
    { L"const -X",      L"hold grip loosely, let it push",      Kind::Constant,  {},           -1, 0, 4000, 2000 },
    { L"pause",         L"",                                    Kind::Pause,     {},            0, 0,    0, 1000 },
    { L"const +Y",      L"hold grip loosely, let it push",      Kind::Constant,  {},            0, 1, 4000, 2000 },
    { L"pause",         L"",                                    Kind::Pause,     {},            0, 0,    0, 1000 },
    { L"const -Y",      L"hold grip loosely, let it push",      Kind::Constant,  {},            0,-1, 4000, 2000 },
    { L"pause",         L"",                                    Kind::Pause,     {},            0, 0,    0, 1000 },
    { L"sine X 4Hz",    L"hold grip loosely",                   Kind::Sine,      {},            1, 0, 3000, 2500 },
    { L"pause",         L"",                                    Kind::Pause,     {},            0, 0,    0, 1000 },
    { L"sine Y 4Hz",    L"hold grip loosely",                   Kind::Sine,      {},            0, 1, 3000, 2500 },
    { L"end",           L"hands on grip, stick still",          Kind::Pause,     {},            0, 0,    0, 2000 },
};

static std::atomic<bool> g_abort{ false };

static BOOL WINAPI OnConsoleCtrl(DWORD) {
    g_abort = true;
    Sleep(2000);  // give the main thread time to stop all effects before the process ends
    return TRUE;
}

// Builds the DirectInput effect for a step. Returns S_FALSE for pause steps.
static HRESULT MakeEffect(IDirectInputDevice8W* dev, const Step& s, IDirectInputEffect** out) {
    *out = nullptr;
    if (s.kind == Kind::Pause) return S_FALSE;
    DWORD axes[2] = { DIJOFS_X, DIJOFS_Y };
    LONG dir[2] = { s.dx, s.dy };
    DICONDITION cond[2]{};
    DICONSTANTFORCE constant{ s.level };
    DIPERIODIC periodic{ static_cast<DWORD>(s.level), 0, 0, 250000 };  // 4 Hz
    DIEFFECT eff{ sizeof(DIEFFECT) };
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = s.ms * 1000;
    eff.dwGain = DI_FFNOMINALMAX;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.rgdwAxes = axes;
    eff.rglDirection = dir;
    GUID guid = s.guid;
    switch (s.kind) {
    case Kind::Condition:
        for (auto& c : cond) { c.lPositiveCoefficient = c.lNegativeCoefficient = s.level; c.dwPositiveSaturation = c.dwNegativeSaturation = 10000; }
        dir[0] = dir[1] = 0;
        eff.cAxes = 2;
        eff.cbTypeSpecificParams = sizeof(cond);
        eff.lpvTypeSpecificParams = cond;
        break;
    case Kind::Constant:
        guid = GUID_ConstantForce;
        eff.cAxes = 2;
        eff.cbTypeSpecificParams = sizeof(constant);
        eff.lpvTypeSpecificParams = &constant;
        break;
    default:  // Sine on a single axis
        guid = GUID_Sine;
        axes[0] = s.dx ? DIJOFS_X : DIJOFS_Y;
        dir[0] = 1;
        eff.cAxes = 1;
        eff.cbTypeSpecificParams = sizeof(periodic);
        eff.lpvTypeSpecificParams = &periodic;
        break;
    }
    return dev->CreateEffect(guid, &eff, out, nullptr);
}

static double Norm(LONG v) { return (v - 32767.5) / 32767.5; }  // default DI range 0..65535 -> -1..1

static int Suite(IDirectInputDevice8W* dev, bool planOnly) {
    DWORD total = 0;
    wprintf(L"\nFFB suite (%s):\n", planOnly ? L"plan only, device not acquired, no effect is downloaded or started" : L"LIVE");
    for (auto& s : kSuite) {
        if (s.kind != Kind::Pause) wprintf(L"  %5.1fs  %-11s %s\n", total / 1000.0, s.name, s.todo);
        total += s.ms;
    }
    wprintf(L"  total %.0f s. Press Esc or Ctrl+C at any time to stop all forces.\n", total / 1000.0);

    // Axis offsets in the effects refer to this data format.
    HRESULT fmt = dev->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(fmt)) { wprintf(L"SetDataFormat failed: 0x%08lX\n", fmt); return 1; }

    if (planOnly) {
        // Not acquired, so CreateEffect only validates parameters; nothing is downloaded.
        int bad = 0;
        for (auto& s : kSuite) {
            IDirectInputEffect* e = nullptr;
            HRESULT hr = MakeEffect(dev, s, &e);
            if (s.kind == Kind::Pause) continue;
            wprintf(L"  %-11s CreateEffect 0x%08lX%s\n", s.name, hr, FAILED(hr) ? L"  <-- FAILED" : L"");
            bad += FAILED(hr);
            if (e) e->Release();
        }
        return bad ? 1 : 0;
    }

    wprintf(L"\nHand on the grip (remove any tape from the palm sensor), motor power switch in reach.\n"
            L"Type YES and press Enter to start: ");
    wchar_t answer[16]{};
    if (!fgetws(answer, 16, stdin) || wcsncmp(answer, L"YES", 3) != 0) { wprintf(L"Cancelled.\n"); return 0; }

    HWND wnd = CreateWindowExW(0, L"STATIC", L"G940FFBCheck", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    HRESULT hr = dev->SetCooperativeLevel(wnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
    if (SUCCEEDED(hr)) hr = dev->Acquire();
    if (FAILED(hr)) { wprintf(L"Could not acquire device exclusively: 0x%08lX\n", hr); DestroyWindow(wnd); return 1; }
    SetConsoleCtrlHandler(OnConsoleCtrl, TRUE);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t csvName[64];
    swprintf_s(csvName, L"g940-ffb-suite-%04u%02u%02u-%02u%02u%02u.csv", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    FILE* csv = nullptr;
    _wfopen_s(&csv, csvName, L"w");
    if (csv) fwprintf(csv, L"t_ms,step,x,y,result\n");

    struct Stats { double sx = 0, sy = 0, minx = 9, maxx = -9, miny = 9, maxy = -9, path = 0; int n = 0; HRESULT hr = S_OK; };
    std::vector<Stats> stats(std::size(kSuite));
    ULONGLONG t0 = GetTickCount64();
    int failures = 0;

    for (size_t i = 0; i < std::size(kSuite) && !g_abort; ++i) {
        const Step& s = kSuite[i];
        IDirectInputEffect* e = nullptr;
        HRESULT ehr = MakeEffect(dev, s, &e);
        if (e) ehr = e->Start(1, 0);
        stats[i].hr = ehr;
        if (FAILED(ehr)) ++failures;
        if (s.kind != Kind::Pause || s.todo[0])
            wprintf(L"\n>> %-11s %s%s", s.name, s.todo, FAILED(ehr) ? L"  [effect FAILED]" : L"");
        ULONGLONG end = GetTickCount64() + s.ms;
        double px = 2, py = 2;
        while (GetTickCount64() < end && !g_abort) {
            if (_kbhit() && _getwch() == 27) g_abort = true;
            DIJOYSTATE2 js{};
            dev->Poll();
            if (SUCCEEDED(dev->GetDeviceState(sizeof(js), &js))) {
                double x = Norm(js.lX), y = Norm(js.lY);
                auto& a = stats[i];
                a.sx += x; a.sy += y; ++a.n;
                a.minx = min(a.minx, x); a.maxx = max(a.maxx, x);
                a.miny = min(a.miny, y); a.maxy = max(a.maxy, y);
                if (px < 2) a.path += std::hypot(x - px, y - py);
                px = x; py = y;
                if (csv) fwprintf(csv, L"%llu,%s,%.4f,%.4f,0x%08lX\n", GetTickCount64() - t0, s.name, x, y, ehr);
            }
            Sleep(10);
        }
        if (e) { e->Stop(); e->Unload(); e->Release(); }
    }

    dev->SendForceFeedbackCommand(DISFFC_STOPALL);
    dev->SendForceFeedbackCommand(DISFFC_RESET);
    dev->Unacquire();
    DestroyWindow(wnd);
    if (csv) fclose(csv);
    wprintf(L"\n\n%s. All forces stopped.\n\n", g_abort ? L"ABORTED" : L"Suite finished");

    wprintf(L"%-11s %10s %7s %7s %13s %13s %6s\n", L"step", L"result", L"mean X", L"mean Y", L"X range", L"Y range", L"path");
    for (size_t i = 0; i < std::size(kSuite); ++i) {
        const auto& a = stats[i];
        if (!a.n || (kSuite[i].kind == Kind::Pause && !kSuite[i].todo[0])) continue;
        wprintf(L"%-11s 0x%08lX %+7.3f %+7.3f %+6.2f..%+5.2f %+6.2f..%+5.2f %6.2f\n", kSuite[i].name, a.hr,
                a.sx / a.n, a.sy / a.n, a.minx, a.maxx, a.miny, a.maxy, a.path);
    }
    wprintf(L"\nLog: %s\n", csvName);
    return (failures || g_abort) ? 1 : 0;
}
int wmain(int argc, wchar_t** argv) {
    const wchar_t* mode = argc > 1 ? argv[1] : L"";
    bool spring = _wcsicmp(mode, L"--spring") == 0;
    bool plan = _wcsicmp(mode, L"--plan") == 0;
    bool suite = _wcsicmp(mode, L"--suite") == 0;

    IDirectInput8W* di = nullptr;
    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&di, nullptr))) {
        wprintf(L"DirectInput8Create failed.\n"); return 1;
    }
    Found f;
    di->EnumDevices(DI8DEVCLASS_GAMECTRL, OnDevice, &f, DIEDFL_ATTACHEDONLY);
    if (f.instance == GUID_NULL) { wprintf(L"G940 (VID 046D, PID C287) not found. Is it plugged in?\n"); di->Release(); return 2; }

    IDirectInputDevice8W* dev = nullptr;
    if (FAILED(di->CreateDevice(f.instance, &dev, nullptr))) { wprintf(L"CreateDevice failed.\n"); di->Release(); return 1; }
    DIDEVCAPS caps{ sizeof(caps) };
    dev->GetCapabilities(&caps);
    bool ff = (caps.dwFlags & DIDC_FORCEFEEDBACK) != 0;
    wprintf(L"Device: %s\n  axes %lu, buttons %lu, POVs %lu\n  force feedback: %s\n",
            f.name, caps.dwAxes, caps.dwButtons, caps.dwPOVs, ff ? L"YES" : L"NO");

    int rc = 0;
    if (!ff) {
        wprintf(L"\nDirectInput does not report force feedback. Run _Install_G940_FFB.bat as administrator,\n"
                L"then unplug and replug the G940.\n");
        rc = 3;
    } else {
        std::vector<DWORD> axes;
        g_dev = dev;
        wprintf(L"  force feedback axes:\n");
        dev->EnumObjects(OnActuator, &axes, DIDFT_AXIS | DIDFT_FFACTUATOR);
        int effects = 0;
        wprintf(L"  supported effects:\n");
        dev->EnumEffects(OnEffect, &effects, DIEFT_ALL);
        if (spring) rc = SpringTest(dev, axes);
        else if (plan || suite) rc = Suite(dev, plan);
        else wprintf(L"\nRead-only check done; nothing was sent to the device. Use --suite for the hands-on test.\n");
    }
    dev->Release();
    di->Release();
    return rc;
}

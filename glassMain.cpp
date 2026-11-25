#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <xamlom.h>
#include <inspectable.h>

#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.h>
#include <string>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")

using namespace winrt;
using namespace Windows::UI::Xaml;

struct VisualTreeTracker : winrt::implements<VisualTreeTracker, IVisualTreeServiceCallback2>
{
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation relation, VisualElement element, VisualMutationType mutationType) override
    {
        if (mutationType == Add && element.Handle != 0)
        {
            try
            {
                std::wstring name = element.Name ? element.Name : L"";
                std::wstring type = element.Type ? element.Type : L"";

                if (name.find(L"Background") != std::wstring::npos || 
                    name.find(L"SystemTray") != std::wstring::npos ||
                    type.find(L"TaskbarFrame") != std::wstring::npos)
                {
                    IInspectable* insp = reinterpret_cast<IInspectable*>(element.Handle);
                    UIElement uiElem{ nullptr };
                    winrt::copy_from_abi(uiElem, insp);

                    if (uiElem)
                    {
                        uiElem.Opacity(0.0);
                    }
                }
            }
            catch (...) {}
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) override
    {
        return S_OK;
    }
};

extern "C" __declspec(dllexport) HRESULT WINAPI InitializeXamlDiagnostics()
{
    HMODULE wux = GetModuleHandleW(L"Windows.UI.Xaml.dll");
    if (!wux) return E_FAIL;

    using pfnInitializeXamlDiagnosticsEx = HRESULT(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID, LPCWSTR);
    auto initFunc = reinterpret_cast<pfnInitializeXamlDiagnosticsEx>(GetProcAddress(wux, "InitializeXamlDiagnosticsEx"));
    if (!initFunc) return E_FAIL;

    wchar_t modPath[MAX_PATH];
    HMODULE hMod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&InitializeXamlDiagnostics), &hMod);
    GetModuleFileNameW(hMod, modPath, MAX_PATH);

    CLSID clsid = { 0x361c3466, 0x8573, 0x4e2a, { 0xb1, 0xb2, 0x9d, 0x1c, 0x7f, 0x4d, 0x8a, 0x11 } };
    return initFunc(L"VisualDiagConnection1", GetCurrentProcessId(), L"", modPath, clsid, L"");
}

struct HookSite : winrt::implements<HookSite, IObjectWithSite>
{
    winrt::com_ptr<IUnknown> m_site;

    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* pUnkSite) override
    {
        m_site.copy_from(pUnkSite);
        if (m_site)
        {
            auto service = m_site.try_as<IVisualTreeService3>();
            if (service)
            {
                auto tracker = winrt::make<VisualTreeTracker>();
                service->AdviseVisualTreeChange(tracker.get());
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppvSite) override
    {
        return m_site ? m_site->QueryInterface(riid, ppvSite) : E_FAIL;
    }
};

struct HookFactory : winrt::implements<HookFactory, IClassFactory>
{
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, LPVOID* ppvObject) override
    {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        return winrt::make<HookSite>()->QueryInterface(riid, ppvObject);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    CLSID clsid = { 0x361c3466, 0x8573, 0x4e2a, { 0xb1, 0xb2, 0x9d, 0x1c, 0x7f, 0x4d, 0x8a, 0x11 } };
    if (rclsid == clsid)
    {
        return winrt::make<HookFactory>()->QueryInterface(riid, ppv);
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

struct AccentPolicy {
    int AccentState;
    int AccentFlags;
    uint32_t GradientColor;
    int AnimationId;
};

struct WindowCompositionAttributeData {
    int Attribute;
    void* Data;
    int SizeOfData;
};

void ApplyZeroOpacity()
{
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (taskbar)
    {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        using pSetWindowCompositionAttribute = int(WINAPI*)(HWND, WindowCompositionAttributeData*);
        auto setComp = reinterpret_cast<pSetWindowCompositionAttribute>(GetProcAddress(user32, "SetWindowCompositionAttribute"));
        if (setComp)
        {
            AccentPolicy accent = { 2, 2, 0x00000000, 0 };
            WindowCompositionAttributeData data = { 19, &accent, sizeof(accent) };
            setComp(taskbar, &data);
        }
    }

    DWORD explorerPid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snap, &pe))
    {
        do {
            if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                explorerPid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (!explorerPid) return;

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, explorerPid);
    if (hProc)
    {
        size_t size = (wcslen(exePath) + 1) * sizeof(wchar_t);
        void* remoteMem = VirtualAllocEx(hProc, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        WriteProcessMemory(hProc, remoteMem, exePath, size, nullptr);

        auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, loadLib, remoteMem, 0, nullptr);
        if (hThread) CloseHandle(hThread);
        CloseHandle(hProc);
        MessageBoxW(nullptr, L"XAML qatlami mutlaq 0 Opacity holatiga o'tkazildi!", L"GlassTaskbar", MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        MessageBoxW(nullptr, L"Dasturni Administrator sifatida ishga tushiring!", L"Xatolik", MB_OK | MB_ICONERROR);
    }
}

void RestartExplorer()
{
    system("taskkill /f /im explorer.exe && start explorer.exe");
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        CreateWindowW(L"BUTTON", L"Mutlaq 0 Opacity (Yoqish)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            30, 25, 240, 40, hwnd, (HMENU)1, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Asl holatga qaytarish", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            30, 80, 240, 35, hwnd, (HMENU)2, nullptr, nullptr);
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) ApplyZeroOpacity();
        if (LOWORD(wParam) == 2) RestartExplorer();
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"GlassTaskbarSingle";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"GlassTaskbarSingle", L"GlassTaskbar",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 315, 175, nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        wchar_t procName[MAX_PATH];
        GetModuleFileNameW(nullptr, procName, MAX_PATH);
        if (wcsstr(procName, L"explorer.exe") != nullptr)
        {
            DisableThreadLibraryCalls(hModule);
            CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                Sleep(500);
                InitializeXamlDiagnostics();
                return 0;
            }, nullptr, 0, nullptr);
        }
    }
    return TRUE;
}

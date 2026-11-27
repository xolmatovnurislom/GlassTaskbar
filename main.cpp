// ============================================================
// GlassTaskbar - TO'LIQ ISHLAYDIGAN main.cpp
// Windows 11 Taskbar orqa fonini olib tashlash
// ============================================================
// QANDAY BUILD QILINADI (Visual Studio):
//  1) Bitta fayldan 2 ta loyiha qilasan:
//     a) GlassHook.dll -> DLL loyiha, Preprocessor ga GLASS_HOOK_DLL qo'sh
//     b) GlassTaskbar.exe -> EXE loyiha, hech narsa qo'shmasdan
//  2) Ikkalasini ham x64 / Release da build qil
//     x86 bo'lsa 64-bit explorer.exe ga inject bo'lmaydi!
//  3) GlassHook.dll va GlassTaskbar.exe ni BITTA papkaga qo'y
//  4) GlassTaskbar.exe ni Admin sifatida ishga tushir
//     va "Yoqish" tugmasini bos
//
// QANDAY ISHLAYDI:
//  EXE -> GlassHook.dll ni explorer.exe ga inject qiladi
//  DLL -> XAML Diagnostics orqali BackgroundFill ni topib
//         Visibility=Collapsed + Fill=Transparent qiladi
//       + SetWindowCompositionAttribute orqali taskbar'ni
//         shaffof qiladi (fallback)
// LOG: C:\Users\Public\glass_log.txt
// ============================================================
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <oleauto.h>
#include <xamlom.h>
#include <inspectable.h>
#include <string>
#include <fstream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")

void LogMsg(const std::wstring& msg)
{
    std::wofstream f(L"C:\\Users\\Public\\glass_log.txt", std::ios::app);
    if (f.is_open()) f << msg << L"\n";
}

static const CLSID CLSID_GlassHook = { 0x361c3466, 0x8573, 0x4e2a, { 0xb1, 0xb2, 0x9d, 0x1c, 0x7f, 0x4d, 0x8a, 0x11 } };

#ifdef GLASS_HOOK_DLL
// ============================================================
// DLL QISMI - explorer.exe ichida ishlaydi
// ============================================================

bool TrySetProperty(IVisualTreeService3* service, InstanceHandle elem, const wchar_t* propName, const wchar_t* typeName, const wchar_t* valueStr)
{
    BSTR bstrType = SysAllocString(typeName);
    BSTR bstrVal = SysAllocString(valueStr);
    InstanceHandle valHandle = 0;
    bool ok = false;

    HRESULT hrCreate = service->CreateInstance(bstrType, bstrVal, &valHandle);
    if (SUCCEEDED(hrCreate) && valHandle != 0)
    {
        unsigned int sourceCount = 0;
        PropertyChainSource* sources = nullptr;
        unsigned int propCount = 0;
        PropertyChainValue* props = nullptr;

        if (SUCCEEDED(service->GetPropertyValuesChain(elem, &sourceCount, &sources, &propCount, &props)))
        {
            bool found = false;
            for (unsigned int i = 0; i < propCount; i++)
            {
                if (props[i].PropertyName && wcscmp(props[i].PropertyName, propName) == 0)
                {
                    found = true;
                    HRESULT hr = service->SetProperty(elem, valHandle, props[i].Index);
                    LogMsg(std::wstring(L"  -> SetProperty ") + propName + L" = " + valueStr + L" HR=" + std::to_wstring(hr));
                    ok = SUCCEEDED(hr);
                    break;
                }
            }
            if (!found)
            {
                LogMsg(std::wstring(L"  !! Chain'da topilmadi: ") + propName);
            }
            if (sources) CoTaskMemFree(sources);
            if (props) CoTaskMemFree(props);
        }
    }
    else
    {
        LogMsg(std::wstring(L"  !! CreateInstance failed: ") + typeName + L" HR=" + std::to_wstring(hrCreate));
    }

    if (bstrType) SysFreeString(bstrType);
    if (bstrVal) SysFreeString(bstrVal);
    return ok;
}

void HideBackgroundElement(IVisualTreeService3* service, InstanceHandle elem, const std::wstring& name)
{
    LogMsg(L"Hide: [" + name + L"]");
    // 1. Eng ishonchlisi - ko'rinmas qilish
    TrySetProperty(service, elem, L"Visibility", L"Windows.UI.Xaml.Visibility", L"Collapsed");
    // 2. Rectangle uchun Fill ni shaffof qilish
    TrySetProperty(service, elem, L"Fill", L"Windows.UI.Xaml.Media.SolidColorBrush", L"#00000000");
    // 3. Grid / Border uchun Background ni shaffof qilish
    TrySetProperty(service, elem, L"Background", L"Windows.UI.Xaml.Media.SolidColorBrush", L"#00000000");
    // 4. Fallback - Opacity
    TrySetProperty(service, elem, L"Opacity", L"Windows.Foundation.Double", L"0");
}

struct VisualTreeTracker : IVisualTreeServiceCallback2
{
    LONG m_ref = 1;
    IVisualTreeService3* m_service = nullptr;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IVisualTreeServiceCallback) || riid == __uuidof(IVisualTreeServiceCallback2))
        {
            *ppv = static_cast<IVisualTreeServiceCallback2*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0)
        {
            if (m_service) m_service->Release();
            delete this;
        }
        return r;
    }

    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation relation, VisualElement element, VisualMutationType mutationType) override
    {
        if ((mutationType == Add || mutationType == Replace) && element.Handle != 0 && m_service)
        {
            std::wstring name = element.Name ? element.Name : L"";
            std::wstring type = element.Type ? element.Type : L"";

            if (name == L"BackgroundFill" ||
                name == L"BackgroundStroke" ||
                name == L"TaskbarBackground" ||
                name == L"SystemTrayFrameBackground" ||
                name == L"SystemTrayFrameGrid" ||
                name == L"TaskbarFrame" ||
                name == L"TaskbarBackgroundControl" ||
                name.find(L"Background") != std::wstring::npos)
            {
                LogMsg(L"Elem: Name=[" + name + L"] Type=[" + type + L"]");
                HideBackgroundElement(m_service, element.Handle, name);
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) override
    {
        return S_OK;
    }
};

static VisualTreeTracker* g_tracker = nullptr;

struct HookSite : IObjectWithSite
{
    LONG m_ref = 1;
    IUnknown* m_site = nullptr;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IObjectWithSite))
        {
            *ppv = static_cast<IObjectWithSite*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0) { if (m_site) m_site->Release(); delete this; }
        return r;
    }

    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* pUnkSite) override
    {
        LogMsg(L"SetSite called!");
        if (m_site) m_site->Release();
        m_site = pUnkSite;
        if (m_site)
        {
            m_site->AddRef();
            IVisualTreeService3* service = nullptr;
            if (SUCCEEDED(m_site->QueryInterface(__uuidof(IVisualTreeService3), (void**)&service)) && service)
            {
                LogMsg(L"IVisualTreeService3 OK, Advising...");
                if (!g_tracker)
                {
                    g_tracker = new VisualTreeTracker();
                    g_tracker->m_service = service;
                }
                HRESULT hr = service->AdviseVisualTreeChange(g_tracker);
                LogMsg(L"AdviseVisualTreeChange HR=" + std::to_wstring(hr));
            }
            else
            {
                LogMsg(L"IVisualTreeService3 olishda xato!");
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppvSite) override
    {
        return m_site ? m_site->QueryInterface(riid, ppvSite) : E_FAIL;
    }
};

struct HookFactory : IClassFactory
{
    LONG m_ref = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory))
        {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_ref); if (r == 0) delete this; return r; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) override
    {
        LogMsg(L"HookFactory::CreateInstance called!");
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        auto site = new HookSite();
        HRESULT hr = site->QueryInterface(riid, ppvObject);
        site->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

// MUHIM: dllexport bo'lmasa XAML Diagnostics bu funksiyani topa olmaydi!
// Eski kodda shu xato bor edi.
extern "C" __declspec(dllexport) HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    LogMsg(L"DllGetClassObject called!");
    if (rclsid == CLSID_GlassHook)
    {
        auto factory = new HookFactory();
        HRESULT hr = factory->QueryInterface(riid, ppv);
        factory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" __declspec(dllexport) HRESULT __stdcall DllCanUnloadNow()
{
    return S_FALSE;
}

void StartXamlHookInExplorer()
{
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LogMsg(L"StartXamlHook PID=" + std::to_wstring(GetCurrentProcessId()) + L" CoInit HR=" + std::to_wstring(hrCo));

    wchar_t modPath[MAX_PATH] = { 0 };
    HMODULE hMod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&StartXamlHookInExplorer), &hMod);
    GetModuleFileNameW(hMod, modPath, MAX_PATH);
    LogMsg(L"Module (DLL bo'lishi kerak): " + std::wstring(modPath));

    using pfnInit = HRESULT(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID, LPCWSTR);

    // Windows 11 taskbar - Windows.UI.Xaml.dll
    HMODULE wux = GetModuleHandleW(L"Windows.UI.Xaml.dll");
    LogMsg(L"Windows.UI.Xaml.dll=" + std::to_wstring((uintptr_t)wux));
    if (wux)
    {
        auto initFunc = (pfnInit)GetProcAddress(wux, "InitializeXamlDiagnosticsEx");
        if (initFunc)
        {
            HRESULT hr = initFunc(L"VisualDiagConnection2", GetCurrentProcessId(), L"", modPath, CLSID_GlassHook, L"");
            LogMsg(L"WUX Initialize HR=" + std::to_wstring(hr));
        }
    }

    // WinUI 3 fallback
    HMODULE mux = GetModuleHandleW(L"Microsoft.UI.Xaml.dll");
    if (mux)
    {
        auto initFunc = (pfnInit)GetProcAddress(mux, "InitializeXamlDiagnosticsEx");
        if (initFunc)
        {
            HRESULT hr = initFunc(L"VisualDiagConnection1", GetCurrentProcessId(), L"", modPath, CLSID_GlassHook, L"");
            LogMsg(L"MUX Initialize HR=" + std::to_wstring(hr));
        }
    }

    // Thread o'lmasligi kerak, COM tirik turadi
    Sleep(INFINITE);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        wchar_t procName[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, procName, MAX_PATH);
        if (wcsstr(procName, L"explorer.exe") != nullptr)
        {
            DisableThreadLibraryCalls(hModule);
            CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                StartXamlHookInExplorer();
                return 0;
            }, nullptr, 0, nullptr);
        }
    }
    return TRUE;
}

#else
// ============================================================
// EXE QISMI - UI + Injector
// ============================================================

typedef struct _ACCENT_POLICY {
    int AccentState;
    int AccentFlags;
    int GradientColor;
    int AnimationId;
} ACCENT_POLICY;

typedef struct _WINDOWCOMPOSITIONATTRIBDATA {
    int Attrib;
    void* pvData;
    SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef BOOL(WINAPI* pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

void MakeTaskbarTransparentFallback()
{
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!taskbar) return;
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    auto SetWindowCompositionAttribute = (pfnSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
    if (!SetWindowCompositionAttribute) return;

    ACCENT_POLICY accent = {};
    accent.AccentState = 2; // ACCENT_ENABLE_TRANSPARENTGRADIENT
    accent.GradientColor = 0;
    WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attrib = 19; // WCA_ACCENT_POLICY
    data.pvData = &accent;
    data.cbData = sizeof(accent);
    SetWindowCompositionAttribute(taskbar, &data);
    LogMsg(L"Fallback: SetWindowCompositionAttribute TRANSPARENT qo'llanildi");
}

void ApplyZeroOpacity()
{
    DeleteFileW(L"C:\\Users\\Public\\glass_log.txt");
    LogMsg(L"ApplyZeroOpacity clicked.");

    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    size_t slash = exeDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) exeDir = exeDir.substr(0, slash + 1);
    std::wstring dllPath = exeDir + L"GlassHook.dll";
    LogMsg(L"DLL path: " + dllPath);

    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        MessageBoxW(nullptr, L"GlassHook.dll topilmadi!\nEXE bilan bir papkada turishi kerak.\nAvval DLL ni build qil (GLASS_HOOK_DLL bilan).", L"Xatolik", MB_OK | MB_ICONERROR);
        return;
    }

    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD targetPid = 0;
    if (taskbar)
    {
        GetWindowThreadProcessId(taskbar, &targetPid);
        LogMsg(L"Shell_TrayWnd PID=" + std::to_wstring(targetPid));
    }
    if (!targetPid)
    {
        MessageBoxW(nullptr, L"Shell_TrayWnd topilmadi!", L"Xatolik", MB_OK | MB_ICONERROR);
        return;
    }

    MakeTaskbarTransparentFallback();

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, targetPid);
    if (!hProc)
    {
        MessageBoxW(nullptr, L"OpenProcess rad etildi! Admin sifatida ishga tushir.", L"Xatolik", MB_OK | MB_ICONERROR);
        return;
    }

    size_t size = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remoteMem = VirtualAllocEx(hProc, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) { CloseHandle(hProc); return; }
    WriteProcessMemory(hProc, remoteMem, dllPath.c_str(), size, nullptr);

    auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, loadLib, remoteMem, 0, nullptr);
    if (hThread)
    {
        WaitForSingleObject(hThread, 5000);
        DWORD exitCode = 0;
        GetExitCodeThread(hThread, &exitCode);
        LogMsg(L"LoadLibraryW exit code: " + std::to_wstring(exitCode));
        CloseHandle(hThread);
    }
    VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProc);

    MessageBoxW(nullptr, L"Hook yuborildi!\nLog: C:\\Users\\Public\\glass_log.txt", L"GlassTaskbar", MB_OK | MB_ICONINFORMATION);
}

void RestartExplorer()
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    wchar_t cmdKill[] = L"taskkill.exe /f /im explorer.exe";
    if (CreateProcessW(nullptr, cmdKill, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
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

#endif

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <oleauto.h>
#include <tlhelp32.h>
#include <xamlom.h>
#include <inspectable.h>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")

// ==========================================
// 1-QISM: XAML VISUAL TREE TRACKER (WINUI 3 + UWP)
// ==========================================

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
        if (mutationType == Add && element.Handle != 0 && m_service)
        {
            std::wstring name = element.Name ? element.Name : L"";

            // Faqat aniq nomlangan Taskbar fon elementlarini ushlash
            if (name == L"BackgroundFill" ||
                name == L"BackgroundStroke" ||
                name == L"TaskbarBackground" ||
                name == L"SystemTrayFrameBackground")
            {
                BSTR bstrType = SysAllocString(L"Windows.Foundation.Double");
                BSTR bstrVal = SysAllocString(L"0");
                InstanceHandle valHandle = 0;

                if (SUCCEEDED(m_service->CreateInstance(bstrType, bstrVal, &valHandle)) && valHandle != 0)
                {
                    unsigned int sourceCount = 0;
                    PropertyChainSource* sources = nullptr;
                    unsigned int propCount = 0;
                    PropertyChainValue* props = nullptr;

                    if (SUCCEEDED(m_service->GetPropertyValuesChain(element.Handle, &sourceCount, &sources, &propCount, &props)))
                    {
                        for (unsigned int i = 0; i < propCount; i++)
                        {
                            if (props[i].PropertyName && wcscmp(props[i].PropertyName, L"Opacity") == 0)
                            {
                                // XAMLOM rasmiy metodi: SetProperty (PropertyChainValue.Index bilan)
                                m_service->SetProperty(element.Handle, valHandle, props[i].Index);
                                break;
                            }
                        }
                        if (sources) CoTaskMemFree(sources);
                        if (props) CoTaskMemFree(props);
                    }
                }

                if (bstrType) SysFreeString(bstrType);
                if (bstrVal) SysFreeString(bstrVal);
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) override
    {
        return S_OK;
    }
};

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
        if (m_site) m_site->Release();
        m_site = pUnkSite;
        if (m_site)
        {
            m_site->AddRef();
            IVisualTreeService3* service = nullptr;
            if (SUCCEEDED(m_site->QueryInterface(__uuidof(IVisualTreeService3), (void**)&service)) && service)
            {
                auto tracker = new VisualTreeTracker();
                tracker->m_service = service;
                service->AdviseVisualTreeChange(tracker);
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
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        auto site = new HookSite();
        HRESULT hr = site->QueryInterface(riid, ppvObject);
        site->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    CLSID clsid = { 0x361c3466, 0x8573, 0x4e2a, { 0xb1, 0xb2, 0x9d, 0x1c, 0x7f, 0x4d, 0x8a, 0x11 } };
    if (rclsid == clsid)
    {
        auto factory = new HookFactory();
        HRESULT hr = factory->QueryInterface(riid, ppv);
        factory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

void StartXamlHookInExplorer()
{
    wchar_t modPath[MAX_PATH];
    HMODULE hMod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&StartXamlHookInExplorer), &hMod);
    GetModuleFileNameW(hMod, modPath, MAX_PATH);

    CLSID clsid = { 0x361c3466, 0x8573, 0x4e2a, { 0xb1, 0xb2, 0x9d, 0x1c, 0x7f, 0x4d, 0x8a, 0x11 } };
    using pfnInit = HRESULT(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID, LPCWSTR);

    HMODULE mux = GetModuleHandleW(L"Microsoft.UI.Xaml.dll");
    if (mux)
    {
        auto initFunc = (pfnInit)GetProcAddress(mux, "InitializeXamlDiagnosticsEx");
        if (initFunc) initFunc(L"VisualDiagConnection1", GetCurrentProcessId(), L"", modPath, clsid, L"");
    }

    HMODULE wux = GetModuleHandleW(L"Windows.UI.Xaml.dll");
    if (wux)
    {
        auto initFunc = (pfnInit)GetProcAddress(wux, "InitializeXamlDiagnosticsEx");
        if (initFunc) initFunc(L"VisualDiagConnection2", GetCurrentProcessId(), L"", modPath, clsid, L"");
    }
}

// ==========================================
// 2-QISM: BOSHQARUV UI VA INJECTOR
// ==========================================

void ApplyZeroOpacity()
{
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

        auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, loadLib, remoteMem, 0, nullptr);
        if (hThread)
        {
            WaitForSingleObject(hThread, 2000);
            CloseHandle(hThread);
        }
        CloseHandle(hProc);
        MessageBoxW(nullptr, L"Xavfsiz XAML Hook muvaffaqiyatli yuborildi!", L"GlassTaskbar", MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        MessageBoxW(nullptr, L"Dasturni Administrator sifatida ishga tushiring!", L"Xatolik", MB_OK | MB_ICONERROR);
    }
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
                StartXamlHookInExplorer();
                return 0;
            }, nullptr, 0, nullptr);
        }
    }
    return TRUE;
}

// ============================================================
// GlassHook.dll - Faqat DLL qismi
// Windows 11 Taskbar orqa fonini olib tashlaydigan hook
// ============================================================
// BUILD:
//  Visual Studio da DLL loyiha ochasan, shu faylni qo'shasan
//  x64 / Release da build qilasan -> GlassHook.dll chiqadi
//  GlassHook.dll va GlassTaskbar.exe ni BITTA papkaga qo'yasan
// LOG: C:\Users\Public\glass_log.txt
// ============================================================
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <xamlom.h>
#include <inspectable.h>
#include <string>
#include <fstream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

void LogMsg(const std::wstring& msg)
{
    std::wofstream f(L"C:\\Users\\Public\\glass_log.txt", std::ios::app);
    if (f.is_open()) f << msg << L"\n";
}

static const CLSID CLSID_GlassHook = { 0x361c3466, 0x8573, 0x4e2a, { 0xb1, 0xb2, 0x9d, 0x1c, 0x7f, 0x4d, 0x8a, 0x11 } };

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
    TrySetProperty(service, elem, L"Visibility", L"Windows.UI.Xaml.Visibility", L"Collapsed");
    TrySetProperty(service, elem, L"Fill", L"Windows.UI.Xaml.Media.SolidColorBrush", L"#00000000");
    TrySetProperty(service, elem, L"Background", L"Windows.UI.Xaml.Media.SolidColorBrush", L"#00000000");
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

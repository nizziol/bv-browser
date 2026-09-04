#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"

#include <windows.h>

class NizzHandler : public CefClient,
    public CefLifeSpanHandler
{
public:
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override
    {
        return this;
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override
    {
        CefQuitMessageLoop();
    }

private:
    IMPLEMENT_REFCOUNTING(NizzHandler);
};


class NizzApp : public CefApp,
    public CefBrowserProcessHandler
{
public:
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override
    {
        return this;
    }

    void OnContextInitialized() override
    {
        CefWindowInfo window_info;

        window_info.SetAsPopup(nullptr, "Nizz Browser");

        CefBrowserSettings browser_settings;

        CefRefPtr<NizzHandler> handler = new NizzHandler();

        CefBrowserHost::CreateBrowser(
            window_info,
            handler,
            "https://www.google.com",
            browser_settings,
            nullptr,
            nullptr
        );
    }

private:
    IMPLEMENT_REFCOUNTING(NizzApp);
};


int APIENTRY wWinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    wchar_t* lpCmdLine,
    int nCmdShow)
{
    CefMainArgs main_args(hInstance);

    CefRefPtr<NizzApp> app = new NizzApp();

    int exit_code = CefExecuteProcess(
        main_args,
        app,
        nullptr
    );

    if (exit_code >= 0)
    {
        return exit_code;
    }

    CefSettings settings;

    settings.no_sandbox = true;

    if (!CefInitialize(
        main_args,
        settings,
        app,
        nullptr))
    {
        return CefGetExitCode();
    }

    CefRunMessageLoop();

    CefShutdown();

    return 0;
}
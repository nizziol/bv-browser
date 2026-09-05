#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_display_handler.h"

#include <windows.h>
#include <dwmapi.h>
#include <string>

#pragma comment(lib, "dwmapi.lib")

HWND g_main_window = nullptr;
HWND g_address_bar = nullptr;

CefRefPtr<CefBrowser> g_browser;

WNDPROC g_old_address_proc = nullptr;

bool g_preserve_search_text = false;
std::wstring g_last_search_text;

const COLORREF NIZZ_COLOR_BACKGROUND = RGB(250, 250, 250);
const COLORREF NIZZ_COLOR_GLASS = RGB(245, 247, 250);
const COLORREF NIZZ_COLOR_GLASS_BORDER = RGB(225, 228, 233);
const COLORREF NIZZ_COLOR_TEXT = RGB(35, 38, 43);
const COLORREF NIZZ_COLOR_ICON = RGB(70, 75, 82);

#define ID_ADDRESS 1004


// ============================================================
// NAWIGACJA
// ============================================================

void NavigateFromAddressBar()
{
    if (!g_browser || !g_address_bar)
        return;

    wchar_t buffer[2048];

    GetWindowTextW(
        g_address_bar,
        buffer,
        2048
    );

    std::wstring address = buffer;

    while (!address.empty() && address.front() == L' ')
        address.erase(address.begin());

    while (!address.empty() && address.back() == L' ')
        address.pop_back();

    if (address.empty())
        return;


    // --------------------------------------------------------
    // NORMALNY URL
    // --------------------------------------------------------

    if (address.find(L"://") != std::wstring::npos)
    {
        g_preserve_search_text = false;

        g_browser
            ->GetMainFrame()
            ->LoadURL(address);

        return;
    }


    // --------------------------------------------------------
    // np. youtube.com
    // --------------------------------------------------------

    if (
        address.find(L".") != std::wstring::npos &&
        address.find(L" ") == std::wstring::npos
        )
    {
        std::wstring url =
            L"https://" + address;

        g_preserve_search_text = false;

        g_browser
            ->GetMainFrame()
            ->LoadURL(url);

        return;
    }


    // --------------------------------------------------------
    // WYSZUKIWANIE GOOGLE
    // --------------------------------------------------------

    g_last_search_text = address;
    g_preserve_search_text = true;

    std::wstring search =
        L"https://www.google.com/search?q=";

    for (wchar_t c : address)
    {
        if (c == L' ')
        {
            search += L"+";
        }
        else
        {
            search += c;
        }
    }

    g_browser
        ->GetMainFrame()
        ->LoadURL(search);
}


// ============================================================
// ADDRESS BAR
// ============================================================

LRESULT CALLBACK AddressBarProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
    case WM_GETDLGCODE:

        return DLGC_WANTALLKEYS |
            DLGC_WANTCHARS;


    case WM_KEYDOWN:

        if (wParam == VK_RETURN)
        {
            NavigateFromAddressBar();

            // Bardzo wa¿ne:
            // nie pozwala Windowsowi obs³u¿yæ Entera
            // i wydaæ systemowego "beep".

            return 0;
        }

        break;


    case WM_CHAR:

        if (wParam == VK_RETURN)
        {
            // Druga warstwa zabezpieczenia
            // przed dŸwiêkiem Windows.

            return 0;
        }

        break;
    }

    return CallWindowProcW(
        g_old_address_proc,
        hwnd,
        msg,
        wParam,
        lParam
    );
}


// ============================================================
// CEF HANDLER
// ============================================================

class NizzHandler :
    public CefClient,
    public CefLifeSpanHandler,
    public CefDisplayHandler
{
public:

    // --------------------------------------------------------
    // LIFE SPAN
    // --------------------------------------------------------

    CefRefPtr<CefLifeSpanHandler>
        GetLifeSpanHandler() override
    {
        return this;
    }


    // --------------------------------------------------------
    // DISPLAY
    // --------------------------------------------------------

    CefRefPtr<CefDisplayHandler>
        GetDisplayHandler() override
    {
        return this;
    }


    // --------------------------------------------------------
    // BROWSER CREATED
    // --------------------------------------------------------

    void OnAfterCreated(
        CefRefPtr<CefBrowser> browser) override
    {
        g_browser = browser;
    }


    // --------------------------------------------------------
    // BROWSER CLOSED
    // --------------------------------------------------------

    void OnBeforeClose(
        CefRefPtr<CefBrowser> browser) override
    {
        g_browser = nullptr;
    }


    // --------------------------------------------------------
    // ZMIANA ADRESU
    // --------------------------------------------------------

    void OnAddressChange(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        const CefString& url) override
    {
        if (!frame->IsMain())
            return;

        std::string url_utf8 = url.ToString();

        std::wstring wide_url(
            url_utf8.begin(),
            url_utf8.end()
        );


        // ----------------------------------------------------
        // JEŒLI TO WYNIK WYSZUKIWANIA GOOGLE
        // ZOSTAWIAMY TO, CO U¯YTKOWNIK WPISA£
        // ----------------------------------------------------

        if (
            g_preserve_search_text &&
            wide_url.find(
                L"https://www.google.com/search?"
            ) == 0
            )
        {
            SetWindowTextW(
                g_address_bar,
                g_last_search_text.c_str()
            );

            return;
        }


        // ----------------------------------------------------
        // NORMALNA STRONA
        // POKAZUJEMY PRAWDZIWY URL
        // ----------------------------------------------------

        g_preserve_search_text = false;

        SetWindowTextW(
            g_address_bar,
            wide_url.c_str()
        );
    }


private:

    IMPLEMENT_REFCOUNTING(NizzHandler);
};


// ============================================================
// CEF APP
// ============================================================

class NizzApp :
    public CefApp,
    public CefBrowserProcessHandler
{
public:

    CefRefPtr<CefBrowserProcessHandler>
        GetBrowserProcessHandler() override
    {
        return this;
    }


    void OnContextInitialized() override
    {
        CefWindowInfo window_info;

        RECT rect;

        GetClientRect(
            g_main_window,
            &rect
        );

        int width =
            rect.right - rect.left;

        int height =
            rect.bottom - rect.top;


        CefRect browser_rect(
            0,
            82,
            width,
            height - 82
        );


        window_info.SetAsChild(
            g_main_window,
            browser_rect
        );


        CefBrowserSettings browser_settings;

        CefRefPtr<NizzHandler> handler =
            new NizzHandler();


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


// ============================================================
// GLASS
// ============================================================

void DrawGlass(
    HDC hdc,
    RECT rect,
    int radius)
{
    HBRUSH brush =
        CreateSolidBrush(
            NIZZ_COLOR_GLASS
        );

    HPEN pen =
        CreatePen(
            PS_SOLID,
            1,
            NIZZ_COLOR_GLASS_BORDER
        );


    HGDIOBJ old_brush =
        SelectObject(
            hdc,
            brush
        );

    HGDIOBJ old_pen =
        SelectObject(
            hdc,
            pen
        );


    RoundRect(
        hdc,
        rect.left,
        rect.top,
        rect.right,
        rect.bottom,
        radius,
        radius
    );


    SelectObject(
        hdc,
        old_brush
    );

    SelectObject(
        hdc,
        old_pen
    );


    DeleteObject(brush);
    DeleteObject(pen);
}


// ============================================================
// WINDOW PROC
// ============================================================

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
        // ========================================================
        // CREATE
        // ========================================================

    case WM_CREATE:
    {
        DWM_WINDOW_CORNER_PREFERENCE corner =
            DWMWCP_ROUND;


        DwmSetWindowAttribute(
            hwnd,
            DWMWA_WINDOW_CORNER_PREFERENCE,
            &corner,
            sizeof(corner)
        );


        // ----------------------------------------------------
        // ADDRESS BAR
        // ----------------------------------------------------

        g_address_bar =
            CreateWindowExW(
                0,
                L"EDIT",
                L"",
                WS_CHILD |
                WS_VISIBLE |
                ES_AUTOHSCROLL,
                215,
                25,
                900,
                34,
                hwnd,
                (HMENU)ID_ADDRESS,
                GetModuleHandle(nullptr),
                nullptr
            );


        // ----------------------------------------------------
        // FONT
        // ----------------------------------------------------

        HFONT font =
            CreateFontW(
                15,
                0,
                0,
                0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH |
                FF_SWISS,
                L"Segoe UI"
            );


        SendMessageW(
            g_address_bar,
            WM_SETFONT,
            (WPARAM)font,
            TRUE
        );


        // ----------------------------------------------------
        // USUWAMY STANDARDOW¥ RAMKÊ
        // ----------------------------------------------------

        LONG style =
            GetWindowLong(
                g_address_bar,
                GWL_STYLE
            );


        style &= ~WS_BORDER;


        SetWindowLong(
            g_address_bar,
            GWL_STYLE,
            style
        );


        // ----------------------------------------------------
        // SUBCLASS
        // ----------------------------------------------------

        g_old_address_proc =
            (WNDPROC)SetWindowLongPtrW(
                g_address_bar,
                GWLP_WNDPROC,
                (LONG_PTR)AddressBarProc
            );


        return 0;
    }


    // ========================================================
    // PAINT
    // ========================================================

    case WM_PAINT:
    {
        PAINTSTRUCT ps;

        HDC hdc =
            BeginPaint(
                hwnd,
                &ps
            );


        RECT client;

        GetClientRect(
            hwnd,
            &client
        );


        // ----------------------------------------------------
        // BACKGROUND
        // ----------------------------------------------------

        HBRUSH background =
            CreateSolidBrush(
                NIZZ_COLOR_BACKGROUND
            );


        FillRect(
            hdc,
            &client,
            background
        );


        DeleteObject(background);


        // ----------------------------------------------------
        // G£ÓWNY TOOLBAR
        // ----------------------------------------------------

        RECT toolbar = {
            12,
            10,
            client.right - 12,
            72
        };


        DrawGlass(
            hdc,
            toolbar,
            25
        );


        // ----------------------------------------------------
        // BACK + FORWARD
        // ----------------------------------------------------

        RECT navigation = {
            24,
            21,
            124,
            61
        };


        DrawGlass(
            hdc,
            navigation,
            22
        );


        // ----------------------------------------------------
        // RELOAD
        // ----------------------------------------------------

        RECT reload = {
            134,
            21,
            178,
            61
        };


        DrawGlass(
            hdc,
            reload,
            22
        );


        // ----------------------------------------------------
        // ADDRESS BAR GLASS
        // ----------------------------------------------------

        RECT address = {
            192,
            21,
            client.right - 24,
            61
        };


        DrawGlass(
            hdc,
            address,
            22
        );


        // ----------------------------------------------------
        // IKONY
        // ----------------------------------------------------

        HFONT icon_font =
            CreateFontW(
                17,
                0,
                0,
                0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH |
                FF_SWISS,
                L"Segoe UI"
            );


        HFONT old_font =
            (HFONT)SelectObject(
                hdc,
                icon_font
            );


        SetBkMode(
            hdc,
            TRANSPARENT
        );


        SetTextColor(
            hdc,
            NIZZ_COLOR_ICON
        );


        // ----------------------------------------------------
        // BACK
        // ----------------------------------------------------

        RECT back = {
            29,
            21,
            74,
            61
        };


        DrawTextW(
            hdc,
            L"\x2190",
            -1,
            &back,
            DT_CENTER |
            DT_VCENTER |
            DT_SINGLELINE
        );


        // ----------------------------------------------------
        // FORWARD
        // ----------------------------------------------------

        RECT forward = {
            74,
            21,
            119,
            61
        };


        DrawTextW(
            hdc,
            L"\x2192",
            -1,
            &forward,
            DT_CENTER |
            DT_VCENTER |
            DT_SINGLELINE
        );


        // ----------------------------------------------------
        // RELOAD
        // ----------------------------------------------------

        RECT reload_text = {
            134,
            21,
            178,
            61
        };


        DrawTextW(
            hdc,
            L"\x21BB",
            -1,
            &reload_text,
            DT_CENTER |
            DT_VCENTER |
            DT_SINGLELINE
        );


        // ----------------------------------------------------
        // TYTU£
        // ----------------------------------------------------

        HFONT title_font =
            CreateFontW(
                14,
                0,
                0,
                0,
                FW_SEMIBOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH |
                FF_SWISS,
                L"Segoe UI"
            );


        SelectObject(
            hdc,
            title_font
        );


        SetTextColor(
            hdc,
            NIZZ_COLOR_TEXT
        );


        RECT title = {
            18,
            78,
            200,
            105
        };


        DrawTextW(
            hdc,
            L"Nizz Browser",
            -1,
            &title,
            DT_LEFT |
            DT_VCENTER |
            DT_SINGLELINE
        );


        SelectObject(
            hdc,
            old_font
        );


        DeleteObject(icon_font);
        DeleteObject(title_font);


        EndPaint(
            hwnd,
            &ps
        );


        return 0;
    }


    // ========================================================
    // MOUSE
    // ========================================================

    case WM_LBUTTONUP:
    {
        int x =
            LOWORD(lParam);

        int y =
            HIWORD(lParam);


        // ----------------------------------------------------
        // BACK
        // ----------------------------------------------------

        if (
            x >= 24 &&
            x <= 74 &&
            y >= 21 &&
            y <= 61)
        {
            if (
                g_browser &&
                g_browser->CanGoBack())
            {
                g_browser->GoBack();
            }

            return 0;
        }


        // ----------------------------------------------------
        // FORWARD
        // ----------------------------------------------------

        if (
            x >= 74 &&
            x <= 124 &&
            y >= 21 &&
            y <= 61)
        {
            if (
                g_browser &&
                g_browser->CanGoForward())
            {
                g_browser->GoForward();
            }

            return 0;
        }


        // ----------------------------------------------------
        // RELOAD
        // ----------------------------------------------------

        if (
            x >= 134 &&
            x <= 178 &&
            y >= 21 &&
            y <= 61)
        {
            if (g_browser)
            {
                g_browser->Reload();
            }

            return 0;
        }


        break;
    }


    // ========================================================
    // RESIZE
    // ========================================================

    case WM_SIZE:
    {
        int width =
            LOWORD(lParam);

        int height =
            HIWORD(lParam);


        // ----------------------------------------------------
        // CEF
        // ----------------------------------------------------

        if (g_browser)
        {
            HWND browser_hwnd =
                g_browser
                ->GetHost()
                ->GetWindowHandle();


            if (browser_hwnd)
            {
                SetWindowPos(
                    browser_hwnd,
                    nullptr,
                    0,
                    82,
                    width,
                    height - 82,
                    SWP_NOZORDER
                );
            }
        }


        // ----------------------------------------------------
        // ADDRESS BAR
        // ----------------------------------------------------

        if (g_address_bar)
        {
            SetWindowPos(
                g_address_bar,
                nullptr,
                215,
                24,
                width - 239,
                36,
                SWP_NOZORDER
            );
        }


        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );


        return 0;
    }


    // ========================================================
    // CLOSE
    // ========================================================

    case WM_CLOSE:
    {
        // Najwa¿niejsza zmiana:
        // nie czekamy na drugie klikniêcie X.

        if (g_browser)
        {
            g_browser
                ->GetHost()
                ->CloseBrowser(true);
        }


        DestroyWindow(hwnd);

        return 0;
    }


    // ========================================================
    // DESTROY
    // ========================================================

    case WM_DESTROY:
    {
        PostQuitMessage(0);

        return 0;
    }
    }


    return DefWindowProc(
        hwnd,
        msg,
        wParam,
        lParam
    );
}


// ============================================================
// WINMAIN
// ============================================================

int APIENTRY wWinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    wchar_t* lpCmdLine,
    int nCmdShow)
{
    CefMainArgs main_args(
        hInstance
    );


    CefRefPtr<NizzApp> app =
        new NizzApp();


    // --------------------------------------------------------
    // CEF SUBPROCESS
    // --------------------------------------------------------

    int exit_code =
        CefExecuteProcess(
            main_args,
            app,
            nullptr
        );


    if (exit_code >= 0)
    {
        return exit_code;
    }


    // --------------------------------------------------------
    // WINDOW CLASS
    // --------------------------------------------------------

    WNDCLASSEXW wc{};

    wc.cbSize =
        sizeof(WNDCLASSEXW);

    wc.lpfnWndProc =
        WindowProc;

    wc.hInstance =
        hInstance;

    wc.hCursor =
        LoadCursor(
            nullptr,
            IDC_ARROW
        );


    wc.hbrBackground =
        CreateSolidBrush(
            NIZZ_COLOR_BACKGROUND
        );


    wc.lpszClassName =
        L"NizzBrowserWindow";


    RegisterClassExW(
        &wc
    );


    // --------------------------------------------------------
    // MAIN WINDOW
    // --------------------------------------------------------

    g_main_window =
        CreateWindowExW(
            0,
            L"NizzBrowserWindow",
            L"Nizz Browser",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1200,
            760,
            nullptr,
            nullptr,
            hInstance,
            nullptr
        );


    if (!g_main_window)
    {
        return 1;
    }


    ShowWindow(
        g_main_window,
        SW_SHOW
    );


    UpdateWindow(
        g_main_window
    );


    // --------------------------------------------------------
    // CEF SETTINGS
    // --------------------------------------------------------

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


    // --------------------------------------------------------
    // MESSAGE LOOP
    // --------------------------------------------------------

    CefRunMessageLoop();


    // --------------------------------------------------------
    // SHUTDOWN
    // --------------------------------------------------------

    CefShutdown();


    return 0;
}
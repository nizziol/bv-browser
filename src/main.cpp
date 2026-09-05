// main.cpp
//
// Wlasna przegladarka na bazie CEF (Chromium Embedded Framework).
//
// Architektura:
//   - Jedno BEZRAMKOWE okno Win32 (wlasny pasek tytulu zamiast natywnego).
//   - "toolbar" - gorny CEF-owy widok obejmujacy DWA rzedy: pasek kart
//     (zakladki) i pasek nawigacji (wstecz/dalej/odswierz/adres/ustawienia
//     + przyciski minimalizuj/maksymalizuj/zamknij).
//   - Wiele "content" browserow CEF - po jednym na kazda karte. Widoczny
//     jest tylko ten nalezacy do aktywnej karty (reszta ukryta ShowWindow).
//
// Przeciaganie okna przez puste miejsce paska kart realizowane jest
// klasycznym trikiem Win32 (uzywanym m.in. przez Electron): JS w
// toolbar.html lapie mousedown na pustym tle paska kart i wysyla
// cefQuery("win:drag"), a C++ odpowiada ReleaseCapture() +
// SendMessage(WM_NCLBUTTONDOWN, HTCAPTION) - system zaczyna traktowac
// to jak klikniecie w pasek tytulu. Nie polegamy na API CEF specyficznym
// dla wersji (draggable regions), bo to bywa niestabilne miedzy wydaniami.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
// windowsx.h definiuje wlasne makra GetFirstChild/GetNextSibling/GetPrevSibling
// (skroty do nawigacji po oknach), ktore koliduja z metodami o tych samych
// nazwach w API CEF (np. CefDOMNode). Usuwamy je zaraz po wlaczeniu naglowka -
// i tak ich nie uzywamy.
#undef GetFirstChild
#undef GetNextSibling
#undef GetPrevSibling
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// Ktorys z posrednich naglowkow Windows (accessibility/COM) definiuje
// makra GetNextSibling/GetFirstChild, ktore koliduja z metodami
// CefDOMNode w cef_dom.h (dolaczanym przez cef_render_process_handler.h
// - CefRenderProcessHandler::OnFocusedNodeChanged bierze CefDOMNode).
// Usuwamy je zanim jakikolwiek naglowek CEF je zobaczy.
#ifdef GetNextSibling
#undef GetNextSibling
#endif
#ifdef GetFirstChild
#undef GetFirstChild
#endif

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_render_process_handler.h"
#include "include/wrapper/cef_message_router.h"

#if defined(CEF_USE_SANDBOX)
#include "include/cef_sandbox_win.h"
#pragma comment(lib, "cef_sandbox.lib")
#endif

namespace {

// ---------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------
constexpr int kTabStripHeight = 40;   // gorny rzad - zakladki
constexpr int kNavHeight = 40;        // dolny rzad - nawigacja/adres
constexpr int kToolbarHeight = kTabStripHeight + kNavHeight;  // 80px
constexpr int kResizeBorder = 6;      // margines lapania krawedzi do resize

// ---------------------------------------------------------------------
// Stan globalny
// ---------------------------------------------------------------------
struct Tab {
  int id = 0;
  CefRefPtr<CefBrowser> browser;  // moze byc null tuz po utworzeniu (async)
};

std::vector<Tab> g_tabs;
int g_active_tab_id = -1;

CefRefPtr<CefBrowser> g_toolbar_browser;
HWND g_main_hwnd = nullptr;

CefRefPtr<CefBrowser> GetActiveBrowser() {
  for (const auto& tab : g_tabs) {
    if (tab.id == g_active_tab_id) return tab.browser;
  }
  return nullptr;
}

void ActivateTab(int id) {
  for (auto& tab : g_tabs) {
    if (!tab.browser) continue;
    HWND h = tab.browser->GetHost()->GetWindowHandle();
    if (!h) continue;
    ShowWindow(h, (tab.id == id) ? SW_SHOW : SW_HIDE);
  }
  g_active_tab_id = id;

  // Wymuszamy przerysowanie calego okna (w tym paska) - bez tego CEF
  // czasem nie odswieza wszystkich braci po zmianie widocznosci jednego
  // z okien-dzieci, co objawia sie jako "zniknieciem" paska na chwile.
  if (g_main_hwnd) {
    RedrawWindow(g_main_hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
  }
}

// Bezpieczne wstrzykniecie stringa jako literal JS (cudzyslowy/backslashe).
std::string JsStringLiteral(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '\\' || c == '"') out += '\\';
    if (c == '\n' || c == '\r') continue;
    out += c;
  }
  return out;
}

void PushToToolbarJs(const std::string& script) {
  if (!g_toolbar_browser) return;
  CefRefPtr<CefFrame> frame = g_toolbar_browser->GetMainFrame();
  if (frame) frame->ExecuteJavaScript(script, frame->GetURL(), 0);
}

// ---------------------------------------------------------------------
// CefApp - proces glowny ORAZ proces renderera (to samo .exe jest
// uruchamiane ponownie przez CEF dla kazdego typu podprocesu).
//
// WAZNE: window.cefQuery w toolbar.html dziala TYLKO jesli po stronie
// renderera istnieje CefMessageRouterRendererSide - bez tego JS nigdy
// nie zobaczy funkcji cefQuery.
// ---------------------------------------------------------------------
class GlassApp : public CefApp,
                  public CefBrowserProcessHandler,
                  public CefRenderProcessHandler {
 public:
  GlassApp() = default;

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }

  void OnWebKitInitialized() override {
    CefMessageRouterConfig config;
    renderer_router_ = CefMessageRouterRendererSide::Create(config);
  }

  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override {
    if (renderer_router_) renderer_router_->OnContextCreated(browser, frame, context);
  }

  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override {
    if (renderer_router_) renderer_router_->OnContextReleased(browser, frame, context);
  }

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefProcessId source_process,
                                 CefRefPtr<CefProcessMessage> message) override {
    return renderer_router_ &&
           renderer_router_->OnProcessMessageReceived(browser, frame,
                                                       source_process, message);
  }

 private:
  CefRefPtr<CefMessageRouterRendererSide> renderer_router_;

  IMPLEMENT_REFCOUNTING(GlassApp);
};

class GlassClient;
void CreateContentBrowser(int tab_id);

// ---------------------------------------------------------------------
// Handler zapytan JS -> C++ z paska (window.cefQuery w toolbar.html).
// ---------------------------------------------------------------------
class ToolbarQueryHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override {
    const std::string req = request.ToString();

    // --- akcje nawigacji dzialaja na AKTYWNEJ karcie ---
    if (req == "action:back" || req == "action:forward" ||
        req == "action:reload" || req == "action:stop" ||
        req.rfind("navigate:", 0) == 0) {
      CefRefPtr<CefBrowser> active = GetActiveBrowser();
      if (!active) {
        callback->Failure(0, "brak aktywnej karty");
        return true;
      }
      if (req == "action:back") {
        if (active->CanGoBack()) active->GoBack();
      } else if (req == "action:forward") {
        if (active->CanGoForward()) active->GoForward();
      } else if (req == "action:reload") {
        active->Reload();
      } else if (req == "action:stop") {
        active->StopLoad();
      } else {
        std::string target = req.substr(9);
        const bool looks_like_url = target.find('.') != std::string::npos ||
                                     target.find("://") != std::string::npos ||
                                     target.rfind("localhost", 0) == 0;
        if (!looks_like_url) {
          target = "https://www.google.com/search?q=" + target;
        } else if (target.find("://") == std::string::npos) {
          target = "https://" + target;
        }
        active->GetMainFrame()->LoadURL(target);
      }
      callback->Success("ok");
      return true;
    }

    // --- karty ---
    if (req.rfind("tab:new:", 0) == 0) {
      const int id = std::stoi(req.substr(8));
      CreateContentBrowser(id);
      callback->Success("ok");
      return true;
    }
    if (req.rfind("tab:activate:", 0) == 0) {
      const int id = std::stoi(req.substr(13));
      ActivateTab(id);
      callback->Success("ok");
      return true;
    }
    if (req.rfind("tab:close:", 0) == 0) {
      const int id = std::stoi(req.substr(10));
      for (auto& tab : g_tabs) {
        if (tab.id == id && tab.browser) {
          tab.browser->GetHost()->CloseBrowser(false);
          break;
        }
      }
      callback->Success("ok");
      return true;
    }

    // --- sterowanie oknem (przyciski w pasku kart) ---
    if (req == "win:minimize") {
      ShowWindow(g_main_hwnd, SW_MINIMIZE);
      callback->Success("ok");
      return true;
    }
    if (req == "win:maximize") {
      ShowWindow(g_main_hwnd, IsZoomed(g_main_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
      callback->Success("ok");
      return true;
    }
    if (req == "win:close") {
      PostMessage(g_main_hwnd, WM_CLOSE, 0, 0);
      callback->Success("ok");
      return true;
    }
    if (req == "win:drag") {
      // Klasyczny trik Win32 na przeciaganie bezramkowego okna: mowimy
      // systemowi "to bylo tak, jakby uzytkownik kliknal pasek tytulu".
      // Dziala niezaleznie od tego, czy CEF wspiera draggable regions
      // w tej konkretnej wersji (nie polegamy na tym API).
      if (!IsZoomed(g_main_hwnd)) {
        ReleaseCapture();
        SendMessage(g_main_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
      }
      callback->Success("ok");
      return true;
    }

    callback->Success("ok");
    return true;
  }

  // UWAGA: CefMessageRouterBrowserSide::Handler NIE dziedziczy z
  // CefBaseRefCounted - to zwykla klasa, ktora AddHandler() trzyma jako
  // surowy wskaznik (nie CefRefPtr). Nie uzywamy tu IMPLEMENT_REFCOUNTING;
  // zywotnoscia obiektu zarzadza GlassClient.
};

// ---------------------------------------------------------------------
// CefClient - wspolny dla toolbar i wszystkich kart tresci.
// ---------------------------------------------------------------------
class GlassClient : public CefClient,
                     public CefLifeSpanHandler,
                     public CefDisplayHandler {
 public:
  explicit GlassClient(bool is_toolbar, int tab_id = -1)
      : is_toolbar_(is_toolbar), tab_id_(tab_id) {
    if (is_toolbar_) {
      query_handler_ = std::make_unique<ToolbarQueryHandler>();
      CefMessageRouterConfig config;
      message_router_ = CefMessageRouterBrowserSide::Create(config);
      message_router_->AddHandler(query_handler_.get(), false);
    }
  }

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefProcessId source_process,
                                 CefRefPtr<CefProcessMessage> message) override {
    if (message_router_ && message_router_->OnProcessMessageReceived(
                               browser, frame, source_process, message)) {
      return true;
    }
    return false;
  }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    if (is_toolbar_) {
      g_toolbar_browser = browser;
      return;
    }
    for (auto& tab : g_tabs) {
      if (tab.id == tab_id_) {
        tab.browser = browser;
        break;
      }
    }
    // Nowa karta od razu staje sie aktywna (tak jak w kazdej normalnej
    // przegladarce) - ukrywa poprzednio aktywna, pokazuje siebie.
    ActivateTab(tab_id_);
  }

  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    if (is_toolbar_) {
      g_toolbar_browser = nullptr;
      return;
    }
    const int closed_id = tab_id_;
    g_tabs.erase(std::remove_if(g_tabs.begin(), g_tabs.end(),
                                 [closed_id](const Tab& t) {
                                   return t.id == closed_id;
                                 }),
                 g_tabs.end());
    if (g_tabs.empty()) {
      // ostatnia karta zamknieta - koncz aplikacje
      CefQuitMessageLoop();
    } else if (g_active_tab_id == closed_id) {
      ActivateTab(g_tabs.back().id);
    }
  }

  bool DoClose(CefRefPtr<CefBrowser> browser) override { return false; }

  // --- CefDisplayHandler: przekazuje tytul/adres karty do paska (JS) ---
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                      const CefString& title) override {
    if (is_toolbar_) return;
    const std::string script =
        "window.__setTabTitle && window.__setTabTitle(" +
        std::to_string(tab_id_) + ", \"" + JsStringLiteral(title.ToString()) +
        "\");";
    PushToToolbarJs(script);
  }

  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        const CefString& url) override {
    if (is_toolbar_ || !frame->IsMain()) return;
    const std::string script =
        "window.__setTabAddress && window.__setTabAddress(" +
        std::to_string(tab_id_) + ", \"" + JsStringLiteral(url.ToString()) +
        "\");";
    PushToToolbarJs(script);
  }

 private:
  bool is_toolbar_;
  int tab_id_;
  CefRefPtr<CefMessageRouterBrowserSide> message_router_;
  std::unique_ptr<ToolbarQueryHandler> query_handler_;

  IMPLEMENT_REFCOUNTING(GlassClient);
};

void CreateContentBrowser(int tab_id) {
  RECT rect;
  GetClientRect(g_main_hwnd, &rect);

  CefWindowInfo window_info;
  window_info.SetAsChild(
      g_main_hwnd,
      CefRect(0, kToolbarHeight, rect.right, rect.bottom - kToolbarHeight));

  CefBrowserSettings browser_settings;
  CefRefPtr<GlassClient> client(new GlassClient(/*is_toolbar=*/false, tab_id));

  g_tabs.push_back(Tab{tab_id, nullptr});

  CefBrowserHost::CreateBrowser(window_info, client, "https://www.google.com",
                                 browser_settings, nullptr, nullptr);
}

// ---------------------------------------------------------------------
// Win32: bezramkowe okno + rozmieszczanie browserow + hit-testing.
// ---------------------------------------------------------------------
void ResizeBrowsers(HWND hwnd) {
  RECT rect;
  GetClientRect(hwnd, &rect);
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;

  if (g_toolbar_browser) {
    HWND h = g_toolbar_browser->GetHost()->GetWindowHandle();
    if (h) SetWindowPos(h, nullptr, 0, 0, width, kToolbarHeight, SWP_NOZORDER);
  }
  for (auto& tab : g_tabs) {
    if (!tab.browser) continue;
    HWND h = tab.browser->GetHost()->GetWindowHandle();
    if (!h) continue;
    SetWindowPos(h, nullptr, 0, kToolbarHeight, width,
                 height - kToolbarHeight, SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCCALCSIZE:
      if (wp) {
        // Usuwamy natywny obszar nie-klienta (pasek tytulu/ramki) -
        // caly prostokat okna staje sie obszarem klienta. WS_THICKFRAME
        // wciaz daje resize po krawedziach dzieki naszemu WM_NCHITTEST
        // ponizej.
        //
        // WAZNE: gdy okno jest ZMAKSYMALIZOWANE, Windows domyslnie
        // dokleja niewidzialny margines o szerokosci ramki resize poza
        // widoczny obszar ekranu (dlatego "wchodzi w krawedzie" i widac
        // urwane rogi/boki). Trzeba go recznie skompensowac, wcinajac
        // proponowany prostokat o ta sama wartosc.
        if (IsZoomed(hwnd)) {
          auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
          const int cx = GetSystemMetrics(SM_CXSIZEFRAME) +
                         GetSystemMetrics(SM_CXPADDEDBORDER);
          const int cy = GetSystemMetrics(SM_CYSIZEFRAME) +
                         GetSystemMetrics(SM_CXPADDEDBORDER);
          params->rgrc[0].left += cx;
          params->rgrc[0].top += cy;
          params->rgrc[0].right -= cx;
          params->rgrc[0].bottom -= cy;
        }
        return 0;
      }
      break;

    case WM_NCHITTEST: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      RECT rc;
      GetClientRect(hwnd, &rc);

      if (!IsZoomed(hwnd)) {
        const int b = kResizeBorder;
        const bool left = pt.x < b;
        const bool right = pt.x >= rc.right - b;
        const bool top = pt.y < b;
        const bool bottom = pt.y >= rc.bottom - b;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
      }

      return HTCLIENT;
    }

    case WM_GETMINMAXINFO: {
      // Bez tego zmaksymalizowane bezramkowe okno zasloni pasek zadan.
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
      HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
      MONITORINFO mi = {sizeof(mi)};
      if (GetMonitorInfo(mon, &mi)) {
        mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
        mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
        mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
        mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
      }
      return 0;
    }

    case WM_SIZE:
      ResizeBrowsers(hwnd);
      return 0;

    case WM_ERASEBKGND:
      return 1;

    case WM_CLOSE:
      if (g_toolbar_browser) g_toolbar_browser->GetHost()->CloseBrowser(false);
      for (auto& tab : g_tabs) {
        if (tab.browser) tab.browser->GetHost()->CloseBrowser(false);
      }
      return 0;

    case WM_DESTROY:
      CefQuitMessageLoop();
      return 0;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

std::wstring GetExeDir() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring full(path);
  const size_t pos = full.find_last_of(L"\\/");
  return full.substr(0, pos);
}

std::string BuildToolbarFileUrl() {
  const std::wstring dir = GetExeDir() + L"\\resources\\toolbar.html";
  std::string url = "file:///";
  for (wchar_t c : dir) {
    url += (c == L'\\') ? '/' : static_cast<char>(c);
  }
  return url;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
  // Per-monitor-v2 zamiast starego "system DPI aware" - CEF renderuje
  // zgodnie z tym trybem, wiec niespojnosc tutaj moze objawiac sie jako
  // "strona nachodzi o kilka pikseli na nasz pasek".
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  void* sandbox_info = nullptr;
#if defined(CEF_USE_SANDBOX)
  CefScopedSandboxInfo scoped_sandbox;
  sandbox_info = scoped_sandbox.sandbox_info();
#endif

  CefMainArgs main_args(hInstance);
  CefRefPtr<GlassApp> app(new GlassApp);

  const int exit_code = CefExecuteProcess(main_args, app.get(), sandbox_info);
  if (exit_code >= 0) {
    return exit_code;
  }

  CefSettings settings;
#if !defined(CEF_USE_SANDBOX)
  settings.no_sandbox = true;
#endif

  CefInitialize(main_args, settings, app.get(), sandbox_info);

  const wchar_t kClassName[] = L"GlassBrowserWindow";
  WNDCLASSW wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = kClassName;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassW(&wc);

  // WS_OVERLAPPEDWINDOW (WS_CAPTION|WS_THICKFRAME|WS_MINIMIZEBOX|
  // WS_MAXIMIZEBOX|WS_SYSMENU) zostaje - dzieki WM_NCCALCSIZE=0 jego
  // natywna ramka/pasek tytulu po prostu nigdy sie nie rysuje, ale
  // funkcje (resize, Alt+Spacja, grupowanie na pasku zadan) zostaja.
  g_main_hwnd = CreateWindowExW(
      0, kClassName, L"Glass Browser", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
      CW_USEDEFAULT, 1280, 800, nullptr, nullptr, hInstance, nullptr);

  // Delikatny natywny cien wokol bezramkowego okna (kosmetyka, opcjonalne).
  MARGINS margins = {1, 1, 1, 1};
  DwmExtendFrameIntoClientArea(g_main_hwnd, &margins);

  ShowWindow(g_main_hwnd, nCmdShow);
  UpdateWindow(g_main_hwnd);

  // -- pasek (karty + nawigacja) - wlasny glassmorphism UI --
  {
    CefWindowInfo window_info;
    RECT rect;
    GetClientRect(g_main_hwnd, &rect);
    window_info.SetAsChild(g_main_hwnd,
                            CefRect(0, 0, rect.right, kToolbarHeight));

    CefBrowserSettings browser_settings;
    CefRefPtr<GlassClient> toolbar_client(new GlassClient(/*is_toolbar=*/true));
    CefBrowserHost::CreateBrowser(window_info, toolbar_client,
                                   BuildToolbarFileUrl(), browser_settings,
                                   nullptr, nullptr);
  }

  // -- pierwsza karta tresci (musi miec id=1, zeby zgadzalo sie z JS) --
  CreateContentBrowser(1);

  CefRunMessageLoop();
  CefShutdown();
  return 0;
}

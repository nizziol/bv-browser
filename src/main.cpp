// main.cpp
//
// Szkielet wlasnej przegladarki na bazie CEF (Chromium Embedded Framework).
// Architektura: jedno okno Win32, w srodku DWA osadzone browsery CEF:
//   - "toolbar"  -> gorny pasek (glassmorphism, wlasny HTML/CSS/JS)
//   - "content"  -> reszta okna, tam wyswietlaja sie prawdziwe strony www
//
// Pasek komunikuje sie z C++ przez window.cefQuery (CefMessageRouter),
// wysylajac proste komunikaty tekstowe: "action:back", "action:forward",
// "action:reload", "action:stop", "navigate:<url-lub-fraza>".
//
// UWAGA: to jest SZKIELET do dalszej rozbudowy, nie testowalem kompilacji
// w tym srodowisku (brak tu binarek CEF i dostepu do sieci) - kod pisany
// scisle wg publicznego API CEF (include/cef_*.h, include/wrapper/*).
// Wymagania do zbudowania opisane w README.md.

// NOMINMAX musi byc zdefiniowany PRZED windows.h - inaczej makra min/max
// z Windows.h psuja kazde wywolanie std::numeric_limits<T>::max() w
// naglowkach CEF (objawia sie dziwnymi bledami skladni w cef_ref_counted.h
// i cef_types_wrappers.h).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <memory>
#include <string>

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

constexpr int kToolbarHeight = 64;  // wysokosc paska w pikselach

CefRefPtr<CefBrowser> g_content_browser;
CefRefPtr<CefBrowser> g_toolbar_browser;
HWND g_main_hwnd = nullptr;

// ---------------------------------------------------------------------
// CefApp - proces glowny ORAZ proces renderera (to samo .exe jest
// uruchamiane ponownie przez CEF dla kazdego typu podprocesu, z inna
// rola). GetBrowserProcessHandler() dziala w procesie glownym,
// GetRenderProcessHandler() w procesie renderera.
//
// WAZNE: window.cefQuery w toolbar.html dziala TYLKO jesli po stronie
// renderera istnieje CefMessageRouterRendererSide - bez tego JS nigdy
// nie zobaczy funkcji cefQuery (przyciski/enter "nic nie robia").
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

  // --- CefRenderProcessHandler ---
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
    if (!g_content_browser) {
      callback->Failure(0, "content browser jeszcze nie gotowy");
      return true;
    }

    const std::string req = request.ToString();

    if (req == "action:back") {
      if (g_content_browser->CanGoBack()) g_content_browser->GoBack();
    } else if (req == "action:forward") {
      if (g_content_browser->CanGoForward()) g_content_browser->GoForward();
    } else if (req == "action:reload") {
      g_content_browser->Reload();
    } else if (req == "action:stop") {
      g_content_browser->StopLoad();
    } else if (req.rfind("navigate:", 0) == 0) {
      std::string target = req.substr(9);

      // Bardzo prosta heurystyka: jesli to nie wyglada na adres,
      // traktujemy to jako fraze wyszukiwania.
      const bool looks_like_url = target.find('.') != std::string::npos ||
                                   target.find("://") != std::string::npos ||
                                   target.rfind("localhost", 0) == 0;

      if (!looks_like_url) {
        target = "https://www.google.com/search?q=" + target;
      } else if (target.find("://") == std::string::npos) {
        target = "https://" + target;
      }

      g_content_browser->GetMainFrame()->LoadURL(target);
    }

    callback->Success("ok");
    return true;
  }

  // UWAGA: CefMessageRouterBrowserSide::Handler NIE dziedziczy z
  // CefBaseRefCounted - to zwykla klasa, ktora AddHandler() trzyma jako
  // surowy wskaznik (nie CefRefPtr). Nie uzywamy tu IMPLEMENT_REFCOUNTING;
  // zywotnoscia obiektu zarzadza GlassClient (patrz jego konstruktor).
};

// ---------------------------------------------------------------------
// CefClient wspolny dla obu browserow (rozroznia je flaga is_toolbar_).
// ---------------------------------------------------------------------
class GlassClient : public CefClient,
                     public CefLifeSpanHandler,
                     public CefDisplayHandler {
 public:
  explicit GlassClient(bool is_toolbar) : is_toolbar_(is_toolbar) {
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
    } else {
      g_content_browser = browser;
    }
  }

  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    if (is_toolbar_) {
      g_toolbar_browser = nullptr;
    } else {
      g_content_browser = nullptr;
      // Uzywamy CefQuitMessageLoop(), NIE surowego PostQuitMessage(0) -
      // CefRunMessageLoop() ma wlasna petle komunikatow i to jest
      // oficjalny, udokumentowany sposob jej zatrzymania. To byl powod,
      // dla ktorego X na oknie nic nie robil.
      CefQuitMessageLoop();
    }
  }

  bool DoClose(CefRefPtr<CefBrowser> browser) override { return false; }

 private:
  bool is_toolbar_;
  CefRefPtr<CefMessageRouterBrowserSide> message_router_;
  std::unique_ptr<ToolbarQueryHandler> query_handler_;

  IMPLEMENT_REFCOUNTING(GlassClient);
};

// ---------------------------------------------------------------------
// Win32: okno-hosta + rozmieszczanie dwoch dzieci-browserow.
// ---------------------------------------------------------------------
void ResizeBrowsers(HWND hwnd) {
  RECT rect;
  GetClientRect(hwnd, &rect);
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;

  if (g_toolbar_browser) {
    HWND toolbar_hwnd = g_toolbar_browser->GetHost()->GetWindowHandle();
    SetWindowPos(toolbar_hwnd, nullptr, 0, 0, width, kToolbarHeight,
                 SWP_NOZORDER);
  }
  if (g_content_browser) {
    HWND content_hwnd = g_content_browser->GetHost()->GetWindowHandle();
    SetWindowPos(content_hwnd, nullptr, 0, kToolbarHeight, width,
                 height - kToolbarHeight, SWP_NOZORDER);
  }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE:
      ResizeBrowsers(hwnd);
      return 0;
    case WM_ERASEBKGND:
      return 1;  // unikamy migotania - browsery i tak zakrywaja cale okno
    case WM_CLOSE:
      if (g_toolbar_browser) g_toolbar_browser->GetHost()->CloseBrowser(false);
      if (g_content_browser) g_content_browser->GetHost()->CloseBrowser(false);
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
  // Ta wersja CEF nie eksportuje juz CefEnableHighDPISupport() (obsluga
  // DPI przeniesiona jest do manifestu aplikacji / API systemowego) -
  // uzywamy wiec bezposrednio Win32. SetProcessDPIAware daje "system DPI
  // aware" (prostsze i zawsze dostepne od Visty), a nie pelne per-monitor
  // v2 - do skeletonu wystarczy; mozna to podbic pozniej manifestem exe.
  SetProcessDPIAware();

  void* sandbox_info = nullptr;
#if defined(CEF_USE_SANDBOX)
  CefScopedSandboxInfo scoped_sandbox;
  sandbox_info = scoped_sandbox.sandbox_info();
#endif

  CefMainArgs main_args(hInstance);
  CefRefPtr<GlassApp> app(new GlassApp);

  // Procesy potomne CEF (renderer, GPU, itd.) wchodza tu i wychodza od razu.
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

  g_main_hwnd =
      CreateWindowExW(0, kClassName, L"Glass Browser", WS_OVERLAPPEDWINDOW,
                       CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800, nullptr,
                       nullptr, hInstance, nullptr);

  ShowWindow(g_main_hwnd, nCmdShow);
  UpdateWindow(g_main_hwnd);

  RECT rect;
  GetClientRect(g_main_hwnd, &rect);
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;

  // -- pasek (toolbar) - wlasny glassmorphism UI --
  {
    CefWindowInfo window_info;
    window_info.SetAsChild(g_main_hwnd, CefRect(0, 0, width, kToolbarHeight));

    CefBrowserSettings browser_settings;

    CefRefPtr<GlassClient> toolbar_client(new GlassClient(/*is_toolbar=*/true));
    CefBrowserHost::CreateBrowser(window_info, toolbar_client,
                                   BuildToolbarFileUrl(), browser_settings,
                                   nullptr, nullptr);
  }

  // -- content - prawdziwe strony www --
  {
    CefWindowInfo window_info;
    window_info.SetAsChild(
        g_main_hwnd, CefRect(0, kToolbarHeight, width, height - kToolbarHeight));

    CefBrowserSettings browser_settings;

    CefRefPtr<GlassClient> content_client(
        new GlassClient(/*is_toolbar=*/false));
    CefBrowserHost::CreateBrowser(window_info, content_client,
                                   "https://www.google.com", browser_settings,
                                   nullptr, nullptr);
  }

  CefRunMessageLoop();
  CefShutdown();
  return 0;
}

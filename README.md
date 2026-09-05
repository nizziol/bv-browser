# Glass Browser (szkielet)

Własna przeglądarka na bazie **CEF (Chromium Embedded Framework)** z paskiem
narzędzi w stylu glassmorphism (rozmyte szkło, delikatne animacje przycisków
przy najechaniu).

## Jak to jest zrobione

Jedno natywne okno Win32, w którym osadzone są **dwa niezależne browsery CEF**:

- **toolbar** — górny pasek (64px), ładuje `resources/toolbar.html`. Cały
  wygląd (blur, przezroczystość, animacje `translateY` na hover) to zwykły
  CSS. Przyciski wysyłają komunikaty do C++ przez `window.cefQuery`.
- **content** — reszta okna, tam ładują się prawdziwe strony (domyślnie
  google.com).

C++ (`main.cpp`) łapie te komunikaty (`action:back`, `action:forward`,
`action:reload`, `navigate:<adres>`) i wykonuje odpowiednią akcję na
browserze `content`.

**Ważna uwaga o "szkle":** ten pasek nie prześwietla realnej zawartości
strony pod spodem (to wymagałoby renderowania bez okna — CEF OSR — i
kompozycji z resztą pulpitu, co jest dużo bardziej złożone i kosztowne
wydajnościowo). Efekt szkła robi `backdrop-filter: blur()` na ambientowym
gradiencie namalowanym w tle samego paska — wizualnie wygląda jak
frosted glass, ale nie "widzi" strony poniżej. Jeśli zależy Ci właśnie na
prawdziwym prześwitywaniu strony przez pasek, daj znać — to osobny,
większy temat (windowless rendering + warstwa kompozytująca).

## Wymagania

1. **CEF binary distribution** — pobierz z
   https://cef-builds.spotifycdn.com/index.html (wybierz najnowszą wersję,
   platformę Windows 64-bit, wariant "standard"). Rozpakuj gdziekolwiek,
   np. `C:\cef_binary_XXX_windows64`.
2. Visual Studio 2022 (Desktop development with C++) + CMake ≥ 3.19.

## Budowanie

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCEF_ROOT=C:/cef_binary_XXX_windows64
cmake --build build --config Release
```

Po zbudowaniu w `build/Release/` powinny się znaleźć m.in.:
`glass_browser.exe`, `libcef.dll`, pliki `*.pak`, `icudtl.dat`, folder
`locales/`, oraz nasz `resources/toolbar.html` — to wszystko musi leżeć
razem, inaczej CEF nie wystartuje.

Uruchamiasz po prostu `glass_browser.exe`.

## Co dalej możesz dodać

- Zakładki (kolejne instancje browserów `content`, przełączane widocznością).
- Historia / autouzupełnianie w pasku adresu (localStorage w toolbar.html
  albo komunikacja z C++, jeśli chcesz trzymać historię trwale).
- Ikonę favicon obok adresu — CEF ma `CefDisplayHandler::OnFaviconURLChange`.
- Prawdziwe przenikanie treści strony pod paskiem — przez CEF OSR (windowless
  rendering) + własna kompozycja obrazu, to już osobny, większy projekt.

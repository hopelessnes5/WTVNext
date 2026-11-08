# План рефакторинга WTVNext

> Объект: `F:\repos\WTVNext` — Windows Texture Viewer Next Gen.
> Документ составлен по фактическому состоянию кода и проверенной сборке (не по предположениям).
> Оценки трудозатрат — в человеко-днях (1 разработчик, знакомый с Win32/COM/D3D11).

---

## 0. TL;DR

| Что | Сейчас | Проблема |
|---|---|---|
| Репозиторий | 64,6 МБ, без git, с бинарными артефактами и кэшем VS (≈1,2 ГБ в `.vs`) | нет истории, нет отката, нет воспроизводимости |
| Своего кода | 3 файла, ~1090 строк (`main.cpp` — 824 строки, ~40 глобальных переменных) | god-object, всё в одном TU |
| Сторонний код | DirectXTex **двумя одинаковыми копиями** + 2 предсобранных `.lib` (16 МБ) без указания ревизии | дублирование, «слепой» бинарник, риск ABI-рассинхрона |
| Сборка Release\|x64 | собирается, 0 предупреждений (W3) | — |
| Сборка Debug\|x64 | **не линкуется** (`LNK2038`) | debug-цикл отсутствует |
| Сборка Win32 | **не компилируется** (`C1083`) | конфигурации в `.sln` мертвы |
| Параллельная сборка (`/m`) | **падает** (`MSB6003`) | общий относительный `IntDir` у двух проектов |
| Тесты | нет | регрессии ловятся только глазами |
| Shell extension | 239 строк, регистрация в HKCR через `regsvr32` + UAC | нет try/catch, не premultiplied alpha, частичное чтение IStream, удаление чужого обработчика при unregister |

**Порядок работ:** 0 (гигиена) → 1 (починить сборку) → 2 (ядро) → 3 (shell extension) → 4 (распил вьюера) → 5 (корректность рендера) → 6 (продуктовая обвязка) → 7 (тесты/CI) → 8 (упаковка).
**MVP за неделю:** этапы 0, 1, 3 (починить и укрепить то, что является продуктом — превью в Проводнике).

---

## 1. Инвентаризация

### 1.1 Состав

```
WTVNext.sln                      — 2 проекта, 4 конфигурации (Debug/Release × x64/Win32)
WTVNext\                         — shell extension DLL: провайдер эскизов .dds
  DDSThumbnailProvider.cpp        239 строк: COM-объект + class factory + DllMain + регистрация
  TextureLoader.h                  26 строк: МЁРТВЫЙ КОД (никем не включается)
  WTVNext.def                       4 экспорта
  DirectXTex\                     — копия №1: 29 исходников/заголовков + DirectXTex.lib (3,4 МБ) + DirectXTex_d.lib (4,8 МБ)
WTVNextApp\                      — Win32-вьюер текстур
  main.cpp                        824 строки: меню, D3D11, шейдеры, ввод, сканирование папки, регистрация
  WTVNextApp.rc                   — шаблон мастера VS: меню/акселераторы/диалог/таблица строк, НЕ используются
  framework.h, WTVNextApp.h       — МЁРТВЫЙ КОД
  DirectXTex\                     — копия №2, побайтово идентична копии №1 (SHA-256 совпал по всем 50 файлам)
x64\                             — закоммиченные результаты сборки (exe, dll, pdb, lib, exp)
.vs\                             — кэш VS 2022, включая ipch на ~780 МБ
```

### 1.2 Карта кода

| Файл | Строк | Роль | Проблема |
|---|---|---|---|
| `WTVNext\DDSThumbnailProvider.cpp` | 239 | 4 ответственности в одном файле: COM-объект, фабрика, экспорт DLL, запись в реестр | нет слоёв, нет обработки исключений, регистрация требует admin |
| `WTVNextApp\main.cpp` | 824 | весь вьюер целиком | ~40 file-scope глобалов, смешаны UI/рендер/IO/реестр |
| `WTVNext\TextureLoader.h` | 26 | загрузчик DDS | не используется ни одним TU |
| `WTVNextApp\WTVNextApp.rc` | ~190 | ресурсы | меню, акселераторы, диалог «О программе», таблица строк — не используются приложением |
| `WTVNextApp\framework.h`, `WTVNextApp.h`, `*.vcxproj.user` | 15/3/4 | — | не используются |

### 1.3 Сборка: что реально настроено

| Свойство | WTVNext (DLL) | WTVNextApp (EXE) |
|---|---|---|
| `ConfigurationType` x64 | `DynamicLibrary` | `Application` |
| `ConfigurationType` Win32 | **`Application`** — DLL превращается в EXE | `Application` |
| `SubSystem` | **`Console`** + `_CONSOLE` | `Windows` + `_WINDOWS` |
| `AdditionalIncludeDirectories` | только x64 (`$(ProjectDir)DirectXTex`) | только x64 |
| DirectXTex | `DirectXTex.lib` в **обеих** конфигурациях + `#pragma comment(lib,"DirectXTex_d.lib")` в Debug | так же |
| `WindowsTargetPlatformVersion` | `10.0` (последний установленный SDK — невоспроизводимо) | `10.0` |
| `OutDir` / `IntDir` | не заданы → `$(SolutionDir)x64\Release\` у **обоих** проектов | то же |

---

## 2. Проверенный baseline (evidence)

Сборка выполнялась на копии репозитория (VS 2022 Community, MSVC 14.44, Windows SDK 10.0.26100):

| # | Команда | Результат |
|---|---|---|
| 1 | `MSBuild WTVNext.sln /p:Configuration=Release /p:Platform=x64 /m:1` | ✅ успех, 0 предупреждений (W3), `x64\Release\WTVNext.dll` (134 КБ) + `WTVNextApp.exe` (188 КБ) |
| 2 | то же, `/m` (параллельно) | ❌ `MSB6003: DirectoryNotFoundException ... WTVNextApp\x64\Release\WTVNext.tlog\CL.read.1.tlog` — **воспроизводится стабильно** |
| 3 | `MSBuild ... /p:Configuration=Debug /p:Platform=x64` | ❌ `LNK2038: RuntimeLibrary "MD_DynamicRelease" не соответствует "MDd_DynamicDebug"`, `_ITERATOR_DEBUG_LEVEL: 0 vs 2` |
| 4 | `MSBuild WTVNext\WTVNext.vcxproj /p:Platform=Win32` | ❌ `C1083: не удаётся открыть DirectXTex.h` (в Win32 не заданы include-каталоги) |

Выводы, которые прямо меняют план:

1. **Release-сборка единственная рабочая** — вся разработка идёт без отладчика; Debug-конфигурация не чинится «когда-нибудь», она должна быть починена первой.
2. **Параллельная сборка ломается.** Вероятная причина — относительный `IntDir`, общий для двух проектов: один и тот же каталог `x64\Release\` разрешается от разных рабочих каталогов узлов MSBuild, и `CL`-задача пишет tlog в несуществующий путь. Лечится абсолютными `OutDir`/`IntDir` (`bin\`, `obj\$(ProjectName)\`), а не «собирайте по одному проекту».
3. **Win32-конфигурации фиктивны** — их надо либо удалить, либо довести до состояния DLL x86 (для 32-битного хоста Проводника), но не оставлять «как есть».
4. `#pragma comment(lib, ...)` в `.cpp` конфликтует с настройками проекта — источник ошибки №3. Единственный владелец списка библиотек — файл проекта/CMake.

---

## 3. Каталог проблем

Легенда приоритетов: **P0** — блокирует разработку/поставку; **P1** — некорректное поведение/риск падения; **P2** — архитектура/сопровождаемость; **P3** — процессы/косметика.
Пометка ✅ — подтверждено сборкой; ⚠️ — подтверждено чтением кода, требует теста для финального подтверждения.

### 3.1 P0 — сборка и инфраструктура

| ID | Проблема | Где | Подтверждение |
|---|---|---|---|
| B1 | Debug-конфигурация не линкуется: release-библиотека в debug-сборке | `WTVNext.vcxproj:120-121`, `main.cpp:1-5`, `DDSThumbnailProvider.cpp:1-5` | ✅ #3 |
| B2 | Нет `DirectXTex_d.lib` в списке зависимостей Debug (тянется только `#pragma`) | там же | ✅ #3 |
| B3 | Win32-конфигурации: нет include/lib-каталогов, тип проекта `Application` вместо `DynamicLibrary`, `_CONSOLE` | `WTVNext.vcxproj:38-50,82-107` | ✅ #4 |
| B4 | Общий относительный `IntDir`/`OutDir` → падение параллельной сборки, коллизии `.obj` | оба `.vcxproj` | ✅ #2 |
| B5 | Бинарные артефакты и кэш VS в рабочем каталоге, нет `.gitignore`, нет git | `x64\`, `.vs\`, `WTVNext\x64\`, `WTVNextApp\x64\` | ✅ |
| B6 | DirectXTex двумя копиями + предсобранные `.lib` без ревизии | `WTVNext\DirectXTex\`, `WTVNextApp\DirectXTex\` | ✅ (SHA-256 совпадают) |
| B7 | `WindowsTargetPlatformVersion=10.0` → сборка зависит от установленного SDK | оба `.vcxproj:35/26` | ✅ |

### 3.2 P1 — shell extension (продукт)

| ID | Проблема | Где |
|---|---|---|
| T1 | Нет барьера исключений: любое `std::bad_alloc`/исключение DirectXTex из `GetThumbnail` пролетает через COM в `explorer.exe` | `DDSThumbnailProvider.cpp:64-132` ⚠️ |
| T2 | `IStream::Read` вызывается **один** раз; короткое чтение (сетевой/архивный поток) даёт обрезанный файл и `E_FAIL`, хотя данные доступны | `:75` ⚠️ |
| T3 | Нет ограничения размера: `std::vector<uint8_t> buffer(size)` на весь файл; `size` кастуется в `ULONG` | `:72-75` ⚠️ |
| T4 | Возвращается **straight alpha** при `WTSAT_ARGB`; оболочка ожидает **premultiplied** ARGB → тёмная кайма по краям | `:112-131` ⚠️ |
| T5 | Всегда `WTSAT_ARGB`, даже для непрозрачных BC1/BC5 → лишние вычисления и неверная композиция | `:130` ⚠️ |
| T6 | Регистрация пишет в `HKCR` → требует admin, `DllRegisterServer` всегда возвращает `S_OK` | `:196-227` ⚠️ |
| T7 | `DllUnregisterServer` удаляет **весь** ключ `.dds\ShellEx\{e357fccd-...}` — если там был обработчик сторонней программы, он уничтожен | `:230-238` ⚠️ |
| T8 | Регистрация/снятие из UI приложения через `regsvr32` + `runas`, с `WaitForSingleObject(10s)` и `taskkill /f explorer.exe` | `main.cpp:288-330` ⚠️ |
| T9 | Нет логирования: диагностика «почему нет превью» невозможна | весь файл ⚠️ |
| T10 | `GetDC(nullptr)` для `CreateDIBSection` вместо `CreateCompatibleDC`; HBITMAP не освобождается на ошибочных путях | `:122-127` ⚠️ |

### 3.3 P1 — вьюер

| ID | Проблема | Где |
|---|---|---|
| V1 | `WM_MOUSEWHEEL`: `lParam` — **экранные** координаты, код трактует как клиентские → зум «не в ту точку» | `main.cpp:575-588` ⚠️ |
| V2 | `LOWORD/HIWORD` вместо `GET_X_LPARAM/GET_Y_LPARAM` → скачок панорамы при отрицательных координатах (перетаскивание за границу окна) | `:551-572` ⚠️ |
| V3 | `ResizeBuffers` вызывается без `OMSetRenderTargets(0,nullptr,nullptr)` + `Flush` → `DXGI_ERROR_INVALID_CALL`, код ошибки игнорируется | `:446-464` ⚠️ |
| V4 | Нет обработки `DXGI_ERROR_DEVICE_REMOVED/DEVICE_RESET` — после смены драйвера/гибернации окно чёрное навсегда | `:377-444,466-523` ⚠️ |
| V5 | Рендер в холостом цикле `PeekMessage`+`Render` → постоянная нагрузка GPU/CPU при статичной картинке | `:810-819` ⚠️ |
| V6 | sRGB-текстуры (`*_UNORM_SRGB`) декодируются в linear, а вывод идёт в `R8G8B8A8_UNORM` → изображение темнее оригинала | `:380` ⚠️ |
| V7 | HDR (BC6H/RGBA16F) не тонально отображается; поле `exposure` в константах шейдера **не используется** | `:58-63,333-375` ⚠️ |
| V8 | Cubemap/2D-array/volume: `CreateShaderResourceView` создаёт cube/array SRV, а шейдер объявляет `Texture2D` → несовпадение типа ресурса | `:265,344` ⚠️ |
| V9 | В меню заявлены `Ctrl+O`, `F5`, но таблицы акселераторов нет и обработчиков нет — «мёртвые» подсказки | `:734-735,742-752` ⚠️ |
| V10 | Ошибки загрузки/инициализации D3D не показываются пользователю: чёрное окно или тихий `return 1` | `:247-278,388-391` ⚠️ |
| V11 | Шейдеры компилируются из строки на старте, результаты `D3DCompile` не проверяются; зависимость от `D3DCOMPILER_47.dll` в рантайме | `:333-375,395-410` ✅ (импорт в PE) |
| V12 | Нет DPI-awareness: на 150 % текст и статус-бар мылятся, ширины панелей в физических пикселях | `main.cpp` (нет вызовов) ✅ |
| V13 | Иконка приложения не устанавливается (`hIcon` = `nullptr`), `IDI_*` из `.rc` не используются | `:779-785` ✅ |
| V14 | `towlower` посимвольно на `std::wstring` — зависимость от локали; сравнение путей регистрозависимо на не-ASCII | `:222,237,256` ⚠️ |
| V15 | Сканирование папки: список фиксируется один раз, `catch(...){}` глушит ошибки, индекс не сбрасывается, если файл не найден | `:216-245` ⚠️ |
| V16 | При неудачной загрузке старое имя файла остаётся в заголовке, а текстура уже уничтожена (несогласованное состояние) | `:247-272` ⚠️ |
| V17 | `__argc/__wargv` вместо `CommandLineToArgvW`; `pCmdLine` помечен `UNREFERENCED_PARAMETER` | `:774,806` ⚠️ |
| V18 | Нет сохранения состояния окна/настроек, нет авто-перезагрузки при изменении файла, нет Drag&Drop нескольких файлов | — |

### 3.4 P2 — архитектура и сопровождаемость

| ID | Проблема | Где |
|---|---|---|
| A1 | ~40 изменяемых file-scope глобалов: `g_pd3dDevice`, `g_Zoom`, `g_ChannelMode`, `g_FolderFiles`… → нечего тестировать, нельзя создать второй экземпляр | `main.cpp:66-104` |
| A2 | `main.cpp` = 824 строки, 8+ ответственностей (меню, статус-бар, D3D, шейдеры, ввод, IO, реестр, message loop) | весь файл |
| A3 | Дублирование логики загрузки: `TextureLoader.h` и `main.cpp:247-278` | оба |
| A4 | Дублирование `#pragma comment(lib ...)` (дважды в одном файле) и в обоих проектах | `main.cpp:1-5,24-28` |
| A5 | Регистрация расширения реализована дважды: в DLL и в приложении | `DDSThumbnailProvider.cpp:196-238`, `main.cpp:288-330` |
| A6 | Магические числа: `150,260,360,480` (панели), `1.25f` (зум), `12.0`, `0.85/0.65` (шахматка), индексы каналов 0…6 | разбросано |
| A7 | `.rc` — шаблон мастера: неиспользуемые меню/акселераторы/диалог/строки, нет `VERSIONINFO`; `WTVNextApp.ico` и `small.ico` побайтово идентичны | `WTVNextApp.rc` ✅ |
| A8 | Мёртвый код: `TextureLoader.h`, `framework.h`, `WTVNextApp.h`, пустые `*.vcxproj.user` | ✅ |
| A9 | Нет `.def`-независимого объявления экспорта (`__declspec(dllexport)`), нет `DllInstall`, нет `AppID`/изоляции | `WTVNext.def` |
| A10 | Нет единого стиля: смесь `g_`, `p`, `sz`, `dw`; нет `.clang-format`/`.editorconfig` | всё |

### 3.5 P3 — процессы

| ID | Проблема |
|---|---|
| P1 | Нет git и `.gitignore` — нет истории, code review, отката |
| P2 | Нет README/LICENSE/CHANGELOG — непонятно, что это, как собирать, под какой лицензией |
| P3 | Нет тестов (ни одного), нет тестовых ассетов DDS |
| P4 | Нет CI — «собирается у меня» |
| P5 | Нет упаковки/установщика, подписи, версии продукта; требуется VC++ Redist (`MSVCP140/VCRUNTIME140` в импортах) |
| P6 | Кодировки: `main.cpp` — UTF-8 с BOM, `DDSThumbnailProvider.cpp` — UTF-8 **без** BOM с 369 не-ASCII байтами, флаг `/utf-8` не выставлен → MSVC трактует комментарии как ANSI |

### 3.6 Чего в списке нет (важно, чтобы не переусердствовать)

- Пайплайн DirectXTex корректно делает `Decompress`/`Convert`/`Resize` — переписывать не нужно.
- `QISearch`/`QITAB` в `QueryInterface` — идиоматично и правильно.
- Статическая линковка DirectXTex в DLL — **правильное** решение для shell extension (никаких лишних DLL рядом с `explorer.exe`). Не менять.
- CLSID `{4A2D4055-6E02-4D3B-A179-880F86F6B2EA}` менять **нельзя** — сломаются существующие установки и привязки.
- Архитектура «одна DLL + одно EXE» адекватна задаче. Никаких сервисов, MVVM и DI-контейнеров.

---

## 4. Целевое состояние

### 4.1 Принципы

1. **Ядро без Win32-окон и COM** — только `DirectXTex` + `std::filesystem`; его можно тестировать в консольном тесте.
2. **Слой оболочки тонкий** — COM/`HBITMAP`/реестр только в адаптерах, вся логика в ядре.
3. **Один владелец зависимостей** — ни одного `#pragma comment(lib ...)`; всё в сборке.
4. **Никаких file-scope изменяемых глобалов** — состояние в объектах, передаётся явно.
5. **Ошибка — значение, а не тишина** — `HRESULT`/`enum` + сообщение пользователю + лог.
6. **Один билд-граф** — `Release|x64` и `Debug|x64` зелёные, тесты в CI.

### 4.2 Структура репозитория

```
WTVNext/
├─ CMakeLists.txt                  # корневой: проекты, версия, опции
├─ CMakePresets.json               # windows-x64-debug / windows-x64-release / ci-warp
├─ .clang-format  .editorconfig  .gitattributes  .gitignore
├─ README.md  CHANGELOG.md  LICENSE
├─ cmake/
│   ├─ Warnings.cmake              # /W4 /WX, /external:W0, /utf-8
│   ├─ Version.cmake               # генерация version.h + VERSIONINFO
│   └─ DirectXTex.cmake            # FetchContent/DirectXTex.cmake, пиннинг тега
├─ external/
│   └─ DirectXTex/                 # git submodule, ОДНА копия (или FetchContent)
├─ src/
│   ├─ core/                       # static lib WTVCore — без окон и COM
│   │   ├─ TextureFile.h/.cpp      # загрузка файла/потока, диспетчеризация по расширению
│   │   ├─ FormatInfo.h/.cpp       # имя формата, sRGB/compressed/HDR/alpha-признаки
│   │   ├─ Thumbnail.h/.cpp        # decode → premultiply → resize → пиксели BGRA8
│   │   ├─ FolderNavigator.h/.cpp  # список файлов папки, next/prev, индекс
│   │   ├─ PathUtils.h/.cpp        # расширения, нормализация, case-insensitive сравнение
│   │   └─ Log.h/.cpp              # OutputDebugString + опционально файл
│   ├─ thumbnails/                 # DLL, выходное имя остаётся WTVNext.dll
│   │   ├─ DllMain.cpp
│   │   ├─ Module.cpp              # DllGetClassObject / DllCanUnloadNow / refcount
│   │   ├─ ClassFactory.cpp
│   │   ├─ ThumbnailProvider.cpp   # только COM-адаптер: stream → core::Thumbnail → HBITMAP
│   │   ├─ Registration.cpp        # HKCR/HKCU, восстановление прежнего обработчика
│   │   └─ WTVThumbnail.def
│   ├─ app/                        # EXE WTVNextApp
│   │   ├─ main.cpp                # ~40 строк: wWinMain → App::Run
│   │   ├─ App.h/.cpp              # цикл сообщений, single instance, командная строка
│   │   ├─ MainWindow.h/.cpp       # рамка, меню, акселераторы, команды
│   │   ├─ StatusBar.h/.cpp
│   │   ├─ TextureView.h/.cpp      # дочернее окно, ввод, DnD, клики
│   │   ├─ Camera.h/.cpp           # zoom/pan/fit, zoom-to-point — чистая математика
│   │   ├─ Renderer.h/.cpp         # device/swapchain/RTV/SRV, resize, device-lost, кадр
│   │   ├─ Settings.h/.cpp         # HKCU\Software\WTVNext
│   │   ├─ ShellExtensionAdmin.h/.cpp
│   │   ├─ shaders/media.hlsl      # компилируется в .cso на этапе сборки
│   │   └─ WTVNextApp.rc           # иконка + VERSIONINFO + меню/акселераторы
│   └─ tests/
│       ├─ unit/                   # Camera, FormatInfo, FolderNavigator, PathUtils
│       ├─ integration/            # thumbnail_smoke.exe: LoadLibrary(DLL) + golden hash
│       └─ assets/                 # генерируются tools/gen_assets (не бинарники в git)
├─ tools/
│   └─ gen_assets.cpp              # создаёт корпус DDS: BC1/3/5/6/7, sRGB, cube, 1x1, 8K
└─ .github/workflows/build.yml
```

### 4.3 Модули и зависимости

```
            ┌──────────────────────────────┐
            │  core (WTVCore, статическая) │  DirectXTex, std::filesystem
            │  TextureFile FormatInfo      │  никаких HWND/COM/реестра
            │  Thumbnail FolderNavigator   │
            └───────┬──────────────┬───────┘
                    │              │
        ┌───────────▼───┐     ┌────▼──────────────┐
        │ thumbnails    │     │ app               │
        │ COM + HBITMAP │     │ HWND + D3D11      │
        │ + реестр      │     │ + реестр (HKCU)   │
        └───────────────┘     └───────────────────┘
```

Обе цели линкуют `WTVCore` статически (DLL остаётся самодостаточной — это требование shell extension).

### 4.4 Ключевые решения (ADR)

| # | Решение | Альтернатива | Почему так |
|---|---|---|---|
| ADR-1 | **CMake + presets** как единственная система сборки | MSBuild + `Directory.Build.props` | DirectXTex уже собирается CMake; CI без VS-решений; один источник правды по флагам; не нужно править XML руками (сейчас 4 конфигурации, 2 из них мертвы). Если команда жёстко в VS — тот же результат даёт `Directory.Build.props` (Приложение B), рефакторинг кода от этого не меняется |
| ADR-2 | DirectXTex — **submodule/FetchContent, одна копия, сборка из исходников** | предсобранный `.lib` | убирает 16 МБ бинарников и 2 копии кода, гарантирует совпадение CRT/ABI, фиксирует ревизию |
| ADR-3 | CRT: `/MD` для EXE, `/MT` **опционально** для DLL | всегда `/MD` | `/MT` в shell extension избавляет `explorer.exe`-окружение от зависимости от VC Redist; требует сборки DirectXTex под тот же CRT — цену считаем на этапе 3 |
| ADR-4 | Регистрация: **HKCU\Software\Classes по умолчанию**, HKLM — опция «для всех» | HKCR + `regsvr32` + UAC | не требует прав администратора, работает в корпоративных средах; `regsvr32` остаётся как совместимый путь |
| ADR-5 | Шейдеры — `.hlsl` → `.cso` на этапе сборки, встраиваются ресурсом; runtime-компиляция только в Debug | строка в коде | ошибки шейдера ловятся на сборке, нет зависимости от `D3DCOMPILER_47.dll` в релизе, быстрее старт |
| ADR-6 | Рендер по «грязному флагу» (`WM_PAINT`/`InvalidateRect`), vsync только при перерисовке | постоянный цикл | 0 % GPU в покое, отсутствие лишнего энергопотребления (ноутбуки) |
| ADR-7 | Тесты: Catch2 (или минимальный assert-раннер) + `ctest`, рендер-тесты на **WARP** | без тестов | GPU на CI-раннерах нет, WARP даёт детерминированный software-рендер |
| ADR-8 | Логирование: `OutputDebugString` + файл `%LOCALAPPDATA%\WTVNext\log.txt` с ротацией 1 МБ | ничего | диагностика проблем с превью в Проводнике без отладчика |

### 4.5 Контракты ядра (эскиз)

```cpp
// core/TextureFile.h
namespace wtv {
enum class LoadStatus { ok, unsupportedExtension, openFailed, parseFailed, empty };
struct FormatTraits { const wchar_t* name; bool compressed, srgb, hasAlpha, hdr, isCube; };
struct TextureFile {
    DirectX::TexMetadata    meta{};
    DirectX::ScratchImage   images;   // все mip/array/slice
    FormatTraits            traits{};
};
struct LoadResult { LoadStatus status = LoadStatus::openFailed; HRESULT hr = E_FAIL; TextureFile texture; };

LoadResult LoadTextureFile(const std::filesystem::path& path);
LoadResult LoadTextureStream(IStream* stream, uint64_t maxBytes = 512ull << 20);
FormatTraits DescribeFormat(DXGI_FORMAT fmt) noexcept;
}

// core/Thumbnail.h — без HBITMAP, чтобы тестировать без окна
struct ThumbnailBitmap { uint32_t width = 0, height = 0; bool hasAlpha = false;
                         std::vector<uint8_t> bgra; };   // top-down, premultiplied
HRESULT RenderThumbnail(const TextureFile&, uint32_t cx, ThumbnailBitmap& out);

// thumbnails/ThumbnailProvider.cpp — единственное место, знающее про Win32/COM
HBITMAP CreateDibSection(const ThumbnailBitmap&);   // CreateCompatibleDC + CopyMemory

// app/Camera.h — чистая математика, тестируется без D3D
struct Camera {
    float zoom = 1.0f; Vec2 pan{};
    void  Fit(Size view, Size image) noexcept;
    void  ZoomAt(float factor, Point anchorClient) noexcept;   // клиентские координаты!
    RectF ImageRect(Size view) const noexcept;
};
```

### 4.6 Карта переноса (старое → новое)

| Сейчас (файл:строки) | Куда |
|---|---|
| `WTVNext\DDSThumbnailProvider.cpp:64-132` (`GetThumbnail`) | `src/thumbnails/ThumbnailProvider.cpp` + `src/core/Thumbnail.cpp` |
| `WTVNext\DDSThumbnailProvider.cpp:138-167` (`ClassFactory`) | `src/thumbnails/ClassFactory.cpp` |
| `WTVNext\DDSThumbnailProvider.cpp:172-193` (`DllMain`, экспорт) | `src/thumbnails/DllMain.cpp`, `src/thumbnails/Module.cpp` |
| `WTVNext\DDSThumbnailProvider.cpp:196-238` (реестр) | `src/thumbnails/Registration.cpp` |
| `WTVNext\TextureLoader.h` | удалить → `src/core/TextureFile.cpp` |
| `main.cpp:106-126` (`GetWTVFormatName`) | `src/core/FormatInfo.cpp` |
| `main.cpp:216-245` (`ScanFolderOnce`) | `src/core/FolderNavigator.cpp` |
| `main.cpp:247-278` (`LoadTexture*`) | `src/core/TextureFile.cpp` |
| `main.cpp:128-184` (статус-бар, заголовок) | `src/app/StatusBar.cpp`, `MainWindow.cpp` |
| `main.cpp:186-214` (zoom/fit) | `src/app/Camera.cpp` |
| `main.cpp:281-330` (регистрация через regsvr32) | `src/app/ShellExtensionAdmin.cpp` |
| `main.cpp:333-375` (HLSL) | `src/app/shaders/media.hlsl` (компиляция на сборке) |
| `main.cpp:377-523` (D3D init/resize/render) | `src/app/Renderer.cpp` |
| `main.cpp:539-595` (`ViewWndProc`) | `src/app/TextureView.cpp` |
| `main.cpp:597-765` (`MainWndProc`, меню) | `src/app/MainWindow.cpp` (+ `.rc`) |
| `main.cpp:767-824` (`wWinMain`) | `src/app/main.cpp` + `src/app/App.cpp` |
| `WTVNextApp.rc` (шаблон мастера) | `src/app/WTVNextApp.rc`: иконка + `VERSIONINFO` + меню/акселераторы; шаблонный мусор удалить |
| `WTVNext\DirectXTex\`, `WTVNextApp\DirectXTex\` | `external/DirectXTex/` (одна копия, submodule/FetchContent) |

---

## 5. Этапы

Формат: цель → работы → DoD → риск/откат → оценка.

### Этап 0. Гигиена репозитория — 1 день

**Цель:** возможность откатываться и видеть диффы до начала любых правок.

Работы:
1. `git init`, первый коммит «as-is» (артефакты удалить из индекса).
2. `.gitignore` (Приложение A), `.gitattributes` (`*.rc text working-tree-encoding=UTF-16LE`, `*.ico binary`).
3. Удалить с диска и из индекса: `x64\`, `WTVNext\x64\`, `WTVNextApp\x64\`, `.vs\`, `*.vcxproj.user`.
4. Удалить мёртвые файлы: `WTVNext\TextureLoader.h`, `WTVNextApp\framework.h`, `WTVNextApp\WTVNextApp.h`, `WTVNextApp\small.ico` (дубль), пустые `*.vcxproj.user`.
5. `README.md` (что это, как собрать, как зарегистрировать), `LICENSE`, `CHANGELOG.md`, `.editorconfig`.
6. Папка `docs/` — перенести этот план.

**DoD:** свежий `git clone` занимает < 15 МБ; `git status` чист; сборка Release\|x64 проходит как раньше.
**Риск:** низкий. **Откат:** удалить `.git`.
**Оценка:** 1 д.

### Этап 1. Починить сборку (hotfix) — 1–2 дня

**Цель:** зелёные `Release|x64` и `Debug|x64`, работающий `/m`, воспроизводимость.

Работы (минимальный вариант, MSBuild):
1. Добавить `Directory.Build.props` (Приложение B): абсолютные `OutDir`/`IntDir`, пиннинг SDK, `/utf-8`, `/W4`, `/external:W0`.
2. Убрать все `#pragma comment(lib, "DirectXTex*.lib")` из `.cpp`; библиотеки задаются только в проекте, по конфигурации (`DirectXTex_d.lib` для Debug).
3. Win32/DLL: либо удалить Win32-конфигурации из `.sln` и `.vcxproj` (рекомендуется), либо довести: `DynamicLibrary`, include/lib-каталоги, `SubSystem=Windows`.
4. `WindowsTargetPlatformVersion` → конкретная версия (например `10.0.26100.0`).
5. Параллельная сборка: проверить `MSBuild /m` на чистом дереве.

DoD:
- `MSBuild /m` для `Debug|x64` и `Release|x64` — успех, 0 ошибок;
- артефакты только в `bin\`, промежуточные только в `obj\$(ProjectName)\`;
- `Debug|x64` запускается и показывает текстуру (с включённым D3D11 debug layer — этап 5);
- в репозитории нет `#pragma comment(lib`.

**Риск:** конфликт с переходом на CMake на этапе 2 (двойная работа ~0,5 д). Осознанно принимается: горячий фикс разблокирует отладку сразу.
**Откат:** коммит этапа 0.
**Оценка:** 1–2 д.

### Этап 2. Ядро `WTVCore` — 2–3 дня

**Цель:** одна реализация загрузки/форматов/навигации для DLL и EXE.

Работы:
1. Перенести `GetWTVFormatName` → `core::DescribeFormat` (расширить: `_SRGB`, BC4/BC5 SNORM, cube, volume). В используемой версии DirectXTex готового `FormatToString` нет — таблицу имён держим своей, но в одном месте.
2. Перенести `LoadTextureInternal`/`LoadTexture` → `core::LoadTextureFile` с `LoadResult` (никаких «тихих» `false`).
3. Перенести `ScanFolderOnce` → `core::FolderNavigator` (без `catch(...)`, сравнение путей через `CompareStringOrdinal`, полный список расширений в одном месте).
4. `core::RenderThumbnail` — общий пайплайн decode → (Decompress|Convert) → **PremultiplyAlpha** → Resize → BGRA8; лимит размера входа.
5. Удалить `TextureLoader.h`; DLL перестаёт содержать логику загрузки.
6. Юнит-тесты на `DescribeFormat`, `PathUtils`, `FolderNavigator` (на временной папке).

DoD: DLL и EXE не содержат дублирующейся логики; `ctest` зелёный; поведение Release-сборки не изменилось (ручной чеклист Приложения D).
**Риск:** средний (перетасовка кода без изменения поведения). **Откат:** этап 1.
**Оценка:** 2–3 д.

### Этап 3. Shell extension: надёжность и корректность — 3–4 дня

Это **основная ценность продукта** — делать сразу после этапа 2.

Работы:
1. Разделить `DDSThumbnailProvider.cpp`: `Module.cpp`, `ClassFactory.cpp`, `ThumbnailProvider.cpp`, `Registration.cpp`.
2. **T1:** `try/catch(...)` вокруг всего тела `GetThumbnail` → `E_FAIL` + лог; SEH-барьер не нужен, но `std::nothrow` и проверки обязательны.
3. **T2:** чтение потока циклом до `cbSize` (или `S_FALSE`), с проверкой `bytesRead`.
4. **T3:** лимит `maxBytes` (по умолчанию 512 МБ) + сначала читать заголовок DDS (128 байт), потом остальное; отказ для `cbSize == 0`.
5. **T4/T5:** `PremultiplyAlpha` перед упаковкой в DIB; `WTSAT_RGB`, если у формата нет альфы (`FormatTraits::hasAlpha == false`).
6. **T6:** `Registration.cpp` с таблицей ключей и режимами `PerUser` (HKCU) / `PerMachine` (HKLM, с elevation); каждый `Reg*` проверяется, ошибка агрегируется в `HRESULT`.
7. **T7:** перед перезаписью читать текущее значение `.dds\ShellEx\{e357fccd-...}`; при unregister восстанавливать прежнее, а не удалять ключ.
8. **T10:** `CreateCompatibleDC` + `RAII` на `HBITMAP`/`HDC`; освобождение на всех путях.
9. **T8:** убрать UI-обвязку `regsvr32` из `main.cpp`; приложение вызывает `ShellExtensionAdmin`, который делает HKCU-регистрацию в процессе (без UAC), а «для всех пользователей» — опционально с elevation.
10. **T9:** `core::Log` + запись в `%LOCALAPPDATA%\WTVNext\log.txt`.
11. Интеграционный harness `tests/integration/thumbnail_smoke.cpp`: `LoadLibrary` → `DllGetClassObject` → `IInitializeWithStream` (поток из файла) → `GetThumbnail` → дамп BMP + SHA-256, сравнение с эталоном.
12. Рассмотреть `ThreadingModel=Both` (меньше оверхеда COM-маршалинга) и, при необходимости изоляции, регистрацию через суррогат (`AppID` + `DllSurrogate`).

DoD:
- на корпусе из ≥20 DDS (BC1/BC3/BC5/BC6H/BC7, sRGB, cube, 1×1, 8192×8192, битый файл, пустой файл) — превью корректны, совпадают с эталонами;
- битый/огромный файл не роняет процесс, возвращает `HRESULT` и пишет лог;
- 200 последовательных `GetThumbnail` — RSS не растёт (проверка в harness);
- `register` → превью в Проводнике; `unregister` → восстанавливается прежний обработчик (проверить на машине с другим DDS-обработчиком);
- регистрация без прав администратора работает на чистой машине;
- ни одно исключение не пересекает COM-границу (проверка: `throw` в тестовой сборке + `_set_se_translator` не нужен).

**Риск:** средний/высокий — затрагивает поведение, видимое пользователю, и реестр. Требуется VM/снапшот для проверки регистрации.
**Откат:** коммит этапа 2; старый `.dll` остаётся в `bin/`.
**Оценка:** 3–4 д.

### Этап 4. Распил вьюера: каркас — 2–3 дня

**Цель:** `main.cpp` ≤ 100 строк, ноль изменяемых глобалов, поведение 1:1.

Работы:
1. Вынести `Camera` (fit/zoom/zoom-to-point/pan) — **первым**, т.к. это чистая математика и её сразу покрываем тестами.
2. `MainWindow`: создание окна, меню (код → ресурс), акселераторы (`Ctrl+O`, `F5`), маршрутизация `WM_COMMAND` по таблице `{id, handler}`.
3. `StatusBar`: 5 панелей, ширины пропорционально/от DPI, форматирование в одном месте.
4. `TextureView`: ввод, DnD, передача событий в `App`/`Camera`; фикс V1 (экранные координаты колеса → `ScreenToClient`) и V2 (`GET_X_LPARAM`).
5. `AppState`/`Document`: текущий файл, метаданные, индекс mip, режим канала — один объект, передаётся по ссылке.
6. Сообщения об ошибках: `MessageBox` с путём, форматом и `HRESULT`; заголовок окна не рассинхронизируется (V16).
7. Ресурсы: иконка (`wcMain.hIcon`), `VERSIONINFO`, удалить шаблонные меню/диалог/строки и дубль `.ico` (A7, V13).

DoD: `main.cpp` < 100 строк; `grep -c "^static .* g_"` = 0; ручной чеклист Приложения D пройден полностью; тесты `Camera` зелёные.
**Риск:** средний (регрессии UI). **Откат:** этап 3.
**Оценка:** 2–3 д.

### Этап 5. Корректность рендера и ввода — 3–4 дня

**Цель:** правильные цвета, отсутствие «чёрного окна», 0 % GPU в покое.

Работы:
1. `Renderer` как класс: device/context/swapchain/RTV/SRV/samplers, `Initialize(HWND, DriverType)`, `Resize`, `SetTexture`, `Render`, `RecreateDevice`.
2. **V3:** перед `ResizeBuffers` → `OMSetRenderTargets(0,nullptr,nullptr)`, `Flush`, освобождение RTV, проверка `HRESULT`; при `DXGI_ERROR_INVALID_CALL` — один повтор.
3. **V4:** обработка `DXGI_ERROR_DEVICE_REMOVED/RESET` → пересоздание device и всех ресурсов (текстуру перезагрузить из файла).
4. **V5:** рендер по `WM_PAINT` + `InvalidateRect` при изменениях (зум/пан/смена mip/канала/размер); свапчейн `Present(1,0)` только в кадре, который действительно изменился.
5. **V6:** sRGB-конвейер: `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` для swapchain/RTV, SRV в `_SRGB`-варианте; либо явная линейная математика + sRGB-кодирование в шейдере (выбрать одно и зафиксировать тестом-эталоном).
6. **V7:** HDR: экспозиция в константах шейдера → tone mapping (Reinhard/ACES) + клавиши `[` `]`; убрать мёртвые поля.
7. **V8:** cubemap/array/volume: SRV по `meta.dimension`, селектор среза/грани (клавиши/меню), юнит-тест на `TextureFile` с cube-DDS.
8. **V11:** `shaders/media.hlsl` → `.cso` на этапе сборки (custom command + `/resource`), runtime-компиляция только `_DEBUG`.
9. D3D11 debug layer в Debug-сборке + `ID3D11InfoQueue` → assert на `ERROR`; прогон чеклиста даёт 0 сообщений.
10. `WM_ENTERSIZEMOVE/EXITSIZEMOVE`: во время тяги — только `MoveWindow` без `ResizeBuffers`, ресайз в конце (или throttling).

DoD: sRGB-текстура визуально совпадает с эталонным PNG (проверка скриншотом/попиксельно через WARP); BC6H отображается с регулируемой экспозицией; cube-DDS показывает выбранную грань; `Present` не вызывается, когда ничего не изменилось (счётчик в debug-логе); debug layer молчит; на 150 % DPI картинка не мылится.
**Риск:** средний (переписывание рендера, но локализовано в одном классе). **Откат:** этап 4.
**Оценка:** 3–4 д.

### Этап 6. Продуктовая обвязка — 2–3 дня

Работы: `Settings` (HKCU: позиция/размер окна, последняя папка, режим канала, фильтр, fit); Per-Monitor-V2 DPI (`app.manifest` + `WM_DPICHANGED`); `IFileOpenDialog` вместо `GetOpenFileNameW`; `CommandLineToArgvW`; single instance (перенаправление открытия в запущенный экземпляр); DnD нескольких файлов; авто-перезагрузка по `ReadDirectoryChangesW`; recent files; `VERSIONINFO`/версия из CMake.

DoD: настройки переживают перезапуск; DPI 100/150/200 % корректен; двойной клик по файлу в Проводнике переиспользует окно.
**Оценка:** 2–3 д.

### Этап 7. Тесты и CI — 3–4 дня (можно параллельно с 2–6)

Работы: `ctest`-наборы (unit: Camera/FormatInfo/FolderNavigator/PathUtils; integration: thumbnail harness + golden-хэши); `tools/gen_assets` (генерация корпуса DDS из градиентов — корпус не хранится в git); GitHub Actions `windows-2022`: матрица `Debug/Release`, `/WX`, `ctest`, публикация артефактов; отдельная job `clang-tidy` + `MSVC /analyze`; job ASan (`/fsanitize=address`) на unit-тестах; рендер-тесты на WARP.

DoD: PR без зелёного CI не мержится; тесты < 60 с; покрытие ядра ≥ 70 % (`OpenCppCoverage`).
**Оценка:** 3–4 д.

### Этап 8. Упаковка и релиз — 2–3 дня

Работы: установщик (рекомендуется **per-user MSIX** либо WiX; альтернатива — `WTVNextApp.exe --register` в `install.cmd`); регистрация HKCU по умолчанию + опция HKLM; подпись `.exe`/`.dll`; сброс thumbcache и `SHChangeNotify` в конце установки; `uninstall` с восстановлением прежнего обработчика; release-checklist (Приложение D + сброс кэша превью).

DoD: установка/удаление на чистой Win11 без прав администратора; после установки превью появляются без перезагрузки Explorer (в худшем случае — после `ie4uinit.exe -show`); удаление возвращает систему в исходное состояние.
**Оценка:** 2–3 д.

---

## 6. Сводный график

| Этап | Содержание | Дней | Зависит от | Ценность |
|---|---|---|---|---|
| 0 | Git, `.gitignore`, чистка мёртвого кода | 1 | — | высокая (безопасность изменений) |
| 1 | Зелёная сборка Debug/Release, `/m`, `/utf-8` | 1–2 | 0 | высокая |
| 2 | Ядро `WTVCore`, устранение дублей | 2–3 | 1 | высокая |
| 3 | Shell extension: надёжность, premultiply, HKCU | 3–4 | 2 | **максимальная** (это продукт) |
| 4 | Распил `main.cpp`, акселераторы, ресурсы | 2–3 | 2 | средняя |
| 5 | Рендер: sRGB, HDR, device-lost, dirty-flag | 3–4 | 4 | средняя |
| 6 | Настройки, DPI, диалоги, single instance | 2–3 | 4 | средняя |
| 7 | Тесты, CI, санитайзеры | 3–4 | 2 | высокая (долгосрочно) |
| 8 | Установщик, подпись, релиз | 2–3 | 3, 6 | высокая |
| | **Итого** | **19–27** | | |

**MVP (1 неделя):** 0 → 1 → 3 (с минимумом тестов из 7). Закрывает главный риск: падения `explorer.exe` и кривые превью.
**Разумный минимум (2 недели):** 0 → 1 → 2 → 3 → 4 + CI-каркас.

---

## 7. Стратегия тестирования

### 7.1 Уровни

| Уровень | Что проверяет | Чем | Время |
|---|---|---|---|
| Unit | `Camera` (fit/zoom-to-point/панорама/границы), `DescribeFormat`, `PathUtils`, `FolderNavigator` | Catch2 + `ctest` | < 5 с |
| Integration | `ThumbnailProvider` через `DllGetClassObject`: размеры, альфа, premultiply, битые входы, утечки | свой harness, SHA-256 BMP | < 20 с |
| Golden-render | `Renderer` на **WARP**: 6 текстур × 7 режимов каналов × 5 mip → хэш кадра | `ctest` + сохранённые хэши | < 60 с |
| Manual | Проводник: превью, кэш, регистрация, DPI, многомониторность | чеклист Приложения D | 30 мин на релиз |

### 7.2 Корпус ассетов

Генерируется `tools/gen_assets` (никаких бинарников в git): BC1 (с альфой/без), BC2, BC3, BC4, BC5, BC6H (HDR), BC7 (все режимы), `_SRGB`-варианты, кубмап 6 граней, 1×1, 2×2, 8192×8192, mip-цепочка, объёмная текстура, обрезанный файл, файл с мусором после заголовка, файл нулевого размера, DDS с `arraySize=6` без `CUBE`-флага.

### 7.3 Harness (эскиз)

```cpp
int wmain(int argc, wchar_t** argv) {
    HMODULE dll = LoadLibraryW(L"WTVNext.dll");   // имя DLL не меняем: на него смотрят реестр и приложение
    auto getClass = reinterpret_cast<HRESULT(__stdcall*)(REFCLSID, REFIID, void**)>(
        GetProcAddress(dll, "DllGetClassObject"));
    ComPtr<IClassFactory> factory;
    getClass(CLSID_DDSThumbnailHandler, IID_PPV_ARGS(&factory));
    ComPtr<IThumbnailProvider> provider;
    factory->CreateInstance(nullptr, IID_PPV_ARGS(&provider));
    ComPtr<IInitializeWithStream> init;
    provider.As(&init);
    ComPtr<IStream> stream;
    SHCreateStreamOnFileEx(argv[1], STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &stream);
    init->Initialize(stream.Get(), STGM_READ);
    HBITMAP bmp{}; WTS_ALPHATYPE alpha{};
    HRESULT hr = provider->GetThumbnail(256, &bmp, &alpha);
    // dump BMP + SHA-256 → сравнение с tests/assets/expected.json
}
```

### 7.4 CI

```yaml
jobs:
  build:
    runs-on: windows-2022
    strategy: { matrix: { config: [Debug, Release] } }
    steps:
      - uses: actions/checkout@v4        # submodules: recursive
      - run: cmake --preset windows-x64-${{ matrix.config }}
      - run: cmake --build --preset windows-x64-${{ matrix.config }}
      - run: ctest --preset windows-x64-${{ matrix.config }} --output-on-failure
```

Рендер-тесты в CI — только WARP (`D3D_DRIVER_TYPE_WARP`), т.к. GPU на раннерах нет.

### 7.5 Ручной чеклист (сокращённо, полностью — Приложение D)

Превью: 20 форматов; файл на сетевом диске; файл только для чтения; файл > 100 МБ; битый файл; два разных DDS-обработчика; регистрация без admin; unregister → прежний обработчик на месте; изменение файла → обновление превью после сброса кэша.
Вьюер: все клавиши из «Keyboard Shortcuts»; колесо над разными точками; перетаскивание за границу окна; ресайз при 60 fps; спящий режим → возврат; смена разрешения; DPI 100/150/200 %; кубмап; HDR; mip-навигация; «Файл → Перезагрузить» после правки файла.

---

## 8. Риски

| Риск | Вероятность | Влияние | Мера |
|---|---|---|---|
| Регрессия видимого поведения при распиле `main.cpp` | средняя | среднее | чеклист до/после + golden-рендер-тесты на WARP |
| Ошибка регистрации ломает превью у пользователей | средняя | высокое | HKCU по умолчанию, восстановление прежнего значения, тестовая VM |
| Explorer держит DLL → `LNK1104` при сборке | высокая | низкое | собирать в `bin\`, ставить копированием; хелпер «unregister + перезапуск Explorer» |
| CMake-миграция затягивается | средняя | среднее | этап 1 закрывает сборку на MSBuild; CMake — отдельный коммит с проверкой паритета флагов |
| Пиннинг DirectXTex ломает сборку на старом SDK | низкая | среднее | CI на `windows-2022`, фиксированный тег + `WindowsTargetPlatformVersion` |
| Расхождение «эталонного» рендера с реальным GPU | средняя | низкое | эталоны для WARP; на GPU — только smoke + визуальная проверка |
| Требование `/MT` для DLL тянет пересборку DirectXTex | средняя | среднее | решается на этапе 3 замером; по умолчанию `/MD` + Redist в установщике |
| Смена CLSID/ключей реестра ломает существующие установки | низкая | высокое | CLSID и GUID интерфейса не меняются; добавочные ключи только новые |

---

## 9. Метрики «до/после»

| Метрика | До | После |
|---|---|---|
| Размер репозитория (без `.vs`) | 64,6 МБ | < 3 МБ (FetchContent) / ~16 МБ (submodule) |
| Рабочих конфигураций сборки | 1 из 4 | 2 из 2 (x64 Debug/Release), +ARM64 опционально |
| Параллельная сборка | падает | проходит |
| Свой код, макс. файл | 824 строки | ≤ 300 строк |
| Изменяемые глобалы | ~40 | 0 |
| Дублирование DirectXTex | 2 копии | 1 |
| Тесты | 0 | unit + integration + golden |
| Idle-нагрузка | постоянный рендер | рендер только по изменению |
| Диагностика сбоя превью | нет | лог + `HRESULT` в сообщении |
| Установка | `regsvr32` + UAC + перезапуск Explorer | per-user, без admin |

---

## 10. Первый день (конкретно)

```powershell
cd F:\repos\WTVNext
git init
# .gitignore из Приложения A
git add -A
git commit -m "chore: baseline as-is (Release|x64 builds)"
Remove-Item -Recurse -Force x64, WTVNext\x64, WTVNextApp\x64, .vs
git rm -r --cached x64 WTVNext\x64 WTVNextApp\x64 .vs 2>$null
git commit -m "chore: drop build artifacts from VCS"
```
Затем — этап 1: `Directory.Build.props` (Приложение B) + удаление `#pragma comment(lib ...)`; проверить `MSBuild /m` для Debug и Release.

---

## Приложение A. `.gitignore`

```gitignore
# build output
x64/  Win32/  ARM64/  bin/  obj/  out/
*.obj  *.o  *.pdb  *.ilk  *.iobj  *.ipdb  *.exp  *.lib  *.exe  *.dll  *.res  *.tlog
*.vcxproj.user  *.suo  *.user  *.aps  *.ncb  *.sdf
# visual studio
.vs/
# cmake
CMakeUserPresets.json  CMakeCache.txt  CMakeFiles/
# misc
Thumbs.db  desktop.ini  .DS_Store  *.log
# generated
src/tests/assets/generated/
src/app/shaders/*.cso
```

## Приложение B. `Directory.Build.props` (hotfix этапа 1)

```xml
<Project>
  <PropertyGroup>
    <OutDir>$(SolutionDir)bin\$(Platform)\$(Configuration)\</OutDir>
    <IntDir>$(SolutionDir)obj\$(ProjectName)\$(Platform)\$(Configuration)\</IntDir>
    <WindowsTargetPlatformVersion>10.0.26100.0</WindowsTargetPlatformVersion>
    <LanguageStandard>stdcpp20</LanguageStandard>
    <WarningLevel>Level4</WarningLevel>
    <ExternalWarningLevel>Level0</ExternalWarningLevel>
    <TreatWarningAsError>false</TreatWarningAsError>   <!-- true после этапа 2 -->
    <AdditionalOptions>/utf-8 /permissive- %(AdditionalOptions)</AdditionalOptions>
    <DirectXTexDir>$(SolutionDir)external\DirectXTex\</DirectXTexDir>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>$(DirectXTexDir);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
    <Link>
      <AdditionalLibraryDirectories>$(SolutionDir)external\DirectXTex\lib\$(Configuration)\;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
    </Link>
  </ItemDefinitionGroup>
</Project>
```

## Приложение C. Эскиз корневого `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.28)
project(WTVNext VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/Version.cmake)
include(cmake/Warnings.cmake)

# --- DirectXTex: одна копия, фиксированная ревизия -------------------------
# В репозитории DirectXTex есть корневой CMakeLists.txt, он определяет цель
# DirectXTex (и, при желании, утилиты Texconv/Texassemble — их сборку отключить).
include(FetchContent)
FetchContent_Declare(DirectXTex
  GIT_REPOSITORY https://github.com/microsoft/DirectXTex.git
  GIT_TAG        <pinned-tag>)           # зафиксировать тег/коммит!
FetchContent_MakeAvailable(DirectXTex)

# --- ядро ------------------------------------------------------------------
add_library(wtv_core STATIC
  src/core/TextureFile.cpp src/core/FormatInfo.cpp src/core/Thumbnail.cpp
  src/core/FolderNavigator.cpp src/core/PathUtils.cpp src/core/Log.cpp)
target_include_directories(wtv_core PUBLIC src)
target_link_libraries(wtv_core PUBLIC DirectXTex)
wtv_apply_warnings(wtv_core)

# --- shell extension -------------------------------------------------------
add_library(wtv_thumbnails SHARED
  src/thumbnails/DllMain.cpp src/thumbnails/Module.cpp
  src/thumbnails/ClassFactory.cpp src/thumbnails/ThumbnailProvider.cpp
  src/thumbnails/Registration.cpp)
target_link_libraries(wtv_thumbnails PRIVATE wtv_core shlwapi shell32 ole32 uuid)
set_target_properties(wtv_thumbnails PROPERTIES OUTPUT_NAME "WTVNext"
                      PREFIX "" LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/thumbnails/WTVThumbnail.def")
target_compile_definitions(wtv_thumbnails PRIVATE WTV_THUMBNAILS_EXPORTS)

# --- приложение ------------------------------------------------------------
add_executable(wtv_app WIN32
  src/app/main.cpp src/app/App.cpp src/app/MainWindow.cpp src/app/StatusBar.cpp
  src/app/TextureView.cpp src/app/Camera.cpp src/app/Renderer.cpp
  src/app/Settings.cpp src/app/ShellExtensionAdmin.cpp
  src/app/WTVNextApp.rc src/app/shaders/media.hlsl)
target_link_libraries(wtv_app PRIVATE wtv_core d3d11 dxgi d3dcompiler comctl32)
set_target_properties(wtv_app PROPERTIES OUTPUT_NAME "WTVNextApp")
add_dependencies(wtv_app wtv_thumbnails)          # DLL рядом с EXE

# --- тесты -----------------------------------------------------------------
option(WTV_BUILD_TESTS "Build tests" ON)
if(WTV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(src/tests)
endif()
```

## Приложение D. Ручной чеклист релиза (v1)

**Превью (Проводник)**
- [ ] 20 файлов корпуса: превью есть, пропорции верные, края с альфой без тёмной каймы
- [ ] BC1 без альфы → `WTSAT_RGB` (нет артефактов композиции)
- [ ] файл 8192×8192 → превью ≤ 256 px, нет OOM, время < 300 мс
- [ ] битый / пустой / обрезанный файл → нет превью, Explorer жив, запись в лог
- [ ] файл на сетевом ресурсе (SMB) и в OneDrive-папке
- [ ] после `register` без admin превью появляются
- [ ] после `unregister` прежний обработчик на месте
- [ ] сброс кэша превью (`ie4uinit.exe -show`) обновляет картинку

**Вьюер**
- [ ] все клавиши из «Keyboard Shortcuts» работают, включая `Ctrl+O`, `F5`
- [ ] колесо над углом текстуры — зум в точку под курсором
- [ ] перетаскивание за пределы окна не даёт скачков
- [ ] ресайз окна: без мерцания и чёрных полос, `ResizeBuffers` без ошибок
- [ ] смена разрешения экрана / выход из сна → картинка восстанавливается
- [ ] sRGB-текстура совпадает с эталоном; HDR-текстура с регулировкой экспозиции
- [ ] кубмап: переключение граней
- [ ] DPI 100 / 150 / 200 %; второй монитор с другим DPI
- [ ] D3D11 debug layer: 0 сообщений уровня ERROR за прогон
- [ ] простой 30 с → загрузка GPU ≈ 0 %, CPU ≈ 0 %

## Приложение E. Приоритетный бэклог (по одной строке)

P0: Debug-сборка (B1, B2) · Win32-конфигурации (B3) · `/m` (B4) · git+.gitignore (B5) · одна копия DirectXTex (B6) · пиннинг SDK (B7)
P1: барьер исключений (T1) · цикл чтения IStream (T2) · лимит размера (T3) · premultiply (T4, T5) · HKCU-регистрация (T6) · восстановление обработчика (T7) · `CreateCompatibleDC` (T10) · координаты колеса (V1) · `GET_X_LPARAM` (V2) · `ResizeBuffers` (V3) · device-lost (V4) · sRGB (V6) · кубмапы (V8) · акселераторы (V9) · сообщения об ошибках (V10) · DPI (V12) · иконка (V13)
P2: глобалы (A1) · распил `main.cpp` (A2) · `TextureLoader.h` (A3) · ресурсы (A7) · мёртвый код (A8) · стиль (A10)
P3: README/LICENSE (P2) · тесты (P3) · CI (P4) · установщик (P5) · `/utf-8` (P6)

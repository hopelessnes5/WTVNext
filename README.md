# WTVNext — Windows Texture Viewer Next Gen

Просмотрщик текстур (DDS, TGA, PNG, JPG, HDR) для Windows и shell-extension,
показывающий превью `.dds` прямо в Проводнике (миниатюры BC1–BC7, HDR/BC6H, mips).

## Состав

| Каталог | Что это |
|---|---|
| `WTVNext/` | COM shell-extension DLL (`WTVNext.dll`) — провайдер эскизов `.dds` |
| `WTVNextApp/` | Win32/D3D11 просмотрщик (`WTVNextApp.exe`) |
| `external/DirectXTex/` | vendored DirectXTex (одна копия) + предсобранные `.lib` |
| `docs/` | план рефакторинга и сопутствующие документы |

## Сборка

Требуется Visual Studio 2022 (toolset v143) и Windows 10/11 SDK.

1. Открыть `WTVNext.sln`.
2. Собрать конфигурацию **Release | x64** (другие конфигурации пока не заведены —
   см. этап 1 в [docs/deepseek-WTVNext-refactoring-plan.md](docs/deepseek-WTVNext-refactoring-plan.md)).
3. Артефакты: `x64\Release\WTVNext.dll` и `x64\Release\WTVNextApp.exe`.

DirectXTex линкуется предсобранными `.lib` из `external/DirectXTex` — исходники
и проекты находятся там же, при необходимости их можно пересобрать.

## Регистрация превью `.dds`

Запустить `WTVNextApp.exe` → меню **Help → Register Shell Extension (Thumbnails)**
(запросит права администратора, вызывает `regsvr32`). Отмена регистрации —
соответствующий пункт того же меню.

## Статус

Выполнена гигиена репозитория (этап 0 плана). Дальше — этап 1: починка
Debug-конфигурации, параллельной сборки и Win32-платформы (см. план).

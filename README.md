# DXF Sketcher

DXF Sketcher — это 2D-редактор для DXF, заточенный под быструю и понятную повседневную работу.

Проект сделан на базе [dune3d](https://github.com/dune3d/dune3d) и переработан в сторону более удобного DXF-скетчинга без лишней 3D-сложности в основном сценарии.

Репозиторий: <https://github.com/EriArk/-DXF-Sketcher>

## Что это дает обычному пользователю

- Быстрый старт: открыл DXF и сразу редактируешь.
- Папочный сценарий: можно открыть целую папку DXF за один шаг.
- Понятное сохранение: `Save` сохраняет текущий скетч в привязанный файл, `Save As` экспортирует `DXF` или `SVG`.
- Более прямое управление инструментами: меньше лишних действий в типовых задачах.

## Два подхода работы (и можно сочетать оба)

В DXF Sketcher сохранен **параметрический подход**:
- геометрия и ограничения (constraints) остаются основой точного редактирования;
- размеры, зависимости и параметрическая логика никуда не исчезли.

И одновременно добавлен **более “объектный” стиль работы**, ближе к графическим редакторам:
- быстрее выбирать, двигать и править то, что видишь на сцене;
- больше прямых действий из интерфейса (toolbar/popover, локальные hotkeys, быстрые режимы инструментов).

Итог: пользователь сам выбирает, как работать в каждом моменте — строго параметрически, более свободно “как в графическом редакторе”, или смешанно.

## Что изменено в этом форке

### DXF-first workflow

- `Open file` открывает сразу несколько файлов.
- `Open folder` импортирует все `*.dxf` из выбранной папки.
- Каждый импортированный DXF становится отдельным sketch group.
- Повторный импорт уже открытого файла распознается, дубли не создаются.

### Упрощенный интерфейс для 2D

- Режим сборки `-Dsketcher_only=true` убирает 3D/solid-model шум из обычного потока.
- Переработан верхний toolbar под частые 2D-задачи.
- Быстрые popover-настройки для grid/symmetry и связанных инструментов.
- Переключение видимости sidebar по `Tab`.

### Инструменты, ускоряющие работу

- Rectangle: corner/center, square mode, rounded corners, радиус скругления.
- Circle: circle/oval, slice/sector, управление углом дуги.
- Regular Polygon: быстрое изменение числа сторон, rounded corners.
- Text: быстрые toggles для bold/italic и настройки шрифта.
- Import Picture: вставка растров и SVG-путей в скетч.

### Горячие клавиши внутри инструментов

- Добавлена отдельная система **in-tool keybindings**.
- Настраивается в Preferences.
- В Help выводятся и основные шорткаты, и in-tool шорткаты.

## Быстрый старт

1. Запустите DXF Sketcher.
2. Нажмите `Open file` (один или несколько DXF) или `Open folder` (вся папка DXF).
3. Отредактируйте активный скетч.
4. Нажмите `Save` для сохранения в привязанный файл, либо `Save As` для DXF/SVG.

## Текущие ограничения

- В режиме sketch-open редактируемый вход через `Open file` ориентирован на DXF.
- Прямое открытие SVG как документа в этом потоке пока не реализовано.
- SVG можно импортировать в скетч через `Import Picture`.

## Сборка (для разработчиков)

### Рекомендуемый режим этого форка

```bash
meson setup build-sketcher -Dsketcher_only=true
meson compile -C build-sketcher
./build-sketcher/dune3d
```

### Полный режим (ближе к upstream)

```bash
meson setup build
meson compile -C build
./build/dune3d
```

## Лицензия и используемые проекты

### Лицензия проекта

- DXF Sketcher распространяется под **GNU GPL v3.0** (см. [LICENSE](LICENSE)).

### Сторонние компоненты в репозитории

По текущему содержимому `3rd_party/` используются, в частности:

- **SolveSpace** — GPL (см. `3rd_party/solvespace/COPYING.txt`)
- **dxflib** — GPL v2 or later / dual-licensed upstream (см. `3rd_party/dxflib/gpl-2.0greater.txt` и заголовки `3rd_party/dxflib/*.h`)
- **Clipper2Lib** — Boost Software License 1.0 (см. заголовки `3rd_party/Clipper2Lib/include/clipper2/*.h`)
- **nlohmann/json** — MIT (SPDX в `3rd_party/nlohmann/*.hpp`)
- **NanoSVG** — permissive license (см. шапку `3rd_party/nanosvg.h`)

Если вы распространяете сборки, сохраняйте лицензионные уведомления и attribution для этих компонентов.

## Благодарности

Проект основан на работе и идеях открытого сообщества:

- [dune3d](https://github.com/dune3d/dune3d) — основа архитектуры и функциональности (Lukas K. и контрибьюторы)
- [SolveSpace](https://github.com/solvespace/solvespace) — параметрическая CAD-база/наследие подхода
- [dxflib](https://www.ribbonsoft.com/dxflib.html) — DXF parsing/writing foundation
- [Clipper2](http://www.angusj.com/clipper2/Docs/Overview.htm) — геометрические операции
- [nlohmann/json](https://github.com/nlohmann/json) — JSON-инфраструктура
- [NanoSVG](https://github.com/memononen/nanosvg) — SVG parsing

Отдельное спасибо всем контрибьюторам DXF Sketcher и upstream-проектов.

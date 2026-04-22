# DXF Sketcher — AppImage / Release Polish Track

Этот файл описывает отдельный трек работы по AppImage и release-quality.

Важно:
- это **часть общего polish backlog**,
- но это **не автоматический first priority**,
- нужно сравнивать этот трек с другими кандидатами на сессию.

---

## Почему этот трек существует

У проекта уже есть Linux AppImage release.
Но появились два явных сигнала, что AppImage-layer еще не дополирован:

1. в AppImage-сборке часть иконок отображается как заглушки / placeholders;
2. есть запрос сделать AppImage более удобным для Gear Lever / update-management сценария.

Значит release artifact надо рассматривать как часть продукта, а не как побочный файл “для галочки”.

---

# Track A — AppImage icon regression

## Симптом

В обычной среде / обычной сборке иконки выглядят нормально или лучше,
но в AppImage часть UI icons деградирует и превращается в заглушки.

Это user-visible defect, даже если основная логика программы при этом работает.

---

## Grounded starting hypotheses

Это **не доказанные причины**, а обоснованные стартовые гипотезы, которые нужно проверить по коду и artifact.

### Hypothesis A1
`build_appimage.sh` может уходить в fallback path без GTK plugin.

Это важно, потому что такой fallback может оставить AppImage без части GTK-related runtime/theme/icon setup.

### Hypothesis A2
Packaging явно заботится о launcher/app icon, но не гарантирует полноту внутренних theme-dependent UI icons.

### Hypothesis A3
Часть UI использует theme icon names, и какая-то их часть не гарантирована внутри AppImage / на хосте / в используемой icon theme.

### Hypothesis A4
Проблема может быть не только в theme icons, но и в loaders / icon search paths / runtime environment of the AppImage.

---

## Required investigation flow for Track A

### 1. Reproduce carefully
- Сравнить native build и AppImage build.
- Зафиксировать, какие именно иконки ломаются.
- Проверить, стабильна ли проблема или зависит от окружения.

### 2. Inspect packaging path
Проверить минимум:
- `scripts/build_appimage.sh`
- `meson.build`
- desktop file
- install paths for icons
- содержимое итогового `AppDir`

### 3. Check if GTK fallback path was actually used
Нельзя гадать.
Нужно проверить:
- лог сборки,
- был ли `--plugin gtk` успешен,
- не был ли silently использован fallback без него.

### 4. Audit icon sources in app code
Найти:
- где UI использует theme icon names,
- какие из них критичны и видимы часто,
- есть ли fallback logic,
- нет ли fragile dependence на specific Adwaita-only icon names.

### 5. Inspect artifact contents
Проверить в AppDir:
- `usr/share/icons`
- `usr/share/glib-2.0/schemas`
- gtk-related runtime assets
- gdk-pixbuf / SVG loaders, если это релевантно
- desktop/icon metadata

### 6. Choose the right fix class
Предпочтительные классы фикса:
- гарантированно доставить нужные icon/theme assets;
- уменьшить опасную theme-dependence;
- для критичных UI icons использовать app-owned or robust fallback assets;
- исправить packaging script / AppRun / environment only if это реальная причина.

Нежелательный класс фикса:
- случайно заменить пару кнопок на другие иконки, не поняв системную причину.

---

## Acceptance criteria for Track A

Шаг считается хорошим, если:
- проблема подтверждена и локализована честно;
- выбранная причина не выдумана, а проверена;
- AppImage показывает критичные иконки корректно;
- решение не ломает native build;
- worklog отражает, что проверено, а что пока нет.

---

# Track B — AppImage update metadata / Gear Lever friendliness

## Что имелось в виду на втором скриншоте

Идея такая:
- у AppImage может быть встроена update information;
- совместимые инструменты управления AppImage могут читать ее и понимать, откуда искать обновление;
- если вместе с release публикуется нужный update companion artifact, обновление может стать заметно удобнее.

Если update information не встроена, пользователю часто приходится настраивать update source вручную, либо обновления вообще остаются внешним ручным процессом.

---

## Desired end state for Track B

Для DXF Sketcher желательно прийти к такому состоянию:
- AppImage содержит корректную embedded update information;
- release flow генерирует нужный companion update artifact;
- этот artifact публикуется вместе с AppImage;
- Gear Lever / compatible AppImage tooling получает шанс работать “из коробки” или почти из коробки;
- release process документирован и повторяем.

---

## Required investigation flow for Track B

### 1. Confirm current state
Нужно честно проверить:
- встраивается ли сейчас update information вообще;
- генерируется ли companion update artifact;
- публикуется ли он в release.

### 2. Inspect current build path
Проверить:
- `scripts/build_appimage.sh`
- используется ли `UPDATE_INFORMATION` / `UPD_INFO`
- используется ли явный `appimagetool -u ...`
- есть ли release-side логика публикации нужного файла.

### 3. Choose the target scheme
Для DXF Sketcher как GitHub-hosted release разумный кандидат — update scheme, совместимый с GitHub releases и `.zsync`-based update flow.

### 4. Implement reproducibly
Предпочтительно:
- build script сам умеет собрать update-aware AppImage;
- рядом генерируется companion update file;
- release process умеет публиковать оба;
- naming и docs согласованы.

### 5. Document the release process
Нужно зафиксировать:
- что именно запускать,
- какие переменные обязательны,
- какой файл загружать в release вместе с `.AppImage`,
- что считать успешной проверкой.

---

## Acceptance criteria for Track B

Шаг считается хорошим, если:
- текущее состояние честно подтверждено;
- update path не держится на ручной магии после сборки;
- release process становится понятнее и надежнее;
- worklog отражает ограничения и непроверенные места.

---

# Session choice guidance for this track

Выбирать AppImage-трек особенно уместно, если:
- нужен сильный user-visible polish шаг без глубокого рефакторинга;
- есть packaging bug, который портит впечатление от релиза;
- нужен product-maturity шаг на Linux release side;
- есть достаточно контекста, чтобы аккуратно сделать один ограниченный packaging move.

---

# Anti-hack guardrails

Не делай так:
- не объявляй причину подтвержденной без проверки build path и artifact;
- не считай launcher icon fix полным решением внутренних UI icon bugs;
- не тащи в один заход и icon regression, и auto-update system, и полную CI migration, если это уже слишком большой scope;
- не завязывай решение на один desktop environment без объяснения ограничений.

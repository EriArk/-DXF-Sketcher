# DXF Sketcher — Refactor / Improvement Plan

Это не жесткая очередь задач.
Это **структурированный backlog с приоритетами и зависимостями**.

Используй его так:
- не игнорируй высокий приоритет,
- но и не считай, что первый незакрытый пункт всегда обязан быть следующим,
- каждый раз выбирай лучший следующий шаг по impact / risk / readiness / session fit.

---

## North star

В конце DXF Sketcher должен ощущаться:
- как цельный продукт,
- как надежный workshop DXF tool,
- как визуально собранное desktop-приложение,
- как кодовая база, в которой можно безопасно продолжать улучшения,
- как release, который не портится при переходе от обычной сборки к AppImage.

---

# P0 — obvious rough edges and correctness

## P0.1 Исправить вертикальную линейку
### Цель
Сделать вертикальную ruler-подпись читаемой, устойчивой и несломленной.

### Что важно
- нормальная читаемость,
- адекватная ориентация и placement,
- устойчивость к zoom / DPI / scale factor,
- major/minor ticks остаются понятными.

### Комментарий
Это важный шаг, но **не обязан быть первым**, если другая сессия сильнее продвинет polish при меньшем риске.

---

## P0.2 Выстроить хороший pattern для control rows
### Цель
Уйти от ощущения ручной слепки параметрических строк.

### Ожидаемый результат
- единая сетка,
- логичное место для units,
- ровные строки,
- консистентные selectors/toggles/actions,
- foundation для дальнейшего распространения.

### Подход
Сначала сделать **один хороший reusable pattern**, потом применять дальше.

---

## P0.3 Убрать самые режущие глаз legacy / placeholder хвосты
### Примеры
- мусорные тексты,
- fork-debt remnant labels,
- debug-ish menu items,
- очевидные времянки в UI.

### Почему это допустимый ранний шаг
Это быстро повышает ощущение зрелости продукта и часто хорошо подходит для первой аккуратной сессии.

---

# P1 — visible product cohesion

## P1.1 Применить новый UI pattern к одному сильному экрану
### Лучшие кандидаты
- gear generator,
- один key inspector-like panel,
- один из frequently used parameter dialogs.

### Результат
Один экран должен стать явным референсом качества.

---

## P1.2 Generator / inspector consistency wave
### Задачи
- унификация spacing,
- единый подход к left labels,
- units presentation,
- selector patterns,
- primary action placement,
- section hierarchy.

---

## P1.3 Product polish review and cleanup wave
### Смотреть особенно
- стартовый экран,
- popovers,
- dialogs,
- status / hints / copy,
- consistency across themes,
- визуальные мелочи, которые делают продукт “сырым”.

---

## P1.4 Убрать AppImage-specific icon / placeholder regressions
### Симптом
В AppImage-сборке часть иконок может заменяться заглушками или визуально ломаться, хотя обычная сборка выглядит лучше.

### Цель
Сделать AppImage по визуальному качеству не хуже обычной Linux-сборки.

### Что проверить
- build path с GTK plugin и fallback path без него,
- что реально попадает в AppDir,
- где используются theme icons,
- не зависит ли часть UI от fragile icon-theme assumptions,
- нет ли missing loaders / missing theme assets / missing icon search path.

### Acceptance idea
Критичные toolbar / popover / frequently visible icons отображаются корректно в AppImage на реальном artifact.

---

# P2 — architecture that supports polish

## P2.1 Shared UI helpers
### Цель
Чтобы хорошие UI-решения не приходилось пересобирать каждый раз вручную.

### Возможные элементы
- inspector row base,
- numeric field with units,
- plus/minus stepper wrapper,
- section/group widget,
- helper for primary/secondary actions.

---

## P2.2 Safe decomposition of big mixed-responsibility files
### Кандидаты
- editor-related monoliths,
- canvas/ruler/overlay mixed zones,
- крупные UI assembly files.

### Правило
Только по ответственности.
Не смешивать с лишними поведенческими изменениями.

---

## P2.3 Logging / diagnostics cleanup
### Цель
Убрать случайный debug output и сделать диагностику более дисциплинированной.

---

# P3 — higher-order polish

## P3.1 Better previews
### Идеи
- вспомогательные оси,
- компактный summary,
- внятные warnings,
- более уверенное ощущение инструмента.

---

## P3.2 Better learnability
### Идеи
- аккуратнее hints,
- меньше хаоса в popovers,
- более очевидные affordances.

---

## P3.3 Release feel
### Идеи
- cleaner copy,
- README / screenshot consistency,
- packaging / metadata polish,
- platform-specific finish.

---

## P3.4 AppImage update metadata / Gear Lever friendliness
### Цель
Сделать Linux AppImage release не только запускаемым, но и update-friendly для совместимых AppImage managers.

### Что сюда входит
- проверить, встроена ли update information в AppImage;
- при необходимости внедрить embedding update information в build/release flow;
- генерировать и публиковать нужный companion update artifact;
- документировать release steps;
- по возможности сделать так, чтобы Gear Lever и похожие инструменты могли обновлять DXF Sketcher без ручной магии пользователя.

### Важно
Это не обязано быть ранним шагом, но это **официальная часть polish backlog**.

---

# Flexible execution guidance

## Хорошие ранние входные точки
Можно стартовать с любой из них, если это сейчас лучший шаг:
- P0.1 vertical ruler,
- P0.2 reusable control rows foundation,
- P0.3 obvious legacy cleanup,
- P1.1 one high-value generator panel makeover,
- P1.4 AppImage icon regression investigation/fix.

## Чего не делать слишком рано
- giant file decomposition без необходимости,
- broad styling rewrites без system pattern,
- deep refactor without user-visible payoff,
- unrelated feature expansion,
- packaging hacks без проверки реального artifact.

---

# Success criteria

План считается хорошо реализуемым, если по мере прогресса:
- исчезают явные шероховатости и баги,
- хотя бы один экран становится эталоном нового качества,
- появляются reusable UI patterns,
- cleanup уменьшает ощущение форка и времянок,
- AppImage artifact не деградирует по иконкам / metadata,
- update-friendly release flow либо реализован, либо как минимум четко спроектирован и документирован,
- архитектура облегчается ровно настолько, насколько нужно следующему шагу,
- в итоге программа ощущается polished, cohesive and intentional.

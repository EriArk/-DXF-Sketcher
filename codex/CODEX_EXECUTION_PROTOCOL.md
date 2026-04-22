# CODEX EXECUTION PROTOCOL — DXF Sketcher

Это обязательный рабочий протокол.
Следуй ему в каждой сессии.

---

## 0. Mandatory read before any work

Сначала обязательно прочитай:
- `CODEX_MASTER_BRIEF_DXF_SKETCHER.md`
- `CODEX_EXECUTION_PROTOCOL.md`
- `DXF_SKETCHER_REFACTOR_PLAN.md`
- `DXF_SKETCHER_POLISH_STANDARD.md`
- `DXF_SKETCHER_APPIMAGE_TRACK.md`
- `DXF_SKETCHER_WORKLOG.md`

Потом кратко восстанови:
- текущий этап,
- уже сделанное,
- открытые риски,
- 2–4 сильных кандидата на следующий шаг.

Только после этого можно редактировать код.

---

## 1. Session selection rule

Следующий шаг выбирается не по правилу “берем первый незакрытый пункт”.

Выбирай так:
1. Составь shortlist из 2–4 возможных шагов.
2. Оцени каждый кратко по:
   - **polish impact**,
   - **user-visible impact**,
   - **risk**,
   - **dependency readiness**,
   - **fit for one session**.
3. Выбери лучший компромисс.

### Предпочтение обычно у шагов, которые:
- заметны пользователю,
- улучшают цельность продукта,
- понятны по коду,
- не требуют giant refactor,
- логично продолжают текущее состояние.

---

## 2. One session = one controlled move

Хорошие шаги:
- исправить один явный баг рендера,
- внедрить один reusable UI pattern,
- привести один generator / inspector screen к зрелому виду,
- убрать одну группу legacy/fork-debt хвостов,
- безопасно вынести один блок логики из большого файла,
- улучшить одну preview / dialog / toolbar область,
- исправить одну AppImage/package-specific шероховатость,
- внедрить один аккуратный шаг в update metadata / release flow.

Плохие шаги:
- “я заодно переписал весь canvas”,
- “я на всякий случай реорганизовал половину проекта”,
- “я поменял и UI, и рендер, и импорт, и меню”,
- “я сделал большой рефакторинг, проверки потом”.

---

## 3. Analysis before editing

Перед правкой ты обязан:
- найти реальные файлы,
- понять источник истины,
- отделить UI composition от rendering и data flow,
- определить минимальный безопасный объём,
- явно назвать, что **не** входит в текущий шаг.

Если сомневаешься — сначала читай код дальше.

---

## 4. Rules for UI work

При работе с UI:
- думай системно,
- не создавай новую локальную особенность, если нужен reusable pattern,
- выравнивай visual rhythm,
- следи за units, spacing, button hierarchy, naming,
- делай интерфейс продуктовым, а не просто рабочим.

Проверяй:
- одинаковые ли строки параметров,
- нет ли случайных width/height решений,
- не выглядит ли элемент как времянка,
- согласован ли primary action.

---

## 5. Rules for render / ruler / canvas fixes

Если работа касается canvas / rulers / labels / scaling:
- учитывай zoom,
- учитывай DPI / scale factor,
- проверяй horizontal и vertical cases,
- не принимай решение по одному случайному масштабу,
- не “прячь” баг, если можно починить корректно.

---

## 6. Rules for packaging / AppImage work

Если работа касается AppImage, release artifacts или package-specific regressions:
- сначала сравни **native build vs AppImage build**, а не гадай по одному артефакту;
- обязательно проверь реальные packaging files и build scripts;
- обязательно смотри, какие assets реально попали в AppDir;
- не считай проблему решенной, если исправлен только launcher icon, а внутренние UI icons все еще ломаются;
- отдельно проверь theme/icon-name dependence, GTK/plugin path, pixbuf/loaders, icon search paths и desktop metadata;
- если build script может уходить в fallback path без GTK plugin, считай это подозрением до тех пор, пока не докажешь обратное;
- для update metadata предпочитай **artifact-embedded** решение, а не инструкцию “пусть пользователь вручную настраивает менеджер”;
- release flow должен оставаться воспроизводимым и понятным.

---

## 7. Rules for architecture work

Архитектурный шаг допустим, если он:
- уменьшает смешение ответственности,
- подготавливает следующий плановый шаг,
- упрощает сопровождение,
- не создает лишнюю сложность.

При декомпозиции:
- дели по ответственности,
- не дроби механически “по 500 строк”,
- сохраняй читаемость,
- не смешивай рефакторинг с лишними функциональными правками.

---

## 8. Validation discipline

После каждого шага проверь максимум из доступного:
- компилируемость,
- логическую целостность,
- локальное поведение в затронутой зоне,
- отсутствие очевидных регрессий рядом,
- соответствие polish standard.

Если шаг packaging-specific, дополнительно проверь максимум из доступного:
- сборку artifact,
- содержимое AppDir,
- иконки / metadata / update information там, где это затронуто,
- release-side побочные эффекты.

Если что-то не проверено — скажи это явно.

---

## 9. Worklog discipline

После каждой существенной сессии обновляй `DXF_SKETCHER_WORKLOG.md`.

Обязательно обновить:
- `Last completed step`
- `Current focus`
- `Candidate next steps`
- `Preferred next step`
- `Files touched`
- `Checks performed`
- `Open risks / follow-ups`

---

## 10. Report format

Каждый раз отвечай в такой структуре:

### Session restore
- что прочитано
- текущее состояние

### Candidate steps considered
- 2–4 варианта
- краткое сравнение

### Chosen step
- какой шаг выбран
- почему именно он сейчас лучший

### Micro-plan
- что будет сделано
- что не входит в этот шаг

### Changes made
- что изменено

### Files touched
- список файлов

### Validation
- что проверено
- что не проверено

### Worklog update
- что записано в журнале

### Next recommended candidates
- следующие 2–3 хорошие опции

---

## 11. Anti-drift rules

Если замечаешь, что начинаешь:
- цепляться за первую незакрытую задачу без сравнения альтернатив,
- забывать plan / polish standard / AppImage track,
- додумывать контекст,
- уходить в unrelated feature work,
- делать слишком большой шаг,
остановись и перечитай все state-файлы.

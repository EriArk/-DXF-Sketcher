# DXF Sketcher × Codex Pack v3

Это обновленная версия adaptive-pack для DXF Sketcher.
Теперь в нее отдельно добавлен **AppImage / release-polish track**, потому что у проекта появились еще две важные product-quality темы:

1. в AppImage-сборке часть иконок заменяется заглушками;
2. AppImage пока не дружит с Gear Lever настолько хорошо, насколько мог бы: нужно проверить и, если возможно, добавить корректную update metadata / auto-update-friendly схему.

Главная идея остается прежней:
- есть общий backlog,
- порядок гибкий,
- но цель строгая: сделать DXF Sketcher полированным, зрелым и цельным продуктом.

---

## Что нового в v3

### Новый файл: `DXF_SKETCHER_APPIMAGE_TRACK.md`
Это отдельный трек по release-quality и AppImage-polish.
Он нужен, чтобы Codex:
- не забывал про AppImage-специфические баги,
- не лечил их поверхностно,
- проверял packaging, иконки, theme-dependence, GTK plugin path, update metadata и release flow.

### Новый файл: `CODEX_APPIMAGE_FOCUS_PROMPT.txt`
Это специальный промпт, когда ты хочешь направить сессию именно в AppImage / packaging / release-polish работу.

---

## Что теперь читать в каждой сессии

Обязательный набор:
- `CODEX_MASTER_BRIEF_DXF_SKETCHER.md`
- `CODEX_EXECUTION_PROTOCOL.md`
- `DXF_SKETCHER_REFACTOR_PLAN.md`
- `DXF_SKETCHER_POLISH_STANDARD.md`
- `DXF_SKETCHER_APPIMAGE_TRACK.md`
- `DXF_SKETCHER_WORKLOG.md`

---

## Как использовать

### 1. Обычная рабочая сессия
Используй `CODEX_SESSION_PROMPT.txt`.

### 2. После compaction / потери нити
Используй `CODEX_CONTEXT_RECOVERY_PROMPT.txt`.

### 3. Если хочешь, чтобы Codex особенно внимательно занялся AppImage-полировкой
Используй `CODEX_APPIMAGE_FOCUS_PROMPT.txt`.

---

## Важное правило v3

AppImage-темы не обязаны быть **первыми** по очереди.
Но теперь они входят в официальный backlog и state-pack.
Значит Codex обязан:
- учитывать их при выборе следующего шага,
- сравнивать их с другими кандидатами,
- и брать их, если это лучший session move по impact / risk / readiness.

---

## Смысл второго скриншота

Там речь про то, что AppImage можно сделать более update-friendly для менеджеров вроде Gear Lever.
В идеале:
- внутри AppImage есть update information,
- рядом публикуется нужный update-asset,
- и тогда менеджер может понимать, откуда брать обновления без ручной возни пользователя.

Именно это теперь добавлено в pack как отдельный release-polish track.

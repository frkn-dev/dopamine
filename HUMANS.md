# HUMANS.md

## Current
- **Repo:** dopamine
- **Branch:** `dev`
- **Task:** импорт шар-подписки региона (`frkn://sub/shared/<uuid>`)
- **Status:** код готов, macOS-сборка проходит; не закоммичено
- **Updated:** 2026-10-21

## Done
- 2026-10-21 — `frkn://sub/shared/<uuid>`: ImportController распознаёт ссылку до `frkn://sub/`, CoreController гонит её тем же импортом с `shared=true`, ApiConfigsController собирает auth_data `{share_token}` для `/v1/services` + `/v1/config` (кэш config_rev для шар-подписок отключён). Сборка macOS локально зелёная.
- 2026-10-21 — AGENTS.md переписан по единому скелету (что читать первым, сборка/тесты, git, деплой, грабли, соседние репы); не закоммичено.
- 2026-10-21 — фикс импорта `frkn://sub/shared/<uuid>`: `mid(19)` → `mid(18)` (префикс = 18 символов, терялся первый символ UUID, regex не матчил → импорт падал всегда). Проверено отдельным Qt-бинарём: mid(19) match=false, mid(18) match=true. PKG пересобран.
- 2026-10-21 — падения демона на маке: `activate()` рекурсивно звал себя, когда `deactivate()` выходил рано по `m_deactivating` (регресс 83f2882f, 2026-10-06) и не чистил `m_connections` — 3035 кадров, stack overflow, launchd поднимал демон (14 крэшей 12–21.10 по `/var/log/Dopamine by FRKN/dopamine-service.log`). Guard в `client/daemon/daemon.cpp`, собран подписанный+нотаризованный PKG `deploy/build-macos-local/pkg/Dopamine-local-4.8.14.70.pkg`; не установлен и не закоммичен.
- 2026-10-20 — проверено: XHTTP/TLS идёт через Go TLS; `fp` применяется только для REALITY (`client/core/serialization/vless.cpp`). Для XHTTP fingerprint не передаётся.
- 2026-10-20 — `dev` на релизе 4.8.14.70.

## Next
- [ ] установить `deploy/build-macos-local/pkg/Dopamine-local-4.8.14.70.pkg` и погонять переподключения: в логе не должно быть серий «Already connected» / «Deactivation in progress» длиннее пары строк.
- [ ] проверить импорт шар-подписки после выката api 0.6.62 (регион `dev`, несколько нод).
- [ ] при необходимости Chrome fingerprint на XHTTP: вынести `fp` из REALITY-ветки и добавить в XHTTP-ссылки. Для совместимости не требуется.

## Blockers / Notes
- Рабочее дерево не чистое: изменены translations (`dopamine_*.ts`), `PageHome.qml`; есть untracked `.tmp_*.py`, deploy-бинарники, `IMG_8287.JPG` — не трогал.
- Секреты (`.p8`, `agw_private.pem`) не коммитить.

## Related
- `AGENTS.md`

# Hulk: бесплатная Windows-сборка без Visual Studio на твоём ноутбуке

Уже подготовлено: исходники, ReXGlue/NativeX скрипты и ручной GitHub Actions
workflow в публичном репозитории **AD010/Hulk-NativePC-Source**.
Приватный **AD010/hulk-cloud** с original Xbox 360 XEX остаётся закрытым.

## Нужно выполнить ТОЛЬКО создание секрета (не посылай его в чат)

1. Открой https://github.com/settings/personal-access-tokens/new
2. Token name: `Hulk-Private-CI`. Resource owner: `AD010`.
3. Expiration: 30 days или меньше.
4. Repository access: **Only select repositories** → выбрать **hulk-cloud**,
   НЕ публичный репозиторий и НЕ All repositories.
5. Repository permissions → **Contents: Read and write**.
   Это позволяет получить приватный default.xex и загрузить ZIP
   обратно в приватный Release. Остальные разрешения не нужны.
6. Нажми Generate token, скопируй его локально. **НЕ ОТПРАВЛЯЙ В ЧАТ.**
7. Открой https://github.com/AD010/Hulk-NativePC-Source/settings/secrets/actions
8. New repository secret → Name: `HULK_PRIVATE_REPO_TOKEN`.
   Secret: вставь токен → Add secret.

## Запуск без оплаты

1. Открой https://github.com/AD010/Hulk-NativePC-Source/actions
2. Выбери **Hulk NativePC - Free Public Windows Builder**.
3. Нажми **Run workflow**, ветка main → Run workflow.
4. Бесплатный Windows runner скачает оригинальный XEX
   из **приватного** GitHub Release, проверит SHA-256, соберёт
   NativeXRenderer, проверит ZIP и отправит результат в приватный
   https://github.com/AD010/hulk-cloud/releases/tag/hulk-ci-input-on-publish

**ВАЖНО:** публичные Actions logs доступны всем. XEX и сгенерированный
PPC-код не сохраняются в публичные artifacts, но сообщения об ошибках
компилятора теоретически могут содержать отдельные фрагменты guest-кода.
Не добавляй XEX/TU1 ни в репозиторий, ни в issue, ни в комментарии.

Текущий NativeX всё ещё экспериментальный. Компиляция не доказывает,
что Vulkan кадр работает. Европейский TU1 для прыжка в воду
ещё НЕ применён к американскому XEX: несовместимые подписи.

Публичный workflow запускается только вручную из main, не на PR
и не при обычном push. При отсутствии секрета он безопасно останавливается.

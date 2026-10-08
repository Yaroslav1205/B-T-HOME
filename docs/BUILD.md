# Збірка й перевірка

## Поточний профіль: Matter через Wi-Fi

Для `esp32c6_rgb8` команди `scripts/idf.ps1 -Action build|flash|monitor|menuconfig|size|reconfigure`
передають роботу до `scripts/matter.ps1`. Збірка відбувається у WSL `BTHMatter`
з ESP-IDF 5.4.1 та ESP-Matter 1.4.2; готові файли копіюються до
`build/matter-esp32c6_rgb8`. Windows SDK використовується лише для USB-прошивання
та монітора. [Налаштування середовища й підключення](MATTER.md).

У VS Code кнопка **ESP-IDF: Build** запускає Matter через Windows CMake-обгортку
та WSL. **Ctrl+Shift+B** запускає лише збірку.
Повний цикл: **Terminal → Run Task → B-T HOME: Build, Flash and Monitor (Matter Wi-Fi)**.
Після зміни налаштувань виконайте **Developer: Reload Window**.
Для конфігурації запускайте **B-T HOME: menuconfig Matter Wi-Fi**. Не запускайте
вбудований ESP-IDF `menuconfig` для цього профілю: Windows bridge не має цілі
`confserver`; Matter menuconfig має виконуватися в WSL.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/matter.ps1 -Action build
powershell -ExecutionPolicy Bypass -File scripts/matter.ps1 -Action flash -Port COM11
powershell -ExecutionPolicy Bypass -File scripts/matter.ps1 -Action monitor -Port COM11
```

Порт потрібно замінити на фактичний. Прошивання записує завантажувач, таблицю
розділів і застосунок; не стирає весь flash. Перед записом скрипт перевіряє,
що артефакти належать Matter-збірці. `-Action flash` використовує вже готові файли.

Опис native Windows-збірки нижче залишається для інших профілів і попередньої
версії Wi-Fi AP. Для AP на `esp32c6_rgb8` потрібно явно додати `-WifiAp`.

## Інструменти

- ESP-IDF **v5.4** — базова версія для локальної перевірки й образу CI. CMake допускає
  гілки 5.4 і 5.5; іншу версію потрібно окремо перевіряти та оновлювати політику.
- CMake 3.20+; Ninja і компілятори чипів встановлюються разом із ESP-IDF.
- Host-тести: C++17 compiler і CMake; у Windows — Visual Studio 2022 C++ Build Tools,
  у Linux — GCC або Clang. ESP-IDF для них не потрібна.
- Генератор модулів: Python 3.9+ без сторонніх пакетів.

Для ESP32-C6 Component Manager завантажує офіційний `espressif/esp-zigbee-lib 2.0.4`
за `main/idf_component.yml`; перша збірка потребує доступу до ESP Component Registry.
Інстальований SDK не змінюється. `managed_components/` не додається в Git,
`dependencies.lock` фіксує версії для останнього вибраного target; при зміні target
Component Manager перераховує залежності.

Профіль `esp32c6_rgb8` тепер використовує `boards/esp32c6_rgb8/partitions.csv`
із 2 МБ для застосунку та окремим NVS `zb_storage`. Для переходу зі старої версії
потрібна повна прошивка з таблицею розділів, а не лише запис застосунку.
Розташування основного NVS і початок застосунку збережено. [Zigbee](ZIGBEE.md).

## Windows і шлях із пробілами

### VS Code: збірка та налаштування SDK

У цій папці використовуйте задачі проєкту:

- `scripts/idf.ps1 -Action build -WifiAp` — попередня збірка Wi-Fi AP.
- **Terminal → Run Task…** → `B-T HOME: menuconfig legacy Wi-Fi AP` — налаштування SDK
  у терміналі. Збережіть зміни та вийдіть із menuconfig перед збіркою.

Обидві задачі викликають `scripts/idf.ps1` і працюють зі спільною конфігурацією
`build/esp32c6_rgb8/sdkconfig`. У menuconfig є розділ `B-T HOME mains measurement`.

Кнопка **ESP-IDF: Build your Project** налаштована на Matter через WSL.
Для попередньої Wi-Fi AP версії використовуйте скрипт із `-WifiAp`;
графічний **SDK Configuration Editor** замінює термінальний menuconfig.
Пряма native ESP-IDF збірка у фізичному шляху з пробілами не підтримується.

Помилка `Environment IDF_TARGET conflicts with esp32c6_rgb8` означає, що
розширення передало інший чип. У `.vscode/settings.json` для цього workspace
явно задано `idf.customExtraVars.IDF_TARGET = esp32c6`. Після зміни налаштувань
виконайте **Developer: Reload Window**. Не змінюйте профіль плати на ESP32,
щоб обійти цю перевірку.

Розширення використовує `build/matter-esp32c6_rgb8` і кореневий `CMakeLists.txt`
з `BTH_WINDOWS_MATTER_BRIDGE=ON`. Повторна конфігурація через `idf.py`
використовує той самий каталог джерел. Native-скрипт явно вимикає цю обгортку.
Відновлення після конфлікту старого CMake-кешу описане в [MATTER.md](MATTER.md).

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\idf.ps1 -Action build
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\idf.ps1 -Action size
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\idf.ps1 -Board generic_esp32s3
```

Скрипт активує SDK і використовує підтриманий CMake frontend. Для цієї папки з
пробілами створюється тимчасове відображення `R:` через `SUBST`. Сам `idf.py` у
перевіреній версії Python повертає такий шлях до фізичного через `realpath`, тому
скрипт викликає CMake напряму. Копії проєкту не створюються. `ccache` вимкнений
для передбачуваної локальної збірки без залежності від загального кешу користувача.

Використовуйте одну й ту саму літеру для наявного build-каталогу. Якщо `R:` зайнятий,
перед першою збіркою передайте `-Drive S`. Якщо літеру або розташування проєкту
змінено, видаліть **лише згенерований** `build/<profile>/` і зберіть заново;
збережіть потрібні menuconfig-зміни в defaults перед видаленням.

Скрипт звільняє власне відображення у `finally`. Після примусового завершення всього
процесу перевірте `subst`; видаляйте залишений диск тільки переконавшись, що це саме
відображення цього проєкту і активних збірок/моніторів уже немає. Паралельні збірки
на одній літері не запускаються: другий скрипт повідомить, що диск зайнятий.

`-Action flash -Port COM5` використовує той самий профіль і target `flash` CMake.
`monitor` потребує вже зібраного ELF. Прошивання апаратури не є частиною тестів.

## Linux/macOS або CI

Із активованою ESP-IDF, у шляху без пробілів:

```sh
unset IDF_TARGET
# Попередній Wi-Fi AP; основну Matter-збірку описано в MATTER.md.
idf.py -B build/esp32c6_rgb8 -D BTH_BOARD=esp32c6_rgb8 -D BTH_WITH_MATTER=OFF build
idf.py -B build/esp32c6_rgb8 -D BTH_BOARD=esp32c6_rgb8 -D BTH_WITH_MATTER=OFF menuconfig

cmake -S tests/host -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

`IDF_TARGET` задається профілем. Для іншого профілю змініть і `BTH_BOARD`, і каталог
збірки. Не використовуйте один build-каталог для різних плат.

## Конфігурація

Порядок першої конфігурації: `sdkconfig.defaults` → `boards/<profile>/sdkconfig.defaults`
→ локальний `build/<profile>/sdkconfig`. Наявний `sdkconfig` зберігає попередні
налаштування; редагування defaults не перезаписує вже обрані значення.

Для локальної зміни використовуйте menuconfig. Для зміни бази проєкту перенесіть
лише потрібні значення у відповідний defaults-файл і перевірте з чистою конфігурацією.
Не комітьте весь згенерований `sdkconfig`. Паролі, токени та ключі не належать до
defaults, профілів, заголовків чи логів.

Модулі налаштовуються в `B-T HOME modules`, цикл — у `B-T HOME application`.
Стартова RGB-індикація — у `B-T HOME startup indication`. Профіль за замовчуванням
`esp32c6_rgb8` має власний `build/esp32c6_rgb8/sdkconfig`; конфігурація старого
`generic_esp32c6` не переноситься автоматично.
Вимкнений heartbeat прибирає лише діагностичний модуль; RGB та вебкерування
мають окремі налаштування.

## Що перевіряється

Host-тести перевіряють порядок викликів, залежності, дублікати, ліміт реєстрації,
очищення після помилок усіх lifecycle-фаз, некоректні переходи, деструктор,
повторний запуск і планування heartbeat з 64-бітним часом. Перевірки працюють також
у Release, оскільки не покладаються на `assert` / `NDEBUG`.

CI описує host-тести в Linux і збірку `esp32c6_rgb8` та чотирьох generic-профілів
у `espressif/idf:v5.4` з явним `BTH_WITH_MATTER=OFF`. Ця матриця перевіряє
попередні native-профілі; Matter перевіряється окремою локальною збіркою з ESP-Matter.
Він запуститься після розміщення проєкту на GitHub; сама наявність workflow не є
свідченням успішного віддаленого запуску.

Успішна компіляція не підтверджує підключення GPIO, розмір реального flash чи роботу
на пристрої. Апаратну перевірку проводьте після вибору конкретної плати.

## Перевірка початкової версії — 25.09.2026

Локально на Windows, ESP-IDF v5.4:

| Перевірка | Результат |
| --- | --- |
| Host-тести, MSVC 2022, C++17 | 16 сценаріїв пройдено |
| Firmware: ESP32-C6, C3, S3, ESP32 | Усі чотири профілі зібрано |
| ESP32-C6 із вимкненим heartbeat | Зібрано |
| Генератор модуля | Створений каркас компілюється; наявні файли не перезаписуються |
| Фізична плата / віддалений GitHub CI | Не запускалися |

## Перевірка після архітектурного огляду — 25.09.2026

- Host: 17 тестових сценаріїв пройдено на MSVC, C++17; новий сценарій охоплює
  9 комбінацій фази відмови (`init`, `start`, `tick`) та позиції модуля, відкат і
  повторний запуск. Заборона копіювання/переміщення heartbeat перевіряється компілятором.
- Ядро й heartbeat збираються окремими host-бібліотеками без exceptions і RTTI.
- ESP32-C6: прошивку зібрано через `scripts/idf.ps1`, ESP-IDF v5.4.
- Інші профілі, фізична плата й віддалений CI у межах цього огляду не перевірялися.

## Перевірка стартової RGB-індикації — 25.09.2026

- Host: 18 сценаріїв пройдено, включно з індикацією та відмовами LED-адаптера.
- Профіль `esp32c6_rgb8`: успішна збірка ESP-IDF v5.4; у згенерованій конфігурації
  GPIO8, 5 спалахів, світіння/пауза по 200 мс.
- Прошивання та фізична перевірка LED не виконувалися.

## Переповнення стека основної задачі

`Stack protection fault` у задачі `main` під час ADC/форматування чисел означає,
що потрібно перевірити стек саме встановленої прошивки. Проєкт вимагає
`CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192` або більше; менше значення зупиняє
компіляцію з поясненням. Shared defaults не перевизначають старий `sdkconfig`.
У цьому workspace розширення ESP-IDF у VS Code використовує окремий
`build/vscode-esp32c6_rgb8/sdkconfig`, а `scripts/idf.ps1` —
`build/esp32c6_rgb8/sdkconfig`. Зміна однієї конфігурації не оновлює іншу.
Для обох потрібні стек 8192, HTTP-заголовки 4096 і flash 4 MB; після зміни
переконфігуруйте саме ту папку збірки, з якої прошиваєте пристрій.
На старті друкується `Main task stack: configured=8192 bytes`; heartbeat показує
`main_stack_min_free` — найменший залишок стека від запуску. Перевіряйте його
після кількох циклів вимірювання, разом з ELF SHA256 у стартовому журналі.

Для `esp32c6_rgb8` фізичний журнал підтвердив flash 4096 КБ, тому profile defaults
задають `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`. Старі конфігурації треба оновити
через menuconfig. Попередження про 4096 КБ проти 2048 КБ не є причиною stack fault.
Таблиця розділів не змінюється. Для застосування параметрів потрібне звичайне
прошивання через `scripts/idf.ps1 -Action flash -Port COMx`, разом із bootloader.

## Зависання VS Code — перевірка 29.09.2026

У `renderer.log` сеансу `20260929T125145` зафіксовано `Extension host ... is unresponsive`.
Профілювання називає `stmicroelectronics.stm32cube-ide-core` та
`stmicroelectronics.stm32cube-ide-debug-stlink-gdbserver`; STM32 tools-manager
повторно повідомляє `Can't find a project` для цієї папки ESP32.
Це свідчення проблеми розширень; перевірочна інкрементальна Matter-збірка через
WSL завершилася успішно, як і Windows host-тести.

У Extensions знайдіть `@installed stm32` і для STM32Cube-розширень виберіть
**Disable (Workspace)**, потім **Developer: Reload Window**. Це залишає їх
доступними для інших проєктів. Після перезапуску перевірте, чи повторюються
зависання; остаточний ефект вимкнення потребує перевірки в інтерфейсі IDE.

У налаштуваннях цього workspace вимкнено автоматичну конфігурацію CMake Tools,
а `build` і `managed_components` виключено з файлового спостереження та пошуку.
**Ctrl+Shift+B** тепер запускає лише Matter-збірку у видимому терміналі.
Повний цикл Build/Flash/Monitor залишається окремою задачею; монітор працює
до **Ctrl+]**, тому його активна задача після компіляції є нормальною.

Компіляція: Windows PowerShell → WSL `BTHMatter` → `/root/bth-home` →
ESP-IDF/Ninja → копіювання у `build/matter-esp32c6_rgb8`.
Остаточний успіх: `Matter build complete (WSL exited successfully; Windows artifacts ready).`
Windows-каталог містить артефакти та CMake-обгортку, але не
`compile_commands.json`: наявне посилання clangd на нього не дає повної
конфігурації IntelliSense. Це окреме обмеження від компіляції у WSL.

## Офіційні довідники

- [ESP-IDF Build System і прямий запуск CMake](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c6/api-guides/build-system.html)
- [Підтримка C++](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c6/api-guides/cplusplus.html)
- [Встановлення ESP-IDF у Windows](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c6/get-started/windows-setup.html)

Перевірки вебкерування реле від 26.09.2026 описані в [WEB_RELAY.md](WEB_RELAY.md).

# Збірка й перевірка

## Огляд коду — 09.10.2026

Причини змін і межі огляду: [REVIEW.md](REVIEW.md).

- `scripts/test.ps1`: пройдено 31 групу host-сценаріїв (одна CTest-ціль),
  MSVC, C++17, попередження як помилки; переносні бібліотеки без exceptions і RTTI.
  Додано відмови виділення ресурсів, зупинку/повторний запуск relay worker,
  межі HTTP-команд, вкладений JSON та нульові байти.
- `tests/web/test_ui.py`: headless Edge з підставним API — пройдено.
  Перевірено вхід, налаштування, перепідключення, реле, Zigbee UI, напруги,
  прострочення даних і повідомлення про помилки. Це не тест реального HTTP-сервера.
- Перевірка компілятором `main/application.cpp` з окремою копією заголовка
  конфігурації: інтервал 50 мс прийнято, 1000 мс відхилено саме новою перевіркою.
  Робочий `sdkconfig` для цього не змінювався.
- Native ESP32-C6 `esp32c6_rgb8`: збірка ESP-IDF v5.4 через `scripts/idf.ps1`
  та експорт артефактів IDE пройдені; образ `0x113970`, 46% розділу вільно.
- `generic_esp32c3` і `generic_esp32s3`: успішна збірка після обмеження
  C6 ADC ISR відповідною платформою. Образи `0xd04e0` та `0xc5650` відповідно.
- `generic_esp32` і `generic_esp32c6`: успішна збірка, образи `0xc7080`
  та `0xd5d10` відповідно. Отже, перевірено всі п'ять native-профілів CI локально.
- Окремий Matter-варіант: збірка через `scripts/idf.ps1 -Matter` і наявний
  WSL/ESP-Matter пройдена, артефакти експортовано у Windows; образ `0x1aeed0`,
  16% розділу вільно.
- Прошивання, живі Wi-Fi/Zigbee/Matter, натискання кнопок IDE та віддалений CI
  під час огляду не запускалися. Калібрування й встановлені SDK не змінювалися.

## Поточний профіль: Zigbee та Wi-Fi AP

Для `esp32c6_rgb8` скрипт `scripts/idf.ps1` збирає native ESP-IDF прошивку
з `BTH_WITH_MATTER=OFF`, Zigbee та точкою доступу Wi-Fi. WSL не потрібен.
Використовуються `build/esp32c6_rgb8` і його окремий `sdkconfig`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/idf.ps1 -Action build
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/idf.ps1 -Action menuconfig
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1
```

**Ctrl+Shift+B** запускає лише збірку. Задачі прошивання, монітора й повного
циклу доступні через **Terminal → Run Task → B-T HOME: ... Zigbee + Wi-Fi AP**.
Кнопка **ESP-IDF: Build** також підтримана: `BTH_WINDOWS_NATIVE_BRIDGE=ON`
вмикає CMake-обгортку з окремим кешем `build/vscode-native-esp32c6_rgb8`.
Вона викликає `scripts/idf.ps1`, який тимчасово відображає папку як `R:`.
Після успішної збірки образи й метадані для Flash/Monitor копіюються до каталогу IDE;
у метаданих тимчасові шляхи замінюються фізичними. Native `sdkconfig` залишається
в `build/esp32c6_rgb8`; кеші CMake не копіюються й не змішуються.
**SDK Configuration Editor** підтриманий через ціль `confserver`. Сервер використовує
фізичні шляхи й зберігає зміни безпосередньо в native `sdkconfig`; відкритий редактор
не займає `R:`. Перед збіркою збережіть зміни. Термінальна задача
**B-T HOME: menuconfig Zigbee + Wi-Fi AP** редагує той самий файл.
Експорт конфігурації та `compile_commands.json` виконується вже при Reconfigure,
тому редактор SDK й аналіз коду доступні до першої компіляції.
Після зміни `.vscode` виконайте **Developer: Reload Window**.

### Стандартні кнопки VS Code

| Команда | Поведінка |
| --- | --- |
| Build / Ctrl+Shift+B | Windows ESP-IDF v5.4, Zigbee + Wi-Fi AP; обидва маршрути оновлюють образи IDE |
| SDK Configuration Editor / Reconfigure | Конфігурація профілю `esp32c6_rgb8`, єдиний `build/esp32c6_rgb8/sdkconfig` |
| Full Clean | Видалення кешу IDE та native-кешу/об’єктів із `idf.extraCleanPaths`; `sdkconfig` і калібрування залишаються |
| Size | Звіт пам’яті після збірки; доступні також CMake-цілі `size`, `size-components`, `size-files` |
| Select Port / Flash / Monitor | Спільний `idf.port` для розширення й задач; вихід із монітора — `Ctrl+]` |
| Build, Flash and Monitor | Послідовно збірка, прошивання та інтерактивний монітор |
| Debug / F5 | Конфігурація `B-T HOME: ESP32-C6 USB JTAG`, ELF із каталогу IDE, зіставлення вихідних шляхів `R:/` |

Debug використовує вбудований USB JTAG (`board/esp32c6-builtin.cfg`), тому потрібні
native USB підключення та відповідний драйвер. Він збирає актуальний ELF перед
підключенням, але не прошиває автоматично: образ на платі має відповідати ELF.
Порт `COM11` — поточне налаштування, його можна змінити кнопкою Select Port.
Вибір іншого чипа не замінює профіль плати: для інших плат використовуйте
`scripts/idf.ps1 -Board <profile>` та відповідні налаштування IDE.

`compile_commands.json` містить Windows-аргументи з фізичними шляхами, включно
зі шляхами з пробілами; його використовують C/C++ IntelliSense та clangd.
Кеш native-компіляції залишається окремим і зберігає шляхи `R:`.

Параметри профілю: `CONFIG_BTH_MATTER_ENABLED=n`, `CONFIG_BTH_ZIGBEE_ENABLED=y`,
`CONFIG_ZB_ENABLED=y`, `CONFIG_ZB_ZED=y`, `CONFIG_ZB_RADIO_NATIVE=y`.
Для наявного `sdkconfig` застосуйте їх через menuconfig; defaults не замінюють
старих значень. Збережіть калібрування мережі: для встановленого датчика
`CONFIG_BTH_MAINS_MILLIVOLTS_PER_COUNT=1355`.

Після запуску активний Zigbee; BOOT 3 с із відпусканням перемикає на Wi-Fi AP
і назад. [Підключення](ZIGBEE.md). Для переходу з Matter потрібне прошивання
з bootloader і таблицею розділів, а не лише застосунку:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/idf.ps1 -Action flash -Port COM11
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/idf.ps1 -Action monitor -Port COM11
```

Порт замініть на фактичний. Повного стирання flash немає: адреси NVS реле,
вебдоступу й Zigbee залишаються тими самими. Старий розділ Matter більше не
використовується. Фізична перевірка потребує окремого прошивання користувачем.

Matter залишається окремою необов’язковою збіркою через `scripts/matter.ps1`
або `scripts/idf.ps1 -Matter`; [інструкція](MATTER.md).
`-WifiAp` збережено як сумісний псевдонім native-збірки; він не вимикає Zigbee.

### Перевірка переходу з Matter — 08.10.2026

- `scripts/test.ps1`: пройдено 30 host-сценаріїв, включно з перемиканням
  Zigbee/Wi-Fi, BOOT, вимірюваннями, батареєю, OLED та реле.
- `scripts/idf.ps1 -Action build`: успішна збірка ESP32-C6 на ESP-IDF v5.4.
  У згенерованій конфігурації Zigbee End Device і SoftAP увімкнені, Matter вимкнений;
  калібрування мережі — 1355. Образ `bt_home.bin` — `0x112a40` байтів,
  вільно 46% розділу застосунку.
- Плату не прошивали; фізичне перемикання інтерфейсів і підключення до шлюзу
  в межах цієї зміни не перевіряли.

Виправлення кнопки Build: попередні налаштування IDE напряму відкривали кеш
native-збірки з шляхами `R:` через фізичний шлях із пробілами. Тепер IDE має
власний кеш і викликає обгортку. Перевірено CMake configure/build через цей
маршрут; прошивання та натискання кнопок у самому вікні VS Code не виконувалися.

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
із двома OTA-слотами по 1,875 MiB та окремим NVS `zb_storage`. Для переходу зі старої версії
потрібна повна прошивка з таблицею розділів, а не лише запис застосунку.
Розташування основного NVS і початок застосунку збережено. У наявному sdkconfig
потрібно ввімкнути `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. [OTA](OTA.md).
Generic-профілі використовують штатну `SINGLE_APP_LARGE` на 1500 KiB у межах
flash 2 МБ: поточний вебзастосунок перевищує старий розділ на 1 МБ. Для наявного
generic sdkconfig виберіть `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`.

## Windows і шлях із пробілами

### VS Code: збірка та налаштування SDK

У цій папці використовуйте стандартні кнопки ESP-IDF або задачі проєкту:

- `scripts/idf.ps1 -Action build` — Zigbee та Wi-Fi AP.
- **Terminal → Run Task…** → `B-T HOME: menuconfig Zigbee + Wi-Fi AP` — налаштування SDK
  у терміналі. Збережіть зміни та вийдіть із menuconfig перед збіркою.

Обидві задачі викликають `scripts/idf.ps1` і працюють зі спільною конфігурацією
`build/esp32c6_rgb8/sdkconfig`. У menuconfig є розділ `B-T HOME mains measurement`.

Графічний SDK Configuration Editor використовує ту саму конфігурацію.

Помилка `Environment IDF_TARGET conflicts with esp32c6_rgb8` означає, що
розширення передало інший чип. У `.vscode/settings.json` для цього workspace
явно задано `idf.customExtraVars.IDF_TARGET = esp32c6`. Після зміни налаштувань
виконайте **Developer: Reload Window**. Не змінюйте профіль плати на ESP32,
щоб обійти цю перевірку.

Налаштування workspace вказують на `build/vscode-native-esp32c6_rgb8`;
native bridge увімкнено, Matter bridge вимкнено.

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
# Основний профіль Zigbee + Wi-Fi AP.
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
native-профілі; Matter перевіряється окремою локальною збіркою з ESP-Matter.
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
Розширення ESP-IDF та `scripts/idf.ps1` тепер використовують єдиний
`build/esp32c6_rgb8/sdkconfig`. Старий `build/vscode-esp32c6_rgb8` більше
не використовується. Потрібні стек 8192, HTTP-заголовки 4096 і flash 4 MB;
після зміни збережіть конфігурацію та зберіть прошивку заново.
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
На дату цієї перевірки **Ctrl+Shift+B** запускав лише Matter-збірку у видимому терміналі.
Із 08.10.2026 основна збірка — Windows Zigbee + Wi-Fi AP, описана вище.
Повний цикл Build/Flash/Monitor залишається окремою задачею; монітор працює
до **Ctrl+]**, тому його активна задача після компіляції є нормальною.

Для окремого Matter-варіанта компіляція: Windows PowerShell → WSL `BTHMatter` → `/root/bth-home` →
ESP-IDF/Ninja → копіювання у `build/matter-esp32c6_rgb8`.
Остаточний успіх: `Matter build complete (WSL exited successfully; Windows artifacts ready).`
Windows-каталог містить артефакти та CMake-обгортку, але не
`compile_commands.json`: наявне посилання clangd на нього не дає повної
конфігурації IntelliSense для Matter. Основна native-збірка вже експортує цю базу.

## Перевірка середовища й команд IDE — 08.10.2026

- `scripts/test.ps1`: 30 host-сценаріїв пройдено.
- Native Build і CMake-обгортка IDE: успішна компіляція ESP32-C6 на ESP-IDF v5.4.
  Повторено видалення тих самих каталогів, які очищує Full Clean розширення,
  та збірку з нуля: 924 кроки, `bt_home.bin` — `0x112a40` байтів, 46% розділу вільно.
  Контрольна сума `sdkconfig` після очищення не змінилася.
- SDK Configuration Editor: перевірено маршрут `idf.py ... confserver`, читання,
  зміну значення й збереження в окремий перевірочний файл. Робоче калібрування
  залишилося 1355. Сервер працює без підключеного диска `R:`.
- Size: пройдено CMake-ціль та всі три JSON-запити GUI (огляд, архіви, файли).
- Перевірено 828 команд компіляції: компілятори, вихідні файли й include-каталоги
  доступні за фізичними шляхами. `clangd --check=main/application.cpp` — 0 помилок.
- Образи, ELF та map у каталозі IDE побайтово збігаються з native-збіркою;
  `esptool image_info` успішно розібрав образ. Монітор запускається з `--help`
  у терміналі. GDB відкрив ELF та знайшов `app_main.cpp` через substitute-path.
- Кнопки перевірено на рівні команд установленого ESP-IDF extension 2.3.0;
  натискання в GUI, Flash, живий Monitor і JTAG-підключення до плати не виконувалися.
  Для завершення апаратної перевірки потрібні підключена плата й відповідний порт.

## Офіційні довідники

- [ESP-IDF Build System і прямий запуск CMake](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c6/api-guides/build-system.html)
- [Підтримка C++](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c6/api-guides/cplusplus.html)
- [Встановлення ESP-IDF у Windows](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c6/get-started/windows-setup.html)

Перевірки вебкерування реле від 26.09.2026 описані в [WEB_RELAY.md](WEB_RELAY.md).

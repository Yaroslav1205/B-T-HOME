# B-T HOME

Модульна прошивка для **ESP32-C6** на **ESP-IDF**, з базовими профілями ESP32-C3,
ESP32-S3 та ESP32. Поточна версія проєкту: `0.1.0`. Базова версія SDK: **ESP-IDF v5.4**,
яка вже встановлена на цьому комп'ютері. Це зафіксована база перевірки, а не твердження
про найновішу версію SDK. Код використовує можливості C++17; ядро не залежить від SDK.

Початкова прошивка друкує профіль плати, час роботи та вільну пам'ять раз на 5 секунд.
GPIO, мережа й периферія ще не налаштовані: для них потрібні модель плати та вимоги.

## Швидкий старт у Windows

У PowerShell, у цій папці:

```powershell
# Зібрати ESP32-C6
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\idf.ps1

# Перевірити логіку без плати (CMake + Visual Studio C++ Build Tools)
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test.ps1

# Налаштування модулів
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\idf.ps1 -Action menuconfig

# Прошити підключену плату; COM5 замініть своїм портом
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\idf.ps1 -Action flash -Port COM5
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\idf.ps1 -Action monitor -Port COM5
```

Вихід із монітора — `Ctrl+]`. Скрипт шукає SDK через `IDF_PATH` або реєстрацію
інсталятора Espressif. Шлях можна передати явно: `-IdfPath C:\Espressif\frameworks\esp-idf-v5.4`.
Для шляху з пробілами скрипт тимчасово підключає цю саму папку як диск `R:` і запускає
CMake напряму. Файли залишаються тут. Диск звільняється після завершення; зайнятий диск
не замінюється. Подробиці та вирішення проблем — [docs/BUILD.md](docs/BUILD.md).

У VS Code доступні задачі збірки (`Ctrl+Shift+B`) і тестів.

## Структура

```text
main/                       точка входу, складання модулів і цикл роботи
components/
  bth_core/                 контракт модуля, залежності, запуск і відкат
  bth_heartbeat/            приклад модуля з тестованою логікою
  bth_platform/             адаптери до ESP-IDF
  bth_board/                дані вибраного профілю плати
boards/<profile>/           параметри чипа та sdkconfig.defaults
tests/host/                 тести C++, запускаються без ESP32
scripts/                    збірка, тести, створення модуля
templates/module/           шаблон нового модуля
docs/                       архітектура та інструкції розширення
.github/workflows/ci.yml    тести й збірка чотирьох профілів у CI
AGENTS.md                   правила для наступних змін у коді
```

## Додавання функціоналу

```powershell
python .\scripts\new_module.py temperature_sensor
```

Генератор створює окремий компонент із контрактом життєвого циклу. Далі реалізуйте
логіку, додайте тести й явно підключіть модуль у `main/application.cpp` та
`main/CMakeLists.txt`. Повний приклад — [docs/ADDING_MODULE.md](docs/ADDING_MODULE.md).

Модулі проходять `init → start → tick`; при помилці ресурси звільняються через
`stop → deinit` у зворотному порядку. Перевірка залежностей відбувається до `init`.
Це контроль життєвого циклу; модулі працюють в одному адресному просторі й не мають
ізоляції пам'яті один від одного.

- [Архітектура й контракти](docs/ARCHITECTURE.md)
- [Додавання модуля](docs/ADDING_MODULE.md)
- [Додавання плати](docs/ADDING_BOARD.md)
- [Збірка й перевірка](docs/BUILD.md)

## Межі початкової конфігурації

Профілі `generic_*` не описують конкретну dev board. Використано стандартний розділ
`factory` ESP-IDF і консервативне налаштування flash 2 MB. Перед роботою з периферією
створіть профіль своєї плати з перевіреними параметрами. OTA, збереження налаштувань,
Wi-Fi/BLE/Thread/Matter додаються окремими модулями за вимогами.

Основа структури — офіційна [компонентна система ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c6/api-guides/build-system.html).

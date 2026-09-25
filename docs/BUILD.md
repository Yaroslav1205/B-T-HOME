# Збірка й перевірка

## Інструменти

- ESP-IDF **v5.4** — базова версія для локальної перевірки й образу CI. CMake допускає
  гілки 5.4 і 5.5; іншу версію потрібно окремо перевіряти та оновлювати політику.
- CMake 3.20+; Ninja і компілятори чипів встановлюються разом із ESP-IDF.
- Host-тести: C++17 compiler і CMake; у Windows — Visual Studio 2022 C++ Build Tools,
  у Linux — GCC або Clang. ESP-IDF для них не потрібна.
- Генератор модулів: Python 3.9+ без сторонніх пакетів.

Проєкт не завантажує сторонні компоненти й не змінює інстальований SDK. Якщо вони
знадобляться пізніше, оголошуйте версії в `idf_component.yml` відповідного компонента
та зберігайте сформований `dependencies.lock` у системі контролю версій.

## Windows і шлях із пробілами

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
idf.py -B build/generic_esp32c6 -D BTH_BOARD=generic_esp32c6 build
idf.py -B build/generic_esp32c6 -D BTH_BOARD=generic_esp32c6 menuconfig

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
Вимкнений heartbeat залишає порожній, але коректний цикл застосунку.

## Що перевіряється

Host-тести перевіряють порядок викликів, залежності, дублікати, ліміт реєстрації,
очищення після помилок усіх lifecycle-фаз, некоректні переходи, деструктор,
повторний запуск і планування heartbeat з 64-бітним часом. Перевірки працюють також
у Release, оскільки не покладаються на `assert` / `NDEBUG`.

CI описує host-тести в Linux і збірку чотирьох generic-профілів у `espressif/idf:v5.4`.
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

## Офіційні довідники

- [ESP-IDF Build System і прямий запуск CMake](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c6/api-guides/build-system.html)
- [Підтримка C++](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c6/api-guides/cplusplus.html)
- [Встановлення ESP-IDF у Windows](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c6/get-started/windows-setup.html)

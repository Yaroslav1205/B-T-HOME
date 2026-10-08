# Додавання модуля

## 1. Створіть компонент

```powershell
python .\scripts\new_module.py temperature_sensor
```

З'явиться `components/bth_temperature_sensor/` з CMake, публічним заголовком і
реалізацією `bth::TemperatureSensorModule`. Генератор не перезаписує наявний компонент.
Створений каркас ще не виконує корисної роботи та не підключений до застосунку.

## 2. Визначте логіку й апаратний інтерфейс

Конфігурацію й залежності передавайте конструктором. Для доступу до сенсора створіть
невеликий інтерфейс, потрібний саме цьому модулю, та ESP-IDF адаптер у
`bth_platform` або окремому компоненті. Приклад — `HeartbeatSink` і `EspHeartbeatSink`.
GPIO, активний рівень та параметри шин беріть із перевіреного профілю плати.

`init()` перевіряє параметри й отримує ресурси. `start()` запускає роботу.
`tick()` виконує короткий крок автомата станів. `stop()` та `deinit()` повинні
прибирати ресурси навіть після частково невдалих `start()` та `init()`.

Якщо потрібен інший модуль, наприклад `storage`, оголосіть залежність:

```cpp
// У класі TemperatureSensorModule:
inline static constexpr const char* required_modules_[] = {"storage"};

bth::Dependencies dependencies() const noexcept override {
    return {required_modules_, 1};
}
```

Це потребує справжнього зареєстрованого модуля `storage`; його немає у початковій
прошивці. Залежності lifecycle доповнюють ін'єкцію об'єктів і CMake, а не замінюють їх.

## 3. Підключіть явно

Додайте `bth_temperature_sensor` до `PRIV_REQUIRES` у `main/CMakeLists.txt`.
У `main/application.cpp`:

```cpp
#include "bth/temperature_sensor/temperature_sensor.hpp"

// Усередині run(), перед Runtime runtime:
bth::TemperatureSensorModule temperature_sensor;

// Після створення runtime та реєстрації потрібних постачальників:
if (runtime.add(temperature_sensor) != bth::Status::ok) {
    log_failure(runtime);
    return;
}
// Далі наявний runtime.start().
```

Для модуля з constructor dependencies спочатку створіть адаптери та постачальників.
Усі об'єкти модулів оголошуйте перед Runtime, щоб порядок знищення залишався коректним.
Не ігноруйте результат `add()`.

Якщо модуль необов'язковий, додайте опцію в його `Kconfig` та огорніть його
конструювання/реєстрацію відповідним `#if CONFIG_...`, як у heartbeat. Не вимикайте
постачальника, якщо увімкнений споживач; відобразіть таке обмеження у Kconfig або
перевірці конфігурації. Runtime додатково перевірить список під час запуску.

## 4. Перевірте поведінку

Додайте окрему бібліотеку для portable-джерел у `tests/host/CMakeLists.txt` за
зразком `bth_heartbeat`: власний include path, явний `target_link_libraries` для
залежностей і підключення до `bth_tests`. Додайте ціль до обох списків перевірки
параметрів компіляції (C++17, попередження, без exceptions і RTTI). Створіть fake
для апаратного інтерфейсу. Перевірте корисну поведінку, помилки отримання ресурсів,
очищення часткового стану й повторний запуск, а не лише повернення `ok` із каркаса.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\idf.ps1
```

Для апаратного драйвера окремо перевірте його на цільовій платі, зокрема від'єднання
периферії та відновлення. Host-тести й компіляція не перевіряють електричну схему.
Оновіть README компонента, якщо з'явилися параметри, підключення або обмеження.

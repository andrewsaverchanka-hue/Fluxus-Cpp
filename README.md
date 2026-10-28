# Fluxus — C++20 / Qt 6

Перенос Python/PySide6-версии Fluxus на C++20 с графическим интерфейсом Qt 6 Widgets.

## Что реализовано

- Тёмный GUI в структуре Python-версии: сайдбар, страницы SSH / анализа / настроек, статус-строка и прогресс.
- Карточки устройств шириной 250 px: выбор кликом, состояние подключения, Lima/W2, ID или счёт, RAM, CPU, uptime и прогресс.
- SSH через gateway, direct-tcpip tunnel, отдельные target/SFTP-сессии для параллельных загрузок.
- До 4 одновременных SFTP-загрузок на устройство, контроль размера, временный `.part` и атомарное переименование.
- Фильтрация логов по часам или диапазону дат, чтение через `grep` / `zgrep` / `zcat` без сохранения на диск.
- Потоковое чтение обычных и gzip-файлов без загрузки всего лога в память.
- Однопроходная обработка `devices_server` анализаторами Lima и W2 Low-Level.
- Параллельный анализ независимых каталогов устройств.
- Lima lifecycle, BNV, инкассации; W2 frontend lifecycle и cash events; LCDM2000/LCDM4000, health и frontend/low-level correlation.
- Окно результатов со статистикой, фильтрами и деревьями событий.
- Сохранение `.anl.gz` версии 2 и чтение версий 1 и 2.
- Совместимый `config.json` с исходными snake_case-ключами.
- Проверка обновлений GitHub и открытие страницы релиза.

## Зависимости

- Компилятор с C++20: MSVC 2022, GCC 11+ или Clang 14+.
- CMake 3.22+.
- Qt 6: Widgets, Network, Svg.
- libssh (не libssh2).
- zlib.
- nlohmann/json уже включён в `third_party`, отдельно устанавливать его не нужно.

## Windows 10/11 x64

### 1. Установить инструменты

1. Visual Studio 2022 Community с рабочей нагрузкой **Desktop development with C++**.
2. CMake.
3. Qt 6 для MSVC 2022 x64. Достаточно компонентов Qt Widgets, Network и Svg (в стандартный desktop-пакет они входят).
4. vcpkg для libssh и zlib:

```bat
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install libssh zlib --triplet x64-windows
```

### 2. Собрать

Пути к Qt и vcpkg замените на свои:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\msvc2022_64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Главный файл: `build\Release\Fluxus.exe`.

CMake по возможности запускает `windeployqt` после сборки. Если DLL не были скопированы автоматически:

```bat
C:\Qt\6.8.0\msvc2022_64\bin\windeployqt.exe --release build\Release\Fluxus.exe
copy C:\vcpkg\installed\x64-windows\bin\libssh.dll build\Release\
copy C:\vcpkg\installed\x64-windows\bin\zlib1.dll build\Release\
```

`Fluxus.exe` нужно запускать из папки, в которую скопированы Qt/libssh/zlib DLL. Python, PySide6 и .NET для C++-версии не требуются.

## Linux (Debian / Ubuntu)

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-svg-dev libssh-dev zlib1g-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/Fluxus
```

## Использование

1. В «Настройках» выберите SSH-ключ и при необходимости задайте fallback-пароль.
2. На странице «Подключение SSH» добавьте VPN-адрес формата `10.8.x.x`.
3. Кликните по карточке устройства, затем нажмите «Подключиться».
4. Выберите период и скачайте логи либо откройте их напрямую на устройстве.
5. Для локальных файлов используйте «Анализатор логов» → «Добавить файлы» или «Сканировать папку».
6. Готовый анализ можно сохранить как `.anl.gz` и открыть позднее.

При первом SSH-подключении ключ сервера сохраняется в known_hosts. Если ключ сервера впоследствии изменится, подключение будет отклонено.

## Тесты

Тестовый исполняемый файл не требует внешнего test framework. Проверяются:

- Lima lifecycle / BNV / collection;
- W2 frontend;
- LCDM2000 и LCDM4000;
- корреляция frontend ↔ low-level;
- однопроходный engine;
- сериализация v2 и восстановление shared links;
- legacy v1;
- gzip и утилиты;
- совместимость конфигурации.

## Структура

- `src/core` — модели, анализаторы, сериализация, конфигурация, SSH/SFTP; не зависит от Qt.
- `src/app` — Qt 6 Widgets GUI и фоновые workers.
- `tests` — тесты ядра.
- `assets`, `logo*` — исходные ресурсы GUI.
- `.github/workflows/windows.yml` — сборка Windows x64 и тесты в GitHub Actions.

# mangoshot

Минималистичный аналог ShareX для Wayland (цель — MangoWM): снимок,
аннотации, сохранение, буфер обмена, загрузка по ссылке.

## Зависимости
- Qt6 (Widgets), CMake, компилятор C++17
- Во время работы: `grim`, `slurp`, `wl-clipboard`, `curl`, `libnotify`

Arch: `sudo pacman -S qt6-base cmake grim slurp wl-clipboard curl libnotify`

## Сборка
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    sudo cmake --install build

## Использование
    mangoshot        # область -> редактор
    mangoshot -f     # весь экран -> редактор
    mangoshot -d     # область -> сразу в файл и буфер

Горячие клавиши в редакторе: A стрелка, R рамка, P кисть, N стикер,
T текст, Ctrl+Z отмена, Ctrl+S сохранить, Ctrl+C копировать,
Ctrl+U загрузить, Esc закрыть.

Файлы: `~/Pictures/Screenshots/`.
Загрузка: `MANGOSHOT_UPLOAD_URL` (по умолчанию https://0x0.st; ответ —
текст со ссылкой). Для Imgur/Dropbox нужен API-ключ — добавьте свой
класс в `Share.h`.

## Интеграция с MangoWM (проверьте по документации Mango)
В `~/.config/mango/config.conf` добавьте бинд на Print и правило
плавающего окна для app_id `mangoshot`. Точный синтаксис `bind` и
`windowrule` сверьте с https://mangowm.github.io/docs/ — я его не проверял.

## Участие в разработке
1. Fork, ветка `feature/...`.
2. Стиль: C++17, 4 пробела, без сырых `new` вне Qt-родителей.
3. Проверка: сборка без предупреждений + ручной тест (-r, -f, -d).
4. Pull request с описанием и скриншотом.

Идеи: запись экрана (wf-recorder), размытие/пиксели, OCR, Imgur/S3, трей.
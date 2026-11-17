cmake_minimum_required(VERSION 3.16)
project(jackshot VERSION 0.10.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets DBus)
find_package(LayerShellQt REQUIRED)

add_executable(jackshot
    src/main.cpp
    src/Canvas.cpp
    src/Editor.cpp
    src/SelectionOverlay.cpp
    src/SelectionOverlay.h
    src/OverlayBars.cpp
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/Daemon.cpp
    src/Daemon.h
    src/Screenshot.cpp
    src/Screenshot.h
    src/JackpotFx.cpp
    src/JackpotFx.h
    src/Settings.h
    src/Portal.h
    src/Icons.h
    jackshot.qrc
)

target_compile_definitions(jackshot PRIVATE JACKSHOT_VERSION="${PROJECT_VERSION}")
target_link_libraries(jackshot PRIVATE Qt6::Widgets Qt6::DBus LayerShellQt::Interface)

install(TARGETS jackshot RUNTIME DESTINATION bin)
# идентификатор приложения для порталов и меню приложений
install(FILES packaging/jackshot.desktop DESTINATION share/applications)
# автозапуск демона глобальных клавиш (XDG autostart: KDE, Cosmic и др.)
install(FILES packaging/jackshot-daemon.desktop DESTINATION /etc/xdg/autostart)
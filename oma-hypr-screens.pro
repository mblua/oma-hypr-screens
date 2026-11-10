QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = oma-hypr-screens
TEMPLATE = app

# The identify overlay puts one layer-shell surface on each screen.
INCLUDEPATH += /usr/include/LayerShellQt
LIBS += -lLayerShellQtInterface

HEADERS += \
    src/backend.h \
    src/identify.h \
    src/layout.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/identify.cpp \
    src/layout.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc

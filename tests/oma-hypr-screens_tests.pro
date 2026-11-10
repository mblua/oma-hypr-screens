QT += core gui quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = oma-hypr-screens_tests
TEMPLATE = app

INCLUDEPATH += ../src
DEFINES += FIXTURES=\\\"$$PWD\\\"

HEADERS += ../src/backend.h ../src/layout.h ../src/theme.h ../src/translator.h
SOURCES += oma-hypr-screens_tests.cpp ../src/backend.cpp ../src/layout.cpp ../src/theme.cpp ../src/translator.cpp

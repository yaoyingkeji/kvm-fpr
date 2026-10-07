QT += widgets
TARGET = kvm-fpr-qt
TEMPLATE = app
CONFIG += c++11
CONFIG -= app_bundle
INCLUDEPATH += ../src
SOURCES += main_qt.cpp ../src/fpr.c ../src/theme.c
HEADERS += ../src/fpr.h ../src/icon_data.h

#-----------------------------------------------------------------------------------------------#
# Gradient Canvas - OpenRGB plugin (plugin API v5 / OpenRGB 1.0)                                #
#                                                                                               #
#   Build:  qmake GradientCanvasPlugin.pro && make        (Linux / macOS)                       #
#           qmake GradientCanvasPlugin.pro && nmake       (Windows, MSVC dev prompt)            #
#                                                                                               #
#   Requires the OpenRGB source tree in ./OpenRGB (git submodule) for the plugin headers.       #
#   Build with the SAME Qt major/minor version and compiler as your OpenRGB build.              #
#-----------------------------------------------------------------------------------------------#

QT          += core gui widgets
TEMPLATE     = lib
CONFIG      += plugin c++17 silent
TARGET       = GradientCanvasPlugin

#-----------------------------------------------------------------------------------------------#
# Version info                                                                                  #
#-----------------------------------------------------------------------------------------------#
GIT_COMMIT = $$system(git -C $$PWD rev-parse --short HEAD 2> $$QMAKE_SYSTEM_NULL_DEVICE)
isEmpty(GIT_COMMIT): GIT_COMMIT = local
DEFINES += GRADIENT_CANVAS_VERSION=\\\"1.2.0\\\" GRADIENT_CANVAS_COMMIT=\\\"$$GIT_COMMIT\\\"

#-----------------------------------------------------------------------------------------------#
# OpenRGB headers (header-only use - the plugin talks to OpenRGB through pure virtual APIs)     #
#-----------------------------------------------------------------------------------------------#
OPENRGB = $$PWD/OpenRGB

!exists($$OPENRGB/OpenRGBPluginInterface.h) {
    error("OpenRGB source not found in $$OPENRGB - run: git submodule update --init")
}

INCLUDEPATH +=                                                                                  \
    $$OPENRGB                                                                                   \
    $$OPENRGB/RGBController                                                                     \
    $$OPENRGB/dependencies/json                                                                 \
    src                                                                                         \
    src/ui                                                                                      \

HEADERS +=                                                                                      \
    $$OPENRGB/OpenRGBPluginInterface.h                                                          \
    $$OPENRGB/ResourceManagerCallback.h                                                         \
    src/GradientCanvasPlugin.h                                                                  \
    src/Gradient.h                                                                              \
    src/Calibration.h                                                                           \
    src/ui/CalibrationDialog.h                                                                  \
    src/ui/ParamSlider.h                                                                        \
    src/Effects.h                                                                               \
    src/LayoutModel.h                                                                           \
    src/RenderEngine.h                                                                          \
    src/ui/CanvasWidget.h                                                                       \
    src/ui/GradientEditor.h                                                                     \
    src/ui/LayoutCanvas.h                                                                       \

DISTFILES += src/GradientCanvasPlugin.json

SOURCES +=                                                                                      \
    src/GradientCanvasPlugin.cpp                                                                \
    src/Gradient.cpp                                                                            \
    src/Calibration.cpp                                                                         \
    src/ui/CalibrationDialog.cpp                                                                \
    src/Effects.cpp                                                                             \
    src/LayoutModel.cpp                                                                         \
    src/RenderEngine.cpp                                                                        \
    src/ui/CanvasWidget.cpp                                                                     \
    src/ui/GradientEditor.cpp                                                                   \
    src/ui/LayoutCanvas.cpp                                                                     \

#-----------------------------------------------------------------------------------------------#
# Platform specifics                                                                            #
#-----------------------------------------------------------------------------------------------#
win32 {
    DEFINES += _MBCS WIN32 _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX
    QMAKE_CXXFLAGS += /utf-8
}

unix:!macx {
    QMAKE_CXXFLAGS += -Wno-psabi
    target.path = $$(HOME)/.config/OpenRGB/plugins
    INSTALLS += target
}

macx {
    CONFIG += c++17
}

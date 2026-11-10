// oma-hypr-screens — arrange Hyprland's monitors: position, rotation, scale,
// and resolution, written to ~/.config/hypr/monitors.lua.

#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "identify.h"
#include "theme.h"
#include "translator.h"

static const char *Version = "0.1.0";

static void printUsage(FILE *to) {
    std::fputs("Usage: oma-hypr-screens [open]\n"
               "       oma-hypr-screens list [--json]   the monitors Hyprland has now\n"
               "       oma-hypr-screens lua             the managed monitors.lua block for them\n"
               "       oma-hypr-screens identify [OUTPUT]  show each monitor's number on it\n",
               to);
}

// Commands that need no display, for scripts and agents. They only read:
// changing the layout always goes through the window, which asks the user
// to confirm the result on their screens.
static int runCommand(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("oma-hypr-screens");
    const QStringList args = app.arguments().mid(1);
    const QString command = args.value(0);
    const bool json = args.contains("--json");

    Backend backend;
    if (!backend.error().isEmpty() && backend.layout().isEmpty()) {
        std::fprintf(stderr, "oma-hypr-screens: %s\n", qPrintable(backend.error()));
        return 1;
    }

    if (command == "lua") {
        std::printf("%s\n", qPrintable(backend.lua()));
        return 0;
    }

    // list
    if (json) {
        QJsonArray list;
        for (const auto &m : backend.layout()) {
            const QSize s = Layout::size(m);
            list << QJsonObject{{"number", m.id}, {"output", m.name}, {"label", m.label},
                                {"description", m.description}, {"inches", m.inches}, {"mode", m.mode},
                                {"modes", QJsonArray::fromStringList(m.modes)}, {"scale", m.scale},
                                {"transform", m.transform + (m.flipped ? 4 : 0)}, {"x", m.x}, {"y", m.y},
                                {"logicalWidth", s.width()}, {"logicalHeight", s.height()}};
        }
        std::printf("%s", QJsonDocument(list).toJson().constData());
    } else {
        for (const auto &m : backend.layout()) {
            const QSize s = Layout::size(m);
            std::printf("%d  %-10s %-24s %-18s ×%-6s %-5s %5d,%-5d %dx%d\n", m.id, qPrintable(m.name),
                        qPrintable(m.description), qPrintable(m.mode), qPrintable(Layout::formatScale(m.scale)),
                        qPrintable(Layout::rotationLabel(m.transform)), m.x, m.y, s.width(), s.height());
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (command == "-h" || command == "--help" || command == "help") {
        printUsage(stdout);
        return 0;
    }
    if (command == "-v" || command == "--version") {
        std::printf("oma-hypr-screens %s\n", Version);
        return 0;
    }
    if (command == "list" || command == "lua")
        return runCommand(argc, argv);
    if (command == "identify") {
        // Shows the numbers on the real screens and exits, so a script can
        // ask "which one is DP-2?" without opening the window.
        QGuiApplication app(argc, argv);
        app.setApplicationName("oma-hypr-screens");
        IdentifyOverlay overlay;
        if (!overlay.available()) {
            std::fprintf(stderr, "oma-hypr-screens: identify needs a Wayland session\n");
            return 1;
        }
        Backend backend(Backend::Options{}, &overlay);
        if (backend.layout().isEmpty()) {
            std::fprintf(stderr, "oma-hypr-screens: %s\n", qPrintable(backend.error()));
            return 1;
        }
        const QString output = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
        if (!output.isEmpty() && Layout::indexOf(backend.layout(), output) < 0) {
            std::fprintf(stderr, "oma-hypr-screens: no output named '%s'\n", qPrintable(output));
            return 1;
        }
        QList<IdentifyTarget> targets;
        for (const auto &m : backend.layout())
            targets << IdentifyTarget{m.name, m.id, m.color, m.label};
        overlay.show(targets, output, 2500);
        QTimer::singleShot(2700, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (!command.isEmpty() && command != "open") {
        std::fprintf(stderr, "oma-hypr-screens: unknown command '%s'\n", qPrintable(command));
        printUsage(stderr);
        return 1;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("oma-hypr-screens");
    app.setApplicationVersion(Version);

    // Matches the window to oma-hypr-screens.desktop, so the compositor
    // (Wayland app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("oma-hypr-screens");
    app.setWindowIcon(QIcon::fromTheme("oma-hypr-screens"));

    QQuickStyle::setStyle("Material");

    // Plain Text items don't inherit the window's font, so the monospace
    // look of Omarchy's own tools is set for the whole app.
    QFont font("JetBrainsMono Nerd Font");
    font.setPixelSize(13);
    app.setFont(font);

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    IdentifyOverlay overlay;
    Backend backend(Backend::Options{}, &overlay);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}

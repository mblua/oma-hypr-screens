#include "identify.h"

#include <LayerShellQt/Window>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>

IdentifyOverlay::IdentifyOverlay(QObject *parent) : QObject(parent) {
    m_hide.setSingleShot(true);
    connect(&m_hide, &QTimer::timeout, this, &IdentifyOverlay::hideAll);
    // Plugging or unplugging a monitor changes which windows are needed.
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &IdentifyOverlay::rebuild);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &IdentifyOverlay::rebuild);
}

IdentifyOverlay::~IdentifyOverlay() {
    for (const auto &view : m_views)
        delete view;
}

bool IdentifyOverlay::available() const {
    return QGuiApplication::platformName().startsWith("wayland");
}

void IdentifyOverlay::rebuild() {
    for (const auto &view : m_views)
        if (view)
            view->deleteLater();
    m_views.clear();
}

void IdentifyOverlay::hideAll() {
    for (const auto &view : m_views)
        if (view)
            view->hide();
}

void IdentifyOverlay::show(const QList<IdentifyTarget> &targets, const QString &selected, int milliseconds) {
    if (!available())
        return;
    if (!m_engine)
        m_engine = new QQmlEngine(this);

    const auto screens = QGuiApplication::screens();
    if (m_views.size() != screens.size())
        rebuild();
    if (m_views.isEmpty()) {
        for (QScreen *screen : screens) {
            auto *view = new QQuickView(m_engine, nullptr);
            view->setScreen(screen);
            view->setColor(Qt::transparent);
            view->setFlags(Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
            view->setResizeMode(QQuickView::SizeRootObjectToView);

            // Set before the first show, so the surface is created as a layer
            // surface rather than an ordinary window Hyprland would tile.
            auto *layer = LayerShellQt::Window::get(view);
            layer->setLayer(LayerShellQt::Window::LayerOverlay);
            layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
            layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                                            | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
            layer->setExclusiveZone(-1);
            layer->setActivateOnShow(false);
            layer->setScope("oma-hypr-screens-identify");
            layer->setScreen(screen);

            view->setSource(QUrl("qrc:/IdentifyOverlay.qml"));
            m_views << view;
        }
    }

    for (const auto &view : m_views) {
        if (!view || !view->rootObject())
            continue;
        const QString name = view->screen() ? view->screen()->name() : QString();
        IdentifyTarget target{name, 0, "#c0caf5", {}};
        for (const auto &t : targets)
            if (t.name == name)
                target = t;
        QObject *root = view->rootObject();
        root->setProperty("number", target.number ? QString::number(target.number) : QStringLiteral("?"));
        root->setProperty("accent", target.color);
        root->setProperty("caption", target.label.isEmpty() ? name : name + QStringLiteral("  ·  ") + target.label);
        root->setProperty("selected", !selected.isEmpty() && name == selected);
        root->setProperty("dimmed", !selected.isEmpty() && name != selected);
        view->show();
    }
    m_hide.start(milliseconds);
}

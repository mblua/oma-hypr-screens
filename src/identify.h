#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

class QQmlEngine;
class QQuickView;

struct IdentifyTarget {
    QString name;     // Hyprland output, matched against QScreen::name()
    int number = 0;
    QString color;
    QString label;
};

// Puts each display's number on the physical monitor it belongs to. The
// backend talks to this interface so tests can run without screens.
class Identifier {
public:
    virtual ~Identifier() = default;
    virtual bool available() const = 0;
    virtual void show(const QList<IdentifyTarget> &targets, const QString &selected, int milliseconds) = 0;
};

// One click-through layer-shell overlay per screen, above everything else,
// that never takes the keyboard. Only Wayland compositors with layer shell
// (Hyprland among them) can place a window on a given screen this way.
class IdentifyOverlay : public QObject, public Identifier {
    Q_OBJECT

public:
    explicit IdentifyOverlay(QObject *parent = nullptr);
    ~IdentifyOverlay() override;

    bool available() const override;
    void show(const QList<IdentifyTarget> &targets, const QString &selected, int milliseconds) override;

private:
    void rebuild();
    void hideAll();

    QQmlEngine *m_engine = nullptr;
    QList<QPointer<QQuickView>> m_views;
    QTimer m_hide;
};

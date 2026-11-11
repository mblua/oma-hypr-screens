#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QRectF>
#include <QTimer>
#include <QVariantList>

#include "layout.h"

class Identifier;

// The displays as a list model, so the canvas keeps its delegates (and their
// animations) while the layout changes underneath them.
class MonitorModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        NameRole = Qt::UserRole + 1, NumberRole, LabelRole, DescriptionRole, InchesRole, ModeRole,
        ScaleTextRole, TransformRole, XRole, YRole, WidthRole, HeightRole, HzRole, ColorRole,
        OverlappingRole, DetachedRole, SelectedRole, OnRole
    };

    explicit MonitorModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void update(const Monitors &mons, const QString &selected);

private:
    Monitors m_mons;
    QString m_selected;
    QSet<QString> m_overlapping, m_detached;
};

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *monitors READ monitors CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QString selected READ selected NOTIFY changed)
    Q_PROPERTY(QVariantMap current READ current NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(int portraitCount READ portraitCount NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(int countdown READ countdown NOTIFY changed)
    Q_PROPERTY(int confirmSeconds READ confirmSeconds CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString statusKind READ statusKind NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString lua READ lua NOTIFY changed)
    Q_PROPERTY(QString luaHtml READ luaHtml NOTIFY changed)
    Q_PROPERTY(QString configPath READ configDisplayPath CONSTANT)
    Q_PROPERTY(QRectF bounds READ bounds NOTIFY changed)
    Q_PROPERTY(QVariantList guidesX READ guidesX NOTIFY guidesChanged)
    Q_PROPERTY(QVariantList guidesY READ guidesY NOTIFY guidesChanged)

public:
    struct Options {
        QString hyprctl;      // empty: the hyprctl on PATH, when Hyprland is running
        QString configPath;   // empty: ~/.config/hypr/monitors.lua
        int confirmSeconds = 10;
    };

    explicit Backend(const Options &options, Identifier *identifier = nullptr, QObject *parent = nullptr);
    explicit Backend(QObject *parent = nullptr) : Backend(Options{}, nullptr, parent) {}

    QObject *monitors() { return &m_model; }
    int count() const { return int(m_mons.size()); }
    QString selected() const { return m_selected; }
    QVariantMap current() const;
    bool dirty() const { return m_mons != m_saved; }
    bool valid() const { return Layout::overlaps(m_mons).isEmpty() && Layout::enabledCount(m_mons) > 0; }
    int portraitCount() const;
    bool pending() const { return m_pending; }
    int countdown() const { return m_countdown; }
    int confirmSeconds() const { return m_options.confirmSeconds; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString status() const { return m_status; }
    QString statusKind() const { return m_statusKind; }
    QString error() const { return m_error; }
    QString lua() const { return Layout::toLua(m_mons); }
    QString luaHtml() const { return Layout::luaHtml(lua()); }
    QString configPath() const { return m_options.configPath; }
    QString configDisplayPath() const;
    QRectF bounds() const;
    QVariantList guidesX() const { return m_guidesX; }
    QVariantList guidesY() const { return m_guidesY; }
    const Monitors &layout() const { return m_mons; }
    void setLayout(const Monitors &mons);   // for tests and the screenshot harness

    Q_INVOKABLE void select(const QString &name);
    Q_INVOKABLE void selectNumber(int number);
    Q_INVOKABLE void selectNext();

    Q_INVOKABLE void beginDrag(const QString &name);
    Q_INVOKABLE void dragTo(const QString &name, double x, double y, double threshold, bool free);
    Q_INVOKABLE void endDrag(const QString &name, bool moved);

    Q_INVOKABLE void rotate(const QString &name);
    Q_INVOKABLE void rotateSelected() { rotate(m_selected); }
    Q_INVOKABLE void toggle(const QString &name);
    Q_INVOKABLE void toggleSelected() { toggle(m_selected); }
    Q_INVOKABLE void move(int dx, int dy, bool nudge);
    Q_INVOKABLE void cycleScale();
    Q_INVOKABLE void cycleMode();
    Q_INVOKABLE void nextPreset();
    Q_INVOKABLE void identify();

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    Q_INVOKABLE void apply();
    Q_INVOKABLE void keep();
    Q_INVOKABLE void revert(bool timedOut = false);
    Q_INVOKABLE void discard();

signals:
    void changed();
    void guidesChanged();
    // Asks the canvas to show the numbers itself when they can't be put on
    // the real screens (no Wayland, or tests).
    void identifyInWindow();

private:
    void loadFromHyprland();
    bool runHyprctl(const QStringList &args, QByteArray *out, QString *error);
    bool reloadHyprland(QString *error);
    void wake(const Monitors &target);
    void project(const QString &selected, bool force = false);
    void push();
    void changedLayout();
    void say(const QString &text, const QString &kind = {});
    int currentIndex() const { return Layout::indexOf(m_mons, m_selected); }
    // False, with a hint, when the display is off and so can't be edited.
    bool editable(int index);

    Options m_options;
    Identifier *m_identifier;
    MonitorModel m_model;
    Monitors m_mons, m_saved;
    QList<Monitors> m_undo, m_redo;
    QString m_selected, m_lastProjected;
    bool m_projectedOnce = false;
    QString m_status, m_statusKind, m_error;
    QVariantList m_guidesX, m_guidesY;
    int m_presetIndex = -1;

    bool m_pending = false;
    int m_countdown = 0;
    QString m_backup;
    QTimer m_tick;
};

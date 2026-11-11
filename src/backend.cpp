#include "backend.h"

#include "identify.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

// ── Model ───────────────────────────────────────────────────────────────────

int MonitorModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_mons.size());
}

QVariant MonitorModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_mons.size())
        return {};
    const Monitor &m = m_mons[index.row()];
    switch (role) {
    case NameRole: return m.name;
    case NumberRole: return m.id;
    case LabelRole: return m.label;
    case DescriptionRole: return m.description;
    case InchesRole: return m.inches;
    case ModeRole: return m.mode;
    case ScaleTextRole: return Layout::formatScale(m.scale);
    case TransformRole: return m.transform;
    case XRole: return m.x;
    case YRole: return m.y;
    case WidthRole: return Layout::size(m).width();
    case HeightRole: return Layout::size(m).height();
    case HzRole: return int(std::lround(Layout::resolution(m).hz));
    case ColorRole: return m.color;
    case OverlappingRole: return m_overlapping.contains(m.name);
    case DetachedRole: return !m_overlapping.contains(m.name) && m_detached.contains(m.name);
    case SelectedRole: return m.name == m_selected;
    case OnRole: return m.enabled;
    }
    return {};
}

QHash<int, QByteArray> MonitorModel::roleNames() const {
    return {{NameRole, "name"}, {NumberRole, "number"}, {LabelRole, "label"}, {DescriptionRole, "description"},
            {InchesRole, "inches"}, {ModeRole, "mode"}, {ScaleTextRole, "scaleText"}, {TransformRole, "quarterTurns"},
            {XRole, "posX"}, {YRole, "posY"}, {WidthRole, "logicalWidth"}, {HeightRole, "logicalHeight"},
            {HzRole, "hz"}, {ColorRole, "monitorColor"}, {OverlappingRole, "overlapping"},
            {DetachedRole, "detached"}, {SelectedRole, "selected"}, {OnRole, "on"}};
}

void MonitorModel::update(const Monitors &mons, const QString &selected) {
    bool sameRows = mons.size() == m_mons.size();
    for (int i = 0; sameRows && i < mons.size(); ++i)
        sameRows = mons[i].name == m_mons[i].name;
    if (!sameRows)
        beginResetModel();
    m_mons = mons;
    m_selected = selected;
    m_overlapping = Layout::overlaps(mons);
    m_detached = Layout::isolated(mons);
    if (!sameRows)
        endResetModel();
    else if (!mons.isEmpty())
        emit dataChanged(index(0), index(int(mons.size()) - 1));
}

// ── Backend ─────────────────────────────────────────────────────────────────

Backend::Backend(const Options &options, Identifier *identifier, QObject *parent)
    : QObject(parent), m_options(options), m_identifier(identifier) {
    if (m_options.configPath.isEmpty()) {
        const QString config = qEnvironmentVariableIsEmpty("XDG_CONFIG_HOME")
                                   ? QDir::homePath() + "/.config" : qEnvironmentVariable("XDG_CONFIG_HOME");
        m_options.configPath = config + "/hypr/monitors.lua";
    }
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (--m_countdown <= 0)
            revert(true);
        else
            emit changed();
    });
    say(tr("Got a portrait monitor? Click the ↻ button on it (or press r)."));
    loadFromHyprland();
}

void Backend::setLayout(const Monitors &mons) {
    m_mons = m_saved = mons;
    m_undo.clear();
    m_redo.clear();
    m_selected = mons.isEmpty() ? QString() : mons.first().name;
    if (!mons.isEmpty() && m_error.startsWith(tr("Hyprland isn't running")))
        m_error.clear();
    changedLayout();
}

bool Backend::runHyprctl(const QStringList &args, QByteArray *out, QString *error) {
    QString program = m_options.hyprctl;
    if (program.isEmpty()) {
        if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")) {
            *error = tr("Hyprland isn't running, so there are no monitors to arrange.");
            return false;
        }
        program = QStandardPaths::findExecutable("hyprctl");
        if (program.isEmpty()) {
            *error = tr("hyprctl wasn't found on PATH.");
            return false;
        }
    }
    QProcess process;
    process.start(program, args);
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        *error = tr("hyprctl %1 didn't answer.").arg(args.join(' '));
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = tr("hyprctl %1 failed: %2").arg(args.join(' '),
                                                 QString::fromUtf8(process.readAllStandardError()).trimmed());
        return false;
    }
    if (out)
        *out = process.readAllStandardOutput();
    return true;
}

void Backend::loadFromHyprland() {
    QByteArray json;
    QString error;
    if (!runHyprctl({"monitors", "all", "-j"}, &json, &error)) {
        m_error = error;
        changedLayout();
        return;
    }
    const Monitors mons = Layout::fromHyprctl(json, &error);
    if (mons.isEmpty()) {
        m_error = error.isEmpty() ? tr("Hyprland reports no active monitors.") : error;
        changedLayout();
        return;
    }
    if (!QFileInfo::exists(m_options.configPath))
        m_error = tr("%1 doesn't exist. oma-hypr-screens writes Hyprland's Lua config, which Omarchy keeps there.")
                      .arg(configDisplayPath());
    setLayout(mons);
}

QString Backend::configDisplayPath() const {
    QString path = m_options.configPath;
    const QString home = QDir::homePath();
    if (path.startsWith(home + "/"))
        path.replace(0, home.size(), "~");
    return path;
}

// The area the displays cover, the ones that are off included, for the
// canvas to fit them into view.
QRectF Backend::bounds() const {
    QRectF r;
    for (const auto &m : m_mons) {
        const Box b = Layout::box(m);
        r = r.united(QRectF(b.x, b.y, b.w, b.h));
    }
    return r;
}

QVariantMap Backend::current() const {
    const int i = currentIndex();
    if (i < 0)
        return {};
    const Monitor &m = m_mons[i];
    const QSize s = Layout::size(m);
    return {{"name", m.name}, {"number", m.id}, {"label", m.label}, {"description", m.description},
            {"inches", m.inches}, {"mode", m.mode}, {"scaleText", Layout::formatScale(m.scale)},
            {"transform", m.transform}, {"posX", m.x}, {"posY", m.y}, {"logicalWidth", s.width()},
            {"logicalHeight", s.height()}, {"monitorColor", m.color}, {"on", m.enabled},
            {"output", Layout::selector(m_mons, m)}};
}

int Backend::portraitCount() const {
    int n = 0;
    for (const auto &m : m_mons)
        n += m.enabled && m.transform % 2;
    return n;
}

void Backend::say(const QString &text, const QString &kind) {
    m_status = text;
    m_statusKind = kind;
    emit changed();
}

void Backend::changedLayout() {
    m_model.update(m_mons, m_selected);
    emit changed();
}

void Backend::push() {
    m_undo << m_mons;
    if (m_undo.size() > 50)
        m_undo.removeFirst();
    m_redo.clear();
}

void Backend::project(const QString &selected, bool force) {
    if (!force && m_projectedOnce && selected == m_lastProjected)
        return;
    m_projectedOnce = true;
    m_lastProjected = selected;
    if (!m_identifier || !m_identifier->available()) {
        emit identifyInWindow();
        return;
    }
    QList<IdentifyTarget> targets;
    for (const auto &m : m_mons)
        if (m.enabled)
            targets << IdentifyTarget{m.name, m.id, m.color, m.label};
    m_identifier->show(targets, selected, 2500);
}

// ── Selection and editing ───────────────────────────────────────────────────

void Backend::select(const QString &name) {
    if (m_pending || Layout::indexOf(m_mons, name) < 0)
        return;
    m_selected = name;
    project(name);
    changedLayout();
}

void Backend::selectNumber(int number) {
    if (m_pending || number < 1 || number > m_mons.size())
        return;
    select(m_mons[number - 1].name);
    say(tr("Selected %1: look at your monitors, number %2 is highlighted.").arg(m_selected).arg(number));
}

void Backend::selectNext() {
    if (m_pending || m_mons.isEmpty())
        return;
    select(m_mons[(currentIndex() + 1) % m_mons.size()].name);
}

bool Backend::editable(int index) {
    if (index < 0)
        return false;
    if (m_mons[index].enabled)
        return true;
    say(tr("%1 is off: turn it on (o) to place it.").arg(m_mons[index].name));
    return false;
}

void Backend::toggle(const QString &name) {
    const int i = Layout::indexOf(m_mons, name);
    if (m_pending || i < 0)
        return;
    if (name != m_selected)
        select(name);
    const bool on = !m_mons[i].enabled;
    if (!on && Layout::enabledCount(m_mons) <= 1)
        return say(tr("%1 is the only display on: it stays on.").arg(name), "err");
    push();
    Layout::setEnabled(m_mons, i, on);
    changedLayout();
    say(on ? tr("%1 on: it goes back on screen when you apply.").arg(name)
           : tr("%1 off: Hyprland turns it off when you apply, and its workspaces move to the others.").arg(name));
}

void Backend::beginDrag(const QString &name) {
    if (m_pending)
        return;
    select(name);
    if (!editable(Layout::indexOf(m_mons, name)))
        return;
    push();
}

void Backend::dragTo(const QString &name, double x, double y, double threshold, bool free) {
    const int i = Layout::indexOf(m_mons, name);
    if (m_pending || i < 0 || !m_mons[i].enabled)
        return;
    Box r = Layout::box(m_mons[i]);
    r.x = x;
    r.y = y;
    QList<Box> others;
    for (int j = 0; j < m_mons.size(); ++j)
        if (j != i && m_mons[j].enabled)
            others << Layout::box(m_mons[j]);
    const Snap s = free ? Snap{x, y, {}, {}} : Layout::snap(r, others, threshold);
    m_mons[i].x = int(std::lround(s.x));
    m_mons[i].y = int(std::lround(s.y));
    m_guidesX.clear();
    m_guidesY.clear();
    for (double g : s.guidesX)
        m_guidesX << g;
    for (double g : s.guidesY)
        m_guidesY << g;
    emit guidesChanged();
    changedLayout();
}

void Backend::endDrag(const QString &name, bool moved) {
    const int i = Layout::indexOf(m_mons, name);
    if (m_pending || i < 0 || !m_mons[i].enabled)
        return;
    m_guidesX.clear();
    m_guidesY.clear();
    emit guidesChanged();
    if (!moved) {
        // A click, not a drag: forget the undo step beginDrag recorded.
        if (!m_undo.isEmpty() && m_undo.last() == m_mons)
            m_undo.removeLast();
        changedLayout();
        return;
    }
    Layout::attach(m_mons, i);
    Layout::normalize(m_mons);
    changedLayout();
    say(tr("Moved %1.").arg(name));
}

void Backend::rotate(const QString &name) {
    const int i = Layout::indexOf(m_mons, name);
    if (m_pending || i < 0)
        return;
    if (name != m_selected)
        select(name);
    if (!editable(i))
        return;
    push();
    Layout::rotate(m_mons, i, m_mons[i].transform + 1);
    changedLayout();
    const Monitor &m = m_mons[i];
    const QSize s = Layout::size(m);
    const bool bad = !valid();
    QString text = tr("%1 rotated to %2 (%3), %4×%5 logical.")
                       .arg(m.name, Layout::rotationLabel(m.transform), Layout::orientation(m.transform))
                       .arg(s.width()).arg(s.height());
    text += " " + (bad ? tr("It overlaps now: move it with the arrows.") : tr("Neighbors were shifted to fit."));
    say(text, bad ? "err" : "");
}

void Backend::move(int dx, int dy, bool nudge) {
    const int i = currentIndex();
    if (m_pending || i < 0 || (!dx && !dy) || !editable(i))
        return;
    push();
    Monitor &m = m_mons[i];
    if (nudge) {
        m.x += dx * 10;
        m.y += dy * 10;
    } else if (dx) {
        m.x = Layout::jump(m_mons, i, true, dx);
    } else {
        m.y = Layout::jump(m_mons, i, false, dy);
    }
    Layout::normalize(m_mons);
    changedLayout();
    say(tr("%1 → %2,%3").arg(m.name).arg(m_mons[i].x).arg(m_mons[i].y));
}

void Backend::cycleScale() {
    const int i = currentIndex();
    if (m_pending || i < 0 || !editable(i))
        return;
    push();
    Monitor &m = m_mons[i];
    const auto list = Layout::scales(m);
    int at = -1;
    for (int k = 0; k < list.size(); ++k)
        if (std::abs(list[k] - m.scale) < 1e-6)
            at = k;
    m.scale = list[(at + 1) % list.size()];
    Layout::normalize(m_mons);
    changedLayout();
    say(tr("%1 scale ×%2.").arg(m.name, Layout::formatScale(m.scale)));
}

void Backend::cycleMode() {
    const int i = currentIndex();
    if (m_pending || i < 0 || m_mons[i].modes.isEmpty() || !editable(i))
        return;
    push();
    Monitor &m = m_mons[i];
    m.mode = m.modes[(m.modes.indexOf(m.mode) + 1) % m.modes.size()];
    Layout::fixScale(m);
    Layout::normalize(m_mons);
    changedLayout();
    say(tr("%1 resolution %2.").arg(m.name, m.mode));
}

void Backend::nextPreset() {
    if (m_pending || m_mons.isEmpty())
        return;
    push();
    const auto list = Layout::presets(m_mons);
    m_presetIndex = (m_presetIndex + 1) % list.size();
    Layout::applyPreset(m_mons, list[m_presetIndex]);
    changedLayout();
    say(tr("Preset %1/%2: %3.").arg(m_presetIndex + 1).arg(list.size()).arg(list[m_presetIndex].label));
}

void Backend::identify() {
    if (m_mons.isEmpty())
        return;
    project({}, true);
    // The next selection should light up again even if it is the same one.
    m_projectedOnce = false;
    say(tr("Identifying… every monitor shows its number."));
}

void Backend::undo() {
    if (m_pending)
        return;
    if (m_undo.isEmpty())
        return say(tr("Nothing to undo."));
    m_redo << m_mons;
    m_mons = m_undo.takeLast();
    changedLayout();
    say(tr("Undone."));
}

void Backend::redo() {
    if (m_pending)
        return;
    if (m_redo.isEmpty())
        return say(tr("Nothing to redo."));
    m_undo << m_mons;
    m_mons = m_redo.takeLast();
    changedLayout();
    say(tr("Redone."));
}

// ── Apply, keep, revert ─────────────────────────────────────────────────────
//
// The canvas is a draft. Apply backs monitors.lua up to monitors.lua.bak,
// rewrites only the managed block (atomically), and reloads Hyprland. Then
// the user has a few seconds to Keep the result; Revert, or no answer at all
// (say the picture went black), puts the previous file back and leaves the
// draft on the canvas.

static bool writeAtomically(const QString &path, const QString &text, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool Backend::reloadHyprland(QString *error) {
    if (!runHyprctl({"reload"}, nullptr, error))
        return false;
    // A Lua error leaves Hyprland on its previous config; configerrors says so.
    QByteArray json;
    if (runHyprctl({"configerrors", "-j"}, &json, error)) {
        QStringList problems;
        for (const auto &v : QJsonDocument::fromJson(json).array())
            if (!v.toString().trimmed().isEmpty())
                problems << v.toString().trimmed();
        if (!problems.isEmpty()) {
            *error = problems.join("; ");
            return false;
        }
    }
    return true;
}

// A reload doesn't turn a disabled output back on: Hyprland only applies
// the new rules to the outputs it is driving. Each display that should be on
// and isn't gets its rule again through `hyprctl eval`, which does.
void Backend::wake(const Monitors &target) {
    QByteArray json;
    QString error;
    if (!runHyprctl({"monitors", "all", "-j"}, &json, &error))
        return;
    QSet<QString> off;
    for (const auto &v : QJsonDocument::fromJson(json).array())
        if (v.toObject().value("disabled").toBool())
            off << v.toObject().value("name").toString();
    for (const auto &m : target)
        if (m.enabled && off.contains(m.name))
            runHyprctl({"eval", Layout::rule(target, m)}, nullptr, &error);
}

void Backend::apply() {
    if (m_pending)
        return;
    if (!m_error.isEmpty())
        return say(m_error, "err");
    if (!Layout::enabledCount(m_mons))
        return say(tr("Can't apply: every display is off."), "err");
    if (!valid())
        return say(tr("Can't apply: some displays overlap."), "err");
    if (!dirty())
        return say(tr("Nothing to apply: this is already your saved layout."));

    QFile file(m_options.configPath);
    if (!file.open(QIODevice::ReadOnly))
        return say(tr("Can't read %1: %2").arg(configDisplayPath(), file.errorString()), "err");
    const QString original = QString::fromUtf8(file.readAll());
    file.close();

    QString error;
    if (!writeAtomically(m_options.configPath + ".bak", original, &error))
        return say(tr("Can't write the backup %1.bak: %2").arg(configDisplayPath(), error), "err");

    QStringList outputs = Layout::selectors(m_mons);
    for (const auto &m : m_mons)
        outputs << m.name;
    const QString merged = Layout::mergeManagedBlock(original, lua(), outputs);
    if (!writeAtomically(m_options.configPath, merged, &error))
        return say(tr("Can't write %1: %2").arg(configDisplayPath(), error), "err");
    m_backup = original;

    if (!reloadHyprland(&error)) {
        QString restoreError;
        writeAtomically(m_options.configPath, m_backup, &restoreError);
        if (reloadHyprland(&restoreError))
            wake(m_saved);
        return say(tr("Hyprland didn't take the new layout (%1). Your previous monitors.lua is back.").arg(error), "err");
    }

    wake(m_mons);
    m_pending = true;
    m_countdown = m_options.confirmSeconds;
    m_tick.start();
    say({});
}

void Backend::keep() {
    if (!m_pending)
        return;
    m_tick.stop();
    m_pending = false;
    m_saved = m_mons;
    say(tr("Saved to %1 (the previous version is in monitors.lua.bak).").arg(configDisplayPath()), "ok");
}

void Backend::revert(bool timedOut) {
    if (!m_pending)
        return discard();
    m_tick.stop();
    m_pending = false;
    QString error;
    const bool restored = writeAtomically(m_options.configPath, m_backup, &error) && reloadHyprland(&error);
    if (restored)
        wake(m_saved);
    // The screens go back; the canvas keeps the draft that was applied, so
    // it can be fixed and applied again. Esc once more discards it.
    changedLayout();
    if (!restored)
        return say(tr("Couldn't restore the previous layout (%1). It is in %2.bak.").arg(error, configDisplayPath()), "err");
    say(timedOut ? tr("No answer in %1 s: your screens are back as they were. The draft is still here: fix it and apply again, or Esc to discard it.").arg(m_options.confirmSeconds)
                 : tr("Your screens are back as they were. The draft is still here: fix it and apply again, or Esc to discard it."),
        timedOut ? "err" : "");
}

void Backend::discard() {
    if (m_pending)
        return revert();
    if (!dirty())
        return say(tr("Already on your saved layout."));
    push();
    m_mons = m_saved;
    changedLayout();
    say(tr("Discarded the changes. Undo (u) brings them back."));
}

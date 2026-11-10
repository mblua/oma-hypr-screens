#pragma once

// The layout math behind oma-hypr-screens, with no Qt Quick in it so the tests
// can drive it directly: edge snapping, overlap and detachment checks,
// rotation that reflows the neighbors, presets, and the Lua block written to
// ~/.config/hypr/monitors.lua.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>

struct Monitor {
    int id = 0;               // 1-based number shown on the canvas and on the screens
    QString name;             // Hyprland output, e.g. "DP-2"
    QString label;            // short brand, or "Laptop" for the built-in panel
    QString description;      // brand and model
    int inches = 0;
    QStringList modes;        // "2560x1440@59.95", best first, as Hyprland lists them
    QString mode;
    double scale = 1;
    int transform = 0;        // 0..3, quarter turns
    bool flipped = false;     // Hyprland's transforms 4..7, kept as they are
    int x = 0, y = 0;         // logical position
    QString color;

    bool operator==(const Monitor &o) const {
        return name == o.name && mode == o.mode && qAbs(scale - o.scale) < 1e-6 && transform == o.transform
            && flipped == o.flipped && x == o.x && y == o.y;
    }
    bool operator!=(const Monitor &o) const { return !(*this == o); }
};

using Monitors = QList<Monitor>;

// Half-open rectangle: [x, x + w) × [y, y + h). QRect's inclusive right()
// and bottom() make edge contact off by one, so the math uses its own.
struct Box {
    double x = 0, y = 0, w = 0, h = 0;
};

struct Snap {
    double x = 0, y = 0;
    QList<double> guidesX, guidesY;
};

struct Candidate {
    double value;   // where the dragged edge would go
    double line;    // the guide drawn for it
};

struct Preset {
    QString id;
    QString label;
    QHash<QString, QPoint> positions;
};

namespace Layout {

struct Resolution {
    int w = 0, h = 0;
    double hz = 0;
};

Resolution resolution(const Monitor &m);
QSize size(const Monitor &m);       // logical size after scale and rotation
Box box(const Monitor &m);
QList<double> scales(const Monitor &m);
QString formatScale(double scale);
void fixScale(Monitor &m);
QString rotationLabel(int transform);
QString orientation(int transform);

int indexOf(const Monitors &mons, const QString &name);
QSet<QString> overlaps(const Monitors &mons);
QSet<QString> isolated(const Monitors &mons);
void normalize(Monitors &mons);

QList<Candidate> candidates(double length, const QList<Box> &others, bool horizontal);
Snap snap(const Box &r, const QList<Box> &others, double threshold);
void attach(Monitors &mons, int index);
void rotate(Monitors &mons, int index, int transform);
int jump(const Monitors &mons, int index, bool horizontal, int direction);

QList<Preset> presets(const Monitors &mons);
void applyPreset(Monitors &mons, const Preset &preset);

QString toLua(const Monitors &mons);
QString luaHtml(const QString &lua);

// Parses `hyprctl monitors all -j`; disabled and mirrored outputs are left
// out because they have no place on the canvas.
Monitors fromHyprctl(const QByteArray &json, QString *error = nullptr);

// Puts the managed block into monitors.lua and keeps everything else. A
// first run comments out the catch-all rule and any rule for an output the
// block now owns, so the file says one thing per monitor.
QString mergeManagedBlock(const QString &file, const QString &block, const QStringList &outputs);

extern const char *const BlockBegin;
extern const char *const BlockEnd;
extern const char *const LegacyBlockBegin;
extern const char *const LegacyBlockEnd;

} // namespace Layout

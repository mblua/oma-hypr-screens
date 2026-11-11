#include "layout.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <climits>
#include <cmath>
#include <optional>

namespace Layout {

const char *const BlockBegin = "-- >>> oma-hypr-screens";
const char *const BlockEnd = "-- <<< oma-hypr-screens";
// The markers from when the app was called multi-monitors, still recognized
// so an existing block is replaced (and renamed) instead of duplicated.
const char *const LegacyBlockBegin = "-- >>> multi-monitors";
const char *const LegacyBlockEnd = "-- <<< multi-monitors";

static QString tr(const char *text) { return QCoreApplication::translate("Layout", text); }

Resolution resolution(const Monitor &m) {
    static const QRegularExpression pattern(R"(^(\d+)x(\d+)(?:@([\d.]+))?)");
    const auto match = pattern.match(m.mode);
    if (!match.hasMatch())
        return {};
    return {match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toDouble()};
}

QSize size(const Monitor &m) {
    auto r = resolution(m);
    if (r.w <= 0 || r.h <= 0)
        r = {1920, 1080, 60};   // nothing known: a nominal size, never written
    const double scale = m.scale > 0 ? m.scale : 1;
    const int w = int(std::lround(r.w / scale)), h = int(std::lround(r.h / scale));
    return m.transform % 2 ? QSize(h, w) : QSize(w, h);
}

Box box(const Monitor &m) {
    const auto s = size(m);
    return {double(m.x), double(m.y), double(s.width()), double(s.height())};
}

// Hyprland wants a scale that divides the mode into whole logical pixels.
QList<double> scales(const Monitor &m) {
    const auto r = resolution(m);
    QList<double> out;
    for (double s : {1.0, 1.25, 4.0 / 3.0, 1.5, 1.6, 1.75, 2.0, 2.5, 3.0}) {
        const double a = r.w / s, b = r.h / s;
        if (std::abs(a - std::round(a)) < 0.01 && std::abs(b - std::round(b)) < 0.01)
            out << s;
    }
    if (out.isEmpty())
        out << 1.0;
    return out;
}

QString formatScale(double scale) {
    if (std::abs(scale - 4.0 / 3.0) < 1e-3)
        return "1.333333";
    return QString::number(std::round(scale * 1000) / 1000, 'g', 6);
}

void fixScale(Monitor &m) {
    const auto ok = scales(m);
    for (double s : ok)
        if (std::abs(s - m.scale) < 1e-6)
            return;
    m.scale = *std::min_element(ok.begin(), ok.end(), [&](double a, double b) {
        return std::abs(a - m.scale) < std::abs(b - m.scale);
    });
}

QString rotationLabel(int transform) {
    static const char *labels[] = {"0°", "90°", "180°", "270°"};
    return QString::fromUtf8(labels[((transform % 4) + 4) % 4]);
}

QString orientation(int transform) { return transform % 2 ? tr("vertical") : tr("horizontal"); }

int indexOf(const Monitors &mons, const QString &name) {
    for (int i = 0; i < mons.size(); ++i)
        if (mons[i].name == name)
            return i;
    return -1;
}

int enabledCount(const Monitors &mons) {
    int n = 0;
    for (const auto &m : mons)
        n += m.enabled;
    return n;
}

QString selector(const Monitors &mons, const Monitor &m) {
    const QString d = m.hyprDescription.trimmed();
    if (d.isEmpty() || d.contains('"') || d.contains('\\'))
        return m.name;
    for (const auto &o : mons)
        if (o.name != m.name && o.hyprDescription.trimmed() == d)
            return m.name;
    return "desc:" + d;
}

QStringList selectors(const Monitors &mons) {
    QStringList out;
    for (const auto &m : mons)
        out << selector(mons, m);
    return out;
}

static bool intersects(const Box &a, const Box &b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static bool touches(const Box &a, const Box &b) {
    return ((a.x + a.w == b.x || b.x + b.w == a.x) && a.y < b.y + b.h && b.y < a.y + a.h)
        || ((a.y + a.h == b.y || b.y + b.h == a.y) && a.x < b.x + b.w && b.x < a.x + a.w);
}

QSet<QString> overlaps(const Monitors &mons) {
    QSet<QString> bad;
    for (int i = 0; i < mons.size(); ++i)
        for (int j = i + 1; j < mons.size(); ++j)
            if (mons[i].enabled && mons[j].enabled && intersects(box(mons[i]), box(mons[j]))) {
                bad << mons[i].name;
                bad << mons[j].name;
            }
    return bad;
}

// Names in canvas order, so reattaching them is deterministic.
static QStringList isolatedNames(const Monitors &mons) {
    QStringList out;
    if (enabledCount(mons) < 2)
        return out;
    for (int i = 0; i < mons.size(); ++i) {
        if (!mons[i].enabled)
            continue;
        bool linked = false;
        for (int j = 0; j < mons.size() && !linked; ++j)
            if (i != j && mons[j].enabled) {
                const Box a = box(mons[i]), b = box(mons[j]);
                linked = touches(a, b) || intersects(a, b);
            }
        if (!linked)
            out << mons[i].name;
    }
    return out;
}

QSet<QString> isolated(const Monitors &mons) {
    const auto names = isolatedNames(mons);
    return QSet<QString>(names.begin(), names.end());
}

// Turned-off displays move along, so they come back where they were.
void normalize(Monitors &mons) {
    if (!enabledCount(mons))
        return;
    int mx = INT_MAX, my = INT_MAX;
    for (const auto &m : mons) {
        if (!m.enabled)
            continue;
        mx = std::min(mx, m.x);
        my = std::min(my, m.y);
    }
    for (auto &m : mons) {
        m.x -= mx;
        m.y -= my;
    }
}

// Every place an edge of the dragged display can line up with another one:
// flush on either side, sharing an edge, or centered.
QList<Candidate> candidates(double length, const QList<Box> &others, bool horizontal) {
    QList<Candidate> out;
    for (const auto &o : others) {
        const double s = horizontal ? o.x : o.y, l = horizontal ? o.w : o.h;
        out << Candidate{s - length, s} << Candidate{s + l, s + l} << Candidate{s, s}
            << Candidate{s + l - length, s + l} << Candidate{s + l / 2 - length / 2, s + l / 2};
    }
    return out;
}

Snap snap(const Box &r, const QList<Box> &others, double threshold) {
    auto pick = [&](double p, const QList<Candidate> &list) -> std::optional<Candidate> {
        std::optional<Candidate> best;
        for (const auto &c : list)
            if (std::abs(c.value - p) < threshold && (!best || std::abs(c.value - p) < std::abs(best->value - p)))
                best = c;
        return best;
    };
    const auto bx = pick(r.x, candidates(r.w, others, true));
    const auto by = pick(r.y, candidates(r.h, others, false));
    Snap s;
    s.x = bx ? bx->value : r.x;
    s.y = by ? by->value : r.y;
    if (bx)
        s.guidesX << bx->line;
    if (by)
        s.guidesY << by->line;
    return s;
}

// A display dropped where it touches nobody is pulled to the nearest one,
// sharing at least 100 px of edge, so Hyprland never gets a gap.
void attach(Monitors &mons, int index) {
    if (index < 0 || index >= mons.size() || !mons[index].enabled || enabledCount(mons) < 2)
        return;
    Monitor &m = mons[index];
    const Box r = box(m);
    QList<Box> others;
    for (int i = 0; i < mons.size(); ++i)
        if (i != index && mons[i].enabled)
            others << box(mons[i]);
    for (const auto &o : others)
        if (touches(r, o) || intersects(r, o))
            return;

    struct Best { Box o; double d, gx, gy; };
    std::optional<Best> best;
    for (const auto &o : others) {
        const double gx = std::max({o.x - (r.x + r.w), r.x - (o.x + o.w), 0.0});
        const double gy = std::max({o.y - (r.y + r.h), r.y - (o.y + o.h), 0.0});
        const double d = std::hypot(gx, gy);
        if (!best || d < best->d)
            best = Best{o, d, gx, gy};
    }
    const Box o = best->o;
    auto clamp = [](double v, double a, double b) { return std::max(a, std::min(b, v)); };
    if (best->gx >= best->gy) {
        m.x = int(r.x < o.x ? o.x - r.w : o.x + o.w);
        m.y = int(std::lround(clamp(r.y, o.y - r.h + 100, o.y + o.h - 100)));
    } else {
        m.y = int(r.y < o.y ? o.y - r.h : o.y + o.h);
        m.x = int(std::lround(clamp(r.x, o.x - r.w + 100, o.x + o.w - 100)));
    }
}

// Rotates like a monitor turned on its stand: the left and bottom edges stay
// put. Whatever sat to the right moves by the change in width and whatever
// sat above moves by the change in height. Four ways of doing that are tried
// (grow up or down, shift everything to the right or only the same row) and
// the first that leaves nothing overlapping or detached wins.
void rotate(Monitors &mons, int index, int transform) {
    if (index < 0 || index >= mons.size())
        return;
    transform = ((transform % 4) + 4) % 4;
    if (transform == mons[index].transform)
        return;
    if (!mons[index].enabled) {
        mons[index].transform = transform;   // off the canvas: nobody to move
        return;
    }

    const Box b = box(mons[index]);
    QList<QPoint> snapshot;
    for (const auto &o : mons)
        snapshot << QPoint(o.x, o.y);
    auto restore = [&](const QList<QPoint> &points) {
        for (int i = 0; i < mons.size(); ++i) {
            mons[i].x = points[i].x();
            mons[i].y = points[i].y();
        }
    };

    mons[index].transform = transform;
    const QSize a = size(mons[index]);
    const int dw = a.width() - int(b.w), dh = a.height() - int(b.h);
    auto sameRow = [&](const Box &r) { return r.y < b.y + b.h && b.y < r.y + r.h; };

    std::optional<QList<QPoint>> fallback;
    const std::pair<bool, bool> strategies[] = {{true, false}, {false, false}, {true, true}, {false, true}};
    for (const auto &[up, rowOnly] : strategies) {
        restore(snapshot);
        if (dw || dh) {
            for (int i = 0; i < mons.size(); ++i) {
                if (i == index || !mons[i].enabled)
                    continue;
                const Box r = box(mons[i]);
                if (r.x >= b.x + b.w && (!rowOnly || sameRow(r)))
                    mons[i].x += dw;
                if (up && r.y + r.h <= b.y)
                    mons[i].y -= dh;
                if (!up && r.y >= b.y + b.h)
                    mons[i].y += dh;
            }
            mons[index].y = int(up ? b.y + b.h - a.height() : b.y);
        }
        for (const auto &name : isolatedNames(mons))
            attach(mons, indexOf(mons, name));
        const bool clear = overlaps(mons).isEmpty();
        if (clear && isolated(mons).isEmpty()) {
            fallback.reset();
            break;
        }
        if (clear && !fallback) {
            QList<QPoint> points;
            for (const auto &o : mons)
                points << QPoint(o.x, o.y);
            fallback = points;
        }
    }
    if (fallback)
        restore(*fallback);
    normalize(mons);
}

void setEnabled(Monitors &mons, int index, bool enabled) {
    if (index < 0 || index >= mons.size() || mons[index].enabled == enabled)
        return;
    if (!enabled) {
        mons[index].enabled = false;
        normalize(mons);
        return;
    }
    Monitor &m = mons[index];
    m.enabled = true;
    if (resolution(m).w <= 0 && !m.modes.isEmpty())
        m.mode = m.modes.first();
    fixScale(m);
    if (enabledCount(mons) == 1) {
        m.x = m.y = 0;
        return;
    }
    if (overlaps(mons).contains(m.name)) {
        int right = INT_MIN, top = INT_MAX;
        for (int i = 0; i < mons.size(); ++i)
            if (i != index && mons[i].enabled) {
                const Box b = box(mons[i]);
                right = std::max(right, int(b.x + b.w));
                top = std::min(top, int(b.y));
            }
        m.x = right;
        m.y = top;
    }
    attach(mons, index);
    normalize(mons);
}

// The arrow keys hop to the next place an edge lines up, or 200 px when
// there is none in that direction.
int jump(const Monitors &mons, int index, bool horizontal, int direction) {
    const Box r = box(mons[index]);
    QList<Box> others;
    for (int i = 0; i < mons.size(); ++i)
        if (i != index && mons[i].enabled)
            others << box(mons[i]);
    const double p = horizontal ? r.x : r.y;
    std::optional<int> best;
    for (const auto &c : candidates(horizontal ? r.w : r.h, others, horizontal)) {
        const int v = int(std::lround(c.value));
        if (direction > 0 ? v > p : v < p)
            if (!best || (direction > 0 ? v < *best : v > *best))
                best = v;
    }
    return best ? *best : int(p) + direction * 200;
}

static bool isInternal(const QString &name) {
    return name.startsWith("eDP") || name.startsWith("LVDS") || name.startsWith("DSI");
}

// Presets are built from whatever is connected: a row, the laptop under the
// externals, and with three or more externals the smallest one on top.
QList<Preset> presets(const Monitors &mons) {
    QHash<QString, QSize> S;
    for (const auto &m : mons)
        if (m.enabled)
            S.insert(m.name, size(m));
    auto row = [&](const QStringList &names, int bottom, int x0 = 0) {
        QHash<QString, QPoint> out;
        int x = x0;
        for (const auto &n : names) {
            out.insert(n, QPoint(x, bottom - S[n].height()));
            x += S[n].width();
        }
        return out;
    };
    auto tallest = [&](const QStringList &names) {
        int h = 0;
        for (const auto &n : names)
            h = std::max(h, S[n].height());
        return h;
    };

    QString laptop;
    QStringList all, externals;
    for (const auto &m : mons) {
        if (!m.enabled)
            continue;
        all << m.name;
        if (laptop.isEmpty() && isInternal(m.name))
            laptop = m.name;
        else
            externals << m.name;
    }

    QList<Preset> list;
    list << Preset{"row", tr("In a row"), row(all, tallest(all))};
    if (all.size() < 2)
        return list;

    if (!laptop.isEmpty() && !externals.isEmpty()) {
        const int H = tallest(externals);
        auto p = row(externals, H);
        // Centered under the middle display, or under the seam between the
        // two middle ones when there is an even number.
        const int n = int(externals.size());
        const QString mid = externals[n / 2];
        const int cx = n % 2 ? p[mid].x() + S[mid].width() / 2 : p[mid].x();
        p.insert(laptop, QPoint(cx - S[laptop].width() / 2, H));
        list << Preset{"laptop-below", tr("Laptop below"), p};
    }

    if (externals.size() >= 3) {
        QString small = externals[0];
        for (const auto &n : externals)
            if (S[n].width() * S[n].height() < S[small].width() * S[small].height())
                small = n;
        QStringList rest = externals;
        rest.removeAll(small);
        const QString label = mons[indexOf(mons, small)].label;

        {
            const int H = tallest(rest);
            auto p = row(rest, H);
            const int seam = p[rest[1]].x();
            p.insert(small, QPoint(seam - S[small].width() / 2, H - tallest(rest) - S[small].height()));
            if (!laptop.isEmpty()) {
                p.insert(laptop, QPoint(seam - S[laptop].width() / 2, H));
                list << Preset{"small-top", tr("%1 on top, laptop below").arg(label), p};
            } else {
                list << Preset{"small-top", tr("%1 on top").arg(label), p};
            }
        }
        if (!laptop.isEmpty()) {
            QStringList names = rest;
            names.prepend(laptop);
            const int H = tallest(names);
            auto p = row(names, H);
            const int seam = p[rest[0]].x() + S[rest[0]].width();
            p.insert(small, QPoint(seam - S[small].width() / 2, -S[small].height()));
            list << Preset{"laptop-left", tr("Laptop left, %1 on top").arg(label), p};
        }
    }
    return list;
}

void applyPreset(Monitors &mons, const Preset &preset) {
    for (auto &m : mons)
        if (preset.positions.contains(m.name)) {
            m.x = preset.positions[m.name].x();
            m.y = preset.positions[m.name].y();
        }
    normalize(mons);
}

QString rule(const Monitors &mons, const Monitor &m) {
    const QString output = selector(mons, m);
    if (!m.enabled)
        return QString(R"(hl.monitor({ output = "%1", disabled = true }))").arg(output);
    const QString mode = resolution(m).w > 0 ? m.mode : QString("preferred");
    QString s = QString(R"(hl.monitor({ output = "%1", mode = "%2", position = "%3x%4", scale = %5)")
                    .arg(output, mode).arg(m.x).arg(m.y).arg(formatScale(m.scale));
    const int transform = m.transform + (m.flipped ? 4 : 0);
    if (transform)
        s += QString(", transform = %1").arg(transform);
    return s + " })";
}

QString toLua(const Monitors &mons) {
    QStringList lines{QString(BlockBegin) + " (managed block: edits here are overwritten on Apply)"};
    Monitors sorted = mons;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Monitor &a, const Monitor &b) {
        if (a.enabled != b.enabled)
            return a.enabled;
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    });
    for (const auto &m : sorted) {
        // The connector it has today, for whoever reads the file.
        QString note = m.name + " · " + m.label;
        if (!m.enabled)
            note += ", " + tr("off");
        else if (m.transform)
            note += QString(", %1 %2").arg(orientation(m.transform), rotationLabel(m.transform));
        lines << rule(mons, m) + "  -- " + note;
    }
    lines << "-- Any other monitor you plug in"
          << R"(hl.monitor({ output = "", mode = "preferred", position = "auto", scale = "auto" }))"
          << BlockEnd;
    return lines.join('\n');
}

QString luaHtml(const QString &lua) {
    static const QRegularExpression token(R"((--.*$)|("[^"]*")|(\bhl\.monitor\b)|(\b\d+(?:\.\d+)?\b))",
                                          QRegularExpression::MultilineOption);
    QString out;
    int last = 0;
    auto it = token.globalMatch(lua);
    while (it.hasNext()) {
        const auto m = it.next();
        out += lua.mid(last, m.capturedStart() - last).toHtmlEscaped();
        const char *color = m.capturedLength(1) ? "#565f89" : m.capturedLength(2) ? "#9ece6a"
                          : m.capturedLength(3) ? "#7aa2f7" : "#ff9e64";
        out += QString("<span style=\"color:%1\">%2</span>").arg(color, m.captured(0).toHtmlEscaped());
        last = m.capturedEnd();
    }
    out += lua.mid(last).toHtmlEscaped();
    return "<pre>" + out + "</pre>";
}

static QString formatHz(double hz) {
    QString s = QString::number(hz, 'f', 2);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.')))
        s.chop(1);
    return s;
}

// The gap between the layout and the displays that load turned off.
static const int Parking = 400;

static const QStringList Palette{"#7aa2f7", "#bb9af7", "#9ece6a", "#e0af68",
                                 "#7dcfff", "#f7768e", "#ff9e64", "#73daca"};

Monitors fromHyprctl(const QByteArray &json, QString *error) {
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(json, &parse);
    if (!doc.isArray()) {
        if (error)
            *error = tr("Couldn't read the monitor list from hyprctl: %1").arg(parse.errorString());
        return {};
    }
    Monitors mons;
    for (const auto &value : doc.array()) {
        const auto o = value.toObject();
        const QString mirror = o.value("mirrorOf").toString();
        if (!mirror.isEmpty() && mirror != "none")
            continue;

        Monitor m;
        m.name = o.value("name").toString();
        m.enabled = !o.value("disabled").toBool();
        const QString make = o.value("make").toString().trimmed();
        const QString brand = make.section(' ', 0, 0);
        m.label = isInternal(m.name) ? tr("Laptop") : brand.isEmpty() ? m.name : brand;
        // Some models repeat the brand ("DELL P2422HE"); say it once.
        const QString model = o.value("model").toString().trimmed();
        m.description = model.startsWith(brand, Qt::CaseInsensitive) ? brand + model.mid(brand.size())
                                                                     : (brand + " " + model).trimmed();
        m.hyprDescription = o.value("description").toString().trimmed();
        if (m.hyprDescription.isEmpty()) {
            QStringList parts{make, model, o.value("serial").toString().trimmed()};
            parts.removeAll(QString());
            m.hyprDescription = parts.join(' ');
        }
        const double mm = std::hypot(o.value("physicalWidth").toDouble(), o.value("physicalHeight").toDouble());
        m.inches = int(std::lround(mm / 25.4));

        for (const auto &mode : o.value("availableModes").toArray()) {
            QString s = mode.toString();
            s.remove("Hz");
            const auto at = s.indexOf('@');
            if (at > 0)
                s = s.left(at + 1) + formatHz(s.mid(at + 1).toDouble());
            if (!s.isEmpty() && !m.modes.contains(s))
                m.modes << s;
        }
        const QString res = QString("%1x%2").arg(o.value("width").toInt()).arg(o.value("height").toInt());
        const double hz = o.value("refreshRate").toDouble();
        m.mode = res + "@" + formatHz(hz);
        if (o.value("width").toInt() <= 0 || o.value("height").toInt() <= 0) {
            // A disabled output may have no live mode; take its best one.
            m.mode = m.modes.isEmpty() ? QString() : m.modes.first();
        } else if (!m.modes.contains(m.mode)) {
            // Hyprland reports the live rate with more digits than the mode
            // list; take the listed mode that matches it.
            QString close;
            for (const auto &candidate : m.modes)
                if (candidate.startsWith(res + "@") && std::abs(candidate.section('@', 1).toDouble() - hz) < 0.05)
                    close = candidate;
            if (close.isEmpty())
                m.modes.prepend(m.mode);
            else
                m.mode = close;
        }

        double scale = o.value("scale").toDouble(1);
        scale = std::abs(scale - 4.0 / 3.0) < 1e-3 ? 4.0 / 3.0 : std::round(scale * 1000) / 1000;
        m.scale = scale > 0 ? scale : 1;
        const int transform = o.value("transform").toInt();
        m.transform = transform % 4;
        m.flipped = transform >= 4;
        m.x = o.value("x").toInt();
        m.y = o.value("y").toInt();
        mons << m;
    }

    // Numbered left to right, then top to bottom, the way they sit today;
    // the ones turned off come last.
    std::stable_sort(mons.begin(), mons.end(), [](const Monitor &a, const Monitor &b) {
        if (a.enabled != b.enabled)
            return a.enabled;
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    });
    for (int i = 0; i < mons.size(); ++i) {
        mons[i].id = i + 1;
        mons[i].color = Palette[i % Palette.size()];
    }
    normalize(mons);
    // Hyprland doesn't say where a disabled output was, so it would sit
    // under the others at 0,0. Park the ones that are off in a row to the
    // right, apart, where they can be seen and turned back on.
    int right = 0, top = 0;
    for (const auto &m : mons)
        if (m.enabled)
            right = std::max(right, int(box(m).x + box(m).w));
    for (auto &m : mons)
        if (!m.enabled) {
            m.x = right + Parking;
            m.y = top;
            right = m.x + size(m).width();
        }
    return mons;
}

QString mergeManagedBlock(const QString &file, const QString &block, const QStringList &outputs) {
    QStringList lines = file.split('\n');
    int begin = -1, end = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines[i].trimmed();
        if (begin < 0 && (t.startsWith(BlockBegin) || t.startsWith(LegacyBlockBegin)))
            begin = i;
        else if (begin >= 0 && (t.startsWith(BlockEnd) || t.startsWith(LegacyBlockEnd))) {
            end = i;
            break;
        }
    }

    // Only single-line rules are touched: commenting the first line of a
    // rule that spans several would break the Lua.
    static const QRegularExpression rule(R"(^(\s*)(hl\.monitor\s*\(\s*\{.*\}\s*\)\s*)$)");
    static const QRegularExpression output(R"re(\boutput\s*=\s*"([^"]*)")re");
    int insertAt = -1, lastRule = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (begin >= 0 && end >= 0 && i >= begin && i <= end)
            continue;
        const auto match = rule.match(lines[i]);
        if (!match.hasMatch())
            continue;
        const auto out = output.match(match.captured(2));
        if (!out.hasMatch())
            continue;
        const QString name = out.captured(1);
        if (!name.isEmpty() && !outputs.contains(name))
            continue;
        lines[i] = match.captured(1) + "-- (replaced by oma-hypr-screens) " + match.captured(2);
        if (name.isEmpty() && insertAt < 0)
            insertAt = i + 1;
        lastRule = i + 1;
    }

    const QStringList blockLines = block.split('\n');
    if (begin >= 0 && end >= 0) {
        lines.remove(begin, end - begin + 1);
        for (int i = 0; i < blockLines.size(); ++i)
            lines.insert(begin + i, blockLines[i]);
        return lines.join('\n');
    }

    if (insertAt < 0)
        insertAt = lastRule;
    if (insertAt < 0) {
        // Nothing to sit next to: append, after a blank line.
        while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
            lines.removeLast();
        if (!lines.isEmpty())
            lines << "";
        lines << blockLines << "";
        return lines.join('\n');
    }
    for (int i = 0; i < blockLines.size(); ++i)
        lines.insert(insertAt + i, blockLines[i]);
    return lines.join('\n');
}

} // namespace Layout

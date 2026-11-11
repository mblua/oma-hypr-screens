#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "identify.h"
#include "layout.h"
#include "theme.h"
#include "translator.h"

// Omarchy's stock monitors.lua, the file a first Apply meets.
static QByteArray stockConfigBytes() {
    QFile file(QString(FIXTURES) + "/stock-monitors.lua");
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
#define StockConfig stockConfigBytes()

class FakeIdentifier : public Identifier {
public:
    bool available() const override { return true; }
    void show(const QList<IdentifyTarget> &targets, const QString &selected, int) override {
        shown = targets.size();
        lastSelected = selected;
        ++calls;
    }
    int shown = 0, calls = 0;
    QString lastSelected;
};

class OmaHyprScreensTests : public QObject {
    Q_OBJECT

private:
    static Monitors fixture() {
        QFile file(QString(FIXTURES) + "/monitors.json");
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return Layout::fromHyprctl(file.readAll());
    }

    static bool write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    }

    static QString read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

    static void checkClean(const Monitors &mons) {
        QVERIFY2(Layout::overlaps(mons).isEmpty(), qPrintable(Layout::toLua(mons)));
        QVERIFY2(Layout::isolated(mons).isEmpty(), qPrintable(Layout::toLua(mons)));
    }

    // A hyprctl stand-in: answers from files in the directory and logs calls.
    // monitors.json starts as the fixture; a test rewrites it to change what
    // Hyprland reports.
    static QString fakeHyprctl(const QTemporaryDir &dir) {
        const QString script = dir.filePath("hyprctl");
        write(dir.filePath("configerrors.json"), "[\"\"]");
        QFile::copy(QString(FIXTURES) + "/monitors.json", dir.filePath("monitors.json"));
        write(script, "#!/bin/sh\n"
                      "echo \"$*\" >> '" + dir.filePath("calls").toUtf8() + "'\n"
                      "case \"$1\" in\n"
                      "  monitors) cat '" + dir.filePath("monitors.json").toUtf8() + "' ;;\n"
                      "  configerrors) cat '" + dir.filePath("configerrors.json").toUtf8() + "' ;;\n"
                      "  reload) echo ok ;;\n"
                      "  eval) echo ok ;;\n"
                      "esac\n");
        QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return script;
    }

private slots:
    // ── Reading Hyprland ────────────────────────────────────────────────────

    void parsesHyprctl() {
        const Monitors mons = fixture();
        QCOMPARE(mons.size(), 5);   // the disabled output comes last, off
        QCOMPARE(mons[0].name, QString("eDP-2"));
        QCOMPARE(mons[0].label, QString("Laptop"));
        QCOMPARE(mons[0].mode, QString("2560x1600@240"));
        QCOMPARE(Layout::size(mons[0]), QSize(1600, 1000));
        QCOMPARE(mons[1].name, QString("DP-2"));
        QCOMPARE(mons[1].mode, QString("2560x1440@59.95"));
        QCOMPARE(mons[2].description, QString("Dell P2422HE"));
        QCOMPARE(mons[3].name, QString("HDMI-A-1"));
        QCOMPARE(mons[3].id, 4);
        QCOMPARE(mons[3].x, 6080);
        QCOMPARE(mons[1].inches, 32);
        QCOMPARE(mons[2].hyprDescription, QString("Dell Inc. DELL P2422HE 2Z7LYB3"));
        QCOMPARE(mons[4].name, QString("DP-9"));
        QVERIFY(!mons[4].enabled);
        QCOMPARE(mons[4].id, 5);
        QVERIFY(mons[0].enabled);
        checkClean(mons);
    }

    // ── Geometry ────────────────────────────────────────────────────────────

    void snapsToNeighborEdges() {
        const Box dragged{2570, 30, 1920, 1080};
        const Snap s = Layout::snap(dragged, {Box{0, 0, 2560, 1440}}, 40);
        QCOMPARE(s.x, 2560.0);
        QCOMPARE(s.y, 0.0);
        QCOMPARE(s.guidesX, QList<double>{2560});
        const Snap far = Layout::snap(Box{3000, 500, 100, 100}, {Box{0, 0, 2560, 1440}}, 40);
        QCOMPARE(far.x, 3000.0);
        QVERIFY(far.guidesX.isEmpty());
    }

    void findsOverlapsAndDetached() {
        Monitors mons = fixture();
        mons[1].x -= 100;   // DP-2 slides into the laptop
        QCOMPARE(Layout::overlaps(mons), (QSet<QString>{"eDP-2", "DP-2"}));
        mons = fixture();
        mons[3].x += 500;   // HDMI-A-1 floats away
        QCOMPARE(Layout::isolated(mons), QSet<QString>{"HDMI-A-1"});
    }

    void attachesDetachedDisplay() {
        Monitors mons = fixture();
        mons[3].x += 500;
        mons[3].y += 3000;
        Layout::attach(mons, 3);
        Layout::normalize(mons);
        checkClean(mons);
    }

    void rotationReflowsNeighbors() {
        Monitors mons = fixture();
        const int dp2 = Layout::indexOf(mons, "DP-2");
        Layout::rotate(mons, dp2, 1);
        QCOMPARE(mons[dp2].transform, 1);
        QCOMPARE(Layout::size(mons[dp2]), QSize(1440, 2560));
        checkClean(mons);
        // The Dell, which sat right of DP-2, moved left by the lost width.
        QCOMPARE(mons[Layout::indexOf(mons, "DP-3")].x, mons[dp2].x + 1440);
    }

    void rotationAlwaysTurnsTheSameWay() {
        // Every display, four quarter turns each: always valid, always
        // back to 0° after four.
        for (int i = 0; i < 4; ++i) {
            Monitors mons = fixture();
            for (int step = 1; step <= 4; ++step) {
                Layout::rotate(mons, i, mons[i].transform + 1);
                QCOMPARE(mons[i].transform, step % 4);
                checkClean(mons);
            }
        }
    }

    void rotatingSeveralStaysClean() {
        Monitors mons = fixture();
        for (int i = 0; i < mons.size(); ++i) {
            Layout::rotate(mons, i, 1);
            checkClean(mons);
        }
    }

    void arrowsJumpToTheNextEdge() {
        const Monitors mons = fixture();
        const int laptop = 0;
        // Down, the nearest line-up is centered on the Dell; left there is
        // none, so it steps 200 px.
        QCOMPARE(Layout::jump(mons, laptop, false, 1), 40);
        QCOMPARE(Layout::jump(mons, laptop, true, -1), -200);
    }

    void scalesDivideTheMode() {
        Monitor m;
        m.mode = "2560x1600@240";
        QVERIFY(Layout::scales(m).contains(1.6));
        m.mode = "1920x1080@60";
        m.scale = 1.6;
        Layout::fixScale(m);
        QVERIFY(Layout::scales(m).contains(m.scale));
        QCOMPARE(Layout::formatScale(4.0 / 3.0), QString("1.333333"));
        QCOMPARE(Layout::formatScale(1.6), QString("1.6"));
    }

    void presetsAreValid() {
        Monitors mons = fixture();
        const auto list = Layout::presets(mons);
        QCOMPARE(list.size(), 4);
        for (const auto &p : list) {
            Monitors copy = mons;
            Layout::applyPreset(copy, p);
            checkClean(copy);
        }
        // Still valid with a portrait display in the mix.
        Layout::rotate(mons, 1, 1);
        for (const auto &p : Layout::presets(mons)) {
            Monitors copy = mons;
            Layout::applyPreset(copy, p);
            checkClean(copy);
        }
    }

    // ── Naming displays ─────────────────────────────────────────────────────

    // Connectors can swap between boots (a dock or a USB-C hub decides the
    // order). The rules must still point at the same physical displays.
    void rulesSurviveSwappedConnectors() {
        Monitors mons = fixture();
        Layout::rotate(mons, 2, 3);
        auto rules = [](const Monitors &list) {
            QStringList out;
            for (const auto &line : Layout::toLua(list).split('\n'))
                out << line.section("  -- ", 0, 0);
            return out;
        };
        const QStringList before = rules(mons);
        Monitors swapped = mons;
        swapped[1].name = "DP-5";
        swapped[2].name = "DP-4";
        QCOMPARE(rules(swapped), before);
    }

    void twinsWithoutSerialFallBackToConnectors() {
        Monitors mons = fixture();
        mons[1].hyprDescription = mons[3].hyprDescription = "ViewSonic Corporation VX3218C-QHD";
        QCOMPARE(Layout::selector(mons, mons[1]), QString("DP-2"));
        QCOMPARE(Layout::selector(mons, mons[3]), QString("HDMI-A-1"));
        QCOMPARE(Layout::selector(mons, mons[2]), QString("desc:Dell Inc. DELL P2422HE 2Z7LYB3"));
        mons[0].hyprDescription.clear();
        QCOMPARE(Layout::selector(mons, mons[0]), QString("eDP-2"));
    }

    void mergeSetsAsideRulesByDescription() {
        const Monitors mons = fixture();
        QStringList outputs = Layout::selectors(mons);
        for (const auto &m : mons)
            outputs << m.name;
        const QString file = QString::fromUtf8(StockConfig)
            + "hl.monitor({ output = \"desc:Dell Inc. DELL P2422HE 2Z7LYB3\", mode = \"preferred\", position = \"auto\", scale = 1 })\n";
        const QString merged = Layout::mergeManagedBlock(file, Layout::toLua(mons), outputs);
        QVERIFY(merged.contains("-- (replaced by oma-hypr-screens) hl.monitor({ output = \"desc:Dell Inc."));
    }

    // ── Turning displays off ────────────────────────────────────────────────

    void offDisplaysLeaveTheGeometry() {
        Monitors mons = fixture();
        // DP-9 is off, parked apart to the right: no overlap, not detached.
        checkClean(mons);
        int right = 0;
        for (int i = 0; i < 4; ++i)
            right = std::max(right, mons[i].x + Layout::size(mons[i]).width());
        QVERIFY(mons[4].x > right);
        Layout::setEnabled(mons, 3, false);   // HDMI-A-1, the rightmost
        QVERIFY(!mons[3].enabled);
        checkClean(mons);
        QCOMPARE(Layout::presets(mons).first().positions.size(), 3);
        const QString lua = Layout::toLua(mons);
        QVERIFY(lua.contains("hl.monitor({ output = \"desc:ViewSonic Corporation VX3218C-QHD X1J242920849\", disabled = true })"));
        // Rotating it off the canvas moves nobody.
        const Monitors before = mons;
        Layout::rotate(mons, 3, 1);
        QCOMPARE(mons[3].transform, 1);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(QPoint(mons[i].x, mons[i].y), QPoint(before[i].x, before[i].y));
    }

    void turningOnFindsAFreeSpot() {
        Monitors mons = fixture();
        const QPoint where(mons[3].x, mons[3].y);
        Layout::setEnabled(mons, 3, false);
        Layout::setEnabled(mons, 3, true);
        QCOMPARE(QPoint(mons[3].x, mons[3].y), where);   // its old place was free
        checkClean(mons);

        // DP-9 was never placed: it lands beside the others, with a real mode.
        Layout::setEnabled(mons, 4, true);
        QVERIFY(mons[4].enabled);
        checkClean(mons);
        QVERIFY(Layout::toLua(mons).contains("output = \"desc:Nobody Off\", mode = \"1920x1080@60\""));
    }

    // ── monitors.lua ────────────────────────────────────────────────────────

    void writesLua() {
        Monitors mons = fixture();
        Layout::rotate(mons, 1, 1);
        const QString lua = Layout::toLua(mons);
        QVERIFY(lua.startsWith(Layout::BlockBegin));
        QVERIFY(lua.endsWith(Layout::BlockEnd));
        QVERIFY(lua.contains("hl.monitor({ output = \"desc:Samsung Display Corp. ATNA60HU01-0\", mode = \"2560x1600@240\", position = \"0x"));
        QVERIFY(lua.contains("output = \"desc:ViewSonic Corporation VX3218C-QHD X1J252340452\""));
        QVERIFY(lua.contains(", transform = 1 })  -- DP-2 · ViewSonic, vertical 90°"));
        QVERIFY(lua.contains("hl.monitor({ output = \"desc:Nobody Off\", disabled = true })  -- DP-9 · Nobody, off"));
        QVERIFY(!lua.contains("output = \"DP-"));
        QVERIFY(lua.contains("hl.monitor({ output = \"\", mode = \"preferred\", position = \"auto\", scale = \"auto\" })"));
    }

    void firstMergeReplacesTheCatchAllRule() {
        const Monitors mons = fixture();
        QStringList outputs;
        for (const auto &m : mons)
            outputs << m.name;
        const QString merged = Layout::mergeManagedBlock(QString::fromUtf8(StockConfig), Layout::toLua(mons), outputs);

        QVERIFY(merged.contains("-- (replaced by oma-hypr-screens) hl.monitor({ output = \"\", mode = \"preferred\", position = \"auto\", scale = omarchy_monitor_scale })"));
        QVERIFY(merged.contains(Layout::toLua(mons)));
        QVERIFY(merged.contains("hl.env(\"GDK_SCALE\", tostring(omarchy_gdk_scale))"));
        QVERIFY(merged.contains("-- hl.monitor({ output = \"DP-2\", mode = \"2560x1440@144\""));
        QVERIFY(merged.endsWith('\n'));
        // The block sits where the catch-all rule was.
        QVERIFY(merged.indexOf(Layout::BlockBegin) > merged.indexOf("replaced by oma-hypr-screens"));
        QVERIFY(merged.indexOf(Layout::BlockBegin) < merged.indexOf("Configure a specific monitor"));
        QCOMPARE(merged.count(Layout::BlockBegin), 1);
    }

    void laterMergesOnlyTouchTheBlock() {
        Monitors mons = fixture();
        const QStringList outputs{"eDP-2", "DP-2", "DP-3", "HDMI-A-1"};
        const QString first = Layout::mergeManagedBlock(QString::fromUtf8(StockConfig), Layout::toLua(mons), outputs);
        Layout::rotate(mons, 1, 1);
        // A rule the user added by hand for a managed output is set aside;
        // one for an output the app doesn't manage is left alone.
        const QString edited = first + "hl.monitor({ output = \"DP-3\", mode = \"preferred\", position = \"auto\", scale = 1 })\n"
                                       "hl.monitor({ output = \"DP-7\", mode = \"preferred\", position = \"auto\", scale = 1 })\n";
        const QString second = Layout::mergeManagedBlock(edited, Layout::toLua(mons), outputs);

        QCOMPARE(second.count(Layout::BlockBegin), 1);
        QCOMPARE(second.count(Layout::BlockEnd), 1);
        QVERIFY(second.contains("transform = 1"));
        QVERIFY(second.contains("-- (replaced by oma-hypr-screens) hl.monitor({ output = \"DP-3\""));
        QVERIFY(second.contains("\nhl.monitor({ output = \"DP-7\""));
        QCOMPARE(second.count("replaced by oma-hypr-screens"), 2);
        // Merging the same block again changes nothing.
        QCOMPARE(Layout::mergeManagedBlock(second, Layout::toLua(mons), outputs), second);
    }

    void replacesTheBlockFromTheOldName() {
        const Monitors mons = fixture();
        const QString legacy = "hl.env(\"A\", \"1\")\n"
                               "-- >>> multi-monitors (managed block: edits here are overwritten on Apply)\n"
                               "hl.monitor({ output = \"eDP-2\", mode = \"preferred\", position = \"0x0\", scale = 1 })\n"
                               "-- <<< multi-monitors\n"
                               "hl.env(\"B\", \"2\")\n";
        const QString merged = Layout::mergeManagedBlock(legacy, Layout::toLua(mons), {"eDP-2"});

        QVERIFY(!merged.contains("multi-monitors"));
        QCOMPARE(merged.count(Layout::BlockBegin), 1);
        QCOMPARE(merged.count(Layout::BlockEnd), 1);
        QVERIFY(merged.startsWith("hl.env(\"A\", \"1\")\n" + QString(Layout::BlockBegin)));
        QVERIFY(merged.endsWith(QString(Layout::BlockEnd) + "\nhl.env(\"B\", \"2\")\n"));
    }

    void appendsWhenThereIsNoRule() {
        const QString merged = Layout::mergeManagedBlock("hl.env(\"A\", \"1\")\n", Layout::toLua(fixture()), {"eDP-2"});
        QVERIFY(merged.startsWith("hl.env(\"A\", \"1\")\n\n" + QString(Layout::BlockBegin)));
    }

    // ── Backend: editing ────────────────────────────────────────────────────

    void backendLoadsAndEdits() {
        QTemporaryDir dir;
        write(dir.filePath("monitors.lua"), StockConfig);
        FakeIdentifier identifier;
        Backend backend({fakeHyprctl(dir), dir.filePath("monitors.lua"), 10}, &identifier);
        QCOMPARE(backend.count(), 5);
        QVERIFY(backend.error().isEmpty());
        QVERIFY(!backend.dirty());

        backend.selectNumber(2);
        QCOMPARE(backend.selected(), QString("DP-2"));
        QCOMPARE(identifier.lastSelected, QString("DP-2"));
        QCOMPARE(identifier.shown, 4);
        backend.identify();
        QCOMPARE(identifier.lastSelected, QString());

        backend.rotateSelected();
        QVERIFY(backend.dirty());
        QCOMPARE(backend.current().value("transform").toInt(), 1);
        backend.undo();
        QVERIFY(!backend.dirty());
        backend.redo();
        QCOMPARE(backend.current().value("transform").toInt(), 1);
        backend.discard();
        QVERIFY(!backend.dirty());
    }

    void backendTogglesDisplays() {
        QTemporaryDir dir;
        write(dir.filePath("monitors.lua"), StockConfig);
        FakeIdentifier identifier;
        Backend backend({fakeHyprctl(dir), dir.filePath("monitors.lua"), 10}, &identifier);
        backend.selectNumber(3);
        QCOMPARE(backend.selected(), QString("DP-3"));
        backend.toggleSelected();
        QVERIFY(backend.dirty());
        QCOMPARE(backend.current().value("on").toBool(), false);
        QVERIFY(backend.lua().contains("desc:Dell Inc. DELL P2422HE 2Z7LYB3\", disabled = true"));

        // Off, it can't be moved or rotated.
        const QVariantMap before = backend.current();
        backend.rotateSelected();
        backend.move(1, 0, false);
        QCOMPARE(backend.current(), before);

        backend.identify();
        QCOMPARE(identifier.shown, 3);

        backend.undo();
        QVERIFY(!backend.dirty());

        // The last display that is on stays on.
        for (const QString name : {"eDP-2", "DP-2", "DP-3"})
            backend.toggle(name);
        QVERIFY(backend.valid());
        backend.toggle("HDMI-A-1");
        QCOMPARE(backend.layout()[Layout::indexOf(backend.layout(), "HDMI-A-1")].enabled, true);
        QCOMPARE(backend.statusKind(), QString("err"));
    }

    // Hyprland's reload leaves a disabled output off even when the restored
    // file wants it on; revert turns it back on through hyprctl eval.
    void revertWakesWhatWasTurnedOff() {
        QTemporaryDir dir;
        const QString config = dir.filePath("monitors.lua");
        write(config, StockConfig);
        Backend backend({fakeHyprctl(dir), config, 10});
        backend.toggle("DP-3");
        backend.apply();
        QVERIFY(backend.pending());
        QVERIFY(read(config).contains("desc:Dell Inc. DELL P2422HE 2Z7LYB3\", disabled = true"));
        QVERIFY(!read(dir.filePath("calls")).contains("eval"));

        // Hyprland now has DP-3 off.
        QByteArray json = read(dir.filePath("monitors.json")).toUtf8();
        const int at = json.indexOf("\"name\":\"DP-3\"");
        json.replace(json.indexOf("\"disabled\":false", at), 16, "\"disabled\":true");
        write(dir.filePath("monitors.json"), json);

        backend.revert();
        QCOMPARE(read(config), QString::fromUtf8(StockConfig));
        QVERIFY2(read(dir.filePath("calls")).contains(
                     "eval hl.monitor({ output = \"desc:Dell Inc. DELL P2422HE 2Z7LYB3\", mode = \"1920x1080@60\""),
                 qPrintable(read(dir.filePath("calls"))));
        // The draft, with DP-3 off, stays on the canvas.
        QVERIFY(!backend.layout()[Layout::indexOf(backend.layout(), "DP-3")].enabled);
    }

    // The same when the change is turning one on: the reload alone won't.
    void applyWakesWhatIsTurnedOn() {
        QTemporaryDir dir;
        const QString config = dir.filePath("monitors.lua");
        write(config, StockConfig);
        Backend backend({fakeHyprctl(dir), config, 10});
        backend.toggle("DP-9");
        QVERIFY(backend.valid());
        backend.apply();
        QVERIFY(backend.pending());
        QVERIFY(read(dir.filePath("calls")).contains("eval hl.monitor({ output = \"desc:Nobody Off\", mode = "));
    }

    void offDisplaysDontDrag() {
        QTemporaryDir dir;
        write(dir.filePath("monitors.lua"), StockConfig);
        Backend backend({fakeHyprctl(dir), dir.filePath("monitors.lua"), 10});
        const Monitor before = backend.layout()[4];
        backend.beginDrag("DP-9");
        backend.dragTo("DP-9", 0, 0, 1, true);
        backend.endDrag("DP-9", true);
        QCOMPARE(backend.layout()[4].x, before.x);
        QCOMPARE(backend.selected(), QString("DP-9"));
        QVERIFY(!backend.canUndo());
    }

    void backendDragSnapsAndAttaches() {
        QTemporaryDir dir;
        write(dir.filePath("monitors.lua"), StockConfig);
        Backend backend({fakeHyprctl(dir), dir.filePath("monitors.lua"), 10});
        backend.beginDrag("HDMI-A-1");
        backend.dragTo("HDMI-A-1", 6085, 2000, 40, false);
        QCOMPARE(backend.layout()[3].x, 6080);
        QVERIFY(!backend.guidesX().isEmpty());
        backend.dragTo("HDMI-A-1", 9000, 5000, 40, false);
        backend.endDrag("HDMI-A-1", true);
        checkClean(backend.layout());
        QVERIFY(backend.guidesX().isEmpty());
    }

    // ── Backend: apply, keep, revert ────────────────────────────────────────

    void applyWritesBackupAndKeeps() {
        QTemporaryDir dir;
        const QString config = dir.filePath("monitors.lua");
        write(config, StockConfig);
        Backend backend({fakeHyprctl(dir), config, 10});
        backend.rotate("DP-2");
        backend.apply();
        QVERIFY2(backend.pending(), qPrintable(backend.status()));
        QCOMPARE(read(config + ".bak"), QString::fromUtf8(StockConfig));
        QVERIFY(read(config).contains("transform = 1"));
        QVERIFY(read(dir.filePath("calls")).contains("reload"));

        backend.keep();
        QVERIFY(!backend.pending());
        QVERIFY(!backend.dirty());
        QCOMPARE(backend.statusKind(), QString("ok"));
        QVERIFY(read(config).contains("transform = 1"));
    }

    void revertRestoresTheFile() {
        QTemporaryDir dir;
        const QString config = dir.filePath("monitors.lua");
        write(config, StockConfig);
        Backend backend({fakeHyprctl(dir), config, 10});
        backend.rotate("DP-2");
        backend.apply();
        QVERIFY(backend.pending());
        backend.revert();
        QVERIFY(!backend.pending());
        QCOMPARE(read(config), QString::fromUtf8(StockConfig));
        // The canvas keeps the draft that was applied, rotation included.
        QVERIFY(backend.dirty());
        QCOMPARE(backend.layout()[Layout::indexOf(backend.layout(), "DP-2")].transform, 1);
        // It can be applied again, or discarded.
        backend.apply();
        QVERIFY(backend.pending());
        backend.revert();
        backend.discard();
        QVERIFY(!backend.dirty());
    }

    void noAnswerReverts() {
        QTemporaryDir dir;
        const QString config = dir.filePath("monitors.lua");
        write(config, StockConfig);
        Backend backend({fakeHyprctl(dir), config, 1});
        backend.rotate("DP-3");
        backend.apply();
        QVERIFY(backend.pending());
        QTRY_VERIFY_WITH_TIMEOUT(!backend.pending(), 3000);
        QCOMPARE(read(config), QString::fromUtf8(StockConfig));
        QCOMPARE(backend.statusKind(), QString("err"));
        QCOMPARE(backend.layout()[Layout::indexOf(backend.layout(), "DP-3")].transform, 1);
    }

    void configErrorsRestoreRightAway() {
        QTemporaryDir dir;
        const QString config = dir.filePath("monitors.lua");
        write(config, StockConfig);
        const QString hyprctl = fakeHyprctl(dir);
        write(dir.filePath("configerrors.json"), "[\"monitors.lua:5: bad mode\"]");
        Backend backend({hyprctl, config, 10});
        backend.rotate("DP-3");
        backend.apply();
        QVERIFY(!backend.pending());
        QCOMPARE(read(config), QString::fromUtf8(StockConfig));
        QVERIFY(backend.status().contains("bad mode"));
    }

    void overlapsBlockApply() {
        QTemporaryDir dir;
        const QString config = dir.filePath("monitors.lua");
        write(config, StockConfig);
        Backend backend({fakeHyprctl(dir), config, 10});
        backend.beginDrag("DP-3");
        backend.dragTo("DP-3", 2000, 100, 0.5, true);
        QVERIFY(!backend.valid());
        backend.apply();
        QVERIFY(!backend.pending());
        QCOMPARE(read(config), QString::fromUtf8(StockConfig));
        QVERIFY(!QFile::exists(config + ".bak"));
    }

    void missingConfigIsReported() {
        QTemporaryDir dir;
        Backend backend({fakeHyprctl(dir), dir.filePath("nope/monitors.lua"), 10});
        QVERIFY(backend.error().contains("doesn't exist"));
        backend.rotate("DP-2");
        backend.apply();
        QVERIFY(!backend.pending());
    }

    // ── Theme and language (from the Omarchy app template) ─────────────────

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QVERIFY(write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n"));
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Apply (enter)").isNull());
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n s": {"one": "%ns", "other": "%ns"}})");
        write(dir.path() + "/ru.json", R"({"%n s": {"one": "%n с", "few": "%n с", "many": "%n с", "other": "%n с"}})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n s", nullptr, 21), QString("%n с"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }
};

QTEST_MAIN(OmaHyprScreensTests)
#include "oma-hypr-screens_tests.moc"

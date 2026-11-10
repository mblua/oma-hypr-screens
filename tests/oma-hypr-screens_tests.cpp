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
    static QString fakeHyprctl(const QTemporaryDir &dir) {
        const QString script = dir.filePath("hyprctl");
        write(dir.filePath("configerrors.json"), "[\"\"]");
        write(script, "#!/bin/sh\n"
                      "echo \"$*\" >> '" + dir.filePath("calls").toUtf8() + "'\n"
                      "case \"$1\" in\n"
                      "  monitors) cat '" + QByteArray(FIXTURES) + "/monitors.json' ;;\n"
                      "  configerrors) cat '" + dir.filePath("configerrors.json").toUtf8() + "' ;;\n"
                      "  reload) echo ok ;;\n"
                      "esac\n");
        QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return script;
    }

private slots:
    // ── Reading Hyprland ────────────────────────────────────────────────────

    void parsesHyprctl() {
        const Monitors mons = fixture();
        QCOMPARE(mons.size(), 4);   // the disabled output is left out
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

    // ── monitors.lua ────────────────────────────────────────────────────────

    void writesLua() {
        Monitors mons = fixture();
        Layout::rotate(mons, 1, 1);
        const QString lua = Layout::toLua(mons);
        QVERIFY(lua.startsWith(Layout::BlockBegin));
        QVERIFY(lua.endsWith(Layout::BlockEnd));
        QVERIFY(lua.contains("hl.monitor({ output = \"eDP-2\", mode = \"2560x1600@240\", position = \"0x"));
        QVERIFY(lua.contains(", transform = 1 })  -- ViewSonic, vertical 90°"));
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
        QCOMPARE(backend.count(), 4);
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
        QVERIFY(!backend.dirty());
        // The rotation is one undo away.
        backend.undo();
        QVERIFY(backend.dirty());
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

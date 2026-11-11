import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180
    height: 760
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: "oma-hypr-screens"
    color: bg

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    font.family: "JetBrainsMono Nerd Font"
    font.pixelSize: 13

    readonly property color bg: "#1a1b26"
    readonly property color bgDark: "#13141c"
    readonly property color bgDarker: "#0e0e14"
    readonly property color bgLight: "#24283b"
    readonly property color fg: "#a9b1d6"
    readonly property color fgDim: "#565f89"
    readonly property color fgBright: "#c0caf5"
    readonly property color muted: "#414868"
    readonly property color red: "#f7768e"
    readonly property color yellow: "#e0af68"
    readonly property color green: "#9ece6a"
    readonly property color orange: "#ff9e64"

    property bool helpVisible: false
    property bool quitVisible: false
    readonly property bool modal: helpVisible || quitVisible
    property bool identifying: false
    property bool quitting: false

    function tint(c, a) {
        const q = Qt.color(c)
        return Qt.rgba(q.r, q.g, q.b, a)
    }

    function requestQuit() {
        if (backend.pending || backend.dirty)
            quitVisible = true
        else
            quit()
    }
    function quit() {
        quitting = true
        Qt.quit()
    }

    onClosing: close => {
        if (!quitting && (backend.pending || backend.dirty)) {
            close.accepted = false
            quitVisible = true
        }
    }

    // Without overlays on the real screens, the canvas shows the numbers.
    Connections {
        target: backend
        function onIdentifyInWindow() {
            win.identifying = true
            identifyTimer.restart()
        }
    }
    Timer { id: identifyTimer; interval: 2500; onTriggered: win.identifying = false }

    // ── Keys ────────────────────────────────────────────────────────────────
    Shortcut { sequences: ["?", "Shift+?", "F1"]; context: Qt.ApplicationShortcut; enabled: !win.quitVisible; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: win.requestQuit() }
    Shortcut {
        sequences: ["Return", "Enter"]
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.quitVisible) quitPrimary.clicked()
            else if (backend.pending) backend.keep()
            else backend.apply()
        }
    }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.modal && !backend.pending; onActivated: backend.apply() }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.quitVisible) win.quitVisible = false
            else backend.revert()
        }
    }
    Instantiator {
        model: 9
        delegate: Shortcut {
            required property int index
            sequence: String(index + 1)
            context: Qt.ApplicationShortcut
            enabled: !win.modal
            onActivated: backend.selectNumber(index + 1)
        }
    }
    Shortcut { sequence: "Tab"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.selectNext() }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.move(-1, 0, false) }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.move(1, 0, false) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.move(0, -1, false) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.move(0, 1, false) }
    Shortcut { sequence: "Shift+Left"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.move(-1, 0, true) }
    Shortcut { sequence: "Shift+Right"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.move(1, 0, true) }
    Shortcut { sequence: "Shift+Up"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.move(0, -1, true) }
    Shortcut { sequence: "Shift+Down"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.move(0, 1, true) }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.rotateSelected() }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.cycleScale() }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.cycleMode() }
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.nextPreset() }
    Shortcut { sequence: "I"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.identify() }
    Shortcut { sequence: "O"; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.toggleSelected() }
    Shortcut { sequences: ["U", "Ctrl+Z"]; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z"]; context: Qt.ApplicationShortcut; enabled: !win.modal; onActivated: backend.redo() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // ── Title strip ─────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 30
                color: theme.accent

                RowLayout {
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                    spacing: 14
                    Text {
                        text: "󰍹 oma-hypr-screens"
                        color: theme.accentForeground === "black" ? win.bgDarker : "white"
                        font.weight: Font.Bold
                    }
                    Text {
                        text: backend.pending ? qsTr("◐ applied, waiting for confirmation")
                            : backend.dirty ? qsTr("● unsaved changes") : qsTr("✓ saved")
                        color: theme.accentForeground === "black" ? win.bgDarker : "white"
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: (backend.valid ? qsTr("✓ valid layout") : qsTr("✗ displays overlap"))
                              + (backend.portraitCount ? " · " + qsTr("%n portrait", "", backend.portraitCount) : "")
                        color: theme.accentForeground === "black" ? win.bgDarker : "white"
                    }
                }
            }

            // ── What's wrong, said plainly ──────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                visible: backend.error !== ""
                implicitHeight: errorText.implicitHeight + 14
                color: win.tint(win.red, 0.15)
                Text {
                    id: errorText
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12; topMargin: 7 }
                    text: "✗ " + backend.error
                    color: win.red
                    wrapMode: Text.Wrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // ── Left panel ──────────────────────────────────────────────
                ScrollView {
                    id: side
                    Layout.preferredWidth: 350
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true

                    Column {
                        width: side.availableWidth
                        topPadding: 12
                        bottomPadding: 14
                        spacing: 0

                        SectionTitle { text: qsTr("── displays ──") }
                        Repeater {
                            model: backend.monitors
                            delegate: DisplayRow {}
                        }

                        SectionTitle { text: qsTr("── rotation ──"); topPadding: 14 }
                        Rectangle {
                            x: 14
                            width: parent.width - 28
                            height: inspector.implicitHeight + 20
                            color: "transparent"
                            border.color: win.muted
                            radius: Math.min(4, theme.radius)
                            visible: backend.selected !== ""

                            Column {
                                id: inspector
                                readonly property var m: backend.current
                                readonly property int t: m.transform === undefined ? 0 : m.transform
                                x: 10
                                y: 10
                                width: parent.width - 20
                                spacing: 8

                                Text {
                                    width: parent.width
                                    elide: Text.ElideRight
                                    text: "[" + inspector.m.number + "] " + inspector.m.name + " · " + inspector.m.label
                                          + (inspector.m.inches ? " " + inspector.m.inches + "\"" : "")
                                    color: inspector.m.monitorColor || win.fg
                                }
                                Row {
                                    spacing: 12
                                    Item {
                                        width: 40
                                        height: 40
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: inspector.t % 2 ? 24 : 38
                                            height: inspector.t % 2 ? 38 : 24
                                            color: win.tint(win.orange, 0.12)
                                            border.width: 2
                                            border.color: win.orange
                                            Behavior on width { NumberAnimation { duration: 200 } }
                                            Behavior on height { NumberAnimation { duration: 200 } }
                                        }
                                    }
                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text {
                                            text: ["0°", "90°", "180°", "270°"][inspector.t] + " · "
                                                  + [qsTr("Normal"), qsTr("Portrait"), qsTr("Upside down"), qsTr("Portrait")][inspector.t]
                                            color: win.fgBright
                                        }
                                        Text {
                                            text: [qsTr("landscape"), qsTr("turned 90°"), qsTr("turned 180°"), qsTr("turned 270°")][inspector.t]
                                            color: win.fgDim
                                        }
                                    }
                                }
                                Row {
                                    width: parent.width
                                    spacing: 4
                                    Repeater {
                                        model: 4
                                        Rectangle {
                                            required property int index
                                            width: (parent.width - 12) / 4
                                            height: 4
                                            color: index <= inspector.t ? win.orange : win.muted
                                        }
                                    }
                                }
                                Rectangle {
                                    width: parent.width
                                    height: 40
                                    radius: Math.min(4, theme.radius)
                                    color: bigSpin.containsMouse ? Qt.lighter(win.orange, 1.1) : win.orange
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 8
                                        RotateGlyph { anchors.verticalCenter: parent.verticalCenter; color: win.bgDarker; size: 16 }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: qsTr("Rotate 90°")
                                            color: win.bgDarker
                                            font.pixelSize: 14
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                    MouseArea {
                                        id: bigSpin
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.rotateSelected()
                                    }
                                }
                                Text {
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    font.pixelSize: 11
                                    color: win.fgDim
                                    text: qsTr("Each click turns it 90° the same way. 4 clicks = back to start. If the picture comes out upside down after applying, click twice more.")
                                }
                                Rectangle {
                                    readonly property bool on: inspector.m.on !== false
                                    width: parent.width
                                    height: 34
                                    radius: Math.min(4, theme.radius)
                                    color: power.containsMouse ? win.bgLight : "transparent"
                                    border.width: 1
                                    border.color: on ? win.muted : win.green
                                    Text {
                                        anchors.centerIn: parent
                                        text: parent.on ? qsTr("⏻ Turn off (o)") : qsTr("⏻ Turn on (o)")
                                        color: parent.on ? win.fg : win.green
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                    }
                                    MouseArea {
                                        id: power
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.toggleSelected()
                                    }
                                }
                            }
                        }

                        SectionTitle { text: "── " + backend.configPath + " ──"; topPadding: 14 }
                        Rectangle {
                            x: 14
                            width: parent.width - 28
                            height: luaText.implicitHeight + 16
                            color: "transparent"
                            border.color: win.muted
                            radius: Math.min(4, theme.radius)

                            Flickable {
                                anchors { fill: parent; margins: 8 }
                                contentWidth: luaText.implicitWidth
                                contentHeight: luaText.implicitHeight
                                flickableDirection: Flickable.HorizontalFlick
                                clip: true
                                ScrollBar.horizontal: ScrollBar {}
                                Text {
                                    id: luaText
                                    text: backend.luaHtml
                                    textFormat: Text.RichText
                                    color: win.fg
                                    font.pixelSize: 11
                                    lineHeight: 1.3
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: win.muted }

                // ── Canvas ──────────────────────────────────────────────────
                Item {
                    id: stage
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    property bool dragging: false
                    property real k: 0.05
                    property real ox: 0
                    property real oy: 0
                    readonly property real pad: 40

                    // Refit only between drags, so the display under the
                    // pointer doesn't slide away while it moves.
                    function fit() {
                        const b = backend.bounds
                        if (b.width <= 0 || b.height <= 0 || width <= 0 || height <= 0)
                            return
                        k = Math.max(0.005, Math.min((width - 2 * pad) / b.width, (height - 2 * pad) / b.height))
                        ox = (width - b.width * k) / 2 - b.x * k
                        oy = (height - b.height * k) / 2 - b.y * k
                    }
                    onWidthChanged: fit()
                    onHeightChanged: fit()
                    Connections {
                        target: backend
                        function onChanged() { if (!stage.dragging) stage.fit() }
                    }
                    Component.onCompleted: fit()

                    Canvas {
                        id: dots
                        anchors.fill: parent
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.fillStyle = win.muted
                            for (let x = 0; x < width; x += 24)
                                for (let y = 0; y < height; y += 24)
                                    ctx.fillRect(x, y, 1, 1)
                        }
                    }

                    Repeater {
                        model: backend.monitors
                        delegate: MonitorTile {
                            canvas: stage
                            k: stage.k
                            ox: stage.ox
                            oy: stage.oy
                            identifying: win.identifying
                        }
                    }

                    Repeater {
                        model: backend.guidesX
                        Rectangle {
                            required property var modelData
                            x: stage.ox + modelData * stage.k
                            width: 1
                            height: stage.height
                            color: win.yellow
                            z: 10
                        }
                    }
                    Repeater {
                        model: backend.guidesY
                        Rectangle {
                            required property var modelData
                            y: stage.oy + modelData * stage.k
                            height: 1
                            width: stage.width
                            color: win.yellow
                            z: 10
                        }
                    }
                }
            }

            // ── Confirmation countdown and footer ───────────────────────────
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 3
                color: "transparent"
                Rectangle {
                    id: countdownBar
                    height: parent.height
                    color: win.yellow
                    width: 0
                    NumberAnimation on width {
                        id: countdownAnimation
                        running: false
                        duration: backend.confirmSeconds * 1000
                    }
                }
                Connections {
                    target: backend
                    property bool wasPending: false
                    function onChanged() {
                        if (backend.pending && !wasPending) {
                            countdownAnimation.from = countdownBar.parent.width
                            countdownAnimation.to = 0
                            countdownAnimation.restart()
                        } else if (!backend.pending && wasPending) {
                            countdownAnimation.stop()
                            countdownBar.width = 0
                        }
                        wasPending = backend.pending
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 46
                color: backend.pending ? Qt.tint(win.bg, win.tint(win.yellow, 0.12)) : win.bg
                Rectangle { width: parent.width; height: 1; color: win.muted }

                RowLayout {
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                    spacing: 10

                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        textFormat: Text.StyledText
                        text: backend.pending
                              ? qsTr("Layout applied. Keep it? Reverting in %1…")
                                    .arg("<b><font color='" + win.yellow + "'>" + qsTr("%n s", "", backend.countdown) + "</font></b>")
                              : backend.status
                        color: backend.statusKind === "err" ? win.red : backend.statusKind === "ok" ? win.green : win.fgBright
                    }
                    ActionButton {
                        visible: !backend.pending
                        text: qsTr("Discard changes")
                        active: backend.dirty
                        onClicked: backend.discard()
                    }
                    ActionButton {
                        visible: !backend.pending
                        text: qsTr("Apply (enter)")
                        active: backend.dirty && backend.valid && backend.error === ""
                        fill: theme.accent
                        ink: theme.accentForeground === "black" ? win.bgDarker : "white"
                        onClicked: backend.apply()
                    }
                    ActionButton {
                        visible: backend.pending
                        text: qsTr("Revert (esc)")
                        onClicked: backend.revert()
                    }
                    ActionButton {
                        visible: backend.pending
                        text: qsTr("Keep (enter)")
                        fill: win.green
                        ink: win.bgDarker
                        onClicked: backend.keep()
                    }
                }
            }

            // ── Key hints ───────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: keys.implicitHeight + 12
                color: win.bgDark
                Flow {
                    id: keys
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6; leftMargin: 12; rightMargin: 12 }
                    spacing: 13
                    Repeater {
                        model: [
                            ["1-" + Math.max(1, backend.count), qsTr("select")], ["tab", qsTr("next")],
                            ["←↑↓→", qsTr("jump to next edge")], ["shift+arrow", qsTr("nudge 10px")],
                            ["r", qsTr("rotate 90°")], ["s", qsTr("scale")], ["m", qsTr("resolution")],
                            ["p", qsTr("preset")], ["o", qsTr("on/off")], ["i", qsTr("identify")], ["u", qsTr("undo")],
                            ["enter", qsTr("apply")], ["esc", qsTr("revert")], ["?", qsTr("help")]
                        ]
                        Text {
                            required property var modelData
                            textFormat: Text.StyledText
                            color: win.fgDim
                            text: "<b><font color='" + (modelData[0] === "r" ? win.orange : theme.accent) + "'>"
                                  + modelData[0] + "</font></b> " + modelData[1]
                        }
                    }
                }
            }
        }
    }

    // ── Help ────────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        visible: win.helpVisible
        color: Qt.rgba(0, 0, 0, 0.6)
        z: 100
        MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

        Rectangle {
            anchors.centerIn: parent
            width: helpGrid.implicitWidth + 48
            height: helpGrid.implicitHeight + 70
            color: win.bg
            border.color: theme.accent
            border.width: 2
            radius: theme.radius

            Text {
                x: 24
                y: 16
                text: qsTr("Keyboard")
                color: theme.accent
                font.weight: Font.Bold
            }
            GridLayout {
                id: helpGrid
                x: 24
                y: 46
                columns: 2
                columnSpacing: 24
                rowSpacing: 4
                Repeater {
                    model: [
                        ["1–9", qsTr("Select a display (its number lights up on every monitor)")],
                        ["Tab", qsTr("Next display")],
                        ["← ↑ ↓ →", qsTr("Jump to the next edge")],
                        ["Shift + arrow", qsTr("Nudge 10 px")],
                        ["R", qsTr("Rotate 90°, always the same way")],
                        ["S", qsTr("Next scale")],
                        ["M", qsTr("Next resolution")],
                        ["P", qsTr("Next preset")],
                        ["O", qsTr("Turn the display off, or back on")],
                        ["I", qsTr("Show every number on the monitors")],
                        ["U / Ctrl + Z", qsTr("Undo")],
                        ["Ctrl + Shift + Z", qsTr("Redo")],
                        ["Enter / Space", qsTr("Apply, then Enter again to keep")],
                        ["Esc", qsTr("Revert (the draft stays), or discard the changes")],
                        ["Alt + drag", qsTr("Move without snapping")],
                        ["?", qsTr("This help")],
                        ["Q", qsTr("Quit")]
                    ]
                    delegate: Item {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 0
                        Layout.columnSpan: 2
                        implicitWidth: keyText.width + descText.implicitWidth
                        implicitHeight: descText.implicitHeight
                        Text { id: keyText; width: 170; text: modelData[0]; color: theme.accent; font.weight: Font.DemiBold }
                        Text { id: descText; x: keyText.width; text: modelData[1]; color: win.fg }
                    }
                }
            }
        }
    }

    // ── Quit with unsaved or unconfirmed work ───────────────────────────────
    Rectangle {
        anchors.fill: parent
        visible: win.quitVisible
        color: Qt.rgba(0, 0, 0, 0.6)
        z: 100
        MouseArea { anchors.fill: parent }

        Rectangle {
            anchors.centerIn: parent
            width: 520
            height: quitColumn.implicitHeight + 40
            color: win.bg
            border.color: theme.accent
            border.width: 2
            radius: theme.radius

            Column {
                id: quitColumn
                x: 20
                y: 20
                width: parent.width - 40
                spacing: 16
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: win.fgBright
                    text: backend.pending
                          ? qsTr("The new layout is still waiting for confirmation. Keep it before quitting?")
                          : qsTr("Quit and lose the unsaved changes?")
                }
                Row {
                    anchors.right: parent.right
                    spacing: 10
                    ActionButton { text: qsTr("Cancel (esc)"); onClicked: win.quitVisible = false }
                    ActionButton {
                        visible: backend.pending
                        text: qsTr("Revert and quit")
                        onClicked: { backend.revert(); win.quit() }
                    }
                    ActionButton {
                        id: quitPrimary
                        text: backend.pending ? qsTr("Keep and quit (enter)") : qsTr("Quit (enter)")
                        fill: backend.pending ? win.green : win.red
                        ink: win.bgDarker
                        onClicked: { if (backend.pending) backend.keep(); win.quit() }
                    }
                }
            }
        }
    }

    component SectionTitle: Text {
        leftPadding: 14
        bottomPadding: 6
        color: win.fgDim
    }
}

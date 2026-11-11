import QtQuick

// One display on the canvas. The face (name, numbers, the "▲ top" strip)
// turns with the monitor's rotation; the Rotate and power buttons stay
// upright. A display that is off stays where it was, grayed out and below
// the others, with only the power button and selection working.
Rectangle {
    id: tile
    required property string name
    required property int number
    required property string label
    required property int inches
    required property string mode
    required property int quarterTurns
    required property int posX
    required property int posY
    required property int logicalWidth
    required property int logicalHeight
    required property int hz
    required property string monitorColor
    required property bool overlapping
    required property bool selected
    required property bool on

    property Item canvas
    property real k: 0.05
    property real ox: 0
    property real oy: 0
    property bool identifying: false
    property bool dragging: false

    readonly property color ink: "#a9b1d6"
    readonly property color dim: "#565f89"
    readonly property color red: "#f7768e"
    readonly property color orange: "#ff9e64"

    function fade(c, a) {
        const q = Qt.color(c)
        return Qt.rgba(q.r, q.g, q.b, a)
    }

    readonly property color gray: "#414868"
    readonly property color green: "#9ece6a"
    // The color everything on the face uses: gray when the display is off.
    readonly property color hue: on ? monitorColor : gray

    x: ox + posX * k
    y: oy + posY * k
    width: logicalWidth * k
    height: logicalHeight * k
    z: dragging ? 5 : !on ? 0 : selected ? 2 : 1
    clip: true
    radius: Math.min(4, theme.radius)
    color: !on ? (selected ? "#1a1b26" : "#101118")
         : overlapping ? Qt.tint("#13141c", tile.fade(red, 0.15))
         : selected ? Qt.tint("#13141c", tile.fade(monitorColor, 0.10)) : "#13141c"
    border.width: selected ? 2 : 1
    border.color: !on ? (selected ? "#565f89" : "#2a2e42")
                : overlapping ? red : selected ? monitorColor : "#414868"

    Behavior on x { enabled: !tile.dragging; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    Behavior on y { enabled: !tile.dragging; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    Behavior on width { enabled: !tile.dragging; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    Behavior on height { enabled: !tile.dragging; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

    // Each quarter turn adds 90°, so going from 270° back to 0° keeps
    // spinning the same way instead of unwinding.
    property int shownTurns: quarterTurns
    property real angle: quarterTurns * 90
    onQuarterTurnsChanged: {
        angle += (((quarterTurns - shownTurns) % 4) + 4) % 4 * 90
        shownTurns = quarterTurns
        spin.restart()
    }

    MouseArea {
        id: drag
        anchors.fill: parent
        preventStealing: true
        cursorShape: !tile.on ? Qt.PointingHandCursor : tile.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property point start
        property int startX
        property int startY
        property bool moved: false

        onPressed: mouse => {
            if (!tile.on) {
                // Off: a click selects it; there is nothing to drag.
                backend.select(tile.name)
                return
            }
            start = mapToItem(tile.canvas, mouse.x, mouse.y)
            startX = tile.posX
            startY = tile.posY
            moved = false
            tile.dragging = true
            tile.canvas.dragging = true
            backend.beginDrag(tile.name)
        }
        onPositionChanged: mouse => {
            if (!tile.dragging)
                return
            const p = mapToItem(tile.canvas, mouse.x, mouse.y)
            if (!moved && Math.hypot(p.x - start.x, p.y - start.y) < 3)
                return
            moved = true
            // Alt moves freely, without snapping to the other edges.
            backend.dragTo(tile.name, startX + (p.x - start.x) / tile.k, startY + (p.y - start.y) / tile.k,
                           16 / tile.k, (mouse.modifiers & Qt.AltModifier) !== 0)
        }
        onReleased: finish()
        onCanceled: finish()
        function finish() {
            if (!tile.dragging)
                return
            tile.dragging = false
            tile.canvas.dragging = false
            backend.endDrag(tile.name, moved)
        }
    }

    Item {
        id: face
        readonly property bool turned: tile.quarterTurns % 2 === 1
        anchors.centerIn: parent
        width: turned ? tile.height : tile.width
        height: turned ? tile.width : tile.height
        rotation: tile.angle
        Behavior on rotation { enabled: !tile.dragging; NumberAnimation { duration: 400; easing.type: Easing.OutBack; easing.overshoot: 1.3 } }

        Rectangle {
            id: topStrip
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 5
            color: tile.hue
        }
        Text {
            anchors { top: topStrip.bottom; topMargin: 1; horizontalCenter: parent.horizontalCenter }
            text: tile.on ? qsTr("▲ top") : qsTr("off")
            color: tile.hue
            font.pixelSize: 10
            font.letterSpacing: 0.5
        }

        Text {
            anchors.centerIn: parent
            text: tile.number
            color: tile.hue
            font.pixelSize: Math.max(8, Math.round(Math.min(face.width * 0.5, face.height * 0.75)))
            font.weight: Font.ExtraBold
            opacity: tile.identifying ? 1 : tile.selected ? 0.4 : 0.22
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }

        Column {
            anchors { left: parent.left; top: parent.top; leftMargin: 10; topMargin: 22 }
            spacing: 0
            opacity: tile.identifying ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: 200 } }

            Text {
                text: "[" + tile.number + "] " + tile.name + (tile.on ? "" : "  " + qsTr("○ off"))
                color: tile.on ? tile.monitorColor : tile.dim
                font.pixelSize: 13
                font.weight: Font.Bold
            }
            Text {
                text: tile.inches > 0 ? qsTr("%1 %2\"").arg(tile.label).arg(tile.inches) : tile.label
                color: tile.dim
                font.pixelSize: 11
            }
            Text {
                text: tile.logicalWidth + "×" + tile.logicalHeight + " @" + tile.hz
                color: tile.dim
                font.pixelSize: 11
            }
            Text {
                visible: tile.on
                text: tile.posX + "," + tile.posY
                color: tile.dim
                font.pixelSize: 11
            }
            Rectangle {
                visible: tile.on && tile.quarterTurns !== 0
                width: badge.implicitWidth + 10
                height: badge.implicitHeight
                color: tile.orange
                Text {
                    id: badge
                    anchors.centerIn: parent
                    text: "↻ " + ["0°", "90°", "180°", "270°"][tile.quarterTurns] + " "
                          + (tile.quarterTurns % 2 ? qsTr("vertical") : qsTr("horizontal"))
                    color: "#0e0e14"
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
            }
        }
    }

    // Always visible, always upright: one click is one quarter turn, the
    // same way every time.
    Rectangle {
        id: rotateButton
        anchors { right: parent.right; bottom: parent.bottom; margins: 8 }
        height: 38
        width: Math.max(38, row.implicitWidth + 20)
        radius: Math.min(4, theme.radius)
        color: hover.containsMouse ? Qt.tint("#24283b", tile.fade(tile.orange, 0.12)) : "#24283b"
        border.width: 1
        border.color: tile.orange
        visible: tile.on
        opacity: tile.identifying ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6
            RotateGlyph {
                id: glyph
                anchors.verticalCenter: parent.verticalCenter
                color: tile.orange
                size: 20
                rotation: hover.containsMouse && !spin.running ? 90 : 0
                Behavior on rotation { enabled: !spin.running; NumberAnimation { duration: 250 } }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: tile.width > 170 && tile.height > 120
                text: qsTr("Rotate")
                color: tile.orange
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }

        NumberAnimation {
            id: spin
            target: glyph
            property: "rotation"
            from: -90
            to: 0
            duration: 400
            easing.type: Easing.OutCubic
        }

        MouseArea {
            id: hover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: backend.rotate(tile.name)
        }
    }

    // On/off, beside Rotate: one click turns the display off (it grays out
    // and stays here) or back on. Applied like any other change.
    Rectangle {
        id: powerButton
        anchors {
            right: tile.on ? rotateButton.left : parent.right
            rightMargin: tile.on ? 6 : 8
            bottom: parent.bottom
            bottomMargin: 8
        }
        height: 38
        width: Math.max(38, powerRow.implicitWidth + 20)
        radius: Math.min(4, theme.radius)
        readonly property color ink: tile.on ? tile.ink : tile.green
        color: powerHover.containsMouse ? Qt.tint("#24283b", tile.fade(ink, 0.12)) : "#24283b"
        border.width: 1
        border.color: ink
        opacity: tile.identifying ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Row {
            id: powerRow
            anchors.centerIn: parent
            spacing: 6
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "⏻"
                color: powerButton.ink
                font.pixelSize: 18
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: tile.width > 230 && tile.height > 120
                text: tile.on ? qsTr("Off") : qsTr("Turn on")
                color: powerButton.ink
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }

        MouseArea {
            id: powerHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: backend.toggle(tile.name)
        }
    }
}

import QtQuick

// One display on the canvas. The face (name, numbers, the "▲ top" strip)
// turns with the monitor's rotation; the Rotate button stays upright.
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

    x: ox + posX * k
    y: oy + posY * k
    width: logicalWidth * k
    height: logicalHeight * k
    z: dragging ? 5 : selected ? 2 : 1
    clip: true
    radius: Math.min(4, theme.radius)
    color: overlapping ? Qt.tint("#13141c", tile.fade(red, 0.15))
         : selected ? Qt.tint("#13141c", tile.fade(monitorColor, 0.10)) : "#13141c"
    border.width: selected ? 2 : 1
    border.color: overlapping ? red : selected ? monitorColor : "#414868"

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
        cursorShape: tile.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property point start
        property int startX
        property int startY
        property bool moved: false

        onPressed: mouse => {
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
            color: tile.monitorColor
        }
        Text {
            anchors { top: topStrip.bottom; topMargin: 1; horizontalCenter: parent.horizontalCenter }
            text: qsTr("▲ top")
            color: tile.monitorColor
            font.pixelSize: 10
            font.letterSpacing: 0.5
        }

        Text {
            anchors.centerIn: parent
            text: tile.number
            color: tile.monitorColor
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
                text: "[" + tile.number + "] " + tile.name
                color: tile.monitorColor
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
                text: tile.posX + "," + tile.posY
                color: tile.dim
                font.pixelSize: 11
            }
            Rectangle {
                visible: tile.quarterTurns !== 0
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
}

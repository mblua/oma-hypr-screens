import QtQuick

// One display in the left-hand list.
Rectangle {
    id: row
    required property string name
    required property int number
    required property string label
    required property string mode
    required property string scaleText
    required property int quarterTurns
    required property int posX
    required property int posY
    required property int logicalWidth
    required property int logicalHeight
    required property string monitorColor
    required property bool overlapping
    required property bool detached
    required property bool selected
    required property bool on

    width: parent ? parent.width : 0
    height: content.implicitHeight + 12
    color: selected ? "#292e42" : area.containsMouse ? "#1f2335" : "transparent"

    Rectangle {
        width: 3
        height: parent.height
        color: row.selected ? row.monitorColor : "transparent"
    }

    Text {
        id: key
        x: 14
        y: 6
        width: 28
        text: "[" + row.number + "]"
        color: row.monitorColor
        font.pixelSize: 13
        font.weight: Font.Bold
    }

    Column {
        id: content
        opacity: row.on ? 1 : 0.55
        anchors { left: key.right; leftMargin: 6; right: shape.left; rightMargin: 6 }
        y: 6

        Text {
            width: parent.width
            elide: Text.ElideRight
            textFormat: Text.StyledText
            color: "#c0caf5"
            font.pixelSize: 13
            text: row.name + " · " + row.label
                  + (!row.on ? "  <font color='#565f89'>" + qsTr("○ off") + "</font>"
                     : row.overlapping ? "  <font color='#f7768e'>" + qsTr("✗ overlaps") + "</font>"
                     : row.detached ? "  <font color='#e0af68'>" + qsTr("· detached") + "</font>" : "")
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            textFormat: Text.StyledText
            color: "#565f89"
            font.pixelSize: 13
            text: row.mode + " <font color='#a9b1d6'>×" + row.scaleText + "</font> ↻<font color='"
                  + (row.quarterTurns ? "#ff9e64" : "#a9b1d6") + "'>" + ["0°", "90°", "180°", "270°"][row.quarterTurns] + "</font>"
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            textFormat: Text.StyledText
            color: "#565f89"
            font.pixelSize: 13
            text: qsTr("pos %1 · logical %2")
                  .arg("<font color='#a9b1d6'>" + row.posX + "," + row.posY + "</font>")
                  .arg("<font color='#a9b1d6'>" + row.logicalWidth + "×" + row.logicalHeight + "</font>")
        }
    }

    // A small outline that turns with the monitor.
    Rectangle {
        id: shape
        anchors { right: parent.right; rightMargin: 14; top: parent.top; topMargin: 10 }
        opacity: row.on ? 1 : 0.3
        width: row.quarterTurns % 2 ? 16 : 26
        height: row.quarterTurns % 2 ? 26 : 16
        color: Qt.rgba(Qt.color(row.monitorColor).r, Qt.color(row.monitorColor).g, Qt.color(row.monitorColor).b, 0.15)
        border.width: 2
        border.color: row.monitorColor
        Behavior on width { NumberAnimation { duration: 180 } }
        Behavior on height { NumberAnimation { duration: 180 } }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: backend.select(row.name)
    }
}

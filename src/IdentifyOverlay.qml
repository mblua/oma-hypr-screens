import QtQuick

// What one physical monitor shows while identifying: its number, huge, and a
// thick border when it is the one selected in the window.
Item {
    id: root
    property string number: "?"
    property color accent: "#c0caf5"
    property string caption: ""
    property bool selected: false
    property bool dimmed: false

    Rectangle {
        anchors.fill: parent
        color: root.selected ? Qt.rgba(0.05, 0.05, 0.08, 0.55) : Qt.rgba(0.05, 0.05, 0.08, 0.35)
        border.width: root.selected ? 18 : 0
        border.color: root.accent
    }

    Column {
        anchors.centerIn: parent
        spacing: 8
        opacity: root.dimmed ? 0.45 : 1

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.number
            color: root.accent
            font.pixelSize: Math.max(24, Math.round(Math.min(root.height * 0.42, root.width * 0.5)))
            font.weight: Font.Black
            font.family: "JetBrainsMono Nerd Font"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.caption
            color: "#c0caf5"
            font.pixelSize: Math.max(22, Math.round(root.height * 0.035))
            font.family: "JetBrainsMono Nerd Font"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.selected
            text: qsTr("◆ SELECTED")
            color: root.accent
            font.pixelSize: Math.max(18, Math.round(root.height * 0.028))
            font.weight: Font.Bold
            font.family: "JetBrainsMono Nerd Font"
        }
    }
}

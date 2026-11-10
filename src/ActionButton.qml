import QtQuick

// A flat, square-cornered button in the prototype's look. It never takes
// focus, so Enter and Space always reach the window's shortcuts.
Rectangle {
    id: button
    property alias text: label.text
    property color fill: "#24283b"
    property color ink: "#c0caf5"
    property color edge: fill
    property bool active: true
    signal clicked()

    implicitWidth: label.implicitWidth + 28
    implicitHeight: 32
    radius: Math.min(4, theme.radius)
    color: !active ? "#24283b" : area.containsMouse ? Qt.lighter(fill, 1.12) : fill
    border.width: 1
    border.color: active ? edge : "#414868"
    opacity: active ? 1 : 0.5

    Text {
        id: label
        anchors.centerIn: parent
        color: button.active ? button.ink : "#565f89"
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: button.active ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (button.active) button.clicked()
    }
}

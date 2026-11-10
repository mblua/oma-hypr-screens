import QtQuick
import QtQuick.Shapes

// The circular arrow on every Rotate button, drawn as a path so it takes
// any color.
Item {
    id: glyph
    property color color: "#ff9e64"
    property real size: 20
    width: size
    height: size

    Shape {
        width: 24
        height: 24
        scale: glyph.size / 24
        anchors.centerIn: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: glyph.color
            strokeWidth: 2.4
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M20 12a8 8 0 1 1-2.34-5.66" }
        }
        ShapePath {
            strokeColor: glyph.color
            strokeWidth: 2.4
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M20 4v5h-5" }
        }
    }
}

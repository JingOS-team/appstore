/*
 * Copyright (C) 2021 Beijing Jingling Information System Technology Co., Ltd. All rights reserved.
 *
 * Authors:
 * Zhang He Gang <zhanghegang@jingos.com>
 *
 */

import QtQuick 2.0

Item {

    id: arrowView
    property bool isActive
    property string imageUrl
    signal arrowClicked

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered: {
            arrowView.opacity = 1.0
        }
        onExited: {
            arrowView.opacity = 0.0
        }
    }

    Component {
        id: leftComponent

        Rectangle {
            id: leftRect
            color: "transparent"
            radius: 6
            width: 26 * appScaleSize
            height: 26 * appScaleSize

            Image {
                id: leftImage

                anchors.centerIn: parent
                source: imageUrl
                width: 16 * appScaleSize
                height: 16 * appScaleSize
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onExited: {
                    leftRect.color = "transparent"
                    arrowView.opacity = 0.0
                }
                onEntered: {
                    leftRect.color = "#1A000000"
                    arrowView.opacity = 1.0
                }
                onPressed: {
                    leftRect.color = "#26000000"
                }
                onReleased: {
                    leftRect.color = "#1A000000"
                }
                onClicked: {
                    arrowClicked()
                }
            }
        }
    }

    Loader {
        id: leftLoader

        anchors.centerIn: parent
        sourceComponent: leftComponent
        active: isActive
    }
}

/*
    SPDX-FileCopyrightText: 2013 Antonis Tsiapaliokas <kok3rs@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcm
import org.kde.kconfig
import org.kde.kirigami 2.10 as Kirigami
import org.kde.newstuff as NewStuff

import org.kde.private.kcms.kwin.effects as Private

ScrollViewKCM {
    ConfigModule.quickHelp: i18n("This module lets you configure desktop effects.")

    header: ColumnLayout {
        QQC2.Label {
            Layout.fillWidth: true

            elide: Text.ElideRight
            text: i18n("Hint: To find out or configure how to activate an effect, look at the effect's settings.")
        }

        RowLayout {
            Kirigami.SearchField {
                id: searchField

                Layout.fillWidth: true
            }

            QQC2.ToolButton {
                id: filterButton

                icon.name: "view-filter"

                checkable: true
                checked: menu.opened
                onClicked: menu.popup(filterButton, filterButton.width - menu.width, filterButton.height)

                QQC2.ToolTip {
                    text: i18n("Configure Filter")
                }
            }

            QQC2.Menu {
                id: menu

                modal: true

                QQC2.MenuItem {
                    checkable: true
                    checked: searchModel.excludeUnsupported
                    text: i18n("Exclude unsupported effects")

                    onToggled: searchModel.excludeUnsupported = checked
                }

                QQC2.MenuItem {
                    checkable: true
                    checked: searchModel.excludeInternal
                    text: i18n("Exclude internal effects")

                    onToggled: searchModel.excludeInternal = checked
                }
            }
        }
    }

    view: ListView {
        id: effectsList

        property var _buttonGroups: []

        clip: true

        model: Private.EffectsFilterProxyModel {
            id: searchModel

            query: searchField.text
            sourceModel: kcm.effectsModel
        }

        delegate: Effect {
            width: effectsList.width
        }

        section.property: "CategoryRole"
        section.delegate: Kirigami.ListSectionHeader {
            width: effectsList.width
            text: section
        }

        function findButtonGroup(name) {
            for (let item of effectsList._buttonGroups) {
                if (item.name == name) {
                    return item.group;
                }
            }

            let group = Qt.createQmlObject(
                'import QtQuick 2.5;' +
                'import QtQuick.Controls 2.5;' +
                'ButtonGroup {}',
                effectsList,
                "dynamicButtonGroup" + effectsList._buttonGroups.length
            );

            effectsList._buttonGroups.push({ name, group });

            return group;
        }
    }

    footer: ColumnLayout {
        RowLayout {
            Layout.alignment: Qt.AlignRight

            NewStuff.Button {
                text: i18n("Get New Desktop Effects...")
                visible: KAuthorized.authorize(KAuthorized.GHNS)
                configFile: "kwineffect.knsrc"
                onEntryEvent: function (entry, event) {
                    if (event === NewStuff.Engine.StatusChangedEvent) {
                        kcm.onGHNSEntriesChanged()
                    }
                }
            }
        }
    }
}

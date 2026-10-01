// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QListWidget;
class QPushButton;
class QComboBox;
class QDoubleSpinBox;

namespace app {
class Application;
}

namespace ui {

class PresetsPanel : public QWidget {
    Q_OBJECT
public:
    explicit PresetsPanel(app::Application &app, QWidget *parent = nullptr);

private:
    void reload();
    int selected() const;
    void saveNew();
    void updateSelected();
    void renameSelected();
    void deleteSelected();
    void updateButtons();

    app::Application &m_app;
    QListWidget *m_list;
    QPushButton *m_apply, *m_save, *m_update, *m_rename, *m_delete, *m_up, *m_down;
    QComboBox *m_shortcut;
    QDoubleSpinBox *m_transition;
};

} // namespace ui

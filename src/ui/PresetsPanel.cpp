// SPDX-License-Identifier: GPL-3.0-or-later
#include "PresetsPanel.h"

#include "app/Application.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {

namespace {

QString describe(const app::Preset &p)
{
    QStringList parts;
    if (p.hasFraming)
        parts << QObject::tr("framing %1×").arg(p.framing.zoom, 0, 'f', 2);
    if (p.hasColor)
        parts << QObject::tr("color");
    if (p.hasCamera)
        parts << QObject::tr("camera");
    if (p.hasOutput)
        parts << QStringLiteral("%1×%2@%3").arg(p.outputWidth).arg(p.outputHeight).arg(p.outputFps);
    if (p.hasEffects)
        parts << QObject::tr("effects");
    return parts.join(QStringLiteral(", "));
}

} // namespace

PresetsPanel::PresetsPanel(app::Application &app, QWidget *parent) : QWidget(parent), m_app(app)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    m_list = new QListWidget;
    m_list->setMinimumHeight(120);
    m_list->setToolTip(tr("Double-click a preset to apply it"));
    v->addWidget(m_list);

    auto *grid = new QGridLayout;
    m_apply = new QPushButton(tr("Apply"));
    m_save = new QPushButton(tr("Save current…"));
    m_update = new QPushButton(tr("Update"));
    m_update->setToolTip(tr("Overwrite the selected preset with the current settings"));
    m_rename = new QPushButton(tr("Rename…"));
    m_delete = new QPushButton(tr("Delete"));
    m_up = new QPushButton(QStringLiteral("▲"));
    m_down = new QPushButton(QStringLiteral("▼"));
    m_up->setToolTip(tr("Move up"));
    m_down->setToolTip(tr("Move down"));
    grid->addWidget(m_apply, 0, 0);
    grid->addWidget(m_save, 0, 1);
    grid->addWidget(m_update, 0, 2);
    grid->addWidget(m_rename, 1, 0);
    grid->addWidget(m_delete, 1, 1);
    auto *updown = new QHBoxLayout;
    updown->addWidget(m_up);
    updown->addWidget(m_down);
    grid->addLayout(updown, 1, 2);
    v->addLayout(grid);

    auto *form = new QFormLayout;
    m_shortcut = new QComboBox;
    m_shortcut->addItem(tr("None"), 0);
    for (int i = 1; i <= 9; ++i)
        m_shortcut->addItem(QStringLiteral("Ctrl+%1").arg(i), i);
    form->addRow(tr("Shortcut:"), m_shortcut);
    m_transition = new QDoubleSpinBox;
    m_transition->setRange(0, 3);
    m_transition->setSingleStep(0.05);
    m_transition->setDecimals(2);
    m_transition->setSuffix(tr(" s"));
    m_transition->setValue(m_app.settings().presetTransitionMs() / 1000.0);
    m_transition->setToolTip(tr("Duration of the animated move between framing presets"));
    form->addRow(tr("Transition:"), m_transition);
    v->addLayout(form);

    connect(m_list, &QListWidget::currentRowChanged, this, &PresetsPanel::updateButtons);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { m_app.applyPresetIndex(selected()); });
    connect(m_apply, &QPushButton::clicked, this, [this] { m_app.applyPresetIndex(selected()); });
    connect(m_save, &QPushButton::clicked, this, &PresetsPanel::saveNew);
    connect(m_update, &QPushButton::clicked, this, &PresetsPanel::updateSelected);
    connect(m_rename, &QPushButton::clicked, this, &PresetsPanel::renameSelected);
    connect(m_delete, &QPushButton::clicked, this, &PresetsPanel::deleteSelected);
    connect(m_up, &QPushButton::clicked, this, [this] {
        int i = selected();
        m_app.presets().move(i, i - 1);
        m_list->setCurrentRow(i - 1);
    });
    connect(m_down, &QPushButton::clicked, this, [this] {
        int i = selected();
        m_app.presets().move(i, i + 1);
        m_list->setCurrentRow(i + 1);
    });
    connect(m_shortcut, qOverload<int>(&QComboBox::activated), this, [this](int idx) {
        int i = selected();
        if (i >= 0)
            m_app.presets().setShortcut(i, m_shortcut->itemData(idx).toInt());
    });
    connect(m_transition, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double s) { m_app.settings().setPresetTransitionMs(int(s * 1000)); });
    connect(&m_app.presets(), &app::PresetStore::changed, this, &PresetsPanel::reload);
    connect(&m_app, &app::Application::presetApplied, this, &PresetsPanel::reload);
    reload();
}

int PresetsPanel::selected() const { return m_list->currentRow(); }

void PresetsPanel::reload()
{
    const int keep = qMax(0, m_list->currentRow());
    QSignalBlocker b(m_list);
    m_list->clear();
    const auto &list = m_app.presets().presets();
    for (const auto &p : list) {
        QString text = p.name;
        if (p.shortcut)
            text += QStringLiteral("    Ctrl+%1").arg(p.shortcut);
        auto *item = new QListWidgetItem(text);
        item->setToolTip(describe(p));
        if (p.name == m_app.currentPresetName()) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
        m_list->addItem(item);
    }
    if (!list.isEmpty())
        m_list->setCurrentRow(qMin(keep, int(list.size()) - 1));
    updateButtons();
}

void PresetsPanel::updateButtons()
{
    const int i = selected();
    const int n = m_app.presets().presets().size();
    const bool has = i >= 0 && i < n;
    m_apply->setEnabled(has);
    m_update->setEnabled(has);
    m_rename->setEnabled(has);
    m_delete->setEnabled(has);
    m_up->setEnabled(has && i > 0);
    m_down->setEnabled(has && i < n - 1);
    m_shortcut->setEnabled(has);
    QSignalBlocker b(m_shortcut);
    m_shortcut->setCurrentIndex(has ? m_app.presets().presets()[i].shortcut : 0);
}

void PresetsPanel::saveNew()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Save preset"));
    auto *form = new QFormLayout(&dlg);
    auto *name = new QLineEdit(m_app.presets().uniqueName(tr("My preset")));
    name->selectAll();
    form->addRow(tr("Name:"), name);
    auto *framing = new QCheckBox(tr("Framing (zoom, pan, crop, rotation)"));
    auto *color = new QCheckBox(tr("Color adjustments"));
    auto *camera = new QCheckBox(tr("Camera hardware controls"));
    auto *output = new QCheckBox(tr("Output resolution and frame rate"));
    auto *effects = new QCheckBox(tr("Background effects"));
    framing->setChecked(true);
    color->setChecked(true);
    camera->setChecked(true);
    output->setChecked(true);
    effects->setChecked(false);
    form->addRow(tr("Include:"), framing);
    for (auto *c : {color, camera, output, effects})
        form->addRow(QString(), c);
    auto *key = new QComboBox;
    key->addItem(tr("None"), 0);
    for (int i = 1; i <= 9; ++i) {
        int owner = m_app.presets().indexForShortcut(i);
        key->addItem(owner >= 0 ? tr("Ctrl+%1 (replaces “%2”)").arg(i).arg(m_app.presets().presets()[owner].name)
                                : QStringLiteral("Ctrl+%1").arg(i),
                     i);
    }
    form->addRow(tr("Shortcut:"), key);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;

    app::Preset p = m_app.controller().capturePreset(name->text().trimmed(), framing->isChecked(),
                                                     color->isChecked(), camera->isChecked(), output->isChecked(),
                                                     effects->isChecked());
    const int shortcut = key->currentData().toInt();
    p.shortcut = 0;
    m_app.presets().add(p);
    const int idx = m_app.presets().presets().size() - 1;
    if (shortcut)
        m_app.presets().setShortcut(idx, shortcut);
    m_list->setCurrentRow(idx);
}

void PresetsPanel::updateSelected()
{
    const int i = selected();
    if (i < 0)
        return;
    const app::Preset old = m_app.presets().presets()[i];
    if (QMessageBox::question(this, tr("Update preset"),
                              tr("Replace “%1” with the current settings?").arg(old.name)) != QMessageBox::Yes)
        return;
    app::Preset p = m_app.controller().capturePreset(old.name, old.hasFraming, old.hasColor, old.hasCamera,
                                                     old.hasOutput, old.hasEffects);
    p.shortcut = old.shortcut;
    m_app.presets().replace(i, p);
}

void PresetsPanel::renameSelected()
{
    const int i = selected();
    if (i < 0)
        return;
    bool ok = false;
    const QString old = m_app.presets().presets()[i].name;
    QString name = QInputDialog::getText(this, tr("Rename preset"), tr("New name:"), QLineEdit::Normal, old, &ok);
    if (!ok || name.trimmed().isEmpty() || name.trimmed() == old)
        return;
    if (!m_app.presets().rename(i, name))
        QMessageBox::warning(this, tr("Rename preset"), tr("A preset named “%1” already exists.").arg(name.trimmed()));
}

void PresetsPanel::deleteSelected()
{
    const int i = selected();
    if (i < 0)
        return;
    const QString name = m_app.presets().presets()[i].name;
    if (QMessageBox::question(this, tr("Delete preset"), tr("Delete “%1”?").arg(name)) == QMessageBox::Yes)
        m_app.presets().remove(i);
}

} // namespace ui

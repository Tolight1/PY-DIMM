#pragma once

#include "AppConfig.h"

#include <QDialog>

class QEvent;
class QObject;

class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const AppConfig &config, QWidget *parent = nullptr);

    AppConfig currentConfig() const;
    bool isConfigAccepted() const { return acceptedConfig_; }

public slots:
    void apply();
    void accept() override;
    void reject() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void configApplied(AppConfig config);

private:
    AppConfig readWidgets() const;
    void populateWidgets(const AppConfig &config);
    void writeSettings(const AppConfig &config) const;
    void installSpinWheelFilters();
    void rebuildProcessingTab();

    AppConfig config_;
    AppConfig originalConfig_;
    bool acceptedConfig_ = false;
};

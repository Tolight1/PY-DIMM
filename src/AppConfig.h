#pragma once

#include "ProcessingTypes.h"

#include <QMetaType>
#include <QStringList>

class QSettings;

struct AppConfig {
    OpticalConfig optical;
    AcquisitionConfig acquisition;
    ProcessingConfig processing;
    TriggerConfig trigger;
    StorageConfig storage;
    UiConfig ui;

    static AppConfig defaults();
    QStringList validate() const;
    void load(QSettings &settings);
    void save(QSettings &settings) const;
};

Q_DECLARE_METATYPE(AppConfig)

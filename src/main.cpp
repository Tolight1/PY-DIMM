#include "AppConfig.h"
#include "CameraTypes.h"
#include "MainWindow.h"
#include "ProcessingTypes.h"
#include "UiTheme.h"

#include <QApplication>
#include <QMetaType>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("KY"));
    app.setApplicationName(QStringLiteral("KY-DIMM"));

    // Register the custom types used by queued cross-thread signals and
    // QMetaObject::invokeMethod arguments.
    qRegisterMetaType<AppConfig>("AppConfig");
    qRegisterMetaType<RoiRect>("RoiRect");
    qRegisterMetaType<CameraCapabilities>("CameraCapabilities");
    qRegisterMetaType<CameraStatistics>("CameraStatistics");
    qRegisterMetaType<DisplayOverlay>("DisplayOverlay");
    qRegisterMetaType<MeasurementResult>("MeasurementResult");
    qRegisterMetaType<AoiEvent>("AoiEvent");
    qRegisterMetaType<AoiTransitionSample>("AoiTransitionSample");

    UiTheme::apply(app);

    MainWindow window;
    window.show();
    return app.exec();
}

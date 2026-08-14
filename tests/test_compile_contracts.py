from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(name: str) -> str:
    return (ROOT / "src" / name).read_text(encoding="utf-8")


def test_pylon_symbols_use_sdk_types_and_namespace():
    source = read_source("PylonCamera.cpp")

    assert "Pylon::PylonInitialize()" in source
    assert "Pylon::PylonTerminate()" in source
    assert "const CDeviceInfo &target = *chosen;" in source
    assert "const CDeviceInfo &deviceInfo = impl_->camera_.GetDeviceInfo();" in source
    assert "const IDeviceInfo &target" not in source


def test_qt_forms_are_initialized_through_generated_ui_objects():
    settings = read_source("SettingsDialog.cpp")
    main_window = read_source("MainWindow.cpp")

    assert "Ui::SettingsDialog ui;" in settings
    assert "ui.setupUi(this);" in settings
    assert "Ui::MainWindow ui;" in main_window
    assert "ui.setupUi(this);" in main_window
    assert "\n    setupUi(this);" not in settings
    assert "\n    setupUi(this);" not in main_window


def test_settings_dialog_declares_dialog_button_overrides_and_field_name_matches_config():
    header = read_source("SettingsDialog.h")
    source = read_source("SettingsDialog.cpp")

    assert "void accept() override;" in header
    assert "void reject() override;" in header
    assert "softwareTriggerEachFrame" in source
    assert "config.trigger.triggerSoftwareEachFrame" not in source
    assert "t.triggerSoftwareEachFrame" not in source


def test_camera_lifecycle_is_separate_from_acquisition_lifecycle():
    main_header = read_source("MainWindow.h")
    main_source = read_source("MainWindow.cpp")
    worker_header = read_source("CameraWorker.h")
    worker_source = read_source("CameraWorker.cpp")
    pylon_header = read_source("PylonCamera.h")

    start_body = worker_source.split("void CameraWorker::start()")[1].split(
        "void CameraWorker::stop()"
    )[0]
    apply_aoi_body = worker_source.split(
        "void CameraWorker::applyHardwareAoi"
    )[1].split("void CameraWorker::requestFullFrame")[0]

    assert "void onConnectClicked();" in main_header
    assert "void onDisconnectClicked();" in main_header
    assert "connectCameraButton_" in main_header
    assert "disconnectCameraButton_" in main_header
    assert "connectCamera" in worker_header
    assert "disconnectCamera" in worker_header
    assert "configureAcquisitionRate" in pylon_header
    assert "startAcquisitionButton_->setEnabled(cameraReady_ && !running)" in main_source
    assert "settingsButton_->setEnabled(true)" in main_source
    assert "openFirstCompatibleCamera" not in start_body
    assert "connectCamera();" not in start_body
    assert "configureExposure(" in start_body
    assert "configureAcquisitionRate(" in start_body
    assert "configureAcquisitionRate(config_.acquisition.measurementRateHz" in apply_aoi_body


def test_settings_dialog_groups_processing_parameters_and_supports_spin_wheel():
    header = read_source("SettingsDialog.h")
    source = read_source("SettingsDialog.cpp")

    assert "bool eventFilter(QObject *watched, QEvent *event) override;" in header
    assert "installSpinWheelFilters()" in source


def test_settings_dialog_installs_wheel_filter_on_spinbox_editor():
    source = read_source("SettingsDialog.cpp")
    assert "spin->findChild<QLineEdit *>()" in source
    assert "lineEdit->installEventFilter(this)" in source
    assert "QAbstractSpinBox *spin = qobject_cast<QAbstractSpinBox *>(watched)" in source
    assert "qobject_cast<QAbstractSpinBox *>(parent)" in source


def test_settings_dialog_blocks_wheel_value_changes():
    source = read_source("SettingsDialog.cpp")
    assert "spin->stepBy(steps)" not in source
    assert "if (spin && event->type() == QEvent::Wheel)" in source
    assert "event->accept();" in source
    assert "rebuildProcessingTab()" in source
    assert "processingGrid->addWidget(group, row / 2, row % 2, 1, 1);" in source
    assert "processingMinimumCentroidIntensitySpin" in source
    assert "processingTauMaximumLagMsSpin" in source


def test_measurement_worker_emits_ui_status_heartbeat_without_forcing_csv_writes():
    main_header = read_source("MainWindow.h")
    main_source = read_source("MainWindow.cpp")
    worker_header = read_source("MeasurementWorker.h")
    worker_source = read_source("MeasurementWorker.cpp")

    assert "void onMeasurementStatusReady(MeasurementResult result);" in main_header
    assert "measurementStatusReady(MeasurementResult result);" in worker_header
    assert "&MeasurementWorker::measurementStatusReady" in main_source
    assert "emit measurementStatusReady(result);" in worker_source

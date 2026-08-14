#pragma once
#include "ui_UI_New.h"
#include <QMainWindow>

class UI_New : public QMainWindow {
    Q_OBJECT
    
public:
    UI_New(QWidget* parent = nullptr);
    ~UI_New();

private:
    Ui_UI_New* ui;
};
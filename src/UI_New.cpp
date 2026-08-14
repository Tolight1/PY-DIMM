#include "UI_New.h"

UI_New::UI_New(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui_UI_New)
{
    ui->setupUi(this);
}

UI_New::~UI_New()
{
    delete ui; 
}
#ifndef OPTIONDIALOG_H
#define OPTIONDIALOG_H

#include <QDialog>

namespace Ui {
class OptionDialog;
}

class OptionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionDialog(QWidget *parent = nullptr);
    ~OptionDialog();

    void setInitialData(const QString &name, int r, int g, int b, bool visible);

    QString getName() const;
    int getR() const;
    int getG() const;
    int getB() const;
    bool getIsVisible() const;

private:
    Ui::OptionDialog *ui;
};

#endif // OPTIONDIALOG_H

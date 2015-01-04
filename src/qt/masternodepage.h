#ifndef MASTERNODEPAGE_H
#define MASTERNODEPAGE_H

#include <QWidget>

namespace Ui {
class MasternodePage;
}

class WalletModel;

class MasternodePage : public QWidget
{
    Q_OBJECT

public:
    explicit MasternodePage(QWidget *parent = 0);
    ~MasternodePage();

    void setModel(WalletModel *model);

private:
    Ui::MasternodePage *ui;

    WalletModel *model;

private slots:

    void updateOutputs(int count);
};

#endif // MASTERNODEPAGE_H

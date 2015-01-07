#ifndef MASTERNODEPAGE_H
#define MASTERNODEPAGE_H

#include <QWidget>
#include <QCheckBox>
#include "main.h"
#undef loop

namespace Ui {
class MasternodePage;
}

class WalletModel;
class CKeyID;
class COutPoint;

class MasternodeCheckbox : public QCheckBox
{
    Q_OBJECT

    CKeyID keyid;
    COutPoint outpoint;

public:
    MasternodeCheckbox(CKeyID keyid, const COutPoint& outpoint);

private slots:
    void updateState(int);

signals:
    void switchMasternode(const CKeyID& keyid, const COutPoint& outpoint, bool state);
};

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
    void switchMasternode(const CKeyID &keyid, const COutPoint &outpoint, bool state);
};

#endif // MASTERNODEPAGE_H

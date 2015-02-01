#ifndef MASTERNODEPAGE_H
#define MASTERNODEPAGE_H

#include <QWidget>
#include <QCheckBox>
#ifndef Q_MOC_RUN // https://stackoverflow.com/questions/15455178/qt4-cgal-parse-error-at-boost-join
#include "main.h"
#endif
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

    enum EColumn
    {
        C_ADDRESS,
        C_AMOUNT,
        C_ELECTED,
        C_NEXT_PAYMENT,
        C_SCORE,
        C_VOTES,
        C_CONTROL,
        C_OUTPUT,

        C_COUNT,
    };


public:
    explicit MasternodePage(QWidget *parent = 0);
    ~MasternodePage();

    void setModel(WalletModel *model);

protected:
    void showEvent(QShowEvent *);

private:
    Ui::MasternodePage *ui;
    WalletModel *model;
    EColumn sortedBy;
    Qt::SortOrder sortOrder;

private slots:
    void updateMasternodes();
    void switchMasternode(const CKeyID &keyid, const COutPoint &outpoint, bool state);
    void headerClicked(int section);
};

#endif // MASTERNODEPAGE_H

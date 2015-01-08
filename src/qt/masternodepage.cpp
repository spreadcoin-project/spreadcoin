#include <map>
#include <QCheckBox>
#include "masternodepage.h"
#include "ui_masternodepage.h"
#include "walletmodel.h"
#include "util.h"
#include "init.h"
#include "masternodes.h"
#include "bitcoinunits.h"

enum EColumn
{
    C_ADDRESS,
    C_AMOUNT,
    C_SCORE,
    C_CONTROL,
    C_OUTPUT,

    C_COUNT,
};

MasternodeCheckbox::MasternodeCheckbox(CKeyID keyid_, const COutPoint &outpoint_)
    : keyid(keyid_), outpoint(outpoint_)
{
    connect(this, SIGNAL(stateChanged(int)), this, SLOT(updateState(int)));
}

void MasternodeCheckbox::updateState(int newState)
{
    emit switchMasternode(keyid, outpoint, newState == Qt::Checked);
}

MasternodePage::MasternodePage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MasternodePage),
    model(NULL)
{
    ui->setupUi(this);

    ui->tableWidget->setColumnCount((int)C_COUNT);

    ui->tableWidget->setHorizontalHeaderItem((int)C_ADDRESS, new QTableWidgetItem(tr("Address")));
    ui->tableWidget->setHorizontalHeaderItem((int)C_AMOUNT,  new QTableWidgetItem(tr("Amount")));
    ui->tableWidget->setHorizontalHeaderItem((int)C_OUTPUT,  new QTableWidgetItem(tr("Output")));
    ui->tableWidget->setHorizontalHeaderItem((int)C_CONTROL, new QTableWidgetItem(tr("Control")));
    ui->tableWidget->setHorizontalHeaderItem((int)C_SCORE,   new QTableWidgetItem(tr("Score")));
}

MasternodePage::~MasternodePage()
{
    delete ui;
}

void MasternodePage::setModel(WalletModel *model)
{
    this->model = model;

 //   connect(model, SIGNAL(numTransactionsChanged(int)), this, SLOT(updateOutputs(count)));
 //   updateOutputs(0);
}

void MasternodePage::showEvent(QShowEvent *)
{
    updateMasternodes();
}

void MasternodePage::updateMasternodes()
{
    if (IsInitialBlockDownload())
        return;

    std::map<QString, std::vector<COutput> > mapCoins;
    model->listCoins(mapCoins);

    BOOST_FOREACH(PAIRTYPE(QString, std::vector<COutput>) coins, mapCoins)
    {
        BOOST_FOREACH(const COutput& out, coins.second)
        {
            COutPoint outpoint(out.tx->GetHash(), out.i);
            MN_SetMy(outpoint, true);
        }
    }

    QTableWidget* pTable = ui->tableWidget;
    pTable->setRowCount(0);

    std::vector<CMasternodeInfo> vmninfo = MN_GetInfoAll();
    for (const CMasternodeInfo& info : vmninfo)
    {
        CBitcoinAddress address;
        address.Set(info.keyid);

        int iRow = pTable->rowCount();
        pTable->setRowCount(iRow + 1);
        pTable->setItem(iRow, (int)C_ADDRESS, new QTableWidgetItem(address.ToString().c_str()));
    //    pTable->setItem(iRow, (int)C_AMOUNT, new QTableWidgetItem(BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, out.tx->vout[out.i].nValue)));
        pTable->setItem(iRow, (int)C_OUTPUT, new QTableWidgetItem(QString("%1:%2").arg(info.outpoint.hash.ToString().c_str()).arg(info.outpoint.n)));
        pTable->setItem(iRow, (int)C_SCORE, new QTableWidgetItem(QString("%1").arg(info.score)));

        if (info.my)
        {
            MasternodeCheckbox* pButton = new MasternodeCheckbox(info.keyid, info.outpoint);
            pButton->setChecked(info.running);
            connect(pButton, SIGNAL(switchMasternode(const CKeyID&, const COutPoint&, bool)), this, SLOT(switchMasternode(const CKeyID&, const COutPoint&, bool)));
            pTable->setCellWidget(iRow, (int)C_CONTROL, pButton);
        }
    }

}

void MasternodePage::switchMasternode(const CKeyID &keyid, const COutPoint &outpoint, bool state)
{
     if (!state)
        MN_Stop(outpoint);
     else
     {
         CKey key;
         WalletModel::UnlockContext context(model->requestUnlock());
         pwalletMain->GetKey(keyid, key);
         MN_Start(outpoint, key);
     }
}

#include <map>
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
    C_OUTPUT,

    C_COUNT,
};

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
}

MasternodePage::~MasternodePage()
{
    delete ui;
}

void MasternodePage::setModel(WalletModel *model)
{
    this->model = model;

    connect(model, SIGNAL(numTransactionsChanged(int)), this, SLOT(updateOutputs(count)));

    updateOutputs(0);
}

void MasternodePage::updateOutputs(int count)
{
    std::map<QString, std::vector<COutput> > mapCoins;
    model->listCoins(mapCoins);

    QTableWidget* pTable = ui->tableWidget;

    pTable->setRowCount(0);

    BOOST_FOREACH(PAIRTYPE(QString, std::vector<COutput>) coins, mapCoins)
    {
        BOOST_FOREACH(const COutput& out, coins.second)
        {
            int64_t nAmount = out.tx->vout[out.i].nValue;
            if (nAmount < g_MinMasternodeAmount)
                continue;

            int iRow = pTable->rowCount();
            pTable->setRowCount(iRow + 1);
            pTable->setItem(iRow, (int)C_ADDRESS, new QTableWidgetItem(coins.first));
            pTable->setItem(iRow, (int)C_AMOUNT, new QTableWidgetItem(BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, nAmount)));
            pTable->setItem(iRow, (int)C_OUTPUT, new QTableWidgetItem(QString("%1:%2").arg(out.tx->GetHash().ToString().c_str()).arg(out.i)));
        }
    }
}

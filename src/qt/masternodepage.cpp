#include <map>
#include <QCheckBox>
#include "masternodepage.h"
#include "ui_masternodepage.h"
#ifndef Q_MOC_RUN // https://stackoverflow.com/questions/15455178/qt4-cgal-parse-error-at-boost-join
#include "walletmodel.h"
#include "util.h"
#include "init.h"
#include "masternodes.h"
#include "bitcoinunits.h"
#endif

static bool compareMNsByAmount(const CMasterNode* pLeft, const CMasterNode* pRight)
{
    if (pLeft->amount == pRight->amount)
        return pLeft->GetScore() < pRight->GetScore();

    return pLeft->amount < pRight->amount;
}

static bool compareMNsByScore(const CMasterNode* pLeft, const CMasterNode* pRight)
{
    if (pLeft->GetScore() == pRight->GetScore())
        return pLeft->amount < pRight->amount;

    return pLeft->GetScore() < pRight->GetScore();
}

static bool compareMNsByAddress(const CMasterNode* pLeft, const CMasterNode* pRight)
{
    CBitcoinAddress address0, address1;
    address0.Set(pLeft->keyid);
    address1.Set(pRight->keyid);
    std::string a = address0.ToString();
    std::string b = address1.ToString();

    if (a == b)
        return pLeft->outpoint < pRight->outpoint;

    return a < b;
}

static bool compareMNsByOutpoint(const CMasterNode* pLeft, const CMasterNode* pRight)
{
    return pLeft->outpoint < pRight->outpoint;
}

static bool compareMNsByNextPayment(const CMasterNode* pLeft, const CMasterNode* pRight)
{
    if (pLeft->nextPayment == pRight->nextPayment)
        return pLeft->amount > pRight->amount;

    return pLeft->nextPayment < pRight->nextPayment;
}

static bool compareMNsByElectionStatus(const CMasterNode* pLeft, const CMasterNode* pRight)
{
    if (pLeft->elected == pRight->elected)
        return pLeft->amount > pRight->amount;

    return (int)pLeft->elected > (int)pRight->elected;
}

static bool compareMNsByVotes(const CMasterNode* pLeft, const CMasterNode* pRight)
{
    int a = pLeft->votes[!pLeft->elected];
    int b = pRight->votes[!pRight->elected];
    if (a == b)
        return pLeft->amount < pRight->amount;

    return a < b;
}

static bool compareMNsByControl(const CMasterNode* pLeft, const CMasterNode* pRight)
{
    int a = pLeft->my*2 + pLeft->IsRunning();
    int b = pRight->my*2 + pRight->IsRunning();
    if (a == b)
        return pLeft->amount < pRight->amount;

    return a < b;
}

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
    model(NULL),
    sortedBy(C_AMOUNT),
    sortOrder(Qt::DescendingOrder)
{
    ui->setupUi(this);

    ui->tableWidget->setColumnCount((int)C_COUNT);

    ui->tableWidget->setHorizontalHeaderItem((int)C_ADDRESS, new QTableWidgetItem(tr("Address")));
    ui->tableWidget->setHorizontalHeaderItem((int)C_AMOUNT,  new QTableWidgetItem(tr("Amount")));
    ui->tableWidget->setHorizontalHeaderItem((int)C_ELECTED, new QTableWidgetItem(tr("Elected")));
    ui->tableWidget->setHorizontalHeaderItem((int)C_VOTES,   new QTableWidgetItem(tr("Votes")));
    ui->tableWidget->setHorizontalHeaderItem((int)C_OUTPUT,  new QTableWidgetItem(tr("Output")));
    ui->tableWidget->setHorizontalHeaderItem((int)C_CONTROL, new QTableWidgetItem(tr("Control")));
    ui->tableWidget->setHorizontalHeaderItem((int)C_SCORE,   new QTableWidgetItem(tr("Score")));
    ui->tableWidget->setHorizontalHeaderItem((int)C_NEXT_PAYMENT, new QTableWidgetItem(tr("Next Payment")));

    connect(ui->updateButton, SIGNAL(pressed()), this, SLOT(updateMasternodes()));
    connect(ui->tableWidget->horizontalHeader(), SIGNAL(sectionClicked(int)), this, SLOT(headerClicked(int)));
}

MasternodePage::~MasternodePage()
{
    delete ui;
}

void MasternodePage::setModel(WalletModel *model)
{
    this->model = model;
}

void MasternodePage::showEvent(QShowEvent *)
{
    updateMasternodes();
}

void MasternodePage::updateMasternodes()
{
    LOCK(cs_main);

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

    std::vector<const CMasterNode*> masternodes;
    MN_GetAll(masternodes);

    bool (*compare)(const CMasterNode* pLeft, const CMasterNode* pRight) = nullptr;
    switch (sortedBy)
    {
    case C_AMOUNT:       compare = compareMNsByAmount;         break;
    case C_OUTPUT:       compare = compareMNsByOutpoint;       break;
    case C_SCORE:        compare = compareMNsByScore;          break;
    case C_ADDRESS:      compare = compareMNsByAddress;        break;
    case C_NEXT_PAYMENT: compare = compareMNsByNextPayment;    break;
    case C_ELECTED:      compare = compareMNsByElectionStatus; break;
    case C_VOTES:        compare = compareMNsByVotes;          break;
    case C_CONTROL:      compare = compareMNsByControl;        break;
    case C_COUNT:        break;
    }
    if (compare)
        std::sort(masternodes.begin(), masternodes.end(), compare);

    if (sortOrder == Qt::DescendingOrder)
        std::reverse(masternodes.begin(), masternodes.end());

    QTableWidget* pTable = ui->tableWidget;
    pTable->setRowCount(0);

    for (const CMasterNode* pmn : masternodes)
    {
        const CMasterNode& mn = *pmn;

        bool elected = mn.elected;
        int votes = mn.votes[!mn.elected];

        CBitcoinAddress address;
        address.Set(mn.keyid);

        int iRow = pTable->rowCount();
        pTable->setRowCount(iRow + 1);
        pTable->setItem(iRow, (int)C_ADDRESS, new QTableWidgetItem(address.ToString().c_str()));
        pTable->setItem(iRow, (int)C_AMOUNT, new QTableWidgetItem(BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, mn.amount)));
        pTable->setItem(iRow, (int)C_OUTPUT, new QTableWidgetItem(QString("%1:%2").arg(mn.outpoint.hash.ToString().c_str()).arg(mn.outpoint.n)));
        pTable->setItem(iRow, (int)C_SCORE, new QTableWidgetItem(QString("%1").arg(mn.GetScore())));
        pTable->setItem(iRow, (int)C_ELECTED, new QTableWidgetItem(QString("%1").arg(elected? tr("yes") : "")));
        pTable->setItem(iRow, (int)C_VOTES, new QTableWidgetItem(QString("%1%").arg(votes*100.0/g_MasternodesElectionPeriod)));

        if (mn.nextPayment < 2*g_MaxMasternodes)
            pTable->setItem(iRow, (int)C_NEXT_PAYMENT, new QTableWidgetItem(QString("%1 min").arg(mn.nextPayment)));
        else
            pTable->setItem(iRow, (int)C_NEXT_PAYMENT, new QTableWidgetItem(QString("")));

        if (mn.my)
        {
            MasternodeCheckbox* pButton = new MasternodeCheckbox(mn.keyid, mn.outpoint);
            pButton->setChecked(mn.IsRunning());
            connect(pButton, SIGNAL(switchMasternode(const CKeyID&, const COutPoint&, bool)), this, SLOT(switchMasternode(const CKeyID&, const COutPoint&, bool)));

            QWidget *pWidget = new QWidget();
            QHBoxLayout *pLayout = new QHBoxLayout(pWidget);
            pLayout->addWidget(pButton);
            pLayout->setAlignment(Qt::AlignCenter);
            pLayout->setContentsMargins(0,0,0,0);
            pWidget->setLayout(pLayout);

            pTable->setCellWidget(iRow, (int)C_CONTROL, pWidget);
        }
    }
}

void MasternodePage::switchMasternode(const CKeyID &keyid, const COutPoint &outpoint, bool state)
{
    LOCK(cs_main);

    if (!state)
        MN_Stop(outpoint);
    else
    {
        CKey key;
        WalletModel::UnlockContext context(model->requestUnlock());
        {
            LOCK(pwalletMain->cs_wallet);
            pwalletMain->GetKey(keyid, key);
        }
        MN_Start(outpoint, CMasterNodeSecret(key));
    }
}

void MasternodePage::headerClicked(int section)
{
    EColumn column = (EColumn)section;
    if (column == sortedBy)
        sortOrder = (Qt::SortOrder)!(bool)sortOrder;
    else
    {
        sortOrder = Qt::AscendingOrder;
        sortedBy = column;
    }
    updateMasternodes();
}

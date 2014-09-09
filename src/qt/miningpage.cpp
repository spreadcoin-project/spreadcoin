#include "miningpage.h"
#include "ui_miningpage.h"
#include "main.h"
#include "util.h"
#include "bitcoinrpc.h"

#include <boost/thread.hpp>
#include <stdio.h>

extern json_spirit::Value GetNetworkHashPS(int lookup, int height);

MiningPage::MiningPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MiningPage)
{
    ui->setupUi(this);

    int nThreads = boost::thread::hardware_concurrency();

    int nUseThreads = GetArg("-genproclimit", -1);
    if (nUseThreads < 0)
         nUseThreads = nThreads;

    ui->sliderCores->setMinimum(1);
    ui->sliderCores->setMaximum(nThreads);
    ui->sliderCores->setValue(nUseThreads);

    connect(ui->sliderCores, SIGNAL(valueChanged(int)), this, SLOT(changeNumberOfCores(int)));
    connect(ui->pushSwitchMining, SIGNAL(clicked()), this, SLOT(switchMining()));

    startTimer(500);
}

MiningPage::~MiningPage()
{
    delete ui;
}

void MiningPage::restartMining(bool fGenerate)
{
    int nThreads = ui->sliderCores->value();

    json_spirit::Array Args;
    Args.push_back(fGenerate);
    Args.push_back(nThreads);
    setgenerate(Args, false);

    ui->labelNCores->setText(QString("%1").arg(nThreads));
    ui->pushSwitchMining->setText(fGenerate? tr("Stop mining") : tr("Start mining"));
}

void MiningPage::changeNumberOfCores(int i)
{
    restartMining(GetBoolArg("-gen"));
}

void MiningPage::switchMining()
{
    restartMining(!GetBoolArg("-gen"));
}

static QString formatTimeInterval(CBigNum t)
{
    enum  EUnit { YEAR, MONTH, DAY, HOUR, MINUTE, SECOND, NUM_UNITS };

    const int SecondsPerUnit[NUM_UNITS] =
    {
        31556952, // average number of seconds in gregorian year
        31556952/12, // average number of seconds in gregorian month
        24*60*60, // number of seconds in a day
        24*60, // number of seconds in an hour
        60, // number of seconds in a minute
        1
    };

    const char* UnitNames[NUM_UNITS] =
    {
        "year",
        "month",
        "day",
        "hour",
        "minute",
        "second"
    };

    if (t > 0xFFFFFFFF)
    {
        t /= SecondsPerUnit[YEAR];
        return QString("%1 years").arg(t.ToString(10).c_str());
    }
    else
    {
        unsigned int t32 = t.getuint();

        int Values[NUM_UNITS];
        for (int i = 0; i < NUM_UNITS; i++)
        {
            Values[i] = t32/SecondsPerUnit[i];
            t32 %= SecondsPerUnit[i];
        }

        int FirstNonZero = 0;
        while (FirstNonZero < NUM_UNITS && Values[FirstNonZero] == 0)
            FirstNonZero++;

        QString TimeStr;
        for (int i = FirstNonZero; i < std::min(FirstNonZero + 3, (int)NUM_UNITS); i++)
        {
            int Value = Values[i];
            TimeStr += QString("%1 %2%3 ").arg(Value).arg(UnitNames[i]).arg((Value == 1)? "" : "s"); // FIXME: this is English specific
        }
        return TimeStr;
    }
}

void MiningPage::timerEvent(QTimerEvent *)
{
    int64 NetworkHashrate = GetNetworkHashPS(120, -1).get_int64();
    int64 Hashrate = GetBoolArg("-gen")? gethashespersec(json_spirit::Array(), false).get_int64() : 0;

    QString NextBlockTime;
    if (Hashrate == 0)
        NextBlockTime = QChar(L'∞');
    else
    {
        CBigNum Target;
        Target.SetCompact(pindexBest->nBits);
        CBigNum ExpectedTime = (CBigNum(1) << 256)/(Target*Hashrate);
        NextBlockTime = formatTimeInterval(ExpectedTime);
    }

    ui->labelNethashrate->setText(QString("%1 H/s").arg(NetworkHashrate));
    ui->labelYourHashrate->setText(QString("%1 H/s").arg(Hashrate));
    ui->labelNextBlock->setText(NextBlockTime);
}

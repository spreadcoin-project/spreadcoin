#ifndef MININGPAGE_H
#define MININGPAGE_H

#include <QWidget>

namespace Ui {
class MiningPage;
}

class MiningPage : public QWidget
{
    Q_OBJECT

public:
    explicit MiningPage(QWidget *parent = 0);
    ~MiningPage();

private:
    Ui::MiningPage *ui;

    void restartMining(bool fGenerate);
    void timerEvent(QTimerEvent *event);

private slots:

    void changeNumberOfCores(int i);
    void switchMining();
};

#endif // MININGPAGE_H

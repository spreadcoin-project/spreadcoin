#include <QDateTime>
#include <QKeyEvent>
#include <QMessageBox>
#include <initializer_list>
#include <set>
#include "blockexplorer.h"
#include "ui_blockexplorer.h"
#include "main.h"
#include "util.h"
#include "ui_interface.h"
#include "bitcoinunits.h"
#include "clientmodel.h"

extern double GetDifficulty(const CBlockIndex* blockindex = NULL);

typedef std::vector<std::string> StringList;

inline std::string utostr(unsigned int n)
{
    return strprintf("%u", n);
}

static std::string makeHRef(const std::string& Str)
{
    return "<a href=\"" + Str + "\">" + Str + "</a>";
}

static CTxOut getPrevOut(const CTxIn& In)
{
    CTransaction tx;
    uint256 hashBlock = 0;
    if (GetTransaction(In.prevout.hash, tx, hashBlock, true))
        return tx.vout[In.prevout.n];
    return CTxOut();
}

static int64_t getTxIn(const CTransaction& tx)
{
    if (tx.IsCoinBase())
        return 0;

    int64_t Sum = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        Sum += getPrevOut(tx.vin[i]).nValue;
    return Sum;
}

static std::string ValueToString(int64 nValue)
{
    if (nValue < 0)
        return _("unknown");

    QString Str = BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, nValue);
    return Str.toUtf8().data();
}

static std::string ScriptToString(const CScript& Script, bool Long = false, bool Highlight = false)
{
    if (Script.empty())
        return "unknown";

    CTxDestination Dest;
    CBitcoinAddress Address;
    if (ExtractDestination(Script, Dest) && Address.Set(Dest))
    {
        if (Highlight)
            return "<span class=\"mono\"><b><font color=\"green\">" + Address.ToString() + "</font></b></span>";
        else
            return makeHRef(Address.ToString());
    }
    else
        return Long? "<pre>" + Script.ToString() + "</pre>" : _("Non-standard script");
}

static std::string TimeToString(uint64_t Time)
{
    QDateTime timestamp;
    timestamp.setTime_t(Time);
    return timestamp.toString("yyyy-MM-dd hh:mm:ss").toUtf8().data();
}

static std::string makeHTMLTableRow(const StringList& Strings)
{
    std::string Result = "<tr>";
    for (const std::string& Str : Strings)
    {
        Result += "<td>";
        Result += Str;
        Result += "</td>";
    }
    Result += "</tr>";
    return Result;
}

static const char* table = "<table border=1 cellpadding=3>";

static std::string makeHTMLTable(const StringList* pRows, int Num)
{
    std::string Table = table;
    for (int i = 0; i < Num; i++)
        Table += makeHTMLTableRow(pRows[i]);
    Table += "</table>";
    return Table;
}

static std::string makeHTMLTable(std::initializer_list<StringList> Rows)
{
    return makeHTMLTable(Rows.begin(), Rows.end() - Rows.begin());
}

static std::string TxToRow(const CTransaction& tx, const CScript& Highlight = CScript(), const StringList& Prepend = StringList(), int64_t* pSum = nullptr)
{
    std::string InAmounts, InAddresses, OutAmounts, OutAddresses;
    int64_t Delta = 0;
    for (unsigned int j = 0; j < tx.vin.size(); j++)
    {
        if (tx.IsCoinBase())
        {
            InAmounts += ValueToString(tx.GetValueOut());
            InAddresses += "coinbase";
        }
        else
        {
            CTxOut PrevOut = getPrevOut(tx.vin[j]);
            InAmounts += ValueToString(PrevOut.nValue);
            InAddresses += ScriptToString(PrevOut.scriptPubKey, false, PrevOut.scriptPubKey == Highlight).c_str();
            if (PrevOut.scriptPubKey == Highlight)
                Delta -= PrevOut.nValue;
        }
        if (j + 1 != tx.vin.size())
        {
            InAmounts += "<br/>";
            InAddresses += "<br/>";
        }
    }
    for (unsigned int j = 0; j < tx.vout.size(); j++)
    {
        CTxOut Out = tx.vout[j];
        OutAmounts += ValueToString(Out.nValue);
        OutAddresses += ScriptToString(Out.scriptPubKey, false, Out.scriptPubKey == Highlight);
        if (Out.scriptPubKey == Highlight)
            Delta += Out.nValue;
        if (j + 1 != tx.vout.size())
        {
            OutAmounts += "<br/>";
            OutAddresses += "<br/>";
        }
    }

    StringList List = Prepend;
    List.push_back(makeHRef(tx.GetHash().GetHex()));
    List.push_back(InAddresses);
    List.push_back(InAmounts);
    List.push_back(OutAddresses);
    List.push_back(OutAmounts);

    if (!Highlight.empty())
    {
        List.push_back(std::string("<font color=\"") + ((Delta > 0)? "green" : "red") + "\">" + ValueToString(Delta) + "</font>"); // Spread-FIXME: negative value
        *pSum += Delta;
        List.push_back(ValueToString(*pSum));
    }
    return makeHTMLTableRow(List);
}

std::string BlockToString(CBlockIndex* pBlock)
{
    if (!pBlock)
        return "";

    CBlock block;
    block.ReadFromDisk(pBlock);

    int64_t Fees = 0;
    int64_t OutVolume = 0;
    int64_t Reward = 0;

    StringList TxLabels({_("Hash"), _("From"), _("Amount"), _("To"), _("Amount")});

    std::string TxContent = table + makeHTMLTableRow(TxLabels);
    for (const CTransaction& tx : block.vtx)
    {
        TxContent += TxToRow(tx);

        int64_t In = getTxIn(tx);
        int64_t Out = tx.GetValueOut();
        if (tx.IsCoinBase())
            Reward += Out;
        else if (In < 0)
            Fees = -MAX_MONEY;
        else
        {
            Fees += In - Out;
            OutVolume += Out;
        }
    }
    TxContent += "</table>";

    std::string BlockContent = makeHTMLTable(
    {
        {_("Height"),      itostr(pBlock->nHeight)},
        {_("Size"),        itostr(GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION))},
        {_("Number of Transactions"), itostr(block.vtx.size())},
        {_("Value Out"),   ValueToString(OutVolume)},
        {_("Fees"),        ValueToString(Fees)},
        {_("Generated"),   ValueToString(Reward - Fees)},
        {_("Timestamp"),   TimeToString(block.nTime)},
        {_("Difficulty"),  strprintf("%.4f", GetDifficulty(pBlock))},
        {_("Bits"),        utostr(block.nBits)},
        {_("Nonce"),       utostr(block.nNonce)},
        {_("Version"),     itostr(block.nVersion)},
        {_("Hash"),        "<pre>" + block.GetHash().GetHex() + "</pre>"},
        {_("Merkle Root"), "<pre>" + block.hashMerkleRoot.GetHex() + "</pre>"}
    });

    std::string Content;
    Content += "<h1><a href=";
    Content += itostr(pBlock->nHeight - 1);
    Content += " style=\"text-decoration: none\">◄&nbsp;</a>";
    Content += _("Block");
    Content += " ";
    Content += itostr(pBlock->nHeight);
    Content += "<a href=";
    Content += itostr(pBlock->nHeight + 1);
    Content += " style=\"text-decoration: none\">&nbsp;►</a></h1>";
    Content += BlockContent;
    Content += "</br>";
    Content += "<h2>" + _("Transactions") + "</h2>";
    Content += TxContent;

    return Content;
}

std::string TxToString(uint256 BlockHash, const CTransaction& tx)
{
    int64_t Input = 0;
    int64_t Output = tx.GetValueOut();

    std::string InputsContent  = makeHTMLTableRow({_("#"), _("Taken from"),  _("Address"), _("Amount")});
    std::string OutputsContent = makeHTMLTableRow({_("#"), /*tr("Redeemed in"),*/ _("Address"), _("Amount")});

    if (tx.IsCoinBase())
    {
        InputsContent += makeHTMLTableRow(
        {
            "0",
            "coinbase",
            "-",
            ValueToString(Output)
        });
    }
    else for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        COutPoint Out = tx.vin[i].prevout;
        CTxOut PrevOut = getPrevOut(tx.vin[i]);
        if (PrevOut.nValue < 0)
            Input = -MAX_MONEY;
        else
            Input += PrevOut.nValue;
        InputsContent += makeHTMLTableRow(
        {
            itostr(i),
            "<span class=\"mono\">" + makeHRef(Out.hash.GetHex()) + ":" + itostr(Out.n) + "</span>",
            ScriptToString(PrevOut.scriptPubKey, true),
            ValueToString(PrevOut.nValue)
        });
    }

    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& Out = tx.vout[i];
        OutputsContent += makeHTMLTableRow(
        {
            itostr(i),
            // "", // Redeemed in
            ScriptToString(Out.scriptPubKey, true),
            ValueToString(Out.nValue)
        });
    }

    InputsContent  = table + InputsContent  + "</table>";
    OutputsContent = table + OutputsContent + "</table>";

    StringList Labels[7] =
    {
        {_("In Block"),  ""},
        {_("Size"),      itostr(GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION))},
        {_("Input"),     tx.IsCoinBase()? "-" : ValueToString(Input)},
        {_("Output"),    ValueToString(Output)},
        {_("Fees"),      tx.IsCoinBase()? "-" : ValueToString(Input - Output)},
        {_("Timestamp"), ""},
        {_("Hash"),      "<pre>" + tx.GetHash().GetHex() + "</pre>"},
    };

    auto iter = mapBlockIndex.find(BlockHash);
    if (iter != mapBlockIndex.end())
    {
        CBlockIndex* pIndex = iter->second;
        Labels[0][1] = makeHRef(itostr(pIndex->nHeight));
        Labels[5][1] = TimeToString(pIndex->nTime);
    }

    std::string Content;
    Content += "<h1>" + _("Transaction") + "&nbsp;<span class=\"mono\">" + Labels[6][1] + "</span></h1>";
    Content += makeHTMLTable(Labels, sizeof(Labels)/sizeof(StringList));
    Content += "</br>";
    Content += "<h2>" + _("Inputs") + "</h2>";
    Content += InputsContent;
    Content += "</br>";
    Content += "<h2>" + _("Outputs") + "</h2>";
    Content += OutputsContent;

    return Content;
}

std::string AddressToString(const CBitcoinAddress& Address)
{
    StringList TxLabels
    {
        _("Date"),
        _("Hash"),
        _("From"),
        _("Amount"),
        _("To"),
        _("Amount"),
        _("Delta"),
        _("Balance")
    };
    std::string TxContent = table + makeHTMLTableRow(TxLabels);

    std::set<COutPoint> PrevOuts;

    CScript AddressScript;
    AddressScript.SetDestination(Address.Get());

    int64_t Sum = 0;

    for (CBlockIndex* pindex = pindexGenesisBlock; pindex; pindex = pindex->pnext)
    {
        CBlock block;
        block.ReadFromDisk(pindex);
        StringList Prepend;
        Prepend.push_back("<a href=\"" + itostr(block.nHeight) + "\">" + TimeToString(block.nTime) + "</a>");
        for(const CTransaction& tx : block.vtx)
        {
            for (const CTxIn& In : tx.vin)
            {
                auto iter = PrevOuts.find(In.prevout);
                if (iter != PrevOuts.end())
                {
                    TxContent += TxToRow(tx, AddressScript, Prepend, &Sum);
                    PrevOuts.erase(iter);
                    goto next;
                }
            }
            for (unsigned int i = 0; i < tx.vout.size(); i++)
            {
                const CTxOut& Out = tx.vout[i];
                if (Out.scriptPubKey == AddressScript)
                {
                    TxContent += TxToRow(tx, AddressScript, Prepend, &Sum);
                    PrevOuts.insert(COutPoint(tx.GetHash(), i));
                    break;
                }
            }
        }
        next:;
    }
    TxContent += "</table>";

    std::string Content;
    Content += "<h1>" + _("Transactions to/from") + "&nbsp;<span class=\"mono\">" + Address.ToString() + "</span></h1>";
    Content += TxContent;
    return Content;
}

BlockExplorer::BlockExplorer(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::BlockExplorer),
    m_NeverShown(true),
    m_HistoryIndex(0)
{
    ui->setupUi(this);

    connect(ui->pushSearch, SIGNAL(released()), this, SLOT(onSearch()));
    connect(ui->content, SIGNAL(linkActivated(const QString&)), this, SLOT(goTo(const QString&)));
    connect(ui->back, SIGNAL(released()), this, SLOT(back()));
    connect(ui->forward, SIGNAL(released()), this, SLOT(forward()));
}

BlockExplorer::~BlockExplorer()
{
    delete ui;
}

void BlockExplorer::keyPressEvent(QKeyEvent *event)
{
    switch ((Qt::Key)event->key())
    {
    case Qt::Key_Enter:
    case Qt::Key_Return:
        onSearch();
        return;

    default:
        return QMainWindow::keyPressEvent(event);
    }
}

void BlockExplorer::showEvent(QShowEvent*)
{
    if (m_NeverShown)
    {
        m_NeverShown = false;

        setBlock(pindexBest);
        QString text = QString("%1").arg(pindexBest->nHeight);
        ui->searchBox->setText(text);
        m_History.push_back(text);
        updateNavButtons();

        if (!GetBoolArg("-txindex", false))
        {
            QString Warning = tr("Not all transactions will be shown. To view all transactions you need to set txindex=1 in the config file (spreadcoin.conf) and rebuild transaction index with -reindex argument.");
            QMessageBox::warning(this, "SpreadCoin Blockchain Explorer", Warning, QMessageBox::Ok);
        }
    }
}

bool BlockExplorer::switchTo(const QString& query)
{
    bool IsOk;
    int AsInt = query.toInt(&IsOk);
    if (IsOk && AsInt >= 0 && AsInt <= nBestHeight)
    {
        CBlockIndex* pIndex = FindBlockByHeight(AsInt);
        if (pIndex)
        {
            setBlock(pIndex);
            return true;
        }
    }

    uint256 hash(query.toUtf8().constData());

    auto iter = mapBlockIndex.find(hash);
    if (iter != mapBlockIndex.end())
    {
        setBlock(iter->second);
        return true;
    }

    CTransaction tx;
    uint256 hashBlock = 0;
    if (GetTransaction(hash, tx, hashBlock, true))
    {
        setContent(TxToString(hashBlock, tx));
        return true;
    }

    CBitcoinAddress Address;
    Address.SetString(query.toUtf8().constData());
    if (Address.IsValid())
    {
        setContent(AddressToString(Address));
        return true;
    }

    return false;
}

void BlockExplorer::goTo(const QString& query)
{
    if (switchTo(query))
    {
        ui->searchBox->setText(query);
        while (m_History.size() > m_HistoryIndex + 1)
            m_History.pop_back();
        m_History.push_back(query);
        m_HistoryIndex = m_History.size() - 1;
        updateNavButtons();
    }
}

void BlockExplorer::onSearch()
{
    goTo(ui->searchBox->text());
}

void BlockExplorer::setBlock(CBlockIndex* pBlock)
{
    setContent(BlockToString(pBlock));
}

void BlockExplorer::setContent(const std::string& Content)
{
    QString CSS = "a, .mono { font-family: \"monospace\" }\n h1, h2 { white-space:nowrap; }";
    QString FullContent = "<html><head><style type=\"text/css\">" + CSS + "</style></head>" + "<body>" + Content.c_str() + "</body></html>";
    ui->content->setText(FullContent);
}

void BlockExplorer::back()
{
    int NewIndex = m_HistoryIndex - 1;
    if (0 <= NewIndex && NewIndex < m_History.size())
    {
         m_HistoryIndex = NewIndex;
         ui->searchBox->setText(m_History[NewIndex]);
         switchTo(m_History[NewIndex]);
         updateNavButtons();
    }
}

void BlockExplorer::forward()
{
    int NewIndex = m_HistoryIndex + 1;
    if (0 <= NewIndex && NewIndex < m_History.size())
    {
         m_HistoryIndex = NewIndex;
         ui->searchBox->setText(m_History[NewIndex]);
         switchTo(m_History[NewIndex]);
         updateNavButtons();
    }
}

void BlockExplorer::updateNavButtons()
{
    ui->back->setEnabled(m_HistoryIndex - 1 >= 0);
    ui->forward->setEnabled(m_HistoryIndex + 1 < m_History.size());
}

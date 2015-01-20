#include "elected_masternodes.h"

static const unsigned int g_MasternodesStartPayments = 6;
static const unsigned int g_MasternodesStopPayments = 3;

bool CElectedMasternodes::NextPayee(const COutPoint& PrevPayee, CCoinsViewCache *pCoins, CKeyID &keyid, COutPoint& outpoint)
{
    if (PrevPayee.IsNull())
    {
        // Not enough masternodes to start payments
        if (masternodes.size() < g_MasternodesStartPayments)
            return false;

        // Start payments form the beginning
        uint64_t amount;
        outpoint = *masternodes.begin();
        return getKeyIDAndAmount(outpoint, keyid, amount, pCoins);
    }
    else
    {
        // Stop payments if there are not enough masternodes
        if (masternodes.size() < g_MasternodesStopPayments)
            return false;

        auto iter = masternodes.upper_bound(PrevPayee);

        // Rewind
        if (iter == masternodes.end())
            iter = masternodes.begin();

        uint64_t amount;
        outpoint = *iter;
        return getKeyIDAndAmount(outpoint, keyid, amount, pCoins);
    }
}

CKeyID CElectedMasternodes::FillBlock(CBlockIndex* pindex, CCoinsViewCache &Coins)
{
    if (pindex->nHeight <= (int)getThirdHardforkBlock())
        return CKeyID(0);

    pindex->velected[0].clear();
    pindex->velected[1].clear();

    COutPoint payeeOutpoint;
    CKeyID payee = NextPayee(pindex->pprev->mn, &Coins, payeeOutpoint);

    for (COutPoint outpoint : masternodes)
    {
        if (!isAcceptableMasternodeInput(outpoint, Coins))
        {
            pindex->velected[0].push_back(outpoint);
        }
    }

    boost::unordered_map<COutPoint, int> vvotes[2];
    MN_GetVotes(pindex, vvotes);

    for (int j = 0; j < 2; j++)
    {
        for (const std::pair<COutPoint, int>& pair : vvotes[j])
        {
            if (pair.second > g_MasternodesElectionPeriod/2)
            {
                COutPoint outpoint = pair.first;

                bool present = masternodes.find(outpoint) != 0;
                if (j == present)
                    continue;

                if (j && masternodes.size() == g_MaxMasternodes)
                    continue;
                if (j && !isAcceptableMasternodeInput(outpoint, Coins))
                    continue;

                if (std::find(pindex->velected[j].begin(), pindex->velected[j].end(), outpoint) == pindex->velected[j]->end())
                    continue;

                pindex->velected[j].push_back(outpoint);
            }
        }
    }

    if (!payee)
        return CKeyID(0);

    pindex->mn = payeeOutpoint;
    return payee;
}

void CElectedMasternodes::ApplyBlock(CBlockIndex* pindex, bool connect)
{
    for (const COutPoint& outpoint : pindex->velected[connect])
    {
        assert(masternodes.insert(outpoint).second);
    }

    for (const COutPoint& outpoint : pindex->velected[!connect])
    {
        assert(masternodes.erase(outpoint) != 0);
    }
}

void CElectedMasternodes::ApplyMany(CBlockIndex* pindex)
{
    for (; pindex; pindex = pindex->pnext)
    {
        ApplyBlock(pindex, true);
    }
}

bool CElectedMasternodes::IsElected(const COutPoint& outpoint)
{
    return masternodes.count(outpoint) != 0;
}

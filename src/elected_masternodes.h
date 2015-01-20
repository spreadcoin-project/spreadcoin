#pragma once

#include "main.h"

class CElectedMasternodes
{
public:
    std::set<COutPoint> masternodes;

    // Executed on connecting new block
    CKeyID FillBlock(CBlockIndex* pindex, CCoinsViewCache &Coins); // returns expected payee

    bool NextPayee(const COutPoint& PrevPayee, CCoinsViewCache *pCoins, CKeyID& keyid, COutPoint& outpoint);

    bool IsElected(const COutPoint& outpoint);

    void ApplyBlock(CBlockIndex* pindex, bool connect);
    void ApplyMany(CBlockIndex* pindex);
};

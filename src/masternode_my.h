#pragma once

#include "main.h"

class CMasterNodeSecret
{
public:
    CKey privkey;
    CSignature signature;

    CMasterNodeSecret()
    {}

    CMasterNodeSecret(CKey privkey);
};

// Control our masternodes
bool MN_SetMy(const COutPoint& outpoint, bool my);
bool MN_Start(const COutPoint& outpoint, const CMasterNodeSecret &secret);
bool MN_Stop(const COutPoint& outpoint);
void MN_MyRemoveOutdated();

// Process network events
void MN_MyProcessBlock(const CBlockIndex* pBlock);
void MN_MyProcessTx(const CTransaction& tx, int64_t nFees);

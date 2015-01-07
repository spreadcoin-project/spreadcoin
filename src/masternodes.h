#ifndef MASTERNODES_H
#define MASTERNODES_H

#include "main.h"

static const int64_t g_MinMasternodeAmount = 1000*COIN;

// Wrapper for masternode messages
class CMasterNodeBaseMsg
{
public:
    // Masternode indentifier
    COutPoint outpoint;

    // Signed with oupoint key.
    CSignature signature;

    virtual uint256 GetHashForSignature() const = 0;
};

// Message broadcasted by masternode to confirm that it is running
class CMasterNodeExistenceMsg : public CMasterNodeBaseMsg
{
public:
    // prove that this message is recent
    int32_t nBlock;
    uint256 hashBlock;

    uint256 GetHashForSignature() const;
    uint256 GetHash() const;

    IMPLEMENT_SERIALIZE
    (
        READWRITE(outpoint);
        READWRITE(signature);
        READWRITE(nBlock);
        READWRITE(hashBlock);
    )
};

void MN_Start(const COutPoint& outpoint, const CKey& key);
void MN_Stop(const COutPoint& outpoint);
CKeyID MN_GetMasternodeKeyID(const COutPoint& outpoint);
void MN_ProcessBlocks();
void MN_ProcessInstantTx(const CTransaction& tx);
void MN_ProcessExistenceMsg(CNode* pfrom, const CMasterNodeExistenceMsg& mnem);

#endif // MASTERNODES_H

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

    // Pubkey message is signed with oupoint key;
    // All other messages are signed with key2.
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

class CMasterNode
{
    struct CReceivedExistenceMsg
    {
        CMasterNodeExistenceMsg msg;
        int64_t nReceiveTime;
    };

    // Messages which confirm that this masternode still exists
    std::vector<CReceivedExistenceMsg> existenceMsgs;

public:

    // Masternode indentifier
    COutPoint outpoint;

    // Only for our masternodes
    CKey privkey;

    uint256 GetHash();

    void GetExistenceBlocks(std::vector<int>& v) const;

    double GetScore() const;

    CMasterNode();

    int AddExistenceMsg(const CMasterNodeExistenceMsg& msg);
    void Cleanup();
};

CMasterNode& getMasterNode(const COutPoint& outpoint);
bool IsAcceptableMasternodeOutpoint(const COutPoint& outpoint);
void MN_ProcessBlocks();
void MN_ProcessInstantTx(const CTransaction& tx);
void MN_ProcessExistenceMsg(CNode* pfrom, const CMasterNodeExistenceMsg& mnem);

#endif // MASTERNODES_H

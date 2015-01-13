#ifndef MASTERNODES_H
#define MASTERNODES_H

#include <boost/unordered_map.hpp>
#include "main.h"

static const int64_t g_MinMasternodeAmount = 1000*COIN;
static const int g_MaxMasternodeVotes = 10;
static const int g_MasternodesElectionPeriod = 200;

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

class CMasterNode
{
    struct CReceivedExistenceMsg
    {
        CMasterNodeExistenceMsg msg;
        int64_t nReceiveTime; // used to mesaure time between block and this message
    };

    // Messages which confirm that this masternode still exists
    std::vector<CReceivedExistenceMsg> existenceMsgs;

    // Should be executed from time to time to remove unnecessary information
    void Cleanup();

    bool misbehaving = false;

public:

    // Masternode indentifier
    COutPoint outpoint;

    CKeyID keyid;

    bool my = false;

    // Only for our masternodes
    CKey privkey;

    // Score based on how fast this node broadcasts its responces.
    // Miners use this value to elect new masternodes or remove old ones.
    // Lower values are better.
    double GetScore() const;

    // Which blocks should be signed by this masternode to prove that it is running
    std::vector<int> GetExistenceBlocks() const;

    // Returns misbehave value or -1 if everything is ok.
    int AddExistenceMsg(const CMasterNodeExistenceMsg& msg);
};

// All these variables and functions require locking cs_main.

extern boost::unordered_map<COutPoint, CMasterNode> g_MasterNodes;

// Control our masternodes
bool MN_SetMy(const COutPoint& outpoint, bool my);
bool MN_Start(const COutPoint& outpoint, const CKey& key);
bool MN_Stop(const COutPoint& outpoint);

void MN_ProcessBlocks();
void MN_ProcessInstantTx(const CTransaction& tx);
void MN_ProcessExistenceMsg(CNode* pfrom, const CMasterNodeExistenceMsg& mnem);

bool MN_Elect(const COutPoint& outpoint, bool elect);

#endif // MASTERNODES_H

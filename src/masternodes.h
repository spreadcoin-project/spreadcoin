#ifndef MASTERNODES_H
#define MASTERNODES_H

#include <boost/unordered_map.hpp>
#include "main.h"

static const int64_t g_MinMasternodeAmount = 1000*COIN;
static const int g_MaxMasternodeVotes = 10;
static const int g_MasternodesElectionPeriod = 200;
static const int g_MasternodeRewardPercentage = 30;

// Base for masternode messages
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

    mutable double score;
    mutable int lastScoreUpdate = 0;

    void UpdateScore() const;

public:

    COutPoint outpoint;
    CKeyID    keyid;
    uint64_t  amount;

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

// All known masternodes, this may include outdated and stopped masternodes as well as not yet elected.
extern boost::unordered_map<COutPoint, CMasterNode> g_MasterNodes;

// Control our masternodes
bool MN_SetMy(const COutPoint& outpoint, bool my);
bool MN_Start(const COutPoint& outpoint, const CKey& key);
bool MN_Stop(const COutPoint& outpoint);

// Process network events
void MN_ProcessBlocks();
void MN_ProcessInstantTx(const CTransaction& tx);
void MN_ProcessExistenceMsg(CNode* pfrom, const CMasterNodeExistenceMsg& mnem);

// Functions necessary for mining
CMasterNode* MN_NextPayee(const COutPoint& PrevPayee);
void MN_CastVotes(std::vector<COutPoint> vvotes[2]);

// Executed on connecting/disconnectig blocks
// These functions add or remove nodes to the set of elected masternodes
CKeyID MN_OnConnectBlock(CBlockIndex* pindex); // returns expected payee
void MN_OnDisconnectBlock(CBlockIndex* pindex);

// Initialize elected masternodes after loading blockchain
void MN_LoadElections();

inline int64_t MN_GetReward(int64_t BlockValue)
{
    return BlockValue*g_MasternodeRewardPercentage/100;
}

// Info
bool MN_IsElected(const COutPoint& outpoint);
void MN_GetVotes(CBlockIndex* pindex, boost::unordered_map<COutPoint, int> vvotes[2]);

#endif // MASTERNODES_H

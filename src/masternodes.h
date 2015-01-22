#ifndef MASTERNODES_H
#define MASTERNODES_H

#include <boost/unordered_map.hpp>
#include "main.h"
#include "masternodes_elected.h"
#include "masternode_messages.h"
#include "masternode_my.h"

static const int64_t g_MinMasternodeAmount = 1000*COIN;
static const int g_MaxMasternodeVotes = 10;
static const int g_MasternodesElectionPeriod = 50;
static const int g_MasternodeRewardPercentage = 30;
static const int g_MaxMasternodes = 1500;

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
    CMasterNodeSecret secret;

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

CMasterNode* MN_Get(const COutPoint& outpoint);

bool MN_IsAcceptableMasternodeInput(const COutPoint& outpoint, CCoinsViewCache *pCoins);
bool MN_GetKeyIDAndAmount(const COutPoint& outpoint, CKeyID& keyid, uint64_t& amount, CCoinsViewCache* pCoins, bool AllowUnconfirmed = false);

void MN_Cleanup();

// Process network events
void MN_ProcessBlocks();
void MN_ProcessExistenceMsg(CNode* pfrom, const CMasterNodeExistenceMsg& mnem);

// Functions necessary for mining
void MN_CastVotes(std::vector<COutPoint> vvotes[2], CCoinsViewCache& coins);


inline int64_t MN_GetReward(int64_t BlockValue)
{
    return BlockValue*g_MasternodeRewardPercentage/100;
}

// Info
void MN_GetVotes(CBlockIndex* pindex, boost::unordered_map<COutPoint, int> vvotes[2]);

#endif // MASTERNODES_H

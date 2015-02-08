#ifndef MASTERNODES_H
#define MASTERNODES_H

#include <boost/unordered_map.hpp>
#include "main.h"
#include "masternodes_elected.h"
#include "masternode_messages.h"
#include "masternode_my.h"

static const int64_t g_MinMasternodeAmount = 100*COIN;
static const int g_MaxMasternodeVotes = 10;
static const int g_MasternodesElectionPeriod = 60;
static const int g_MasternodeRewardPercentage = 30;
static const int g_MaxMasternodes = 1440;

// Instant transactions
static const int g_MaxInstantTxInputs = 15;
static const int g_InstantTxFeePerInput = COIN/1000;
static const int g_InstantTxPeriod = 100;
static const int g_InstantTxMaxConfirmations = 10;
static const int g_InstantTxMinConfirmations = 7;
static const int g_InstantTxInterval = g_InstantTxPeriod*g_InstantTxMaxConfirmations;

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

    bool misbehaving;

    mutable double score;
    mutable int lastScoreUpdate;

    void UpdateScore() const;

public:
    COutPoint outpoint;
    CKeyID    keyid;
    uint64_t  amount;

    bool my;

    // Only for our masternodes
    CMasterNodeSecret secret;

    CMasterNode();

    // Score based on how fast this node broadcasts its responces.
    // Miners use this value to elect new masternodes or remove old ones.
    // Lower values are better.
    double GetScore() const;

    // Which blocks should be signed by this masternode to prove that it is running
    std::vector<int> GetExistenceBlocks() const;

    // Data for display/output info
    int nextPayment;
    int votes[2];
    bool elected;

    bool AddExistenceMsg(const CMasterNodeExistenceMsg& msg, CValidationState &state);

    bool IsRunning() const
    {
        return secret.privkey.IsValid();
    }
};

// All these variables and functions require locking cs_main.

// All known masternodes, this may include outdated and stopped masternodes as well as not yet elected.
extern boost::unordered_map<COutPoint, CMasterNode> g_MasterNodes;

CMasterNode* MN_Get(const COutPoint& outpoint);
void MN_GetAll(std::vector<const CMasterNode*>& masternodes);

bool MN_IsAcceptableMasternodeInput(const COutPoint& outpoint, CCoinsViewCache *pCoins);
bool MN_GetKeyIDAndAmount(const COutPoint& outpoint, CKeyID& keyid, uint64_t& amount, CCoinsViewCache* pCoins, bool AllowUnconfirmed = false);

void MN_Cleanup();

// Process network events
void MN_ProcessBlocks();
void MN_ProcessExistenceMsg(CNode* pfrom, const CMasterNodeExistenceMsg& mnem);
void MN_ProcessInstantTxMsg(CNode* pfrom, const CMasterNodeInstantTxMsg& mnitx);

// Functions necessary for mining
void MN_CastVotes(std::vector<COutPoint> vvotes[2], CCoinsViewCache& coins);

inline int64_t MN_GetReward(int64_t BlockValue)
{
    return BlockValue*g_MasternodeRewardPercentage/100;
}

// Instant tx
bool MN_CanBeInstantTx(const CTransaction& tx, int64_t nFees);
std::vector<int> MN_GetConfirmationBlocks(COutPoint outpoint, int nBlockBegin, int nBlockEnd);
int MN_GetNumConfirms(const CTransaction& tx);
bool MN_CheckInstantTx(const CInstantTx& tx);
extern uint64_t g_LastInstantTxConfirmationHash;

// Info
void MN_GetVotes(CBlockIndex* pindex, boost::unordered_map<COutPoint, int> vvotes[2]);

#endif // MASTERNODES_H

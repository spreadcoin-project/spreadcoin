#ifndef MASTERNODES_H
#define MASTERNODES_H

#include <boost/unordered_map.hpp>
#include "main.h"
#include "elected_masternodes.h"

static const int64_t g_MinMasternodeAmount = 1000*COIN;
static const int g_MaxMasternodeVotes = 10;
static const int g_MasternodesElectionPeriod = 50;
static const int g_MasternodeRewardPercentage = 30;
static const int g_MaxMasternodes = 1500;

// Base for masternode messages
class CMasterNodePubkey2
{
public:
    CPubKey pubkey2;

    // Signed with outpoint key.
    CSignature signature;

    CKeyID GetOutpointKeyID() const
    {
        CPubKey pubkey;
        pubkey.RecoverCompact(pubkey2.GetHash(), signature);
        return pubkey.GetID();
    }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(pubkey2);
        READWRITE(signature);
    )
};

// Base for masternode messages
class CMasterNodeBaseMsg
{
public:
    CMasterNodePubkey2 pubkey2;

    // Masternode indentifier
    COutPoint outpoint;

    // Signed with pubkey2.
    CSignature signature;

    virtual uint256 GetHashForSignature() const = 0;

    bool CheckSignature() const
    {
        CPubKey pubkey;
        pubkey.RecoverCompact(GetHashForSignature(), signature);
        return pubkey == pubkey2.pubkey2;
    }

    CKeyID GetOutpointKeyID() const
    {
        return pubkey2.GetOutpointKeyID();
    }
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
        READWRITE(pubkey2);
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

extern CElectedMasternodes g_ElectedMasternodes;

bool MN_IsAcceptableMasternodeInput(const COutPoint& outpoint, CCoinsViewCache *pCoins);
bool MN_GetKeyIDAndAmount(const COutPoint& outpoint, CKeyID& keyid, uint64_t& amount, CCoinsViewCache* pCoins);

// Control our masternodes
bool MN_SetMy(const COutPoint& outpoint, bool my);
bool MN_Start(const COutPoint& outpoint, const CKey& key);
bool MN_Stop(const COutPoint& outpoint);

// Process network events
void MN_ProcessBlocks();
void MN_ProcessInstantTx(const CTransaction& tx);
void MN_ProcessExistenceMsg(CNode* pfrom, const CMasterNodeExistenceMsg& mnem);

// Functions necessary for mining
void MN_CastVotes(std::vector<COutPoint> vvotes[2], CCoinsViewCache& coins);

// Initialize elected masternodes after loading blockchain
void MN_LoadElections();

inline int64_t MN_GetReward(int64_t BlockValue)
{
    return BlockValue*g_MasternodeRewardPercentage/100;
}

// Info
void MN_GetVotes(CBlockIndex* pindex, boost::unordered_map<COutPoint, int> vvotes[2]);

#endif // MASTERNODES_H

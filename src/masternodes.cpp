#include <boost/chrono.hpp>

#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

#include "txdb.h"
#include "masternodes.h"

static const int g_MasternodeMinConfirmations = 1000;

static const int g_AnnounceExistenceRestartPeriod = 2000;
static const int g_AnnounceExistencePeriod = 500;


static const double g_PenaltyTime = 500.0;

// Blockchain length at startup after sync. We don't know anythyng about how well masternodes were behaving before this block.
static int32_t g_InitialBlock = 0;

static std::size_t hash_value(uint256 v)
{
    return (std::size_t)v.Get64(0);
}

static int64_t GetMontoneTimeMs()
{
    return boost::chrono::duration_cast<boost::chrono::milliseconds>(boost::chrono::steady_clock::now().time_since_epoch()).count();
}

class CMasterNode
{
    struct CReceivedExistenceMsg
    {
        CMasterNodeExistenceMsg msg;
        int64_t nReceiveTime; // used to mesaure time between block and this message
    };

    // Messages which confirm that this masternode still exists
    std::vector<CReceivedExistenceMsg> existenceMsgs;

public:

    // Masternode indentifier
    COutPoint outpoint;

    // Only for our masternodes
    CKey privkey;

    CMasterNode();

    // Score based on how fast this node broadcasts its responces.
    // Miners use this value to elect new masternodes or remove old ones.
    double GetScore() const;

    // Which blocks should be signed by this masternode to prove that it is running
    void GetExistenceBlocks(std::vector<int>& v) const;

    // Returns misbehave value or -1 if everything is ok.
    int AddExistenceMsg(const CMasterNodeExistenceMsg& msg);

    // Should be executed from time to time to remove unnecessary information
    void Cleanup();
};

static boost::unordered_map<uint256, CMasterNode> g_MasterNodes;
static boost::unordered_set<uint256> g_OurMasterNodes;

CMasterNode::CMasterNode()
{
}

void CMasterNode::GetExistenceBlocks(std::vector<int> &v) const
{
    v.clear();

    int nCurHeight = nBestHeight;
    int nBlock = nCurHeight/g_AnnounceExistenceRestartPeriod*g_AnnounceExistenceRestartPeriod;
    for (int i = 1; i >= 0; i--)
    {
        int nSeedBlock = nBlock - i*g_AnnounceExistenceRestartPeriod;
        CBlockIndex* pSeedBlock = FindBlockByHeight(nSeedBlock - g_AnnounceExistencePeriod);

        CHashWriter hasher(SER_GETHASH, 0);
        hasher << pSeedBlock->GetBlockHash();
        hasher << outpoint;

        int Shift = hasher.GetHash().Get64(0) % g_AnnounceExistencePeriod;
        for (int j = nSeedBlock + Shift; j < nSeedBlock + g_AnnounceExistenceRestartPeriod; j += g_AnnounceExistencePeriod)
        {
            if (j > nCurHeight - g_AnnounceExistenceRestartPeriod)
            {
                v.push_back(j);
            }
        }
    }
}

int CMasterNode::AddExistenceMsg(const CMasterNodeExistenceMsg& newMsg)
{
    std::vector<int> vblocks;
    GetExistenceBlocks(vblocks);

    // Check that this masternode should sign this block
    if (std::find(vblocks.begin(), vblocks.end(), newMsg.nBlock) == vblocks.end())
        return 20;

    uint256 hash = newMsg.GetHash();
    for (unsigned int i = 0; i < existenceMsgs.size(); i++)
    {
        if (existenceMsgs[i].msg.GetHash() == hash)
            return 0;
    }

    // FIXME: mark masternode as misbehabing
    if (existenceMsgs.size() > g_AnnounceExistenceRestartPeriod/g_AnnounceExistencePeriod*10)
        return 20;

    CReceivedExistenceMsg receivedMsg;
    receivedMsg.msg = newMsg;
    receivedMsg.nReceiveTime = GetMontoneTimeMs();
    existenceMsgs.push_back(receivedMsg);
    return -1;
}

void CMasterNode::Cleanup()
{
    std::remove_if(existenceMsgs.begin(), existenceMsgs.end(), [](const CReceivedExistenceMsg& msg)
    {
        return msg.msg.nBlock < nBestHeight - 3*g_AnnounceExistenceRestartPeriod;
    });
}

double CMasterNode::GetScore() const
{
    std::vector<int> vblocks;
    GetExistenceBlocks(vblocks);

    double score = 0.0;

    for (uint32_t i = 0; i < vblocks.size(); i++)
    {
        // FIXME: count our ignorance
        if (vblocks[i] <= g_InitialBlock)
            continue;

        CBlockIndex* pBlock = FindBlockByHeight(vblocks[i]);

        double timeDelta = g_PenaltyTime;
        for (unsigned int j = 0; j < existenceMsgs.size(); j++)
        {
            if (existenceMsgs[j].msg.nBlock == pBlock->nHeight && existenceMsgs[j].msg.hashBlock == pBlock->GetBlockHash())
            {
                if (pBlock->nReceiveTime == 0 || existenceMsgs[j].nReceiveTime < pBlock->nReceiveTime)
                {
                    timeDelta = 0;
                }
                else
                {
                    timeDelta = existenceMsgs[j].nReceiveTime - pBlock->nReceiveTime;
                }
                break;
            }
        }
        score += timeDelta;
    }

    if (vblocks.size() != 0.0)
        score /= vblocks.size();

    return score;
}

CMasterNode& getMasterNode(const COutPoint& outpoint)
{
    CMasterNode& mn = g_MasterNodes[outpoint.GetHash()];
    mn.outpoint = outpoint;
    return mn;
}

uint256 CMasterNodeExistenceMsg::GetHashForSignature() const
{
    CHashWriter hasher(SER_GETHASH, 0);
    hasher << outpoint << nBlock << hashBlock;
    return hasher.GetHash();
}

uint256 CMasterNodeExistenceMsg::GetHash() const
{
    CHashWriter hasher(SER_GETHASH, 0);
    hasher << outpoint << nBlock << hashBlock << signature;
    return hasher.GetHash();
}

void MN_ProcessBlocks()
{
    if (IsInitialBlockDownload())
        return;

    if (g_InitialBlock == 0)
        g_InitialBlock = nBestHeight;

    CBlockIndex* pBlock = pindexBest;
    while (pBlock->nReceiveTime == 0)
    {
        pBlock->nReceiveTime = GetMontoneTimeMs();

        for (uint256 hashMN : g_OurMasterNodes)
        {
            CMasterNode& mn = g_MasterNodes[hashMN];
            assert (mn.privkey.IsValid());

            std::vector<int> vblocks;
            mn.GetExistenceBlocks(vblocks);

            bool ShouldSign = false;
            for (unsigned int i = 0; i < vblocks.size(); i++)
            {
                if (vblocks[i] == pBlock->nHeight)
                {
                    ShouldSign = true;
                    break;
                }
            }

            if (!ShouldSign)
                continue;

            CMasterNodeExistenceMsg mnem;
            mnem.outpoint = mn.outpoint;
            mnem.hashBlock = pBlock->GetBlockHash();
            mnem.nBlock = pBlock->nHeight;
            mnem.signature = mn.privkey.Sign(mnem.GetHashForSignature());

            MN_ProcessExistenceMsg(nullptr, mnem);
        }
    }
}

CKeyID MN_GetMasternodeKeyID(const COutPoint& outpoint)
{
    CValidationState state;
    CTransaction tx;

    CTxIn in;
    in.prevout = outpoint;

    CTxOut testOut = CTxOut(g_MinMasternodeAmount, CScript());
    tx.vin.push_back(in);
    tx.vout.push_back(testOut);
    if (!tx.AcceptableInputs(state, true))
        return CKeyID();

    if(GetInputAge(outpoint) < g_MasternodeMinConfirmations)
        return CKeyID();

    CTxOut out = getPrevOut(outpoint);
    if (out.IsNull())
        return CKeyID();

    // Extract masternode's address from coinbase
    const CScript& OutScript = out.scriptPubKey;
    if (OutScript.size() != 22)
        return CKeyID();
    CTxDestination Dest;
    if (!ExtractDestination(OutScript, Dest))
        return CKeyID();
    CBitcoinAddress Address;
    if (!Address.Set(Dest) || !Address.IsValid())
        return CKeyID();
    CKeyID KeyID;
    if (!Address.GetKeyID(KeyID))
        return CKeyID();

    return KeyID;
}

static int MN_ProcessExistenceMsg_Impl(const CMasterNodeExistenceMsg& mnem)
{
    // Too old message, it should not be retranslated
    if (mnem.nBlock < nBestHeight - 100)
        return 20;

    // Too old message
    if (mnem.nBlock < nBestHeight- 50)
        return 0;

    CKeyID KeyID = MN_GetMasternodeKeyID(mnem.outpoint);
    if (!KeyID)
        return 20;

    // Check signature
    CPubKey pubkey;
    pubkey.RecoverCompact(mnem.GetHashForSignature(), mnem.signature);
    if (pubkey.GetID() != KeyID)
        return 100;

    printf("Masternode existence message mn=%s:%u, block=%u\n", mnem.outpoint.hash.ToString().c_str(), mnem.outpoint.n, mnem.nBlock);

    CMasterNode& mn = getMasterNode(mnem.outpoint);
    return mn.AddExistenceMsg(mnem);
}

void MN_ProcessExistenceMsg(CNode* pfrom, const CMasterNodeExistenceMsg& mnem)
{
    if (IsInitialBlockDownload())
        return;

    int misbehave = MN_ProcessExistenceMsg_Impl(mnem);
    if (misbehave > 0 && pfrom)
        pfrom->Misbehaving(misbehave);

    if (misbehave >= 0)
        return;

    uint256 mnemHash = mnem.GetHash();
    if (pfrom)
        pfrom->setKnown.insert(mnemHash);

    // Relay
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes)
        {
            // returns true if wasn't already contained in the set
            if (pnode->setKnown.insert(mnemHash).second)
            {
                pnode->PushMessage("mnexists", mnem);
            }
        }
    }
}

void MN_Start(const COutPoint& outpoint, const CKey& key)
{
    CMasterNode& mn = getMasterNode(outpoint);
    mn.privkey = key;
    g_OurMasterNodes.insert(outpoint.GetHash());
}

void MN_Stop(const COutPoint& outpoint)
{
    CMasterNode& mn = getMasterNode(outpoint);
    mn.privkey = CKey();
    g_OurMasterNodes.erase(outpoint.GetHash());
}

#include <boost/chrono.hpp>

#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

#include "txdb.h"
#include "masternodes.h"

static boost::unordered_map<uint256, CMasterNode> g_MasterNodes;
static boost::unordered_set<uint256> g_OurMasterNodes;

static const int g_MaxMasternodes = 2000;
static const int g_MasternodeMinConfirmations = 1000;
static const int g_AnnounceExistencePeriod = 500;


static const double g_PenaltyTime = 500.0;

// Blockchain length at startup
static int32_t g_InitialBlock = 0;

std::size_t hash_value(uint256 v)
{
    return (std::size_t)v.Get64(0);
}

int64_t GetMontoneTimeMs()
{
    return boost::chrono::duration_cast<boost::chrono::milliseconds>(boost::chrono::steady_clock::now().time_since_epoch()).count();
}

CMasterNode::CMasterNode()
{
}

void CMasterNode::GetExistenceBlocks(std::vector<int> &v) const
{
    v.clear();

    int nCurHeight = pindexBest->nHeight;
    int nBlock = nCurHeight/g_MaxMasternodes*g_MaxMasternodes;
    for (int i = 1; i >= 0; i--)
    {
        int nSeedBlock = nBlock - i*g_MaxMasternodes;
        CBlockIndex* pSeedBlock = FindBlockByHeight(nSeedBlock - g_AnnounceExistencePeriod);

        CHashWriter hasher(SER_GETHASH, 0);
        hasher << pSeedBlock->GetBlockHash();
        hasher << outpoint;

        int Shift = hasher.GetHash().Get64(0) % g_AnnounceExistencePeriod;
        for (int j = nSeedBlock + Shift; j < nSeedBlock + g_MaxMasternodes; j += g_AnnounceExistencePeriod)
        {
            if (j > nCurHeight - g_MaxMasternodes)
            {
                v.push_back(j);
            }
        }
    }
}

int CMasterNode::AddExistenceMsg(const CMasterNodeExistenceMsg& msg)
{
    // FIXME: mark masternode as misbehabing
    if (existenceMsgs.size() > g_MaxMasternodes/g_AnnounceExistencePeriod*10)
        return 20;

    std::vector<int> vblocks;
    GetExistenceBlocks(vblocks);

    bool bFound = false;
    for (unsigned int i = 0; i < vblocks.size(); i++)
    {
        if (vblocks[i] == msg.nBlock)
        {
            bFound = true;
            break;
        }
    }
    if (!bFound)
        return 20;

    uint256 hash = msg.GetHash();
    for (unsigned int i = 0; i < existenceMsgs.size(); i++)
    {
        if (existenceMsgs[i].msg.GetHash() == hash)
            return 0;
    }

    CReceivedExistenceMsg receivedMsg;
    receivedMsg.msg = msg;
    receivedMsg.nReceiveTime = GetMontoneTimeMs();
    existenceMsgs.push_back(receivedMsg);
    return -1;
}

void CMasterNode::Cleanup()
{
    std::remove_if(existenceMsgs.begin(), existenceMsgs.end(), [](const CReceivedExistenceMsg& msg)
    {
        return msg.msg.nBlock < pindexBest->nHeight - 3*g_MaxMasternodes;
    });
}

double CMasterNode::GetScore() const
{
    std::vector<int> vblocks;
    GetExistenceBlocks(vblocks);

    double score = 0.0;

    for (uint32_t i = 0; i < vblocks.size(); i++)
    {
        if (vblocks[i] <= g_InitialBlock)
            continue;

        CBlockIndex* pBlock = FindBlockByHeight(vblocks[i]);

        double timeDelta = g_PenaltyTime;
        for (unsigned int j = 0; j < existenceMsgs.size(); j++)
        {
            if (existenceMsgs[j].msg.nBlock == pBlock->nHeight && existenceMsgs[j].msg.hashBlock == pBlock->GetBlockHash())
            {
                if (pBlock->nReceiveTime == 0)
                {
                    timeDelta = 0;
                }
                else
                {
                    timeDelta = std::max(int64_t(0), existenceMsgs[j].nReceiveTime - pBlock->nReceiveTime);
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

void MN_ProcessBlock()
{
    if (IsInitialBlockDownload())
        return;

    if (g_InitialBlock == 0)
        g_InitialBlock = pindexBest->nHeight;

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

static int MN_ProcessExistenceMsg_Impl(const CMasterNodeExistenceMsg& mnem)
{
    if (mnem.nBlock < pindexBest->nHeight - 100)
        return 20;

    if (mnem.nBlock < pindexBest->nHeight - 50)
        return 0;

    CValidationState state;
    CTransaction tx = CTransaction();

    CTxIn in;
    in.prevout = mnem.outpoint;

    CTxOut testOut = CTxOut(g_MinMasternodeAmount, CScript());
    tx.vin.push_back(in);
    tx.vout.push_back(testOut);
    if (!tx.AcceptableInputs(state, true))
        return 20;

    if(GetInputAge(in) < g_MasternodeMinConfirmations)
        return 20;

    uint256 hashBlock;
    if (GetTransaction(mnem.outpoint.hash, tx, hashBlock, false))
        return 20;

    CTxOut out = getPrevOut(mnem.outpoint);

    // Extract masternode's address from coinbase
    const CScript& OutScript = out.scriptPubKey;
    if (OutScript.size() != 22)
        return 100;
    CTxDestination Dest;
    if (!ExtractDestination(OutScript, Dest))
        return 100;
    CBitcoinAddress Address;
    if (!Address.Set(Dest) || !Address.IsValid())
        return 100;
    CKeyID KeyID;
    if (!Address.GetKeyID(KeyID))
        return 100;

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

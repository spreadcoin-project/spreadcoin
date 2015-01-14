#include <boost/chrono.hpp>
#include <boost/unordered_set.hpp>

#include "txdb.h"
#include "masternodes.h"

static int64_t GetMontoneTimeMs()
{
    return boost::chrono::duration_cast<boost::chrono::milliseconds>(boost::chrono::steady_clock::now().time_since_epoch()).count();
}

static const int g_MasternodeMinConfirmations = 10;

static const int g_AnnounceExistenceRestartPeriod = 20;
static const int g_AnnounceExistencePeriod = 5;
static const int g_MonitoringPeriod = 100;

// If masternode doesn't respond to some message we assume that it has responded in this amount of time.
static const double g_PenaltyTime = 500.0;

// Blockchain length at startup after sync. We don't know anythyng about how well masternodes were behaving before this block.
static int32_t g_InitialBlock = 0;

boost::unordered_map<COutPoint, CMasterNode> g_MasterNodes;
static boost::unordered_set<COutPoint> g_OurMasterNodes;
static boost::unordered_set<COutPoint> g_ElectedMasternodes;

std::vector<int> CMasterNode::GetExistenceBlocks() const
{
    std::vector<int> v;

    if (nBestHeight < 4*g_AnnounceExistenceRestartPeriod)
        return v;

    int nCurHeight = nBestHeight;
    int nBlock = nCurHeight/g_AnnounceExistenceRestartPeriod*g_AnnounceExistenceRestartPeriod;
    for (int i = 1; i >= 0; i--)
    {
        int nSeedBlock = nBlock - i*g_AnnounceExistenceRestartPeriod;
        CBlockIndex* pSeedBlock = FindBlockByHeight(nSeedBlock - g_AnnounceExistencePeriod);

        CHashWriter hasher(SER_GETHASH, 0);
        hasher << pSeedBlock->GetBlockHash();
        hasher << outpoint;

        uint64_t hash = hasher.GetHash().Get64(0);
        int Shift = hash % g_AnnounceExistencePeriod;
        for (int j = nSeedBlock + Shift; j < nSeedBlock + g_AnnounceExistenceRestartPeriod; j += g_AnnounceExistencePeriod)
        {
            if (j <= nBestHeight && j > nCurHeight - g_AnnounceExistenceRestartPeriod)
            {
                v.push_back(j);
            }
        }
    }
    return v;
}

int CMasterNode::AddExistenceMsg(const CMasterNodeExistenceMsg& newMsg)
{
    uint256 hash = newMsg.GetHash();
    for (unsigned int i = 0; i < existenceMsgs.size(); i++)
    {
        if (existenceMsgs[i].msg.GetHash() == hash)
            return 0;
    }

    Cleanup();

    // Check if this masternode sends too many messages
    if (existenceMsgs.size() > g_MonitoringPeriod/g_AnnounceExistencePeriod*10)
    {
        misbehaving = true;
        return 20;
    }

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
        return msg.msg.nBlock < nBestHeight - 2*g_MonitoringPeriod;
    });
}

double CMasterNode::GetScore() const
{
    if (misbehaving)
        return 99999999999.0;

    std::vector<int> vblocks = GetExistenceBlocks();

    double score = 0.0;

    int nblocks = 0;
    for (uint32_t i = 0; i < vblocks.size(); i++)
    {
        // FIXME: count our ignorance
        if (vblocks[i] <= g_InitialBlock)
            continue;
        nblocks++;

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
                    timeDelta = (existenceMsgs[j].nReceiveTime - pBlock->nReceiveTime)*0.001;
                }
                break;
            }
        }
        score += timeDelta;
    }

    if (nblocks != 0)
        score /= nblocks;

    return score;
}

static bool getKeyIDAndAmount(const COutPoint& outpoint, CKeyID& keyid, uint64_t& amount)
{
    CValidationState state;
    CTransaction tx;

    CTxIn in;
    in.prevout = outpoint;

    CTxOut testOut = CTxOut(g_MinMasternodeAmount, CScript());
    tx.vin.push_back(in);
    tx.vout.push_back(testOut);
    if (!tx.AcceptableInputs(state, true))
        return false;

    if(GetInputAge(outpoint) < g_MasternodeMinConfirmations)
        return false;

    CTxOut out = getPrevOut(outpoint);
    if (out.IsNull())
        return false;

    // Extract masternode's address from coinbase
    const CScript& OutScript = out.scriptPubKey;
    if (OutScript.size() != 22)
        return false;
    CTxDestination Dest;
    if (!ExtractDestination(OutScript, Dest))
        return false;
    CBitcoinAddress Address;
    if (!Address.Set(Dest) || !Address.IsValid())
        return false;
    if (!Address.GetKeyID(keyid))
        return false;

    amount = out.nValue;
    return true;
}

static CMasterNode* getMasterNode(const COutPoint& outpoint)
{
    auto iter = g_MasterNodes.find(outpoint);
    if (iter != g_MasterNodes.end())
    {
        return &iter->second;
    }
    else
    {
        CKeyID keyid;
        uint64_t amount;
        if (!getKeyIDAndAmount(outpoint, keyid, amount))
            return nullptr;

        CMasterNode& mn = g_MasterNodes[outpoint];
        mn.outpoint = outpoint;
        mn.keyid = keyid;
        mn.amount = amount;
        return &mn;
    }
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

    for (CBlockIndex* pBlock = pindexBest;
         pBlock->nHeight > g_InitialBlock && pBlock->nReceiveTime == 0;
         pBlock = pBlock->pprev)
    {
        pBlock->nReceiveTime = GetMontoneTimeMs();

        for (COutPoint outpoint : g_OurMasterNodes)
        {
            CMasterNode& mn = g_MasterNodes[outpoint];
            assert (mn.privkey.IsValid());

            std::vector<int> vblocks = mn.GetExistenceBlocks();
            if (std::find(vblocks.begin(), vblocks.end(), pBlock->nHeight) == vblocks.end())
                continue;

            CMasterNodeExistenceMsg mnem;
            mnem.outpoint = mn.outpoint;
            mnem.hashBlock = pBlock->GetBlockHash();
            mnem.nBlock = pBlock->nHeight;
            mnem.signature = mn.privkey.Sign(mnem.GetHashForSignature());

            MN_ProcessExistenceMsg(NULL, mnem);
        }
    }
}

static int MN_ProcessExistenceMsg_Impl(const CMasterNodeExistenceMsg& mnem)
{
    // Too old message, it should not be retranslated
    if (mnem.nBlock < nBestHeight - 100)
        return 20;

    // Too old message
    if (mnem.nBlock < nBestHeight- 50)
        return 0;

    CMasterNode* pmn = getMasterNode(mnem.outpoint);
    if (!pmn)
        return 20;

    // Check signature
    CPubKey pubkey;
    pubkey.RecoverCompact(mnem.GetHashForSignature(), mnem.signature);
    if (pubkey.GetID() != pmn->keyid)
        return 100;

    printf("Masternode existence message mn=%s:%u, block=%u\n", mnem.outpoint.hash.ToString().c_str(), mnem.outpoint.n, mnem.nBlock);

    return pmn->AddExistenceMsg(mnem);
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

bool MN_Start(const COutPoint& outpoint, const CKey& key)
{
    CMasterNode* pmn = getMasterNode(outpoint);
    if (!pmn)
        return false;

    pmn->my = true;
    pmn->privkey = key;
    g_OurMasterNodes.insert(outpoint);
    return true;
}

bool MN_Stop(const COutPoint& outpoint)
{
    CMasterNode* pmn = getMasterNode(outpoint);
    if (!pmn)
        return false;

    pmn->privkey = CKey();
    g_OurMasterNodes.erase(outpoint);
    return true;
}

bool MN_SetMy(const COutPoint& outpoint, bool my)
{
    CMasterNode* pmn = getMasterNode(outpoint);
    if (!pmn)
        return false;

    pmn->my = my;
    return true;
}

bool MN_Elect(const COutPoint& outpoint, bool elect)
{
    if (elect)
    {
        if (!getMasterNode(outpoint))
            return false;
        return g_ElectedMasternodes.insert(outpoint).second;
    }
    else
    {
        return g_ElectedMasternodes.erase(outpoint) != 0;
    }
}

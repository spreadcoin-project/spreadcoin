#include <boost/chrono.hpp>
#include <boost/unordered_set.hpp>
#include <boost/algorithm/clamp.hpp>

#include "txdb.h"
#include "masternodes.h"

static int64_t GetMontoneTimeMs()
{
    return boost::chrono::duration_cast<boost::chrono::milliseconds>(boost::chrono::steady_clock::now().time_since_epoch()).count();
}

static const int g_MasternodeMinConfirmations = 10;

static const unsigned int g_MasternodesStartPayments = 150;
static const unsigned int g_MasternodesStopPayments = 100;

static const int g_AnnounceExistenceRestartPeriod = 20;
static const int g_AnnounceExistencePeriod = 5;
static const int g_MonitoringPeriod = 100;
static const int g_MonitoringPeriodMin = 30;

static const int g_MaxMasternodes = 1500;

// If masternode doesn't respond to some message we assume that it has responded in this amount of time.
static const double g_PenaltyTime = 500.0;

// Blockchain length at startup after sync. We don't know anythyng about how well masternodes were behaving before this block.
static int32_t g_InitialBlock = 0;

boost::unordered_map<COutPoint, CMasterNode> g_MasterNodes;
static boost::unordered_set<COutPoint> g_OurMasterNodes;
CElectedMasternodes g_ElectedMasternodes;

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

void CMasterNode::UpdateScore() const
{
    if (misbehaving)
    {
        score = 99999999999.0;
        return;
    }

    std::vector<int> vblocks = GetExistenceBlocks();

    score = 0.0;

    int nblocks = 0;
    for (uint32_t i = 0; i < vblocks.size(); i++)
    {
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
}

double CMasterNode::GetScore() const
{
    if (lastScoreUpdate < nBestHeight - 5)
    {
        UpdateScore();
        lastScoreUpdate = nBestHeight;
    }
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

    // Extract masternode's address from transaction
    keyid = extractKeyID(out.scriptPubKey);
    if (!keyid)
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

CMasterNode* CElectedMasternodes::NextPayee(const COutPoint& PrevPayee)
{
    if (PrevPayee.IsNull())
    {
        // Not enough masternodes to start payments
        if (masternodes.size() < g_MasternodesStartPayments)
            return nullptr;

        // Start payments form the beginning
        return getMasterNode(*masternodes.begin());
    }
    else
    {
        // Stop payments if there are not enough masternodes
        if (masternodes.size() < g_MasternodesStopPayments)
            return nullptr;

        auto iter = masternodes.upper_bound(PrevPayee);

        // Rewind
        if (iter == masternodes.end())
            iter = masternodes.begin();

        return getMasterNode(*iter);
    }
}

template<typename T, typename TComp>
inline void set_differences(const std::vector<T>& A, const std::vector<T>& B, std::vector<T>& resultA, std::vector<T>& resultB, TComp comp)
{
    auto firstA = A.begin();
    auto lastA = A.end();
    auto firstB = B.begin();
    auto lastB = B.end();

    while (true)
    {
        if (firstA == lastA)
        {
            resultB.insert(resultB.end(), firstB, lastB);
            return;
        }
        if (firstB == lastB)
        {
            resultA.insert(resultA.end(), firstA, lastA);
            return;
        }

        if (comp(*firstA, *firstB))
        {
            resultA.push_back(*firstA++);
        }
        else if (comp(*firstB, *firstA))
        {
            resultB.push_back(*firstB++);
        }
        else
        {
            ++firstA;
            ++firstB;
        }
    }
}

static bool compareMasternodesByScore(const CMasterNode* pLeft, const CMasterNode* pRight)
{
    double a = pLeft ->GetScore() - 0.001*pLeft ->amount*1.0/COIN;
    double b = pRight->GetScore() - 0.001*pRight->amount*1.0/COIN;
    return a < b;
}

void MN_CastVotes(std::vector<COutPoint> vvotes[])
{
    vvotes[0].clear();
    vvotes[1].clear();

    // Check if we are monitoring network long enough to start voting
    if (nBestHeight < g_InitialBlock + g_MonitoringPeriodMin)
        return;

    std::vector<const CMasterNode*> velected;
    std::vector<const CMasterNode*> vknown;

    for (const auto& pair : g_MasterNodes)
    {
        vknown.push_back(&pair.second);
    }

    for (const COutPoint& outpoint : g_ElectedMasternodes.masternodes)
    {
        velected.push_back(getMasterNode(outpoint));
    }

    std::sort(vknown.begin(), vknown.end(), compareMasternodesByScore);
    std::sort(velected.begin(), velected.end(), compareMasternodesByScore);

    if (vknown.size() > g_MaxMasternodes)
        vknown.resize(g_MaxMasternodes);

    std::vector<const CMasterNode*> vpvotes[2];

    // Find differences between elected masternodes and our opinion on what masternodes should be elected.
    // These differences are our votes.
    set_differences(velected, vknown, vpvotes[0], vpvotes[1], compareMasternodesByScore);

    std::reverse(vpvotes[0].begin(), vpvotes[0].end());

    // Check if there too many votes
    int totalVotes = vpvotes[0].size() + vpvotes[1].size();
    if (totalVotes > g_MaxMasternodeVotes)
    {
        int numVotes0;

        if (vpvotes[0].empty())
            numVotes0 = 0;
        else if (vpvotes[1].empty())
            numVotes0 = g_MaxMasternodeVotes;
        else
        {
            numVotes0 = (int)round(double(vpvotes[0].size())*g_MaxMasternodeVotes/totalVotes);
            numVotes0 = boost::algorithm::clamp(numVotes0, 1, g_MaxMasternodeVotes - 1);
        }

        vpvotes[0].resize(numVotes0);
        vpvotes[1].resize(g_MaxMasternodeVotes - numVotes0);
    }

    for (int i = 0; i < 2; i++)
    {
        for (const CMasterNode* pmn : vpvotes[i])
        {
            vvotes[i].push_back(pmn->outpoint);
        }
    }
}

void MN_GetVotes(CBlockIndex* pindex, boost::unordered_map<COutPoint, int> vvotes[2])
{
    CBlockIndex* pCurBlock = pindex->pprev;
    for (int i = 0; i < g_MasternodesElectionPeriod && pCurBlock; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (COutPoint vote : pCurBlock->vvotes[j])
            {
                auto pair = vvotes[j].insert(std::make_pair(vote, 1));
                if (!pair.second)
                {
                    pair.first->second++;
                }
            }
        }
        pCurBlock = pCurBlock->pprev;
    }

    for (int i = 0; i < 2; i++)
    {
        for (const auto& pair : vvotes[i])
        {
            // Add to known
            getMasterNode(pair.first);
        }
    }
}

bool CElectedMasternodes::elect(const COutPoint& outpoint, bool elect)
{
    if (elect)
    {
        if (masternodes.size() == g_MaxMasternodes)
            return false;
        if (!getMasterNode(outpoint))
            return false;
        return masternodes.insert(outpoint).second;
    }
    else
    {
        return masternodes.erase(outpoint) != 0;
    }
}

CKeyID CElectedMasternodes::OnConnectBlock(CBlockIndex* pindex)
{
    if (pindex->nHeight <= (int)getThirdHardforkBlock())
        return CKeyID(0);

    boost::unordered_map<COutPoint, int> vvotes[2];
    MN_GetVotes(pindex, vvotes);

    for (int j = 0; j < 2; j++)
    {
        for (const std::pair<COutPoint, int>& pair : vvotes[j])
        {
            if (pair.second > g_MasternodesElectionPeriod/2 && elect(pair.first, j))
                pindex->velected[j].push_back(pair.first);
        }
    }

    CMasterNode *pPayee = NextPayee(pindex->pprev->mn);
    if (!pPayee)
        return CKeyID(0);

    pindex->mn = pPayee->outpoint;
    return pPayee->keyid;
}

void CElectedMasternodes::OnDisconnectBlock(CBlockIndex* pindex)
{
    // Undo masternode election
    for (int j = 0; j < 2; j++)
    {
        for (const COutPoint& outpoint : pindex->velected[j])
        {
            assert(elect(outpoint, !j));
        }
    }
}

void MN_LoadElections()
{
    for (CBlockIndex* pindex = FindBlockByHeight(getThirdHardforkBlock() + 1);
         pindex;
         pindex = pindex->pnext)
    {
        g_ElectedMasternodes.OnConnectBlock(pindex);
    }
}

bool CElectedMasternodes::IsElected(const COutPoint& outpoint)
{
    return masternodes.count(outpoint) != 0;
}

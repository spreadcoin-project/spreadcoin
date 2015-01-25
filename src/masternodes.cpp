#include <boost/chrono.hpp>
#include <boost/algorithm/clamp.hpp>

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
static const int g_MonitoringPeriodMin = 10;

// If masternode doesn't respond to some message we assume that it has responded in this amount of time.
static const double g_PenaltyTime = 500.0;
static const double g_MaxScore = 100.0;

// Blockchain length at startup after sync. We don't know anythyng about how well masternodes were behaving before this block.
static int32_t g_InitialBlock = 0;

boost::unordered_map<COutPoint, CMasterNode> g_MasterNodes;

static std::vector<CMasterNodeInstantTxMsg> g_TxConfirms[g_InstantTxMaxConfirmations*g_InstantTxPeriod];

bool MN_IsAcceptableMasternodeInput(const COutPoint& outpoint, CCoinsViewCache* pCoins)
{
    CKeyID keyid;
    uint64_t amount;
    return MN_GetKeyIDAndAmount(outpoint, keyid, amount, pCoins);
}

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

bool CMasterNode::AddExistenceMsg(const CMasterNodeExistenceMsg& newMsg, CValidationState& state)
{
    uint256 hash = newMsg.GetHash();
    for (unsigned int i = 0; i < existenceMsgs.size(); i++)
    {
        if (existenceMsgs[i].msg.GetHash() == hash)
            return state.Invalid();
    }

    Cleanup();

    // Check if this masternode sends too many messages
    if (existenceMsgs.size() > g_MonitoringPeriod/g_AnnounceExistencePeriod*10)
    {
        misbehaving = true;
        return state.DoS(20, error("CMasterNode::AddExistenceMsg: too many messages"));
    }

    CReceivedExistenceMsg receivedMsg;
    receivedMsg.msg = newMsg;
    receivedMsg.nReceiveTime = GetMontoneTimeMs();
    existenceMsgs.push_back(receivedMsg);
    return true;
}

void CMasterNode::Cleanup()
{
    auto newEnd = std::remove_if(existenceMsgs.begin(), existenceMsgs.end(), [](const CReceivedExistenceMsg& msg)
    {
        return msg.msg.nBlock < nBestHeight - 2*g_MonitoringPeriod;
    });

    existenceMsgs.resize(newEnd - existenceMsgs.begin());
}

void CMasterNode::UpdateScore() const
{
    if (misbehaving)
    {
        score = 99*g_MaxScore;
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

bool MN_GetKeyIDAndAmount(const COutPoint& outpoint, CKeyID& keyid, uint64_t& amount, CCoinsViewCache* pCoins, bool AllowUnconfirmed)
{
    int age;
    CTxOut out;
    if(!GetOutput(outpoint, pCoins, age, out))
        return false;

    if (!AllowUnconfirmed && age < g_MasternodeMinConfirmations)
        return false;

    if (out.IsNull() || out.nValue < g_MinMasternodeAmount)
        return false;

    // Extract masternode's address from transaction
    keyid = extractKeyID(out.scriptPubKey);
    if (!keyid)
        return false;

    amount = out.nValue;
    return true;
}

CMasterNode* MN_Get(const COutPoint& outpoint)
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
        if (!MN_GetKeyIDAndAmount(outpoint, keyid, amount, nullptr))
            return nullptr;

        CMasterNode& mn = g_MasterNodes[outpoint];
        mn.outpoint = outpoint;
        mn.keyid = keyid;
        mn.amount = amount;
        return &mn;
    }
}

void MN_Cleanup()
{
    boost::unordered_map<COutPoint, CMasterNode> masternodes;
    for (auto& pair : g_MasterNodes)
    {
        if (MN_IsAcceptableMasternodeInput(pair.first, nullptr))
            masternodes[pair.first] = std::move(pair.second);
    }
    g_MasterNodes = masternodes;

    MN_MyRemoveOutdated();
}

void MN_ProcessBlocks()
{
    if (IsInitialBlockDownload())
        return;

    if (g_InitialBlock == 0)
        g_InitialBlock = nBestHeight;

    if (nBestHeight % 10 == 0)
        MN_Cleanup();

    for (CBlockIndex* pBlock = pindexBest;
         pBlock->nHeight > g_InitialBlock && pBlock->nReceiveTime == 0;
         pBlock = pBlock->pprev)
    {
        pBlock->nReceiveTime = GetMontoneTimeMs();
        MN_MyProcessBlock(pBlock);

        int i = pBlock->nHeight % (g_InstantTxMaxConfirmations*g_InstantTxPeriod);
        g_TxConfirms[i].clear();
    }
}

bool MN_CanBeInstantTx(const CTransaction& tx, int64_t nFees)
{
    if (tx.vin.size() > g_MaxInstantTxInputs)
        return false;

    if (nFees < (int64_t)tx.vin.size()*g_InstantTxFeePerInput)
        return false;

    if (!tx.IsFinal())
        return false;

    return true;
}

static bool MN_ProcessExistenceMsg_Impl(const CMasterNodeExistenceMsg& mnem, CValidationState& state)
{
    // Too old message, it should not be retranslated
    if (mnem.nBlock < nBestHeight - 100)
        return state.DoS(20, error("MN_ProcessExistenceMsg_Impl: mnem too old"));

    // Too old message
    if (mnem.nBlock < nBestHeight- 50)
        return state.Invalid(error("MN_ProcessExistenceMsg_Impl: mnem too old"));

    CMasterNode* pmn = MN_Get(mnem.outpoint);
    if (!pmn)
        return state.DoS(20, error("MN_ProcessExistenceMsg_Impl: masternode not found"));

    // Check signature
    if (!mnem.CheckSignature() || mnem.GetOutpointKeyID() != pmn->keyid)
        return state.DoS(20, error("MN_ProcessExistenceMsg_Impl: invalid signature"));

    printf("Masternode existence message mn=%s:%u, block=%u\n", mnem.outpoint.hash.ToString().c_str(), mnem.outpoint.n, mnem.nBlock);

    return pmn->AddExistenceMsg(mnem, state);
}

void MN_ProcessExistenceMsg(CNode* pfrom, const CMasterNodeExistenceMsg& mnem)
{
    if (IsInitialBlockDownload())
        return;

    CValidationState state;
    bool Ok = MN_ProcessExistenceMsg_Impl(mnem, state);

    int nDoS = 0;
    if (state.IsInvalid(nDoS) && nDoS > 0)
        pfrom->Misbehaving(nDoS);

    if (!Ok)
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

std::vector<int> MN_GetConfirmationBlocks(COutPoint outpoint, int nBlockBegin, int nBlockEnd)
{
    if (nBlockBegin < (int)getThirdHardforkBlock())
        nBlockBegin = getThirdHardforkBlock();

    std::vector<int> blocks;
    for (int i = nBlockBegin/g_InstantTxPeriod; i < nBlockEnd/g_InstantTxPeriod + 1; i++)
    {
        CHashWriter hasher(SER_GETHASH, 0);;
        hasher << outpoint;
        hasher << i;
        int nBlock = i*g_InstantTxPeriod + (hasher.GetHash().Get64() % g_InstantTxPeriod);
        if (nBlock >= nBlockBegin && nBlock <= nBlockEnd)
            blocks.push_back(nBlock);
    }
    return blocks;
}

static bool ShouldConfirm(int nBlock, COutPoint outpoint)
{
    std::vector<int> blocks = MN_GetConfirmationBlocks(outpoint, nBlock, nBlock);
    return blocks.size() == 1 && blocks[0] == nBlock;
}

static int MN_ProcessInstantTxMsg_Impl(const CMasterNodeInstantTxMsg& mnitx, CValidationState& state)
{
    if (mnitx.nMnBlock > nBestHeight + 5 || mnitx.nMnBlock < nBestHeight - g_InstantTxPeriod*g_InstantTxMaxConfirmations*2)
        return state.DoS(20, error("MN_ProcessInstantTxMsg_Impl: block out of range"));

    if (!ShouldConfirm(mnitx.nMnBlock, mnitx.outpointTx))
        return state.DoS(20, error("MN_ProcessInstantTxMsg_Impl: should not confirm"));

    CBlockIndex* pIndex = FindBlockByHeight(mnitx.nMnBlock);
    if (!pIndex)
        return state.Invalid(error("MN_ProcessInstantTxMsg_Impl: couldn't find block"));

    if (pIndex->mn != mnitx.outpoint)
        return state.DoS(20, error("MN_ProcessInstantTxMsg_Impl: wrong masternode"));

    // Check signature
    if (!mnitx.CheckSignature() || mnitx.GetOutpointKeyID() != pIndex->mnKeyId)
        return state.DoS(20, error("MN_ProcessInstantTxMsg_Impl: invalid signature"));

    int i = mnitx.nMnBlock % (g_InstantTxMaxConfirmations*g_InstantTxPeriod);
    auto& confirms = g_TxConfirms[i];

    if (confirms.size() > 1000)
        return state.Invalid(error("MN_ProcessInstantTxMsg_Impl: too many messsages"));

    uint256 hash = mnitx.GetHash();
    for (const auto& confirm : confirms)
    {
        if (confirm.GetHash() == hash)
            return state.Invalid(error("MN_ProcessInstantTxMsg_Impl: already have"));
    }

    printf("Masternode instant tx message mn=%s:%u, block=%i, %s:%u -> %s\n", mnitx.outpoint.hash.ToString().c_str(), mnitx.outpoint.n,
           mnitx.nMnBlock, mnitx.outpointTx.hash.GetHex().c_str(), mnitx.outpointTx.n, mnitx.hashTx.GetHex().c_str());

    confirms.push_back(mnitx);
    return true;
}

void MN_ProcessInstantTxMsg(CNode* pfrom, const CMasterNodeInstantTxMsg& mnitx)
{
    if (IsInitialBlockDownload())
        return;

    CValidationState state;
    bool Ok = MN_ProcessInstantTxMsg_Impl(mnitx, state);

    int nDoS = 0;
    if (pfrom && state.IsInvalid(nDoS) && nDoS > 0)
        pfrom->Misbehaving(nDoS);

    if (!Ok)
        return;

    uint256 mnitxHash = mnitx.GetHash();
    if (pfrom)
        pfrom->setKnown.insert(mnitxHash);

    // Relay
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes)
        {
            // returns true if wasn't already contained in the set
            if (pnode->setKnown.insert(mnitxHash).second)
            {
                pnode->PushMessage("mnitx", mnitx);
            }
        }
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

void MN_CastVotes(std::vector<COutPoint> vvotes[], CCoinsViewCache &coins)
{
    vvotes[0].clear();
    vvotes[1].clear();

    // Check if we are monitoring network long enough to start voting
    if (nBestHeight < g_InitialBlock + g_MonitoringPeriodMin)
        return;

    std::vector<const CMasterNode*> velected;
    std::vector<const CMasterNode*> vknown;

    MN_Cleanup();
    for (const auto& pair : g_MasterNodes)
    {
        const CMasterNode& mn = pair.second;

        if (mn.GetScore() > g_MaxScore)
            continue;

        vknown.push_back(&mn);
    }

    for (const COutPoint& outpoint : g_ElectedMasternodes.masternodes)
    {
        velected.push_back(MN_Get(outpoint));
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
            MN_Get(pair.first);
        }
    }
}

int MN_GetNumConfirms(const CTransaction& tx)
{
    uint256 hashTx = tx.GetHash();

    int minConfirmations = 100;

    for (const CTxIn& txin : tx.vin)
    {
        COutPoint outpointTx = txin.prevout;
        std::vector<int> blocks = MN_GetConfirmationBlocks(outpointTx, nBestHeight - g_InstantTxPeriod*g_InstantTxPeriod, nBestHeight);

        int inputConfirmations = 0;
        for (const int nBlock : blocks)
        {
            for (const auto& confirm : g_TxConfirms[nBlock % (g_InstantTxMaxConfirmations*g_InstantTxPeriod)])
            {
                if (confirm.outpointTx == outpointTx && confirm.hashTx == hashTx)
                    inputConfirmations++;
            }
        }
        if (inputConfirmations < minConfirmations)
            minConfirmations = inputConfirmations;
    }

    return minConfirmations;
}

bool MN_CheckInstantTx(const CInstantTx& tx)
{
    return false;
}

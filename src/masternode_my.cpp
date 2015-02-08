#include <boost/unordered_set.hpp>
#include <boost/algorithm/string.hpp>

#include "masternodes.h"
#include "init.h"

static boost::unordered_set<COutPoint> g_OurMasterNodes;
uint256 g_OurMasternodesXor;

CMasterNodeSecret::CMasterNodeSecret(CKey privkey)
{
    CKey privkey2;
    privkey2.MakeNewKey();
    CPubKey pubkey2 = privkey2.GetPubKey();

    this->privkey = privkey2;
    this->signature = privkey.Sign(pubkey2.GetHash());
}

CMasterNode* MN_SetMy(const COutPoint& outpoint, bool my)
{
    CMasterNode* pmn = MN_Get(outpoint);
    if (!pmn)
        return NULL;

    pmn->my = my;
    return pmn;
}

bool MN_Start(const COutPoint& outpoint, const CMasterNodeSecret& secret)
{
    CMasterNode* pmn = MN_Get(outpoint);
    if (!pmn)
        return false;

    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->LockCoin(outpoint);
    }

    CPubKey pubkey;
    pubkey.RecoverCompact(secret.privkey.GetPubKey().GetHash(), secret.signature);
    if (pubkey.GetID() != pmn->keyid)
        return false;

    pmn->my = true;
    pmn->secret = secret;
    g_OurMasterNodes.insert(outpoint);
    g_OurMasternodesXor ^= outpoint.GetHash();
    return true;
}

bool MN_StartFromStr(const std::string& secretStr)
{
    std::vector<std::string> params2;
    boost::split(params2, secretStr, boost::is_any_of(":"));
    if (params2.size() != 4)
        return false;

    COutPoint outpoint;
    outpoint.n = atoi(params2[1]);
    outpoint.hash.SetHex(params2[0]);

    CBigNum signature;
    signature.SetHex(params2[2]);
    auto vec = signature.getvch();
    std::reverse(vec.begin(), vec.end());

    CMasterNodeSecret mnsecret;
    std::copy(vec.begin(), vec.end(), mnsecret.signature.begin());

    CBitcoinSecret secret;
    secret.SetString(params2[3]);
    mnsecret.privkey = secret.GetKey();

    return MN_Start(outpoint, mnsecret);
}

bool MN_Stop(const COutPoint& outpoint)
{
    CMasterNode* pmn = MN_Get(outpoint);
    if (!pmn)
        return false;

    pmn->secret.privkey = CKey();
    g_OurMasterNodes.erase(outpoint);
    g_OurMasternodesXor ^= outpoint.GetHash();

    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->UnlockCoin(outpoint);
    }
    return true;
}

void MN_MyRemoveOutdated()
{
    boost::unordered_set<COutPoint> OurMN;

    for (const COutPoint& outpoint : g_OurMasterNodes)
    {
        if (g_MasterNodes.count(outpoint) != 0)
            OurMN.insert(outpoint);
    }

    g_OurMasterNodes = std::move(OurMN);
}

static void InitBaseMsg(CMasterNodeBaseMsg& msg, CMasterNode& mn)
{
    msg.pubkey2.pubkey2 = mn.secret.privkey.GetPubKey();
    msg.pubkey2.signature = mn.secret.signature;
    msg.outpoint = mn.outpoint;
}

void MN_MyProcessBlock(const CBlockIndex* pBlock)
{
    for (COutPoint outpoint : g_OurMasterNodes)
    {
        CMasterNode& mn = g_MasterNodes[outpoint];
        assert (mn.IsRunning());

        std::vector<int> vblocks = mn.GetExistenceBlocks();
        if (std::find(vblocks.begin(), vblocks.end(), pBlock->nHeight) == vblocks.end())
            continue;

        if (!MN_IsAcceptableMasternodeInput(mn.outpoint, nullptr))
            continue;

        CMasterNodeExistenceMsg mnem;
        InitBaseMsg(mnem, mn);
        mnem.hashBlock = pBlock->GetBlockHash();
        mnem.nBlock = pBlock->nHeight;
        mnem.signature = mn.secret.privkey.Sign(mnem.GetHashForSignature());

        MN_ProcessExistenceMsg(NULL, mnem);
    }
}

void MN_MyProcessTx(const CTransaction& tx, int64_t nFees)
{
    if (!MN_CanBeInstantTx(tx, nFees))
        return;

    for (const CTxIn& txin : tx.vin)
    {
        COutPoint outpointTx = txin.prevout;
        std::vector<int> blocks = MN_GetConfirmationBlocks(outpointTx, nBestHeight - g_InstantTxInterval, nBestHeight);
        for (const int nBlock : blocks)
        {
            CBlockIndex* pindex = FindBlockByHeight(nBlock);
            if (!pindex)
                continue;

            CMasterNode* pmn = MN_Get(pindex->mn);
            if (!pmn || !pmn->IsRunning())
                continue;

            CMasterNodeInstantTxMsg mnitx;
            InitBaseMsg(mnitx, *pmn);

            mnitx.hashTx = tx.GetHash();
            mnitx.outpointTx = outpointTx;
            mnitx.nMnBlock = nBlock;
            mnitx.signature = pmn->secret.privkey.Sign(mnitx.GetHashForSignature());

            MN_ProcessInstantTxMsg(NULL, mnitx);
        }
    }
}

void MN_StartFromConfig()
{
    std::vector<std::string> mnsecrets = mapMultiArgs["-mnstart"];

    std::for_each(mnsecrets.begin(), mnsecrets.end(), MN_StartFromStr);
}

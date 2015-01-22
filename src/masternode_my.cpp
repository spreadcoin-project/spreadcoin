#include <boost/unordered_set.hpp>

#include "masternodes.h"
#include "init.h"

static boost::unordered_set<COutPoint> g_OurMasterNodes;

CMasterNodeSecret::CMasterNodeSecret(CKey privkey)
{
    CKey privkey2;
    privkey2.MakeNewKey();
    CPubKey pubkey2 = privkey2.GetPubKey();

    this->privkey = privkey2;
    this->signature = privkey.Sign(pubkey2.GetHash());
}

bool MN_SetMy(const COutPoint& outpoint, bool my)
{
    CMasterNode* pmn = MN_Get(outpoint);
    if (!pmn)
        return false;

    pmn->my = my;
    return true;
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
    return true;
}

bool MN_Stop(const COutPoint& outpoint)
{
    CMasterNode* pmn = MN_Get(outpoint);
    if (!pmn)
        return false;

    pmn->secret.privkey = CKey();
    g_OurMasterNodes.erase(outpoint);

    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->UnlockCoin(outpoint);
    }
    return true;
}

void MN_MyProcessBlock(const CBlockIndex* pBlock)
{
    for (COutPoint outpoint : g_OurMasterNodes)
    {
        CMasterNode& mn = g_MasterNodes[outpoint];
        assert (mn.secret.privkey.IsValid());

        std::vector<int> vblocks = mn.GetExistenceBlocks();
        if (std::find(vblocks.begin(), vblocks.end(), pBlock->nHeight) == vblocks.end())
            continue;

        if (!MN_IsAcceptableMasternodeInput(mn.outpoint, nullptr))
            continue;

        CMasterNodeExistenceMsg mnem;
        mnem.pubkey2.pubkey2 = mn.secret.privkey.GetPubKey();
        mnem.pubkey2.signature = mn.secret.signature;
        mnem.outpoint = mn.outpoint;
        mnem.hashBlock = pBlock->GetBlockHash();
        mnem.nBlock = pBlock->nHeight;
        mnem.signature = mn.secret.privkey.Sign(mnem.GetHashForSignature());

        MN_ProcessExistenceMsg(NULL, mnem);
    }
}

void MN_ProcessInstantTx(const CTransaction& tx)
{

}

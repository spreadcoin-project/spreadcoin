#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

#include "masternodes.h"

static boost::unordered_map<uint256, CMasterNode> g_MasterNodes;
static boost::unordered_set<uint256> g_OurMasterNodes;

#if 0
static const int g_VotingNumNodes = 10;
static const int g_VotingInterval = 1000;
static const int g_VotingSeedBlock = 100;
static const int g_VotingFuture = 5;

// How many pseudorandom indexes is it possible to extract from one pseudotandom uint64_t
static const int g_VotingIndexesPer64Bits = static_cast<int>(64.0 / log2(g_VotingInterval));

enum VotingSeed
{
    VSMasterNodeElect = 0,
    VSMinerElect = 1,
};
#endif

CMasterNode::CMasterNode()
{
}

CMasterNode& getMasterNode(const COutPoint& outpoint)
{
    // FIXME: check input
    
    g_MasterNodes.
}

uint256 CMasterNodeExistenceMsg::GetHashForSignature()
{
    CHashWriter hasher;
    hasher << outpoint << nBlock << blockHash;
}

static bool shouldMasternodeSignBlock(const CMasterNode& mn, const CBlockHeader& block)
{

}

#if 0
// Determine which nodes should vote for block n
static void GetVotingNodes(std::set<int64_t>& vvoters, int n, int seed)
{
    const int seedHeight = std::max(0, n - g_VotingSeedBlock);
    uint256 seedBlockHash = FindBlockByHeight(seedHeight)->GetBlockHash();

    vvoters.clear();
    for (int k = 0; true; k++)
    {
        CHashWriter hasher;
        hasher << n;
        hasher << seedBlockHash;
        hasher << seed;
        hasher << k;

        uint256 pr256 = hasher.GetHash();

        for (int i = 0; i < 4; i++)
        {
            uint64_t pr64 = pr256.Get64(i);
            for (int j = 0; j < g_VotingIndexesPer64Bits; j++)
            {
                vvoters.insert(n - pr64 % g_VotingInterval);

                if (vvoters.size() == g_VotingNumNodes)
                    return;

                pr64 /= g_VotingInterval;
            }
        }
    }
}
#endif

void MN_ProcessBlock(const CBlockHeader& block)
{
    if (IsInitialBlockDownload())
        return;

    for (uint256 hashMN : g_OurMasterNodes)
    {
        CMasterNode& mn = g_MasterNodes[hashMN];
        assert (mn.privkey.IsValid());

        if (!shouldMasternodeSignBlock(mn, block))
            continue;

        CMasterNodeExistenceMsg mnem;
        mnem.outpoint = mn.outpoint;
        mnem.blockHash = block.GetHash();
        mnem.nBlock = block.nHeight;
        mnem.signature = mn.privkey.Sign(mnem.GetHashForSignature());

        Broadcast(mnem);
    }

    std::set<int64_t> mn_voters, miner_voters;
    GetVotingNodes(mn_voters,    pindexBest->nHeight + g_VotingFuture, VSMasterNodeElect);
    GetVotingNodes(miner_voters, pindexBest->nHeight + g_VotingFuture, VSMinerElect);

}

void MN_ProcessExistenceMsg(const CMasterNodeExistenceMsg& mnem)
{
    uint256 hashMN = mnem.outpoint.GetHash();

    CPubKey pubkey;
    pubkey.RecoverCompact(mnem.GetHashForSignature(), mnem.signature);


}

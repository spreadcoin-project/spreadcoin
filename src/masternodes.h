#ifndef MASTERNODES_H
#define MASTERNODES_H

#include "main.h"

class CMasterNode
{
public:
    // Masternode indentifier
    COutPoint outpoint;

    // Key used to sign masternode's messages so that it doesn't need to keep in memory it's main key
    CPubKey key2;

    // Only for our masternodes obviously
    CKey privkey;

    uint256 GetHash();

    CMasterNode();
};

// Wrapper for masternode messages
class CMasterNodeBaseMsg
{
public:
    // Masternode indentifier
    COutPoint outpoint;

    // Pubkey message is signed with oupoint key;
    // All other messages are signed with key2.
    CSignature signature;

    virtual uint256 GetHashForSignature() = 0;
};

// Message broadcasted by masternode to announce its public key.
// Usually broadcasted at startup.
class CMasterNodePubKeyMsg : public CMasterNodeBaseMsg
{
public:
    // new key to sign all subsequent messages
    CPubKey key2;

    // prove that this message is recent
    uint64_t nBlock;
    uint256 blockHash;

    uint256 GetHashForSignature();
};

// Message broadcasted by masternode to confirm that it is running
class CMasterNodeExistenceMsg : public CMasterNodeBaseMsg
{
public:
    // prove that this message is recent
    uint64_t nBlock;
    uint256 blockHash;

    uint256 GetHashForSignature();
};

void MN_ProcessBlock(const CBlockHeader& block);
void MN_ProcessInstantTx(const CTransaction& tx);
void MN_ProcessExistenceMsg(const CMasterNodeExistenceMsg& mnem);

#endif // MASTERNODES_H

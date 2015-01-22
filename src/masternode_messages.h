#pragma once

#include "main.h"

class CMasterNodePubkey2
{
public:
    CPubKey pubkey2;

    // Signed with outpoint key.
    CSignature signature;

    CKeyID GetOutpointKeyID() const;

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

    bool CheckSignature() const;

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

// Message broadcasted by masternode to confirm that it is running
class CMasterNodeInstantTxMsg : public CMasterNodeBaseMsg
{
public:
    COutPoint outpointTx;

    uint256 hashTx;

    uint256 GetHashForSignature() const;
    uint256 GetHash() const;

    IMPLEMENT_SERIALIZE
    (
        READWRITE(pubkey2);
        READWRITE(outpoint);
        READWRITE(signature);

        READWRITE(outpointTx);
        READWRITE(hashTx);
    )
};

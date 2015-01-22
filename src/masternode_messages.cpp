#include "masternode_messages.h"

CKeyID CMasterNodePubkey2::GetOutpointKeyID() const
{
    CPubKey pubkey;
    pubkey.RecoverCompact(pubkey2.GetHash(), signature);
    return pubkey.GetID();
}

bool CMasterNodeBaseMsg::CheckSignature() const
{
    CPubKey pubkey;
    pubkey.RecoverCompact(GetHashForSignature(), signature);
    return pubkey == pubkey2.pubkey2;
}

uint256 CMasterNodeExistenceMsg::GetHashForSignature() const
{
    CMasterNodeExistenceMsg copy = *this;
    copy.signature.SetNull();
    return copy.GetHash();
}

uint256 CMasterNodeExistenceMsg::GetHash() const
{
    CHashWriter hasher(SER_GETHASH, 0);
    hasher << *this;
    return hasher.GetHash();
}

uint256 CMasterNodeInstantTxMsg::GetHashForSignature() const
{
    CMasterNodeInstantTxMsg copy = *this;
    copy.signature.SetNull();
    return copy.GetHash();
}

uint256 CMasterNodeInstantTxMsg::GetHash() const
{
    CHashWriter hasher(SER_GETHASH, 0);
    hasher << *this;
    return hasher.GetHash();
}

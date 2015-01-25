// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string.hpp>

#include "main.h"
#include "bitcoinrpc.h"
#include "masternodes.h"
#include "init.h"

using namespace json_spirit;
using namespace std;
using namespace boost;
using namespace boost::assign;

Value mnsecret(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "mnsecret tx:n\n"
            "Get masternode secret.");

    vector<string> params2;
    boost::split(params2, params[0].get_str(), boost::is_any_of(":"));
    if (params2.size() != 2)
        throw runtime_error(
            "mnsecret tx:n\n"
            "Get masternode secret.");

    COutPoint outpoint;
    outpoint.n = atoi(params2[1]);
    outpoint.hash.SetHex(params2[0]);

    CKeyID keyid;
    uint64_t amount;
    if (!MN_GetKeyIDAndAmount(outpoint, keyid, amount, nullptr, true))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Could not find this output, maybe it is already spent?");

    CKey key;
    if (!pwalletMain->GetKey(keyid, key))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Could not get private key, wallet is locked or it is not your transaction");

    CMasterNodeSecret mnsecret(key);

    CBitcoinSecret secret;
    secret.SetKey(mnsecret.privkey);

    return outpoint.hash.ToString() + ':' + itostr(outpoint.n) + ':' + mnsecret.signature.ToString() + ':' + secret.ToString();
}

Value mnstart(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "mnstart tx:n:signature:privkey\n"
            "Start masternode.");

    return MN_StartFromStr(params[0].get_str());
}

Value mnstop(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "mnstop tx:n\n"
            "Stop masternode.");

    vector<string> params2;
    boost::split(params2, params[0].get_str(), boost::is_any_of(":"));
    if (params2.size() != 2)
        throw runtime_error(
            "mnstop tx:n\n"
            "Stop masternode.");

    COutPoint outpoint;
    outpoint.n = atoi(params2[1]);
    outpoint.hash.SetHex(params2[0]);

    return MN_Stop(outpoint);
}

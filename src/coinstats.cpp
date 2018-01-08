// Copyright (c) 2014-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coinstats.h"
#include "validation.h"

#include <stdint.h>

#include <boost/thread/thread.hpp> // boost::thread::interrupt

using namespace std;

extern CCriticalSection cs_main;


//! Calculate statistics about the unspent transaction output set
bool GetUTXOStats(CCoinsView *view, CCoinsByScriptViewDB *viewbyscriptdb, CCoinsStats &stats)
{
    std::unique_ptr<CCoinsViewCursor> pcursor(view->Cursor());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = pcursor->GetBestBlock();
    {
        LOCK(cs_main);
        BlockMap::const_iterator iter = mapBlockIndex.find(stats.hashBlock);
        if (iter == mapBlockIndex.end())
            stats.nHeight = 0;
        else
            stats.nHeight = iter->second->nHeight;
    }

    ss << stats.hashBlock;
    CAmount nTotalAmount = 0;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        uint256 key;
        CCoins coins;
        if (pcursor->GetKey(key) && pcursor->GetValue(coins)) {
            stats.nTransactions++;
            ss << key;
            for (unsigned int i=0; i<coins.vout.size(); i++) {
                const CTxOut &out = coins.vout[i];
                if (!out.IsNull()) {
                    stats.nTransactionOutputs++;
                    ss << VARINT(i+1);
                    ss << out;
                    nTotalAmount += out.nValue;
                }
            }
            stats.nSerializedSize += 32 + pcursor->GetValueSize();
            ss << VARINT(0);
        } else {
            return error("%s: unable to read value", __func__);
        }
        pcursor->Next();
    }
    stats.hashSerialized = ss.GetHash();
    stats.nTotalAmount = nTotalAmount;

    std::unique_ptr<CCoinsByScriptViewDBCursor> pcursordb(viewbyscriptdb->Cursor());
    while (pcursordb->Valid()) {
        boost::this_thread::interruption_point();
        CScriptID hash;
        unspentcoins_t coinsByScript;
        if (pcursordb->GetKey(hash) && pcursordb->GetValue(coinsByScript)) {
            stats.nAddresses++;
            stats.nAddressesOutputs += coinsByScript.size();
        } else {
            return error("%s: unable to read value", __func__);
        }
        pcursordb->Next();
    }
    return true;
}

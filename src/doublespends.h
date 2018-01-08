// Copyright (c) 2014-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZCOIN_DOUBLESPENDS_H
#define ZCOIN_DOUBLESPENDS_H

#include "dbwrapper.h"
#include "uint256.h"
#include "util.h"
#include "serialize.h"
#include "primitives/transaction.h"


#include <set>
#include <map>
#include <memory>

/*
    Optional functionality - doublespends database.

    Once a double spend attempt is detected (via AcceptToMemoryPoolWorker function), it's information is recorded:
    -txid and vout (outpoint) of the transaction output that has been attempted to be spent more than once
    -txid of every transaction that has attempted to claim the output
    -the block height of the attempt (for cleanup purposes; attempts older than 6 blocks are deleted)

*/

class CDoubleSpend
{
public:
    //An original outpoint used as input multiple times
    COutPoint conflictedOutpoint;

    //Set of txids attempting to spend the input
    std::set<uint256> conflictingTx;

    //The height of conflictedOutpoint's tx
    int nOriginHeight;

    CDoubleSpend();
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(conflictedOutpoint);
        READWRITE(conflictingTx);
        READWRITE(nOriginHeight);
    }
};

typedef std::map<COutPoint, CDoubleSpend> doublespendsmap_t;

/** Set of all recent (up to 6 blocks old) double spend records (doublespendattempts/) */
class CDoubleSpendsView
{
    //memory storage
    doublespendsmap_t cache;
    //database backup
    CDBWrapper db;

    /*  
        Returns a modifiable reference to CDoubleSpend for given outpoint.
        Loads the record from database if its not loaded alreeady. 
        Returns false, if the record was not found.
    */
    CDoubleSpend* GetRecord(const COutPoint& key);

    /* 
        Creates a new double-spend record 
    */
    CDoubleSpend* CreateRecord(const COutPoint& key, const int nHeight);

     /*
        Write a record to database
     */
     bool WriteBatch(const CDoubleSpend& rec);
public:

    CDoubleSpendsView(size_t nCacheSize);

    //Once a double spend attempt is detected in mempool, this function is called to memorize it
    bool RegisterDoubleSpendAttempt(const CTxIn& txin, const uint256& txHash, int nHeight);

    //Delete all double spend records older than 6 blocks
    void DeleteOldRecords(int currentHeight);

    std::vector<CDoubleSpend> GetAllRecords() const;

};



extern std::unique_ptr<CDoubleSpendsView> pDoubleSpendsView;

#endif //ZCOIN_DOUBLESPENDS_H
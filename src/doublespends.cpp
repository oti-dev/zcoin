#include <algorithm>
#include "doublespends.h"
#include "util.h"

std::unique_ptr<CDoubleSpendsView> pDoubleSpendsView{ nullptr };


static const char DB_DOUBLESPEND_ATTEMPT = 'D';

//since database's key differs from cache's key (additional 'leading' char), its easier to address it through the helper function
using dbKey_t = std::pair<char, COutPoint>;
static inline dbKey_t dbKey(const dbKey_t::second_type& k)
{ 
    return std::make_pair(DB_DOUBLESPEND_ATTEMPT, static_cast<dbKey_t::second_type>(k));
} 


CDoubleSpend::CDoubleSpend()
{
}


CDoubleSpend* CDoubleSpendsView::GetRecord(const COutPoint& key)
{
    //check whether the record is already in memory
    auto it = cache.find(key);
    if (it != cache.end())
    {
        return &(it->second);
    }

    //if its not, try to load it to memory 
    //thats more of a double-check, since(in normal situation) the entire database content would be reflected in cache anyway
    CDoubleSpend value;
    if (db.Read(dbKey(key), value))
    {
        return &(cache.emplace(key, value).first->second);
    }
    return nullptr;
}

CDoubleSpend* CDoubleSpendsView::CreateRecord(const COutPoint& key, const int nHeight)
{
    if (cache.find(key) != cache.end())
        throw std::runtime_error("CDoubleSpendsView::CreateRecord - Attempt to recreate a double-spend record\n");
    CDoubleSpend value;
    value.conflictedOutpoint = key;
    value.nOriginHeight = nHeight;
    return &(cache.emplace(key, value).first->second);
}

bool CDoubleSpendsView::WriteBatch(const CDoubleSpend& rec)
{
    CDBBatch batch(db);
    batch.Write(dbKey(rec.conflictedOutpoint), rec);
    return db.WriteBatch(batch);
}

CDoubleSpendsView::CDoubleSpendsView(size_t nCacheSize)
    : db(GetDataDir() / "doublespendattempts", nCacheSize)
{
    int64_t nRecordsRead = 0;
    //load entire database on startup
    std::unique_ptr<CDBIterator> dbIter(db.NewIterator());
    for (dbIter->SeekToFirst(); dbIter->Valid(); dbIter->Next())
    {
        dbKey_t key;
        CDoubleSpend value;
        if (!dbIter->GetKey(key))
        {
            LogPrintf("CDoubleSpendsView constructor - read key failure\n");
            break;
        }
        if (!dbIter->GetValue(value))
        {
            LogPrintf("CDoubleSpendsView constructor - read value failure\n");
            break;
        }

        cache.emplace(key.second, value);
        nRecordsRead++;
    }
    LogPrintf("CDoubleSpendsView constructor - %d records read\n", nRecordsRead);
}


bool CDoubleSpendsView::RegisterDoubleSpendAttempt(const CTxIn& txin, const uint256& txHash, int nHeight)
{
    const COutPoint& prevout = txin.prevout;

    CDoubleSpend* rec = GetRecord(prevout);
    if (!rec)
    {
        rec = CreateRecord(prevout, nHeight);
        if(!rec)
        {
            LogPrintf("CDoubleSpendsView::RegisterDoubleSpendAttempt failed\n");
            return false;
        }
    }

    // store the height of the oldest block involved
    rec->nOriginHeight = std::min(rec->nOriginHeight, nHeight);
    
    bool inserted = rec->conflictingTx.insert(txHash).second;


    LogPrintf("CDoubleSpendsView::RegisterDoubleSpendAttempt succeeded\n");

    return WriteBatch(*rec);
}


void CDoubleSpendsView::DeleteOldRecords(int currentHeight)
{
    CDBBatch batch(db);
    for (auto it = cache.begin(); it != cache.end(); )
    {
        const CDoubleSpend& r = it->second;
        if ((currentHeight - r.nOriginHeight) >= 6)
        {
            batch.Erase(dbKey(it->first));

            //delete this item, navigate to the next valid
            it = cache.erase(it);

            LogPrintf("CDoubleSpendsView::DeleteOldRecords - record erased\n");
        }
        else it++;
    }
    if (!db.WriteBatch(batch))
        throw std::runtime_error("CDoubleSpendsView::DeleteOldRecords - error erasing old records\n");
}

std::vector<CDoubleSpend> CDoubleSpendsView::GetAllRecords() const
{
    std::vector<CDoubleSpend> result;
    
    for (auto it = cache.begin(); it != cache.end(); it++)
    {
        result.push_back(it->second);
    }

    return result;
}
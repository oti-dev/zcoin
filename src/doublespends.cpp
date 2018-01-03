#include <algorithm>
#include "doublespends.h"
#include <memory>
#include "util.h"

static const char DB_DOUBLESPEND_ATTEMPT = 'D';


CDoubleSpend::CDoubleSpend()
{
}


bool CDoubleSpendsView::GetRecord(const COutPoint& key, CDoubleSpend& outRec)
{
    //check whether the record is already in memory
    auto it = cache.find(key);
    if (it != cache.end())
    {
        outRec = it->second;
        return true;
    }

    //if its not, try to load it to memory
    CDoubleSpend value;
    if (db.Read(std::make_pair(DB_DOUBLESPEND_ATTEMPT, static_cast<COutPoint>(key)), value))
    {
        outRec = cache.emplace(key, value).first->second;
        return true;
    }
    return false;
}

bool CDoubleSpendsView::CreateRecord(const COutPoint& key, const int nHeight, CDoubleSpend& outRec)
{
    if (cache.find(key) != cache.end())
        throw std::runtime_error("CDoubleSpendsView::CreateRecord - Attempt to recreate a double-spend record");
    CDoubleSpend value;
    value.conflictedOutpoint = key;
    value.nOriginHeight = nHeight;
    outRec = cache.emplace(key, value).first->second;

   
    return true;
}

bool CDoubleSpendsView::WriteBatch(const CDoubleSpend& rec)
{
    CDBBatch batch(db);
    batch.Write(std::make_pair(DB_DOUBLESPEND_ATTEMPT, static_cast<COutPoint>(rec.conflictedOutpoint)), rec);
    return db.WriteBatch(batch);
}

CDoubleSpendsView::CDoubleSpendsView(size_t nCacheSize)
    : db(GetDataDir() / "doublespendattempts", nCacheSize)
{

    //load entire database on startup
    std::unique_ptr<CDBIterator> dbIter(db.NewIterator());
    for (; dbIter->Valid(); dbIter->Next())
    {
        std::pair<char, COutPoint> key;
        CDoubleSpend value;
        if (!dbIter->GetKey(key))
        {
            LogPrintf("CDoubleSpendsView constructor - read key failure");
            break;
        }
        if (!dbIter->GetValue(value))
        {
            LogPrintf("CDoubleSpendsView constructor - read value failure");
            break;
        }

        cache.emplace(key.second, value);
    }
}


bool CDoubleSpendsView::RegisterDoubleSpendAttempt(const CTxIn& txin, const uint256& txHash, int nHeight)
{
    const COutPoint& prevout = txin.prevout;
    CDoubleSpend rec;
    if (!GetRecord(prevout, rec))
    {
        if(!CreateRecord(prevout, nHeight, rec))
        {
            return false;
        }
    }

    // store the height of the oldest block involved
    rec.nOriginHeight = std::min(rec.nOriginHeight, nHeight);
    
    bool inserted = rec.conflictingTx.insert(txHash).second;
    return WriteBatch(rec);
}


void CDoubleSpendsView::DeleteOldRecords(int currentHeight)
{
    CDBBatch batch(db);
    for (auto it = cache.begin(); it != cache.end(); it++)
    {
        const CDoubleSpend& r = it->second;
        if ((currentHeight - r.nOriginHeight) >= 6)
        {
            batch.Erase(std::make_pair(DB_DOUBLESPEND_ATTEMPT, static_cast<COutPoint>(it->first)));
            cache.erase(it);
        }
    }
    if (!db.WriteBatch(batch))
        throw std::runtime_error("CDoubleSpendsView::DeleteOldRecords - error erasing old records");
}
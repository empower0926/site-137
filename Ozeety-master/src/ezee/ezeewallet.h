// Copyright (c) 2017-2020 The OZEETY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OZEETY_EZEEWALLET_H
#define OZEETY_EZEEWALLET_H

#include <map>
#include "libeshadow/Coin.h"
#include "mintpool.h"
#include "uint256.h"
#include "wallet/wallet.h"
#include "eshadow.h"

class CDeterministicMint;

class CeZEEWallet
{
private:
    uint256 seedMaster;
    uint32_t nCountLastUsed;
    std::string strWalletFile;
    CMintPool mintPool;

public:
    CeZEEWallet(CWallet* parent);

    void AddToMintPool(const std::pair<uint256, uint32_t>& pMint, bool fVerbose);
    bool SetMasterSeed(const uint256& seedMaster, bool fResetCount = false);
    uint256 GetMasterSeed() { return seedMaster; }
    void SyncWithChain(bool fGenerateMintPool = true);
    void GenerateDeterministicEZEE(libeshadow::CoinDenomination denom, libeshadow::PrivateCoin& coin, CDeterministicMint& dMint, bool fGenerateOnly = false);
    void GenerateMint(const uint32_t& nCount, const libeshadow::CoinDenomination denom, libeshadow::PrivateCoin& coin, CDeterministicMint& dMint);
    void GetState(int& nCount, int& nLastGenerated);
    bool RegenerateMint(const CDeterministicMint& dMint, CEshadowMint& mint);
    void GenerateMintPool(uint32_t nCountStart = 0, uint32_t nCountEnd = 0);
    bool LoadMintPoolFromDB();
    void RemoveMintsFromPool(const std::vector<uint256>& vPubcoinHashes);
    bool SetMintSeen(const CBigNum& bnValue, const int& nHeight, const uint256& txid, const libeshadow::CoinDenomination& denom);
    bool IsInMintPool(const CBigNum& bnValue) { return mintPool.Has(bnValue); }
    void UpdateCount();
    void Lock();
    void SeedToEZEE(const uint512& seed, CBigNum& bnValue, CBigNum& bnSerial, CBigNum& bnRandomness, CKey& key);
    bool CheckSeed(const CDeterministicMint& dMint);

private:
    /* Parent wallet */
    CWallet* wallet{nullptr};

    uint512 GetEshadowSeed(uint32_t n);
};

#endif //OZEETY_EZEEWALLET_H

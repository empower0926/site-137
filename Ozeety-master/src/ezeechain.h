// Copyright (c) 2018-2020 The OZEETY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OZEETY_EZEECHAIN_H
#define OZEETY_EZEECHAIN_H

#include "chain.h"
#include "libeshadow/Coin.h"
#include "libeshadow/Denominations.h"
#include "libeshadow/CoinSpend.h"
#include <list>
#include <string>

class CBlock;
class CBlockIndex;
class CBigNum;
struct CMintMeta;
class CTransaction;
class CTxIn;
class CTxOut;
class CValidationState;
class CEshadowMint;
class uint256;

bool BlockToMintValueVector(const CBlock& block, const libeshadow::CoinDenomination denom, std::vector<CBigNum>& vValues);
bool BlockToPubcoinList(const CBlock& block, std::list<libeshadow::PublicCoin>& listPubcoins, bool fFilterInvalid);
bool BlockToEshadowMintList(const CBlock& block, std::list<CEshadowMint>& vMints, bool fFilterInvalid);
void FindMints(std::vector<CMintMeta> vMintsToFind, std::vector<CMintMeta>& vMintsToUpdate, std::vector<CMintMeta>& vMissingMints);
bool GetEshadowMint(const CBigNum& bnPubcoin, uint256& txHash);
bool IsPubcoinInBlockchain(const uint256& hashPubcoin, uint256& txid);
bool IsSerialInBlockchain(const CBigNum& bnSerial, int& nHeightTx);
bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend);
bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend, CTransaction& tx);
bool RemoveSerialFromDB(const CBigNum& bnSerial);
std::string ReindexEshadowDB();
libeshadow::CoinSpend TxInToEshadowSpend(const CTxIn& txin);
bool TxOutToPublicCoin(const CTxOut& txout, libeshadow::PublicCoin& pubCoin, CValidationState& state);
std::list<libeshadow::CoinDenomination> EshadowSpendListFromBlock(const CBlock& block, bool fFilterInvalid);

/** Global variable for the eshadow supply */
extern std::map<libeshadow::CoinDenomination, int64_t> mapEshadowSupply;
int64_t GetEshadowSupply();
bool UpdateEZEESupplyConnect(const CBlock& block, CBlockIndex* pindex, bool fJustCheck);
bool UpdateEZEESupplyDisconnect(const CBlock& block, CBlockIndex* pindex);

#endif //OZEETY_EZEECHAIN_H

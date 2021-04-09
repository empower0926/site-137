// Copyright (c) 2018-2020 The OZEETY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ezeechain.h"

#include "guiinterface.h"
#include "invalid.h"
#include "main.h"
#include "txdb.h"
#include "wallet/wallet.h"
#include "ezee/ezeemodule.h"

// 6 comes from OPCODE (1) + vch.size() (1) + BIGNUM size (4)
#define SCRIPT_OFFSET 6
// For Script size (BIGNUM/Uint256 size)
#define BIGNUM_SIZE   4

std::map<libeshadow::CoinDenomination, int64_t> mapEshadowSupply;

bool BlockToMintValueVector(const CBlock& block, const libeshadow::CoinDenomination denom, std::vector<CBigNum>& vValues)
{
    for (const CTransaction& tx : block.vtx) {
        if(!tx.HasEshadowMintOutputs())
            continue;

        for (const CTxOut& txOut : tx.vout) {
            if(!txOut.IsEshadowMint())
                continue;

            CValidationState state;
            libeshadow::PublicCoin coin(Params().GetConsensus().Eshadow_Params(false));
            if(!TxOutToPublicCoin(txOut, coin, state))
                return false;

            if (coin.getDenomination() != denom)
                continue;

            vValues.push_back(coin.getValue());
        }
    }

    return true;
}

bool BlockToPubcoinList(const CBlock& block, std::list<libeshadow::PublicCoin>& listPubcoins, bool fFilterInvalid)
{
    for (const CTransaction& tx : block.vtx) {
        if(!tx.HasEshadowMintOutputs())
            continue;

        // Filter out mints that have used invalid outpoints
        if (fFilterInvalid) {
            bool fValid = true;
            for (const CTxIn& in : tx.vin) {
                if (!ValidOutPoint(in.prevout, INT_MAX)) {
                    fValid = false;
                    break;
                }
            }
            if (!fValid)
                continue;
        }

        uint256 txHash = tx.GetHash();
        for (unsigned int i = 0; i < tx.vout.size(); i++) {
            //Filter out mints that use invalid outpoints - edge case: invalid spend with minted change
            if (fFilterInvalid && !ValidOutPoint(COutPoint(txHash, i), INT_MAX))
                break;

            const CTxOut txOut = tx.vout[i];
            if(!txOut.IsEshadowMint())
                continue;

            CValidationState state;
            libeshadow::PublicCoin pubCoin(Params().GetConsensus().Eshadow_Params(false));
            if(!TxOutToPublicCoin(txOut, pubCoin, state))
                return false;

            listPubcoins.emplace_back(pubCoin);
        }
    }

    return true;
}

//return a list of eshadow mints contained in a specific block
bool BlockToEshadowMintList(const CBlock& block, std::list<CEshadowMint>& vMints, bool fFilterInvalid)
{
    for (const CTransaction& tx : block.vtx) {
        if(!tx.HasEshadowMintOutputs())
            continue;

        // Filter out mints that have used invalid outpoints
        if (fFilterInvalid) {
            bool fValid = true;
            for (const CTxIn& in : tx.vin) {
                if (!ValidOutPoint(in.prevout, INT_MAX)) {
                    fValid = false;
                    break;
                }
            }
            if (!fValid)
                continue;
        }

        uint256 txHash = tx.GetHash();
        for (unsigned int i = 0; i < tx.vout.size(); i++) {
            //Filter out mints that use invalid outpoints - edge case: invalid spend with minted change
            if (fFilterInvalid && !ValidOutPoint(COutPoint(txHash, i), INT_MAX))
                break;

            const CTxOut txOut = tx.vout[i];
            if(!txOut.IsEshadowMint())
                continue;

            CValidationState state;
            libeshadow::PublicCoin pubCoin(Params().GetConsensus().Eshadow_Params(false));
            if(!TxOutToPublicCoin(txOut, pubCoin, state))
                return false;

            //version should not actually matter here since it is just a reference to the pubcoin, not to the privcoin
            uint8_t version = 1;
            CEshadowMint mint = CEshadowMint(pubCoin.getDenomination(), pubCoin.getValue(), 0, 0, false, version, nullptr);
            mint.SetTxHash(tx.GetHash());
            vMints.push_back(mint);
        }
    }

    return true;
}

void FindMints(std::vector<CMintMeta> vMintsToFind, std::vector<CMintMeta>& vMintsToUpdate, std::vector<CMintMeta>& vMissingMints)
{
    // see which mints are in our public eshadow database. The mint should be here if it exists, unless
    // something went wrong
    for (CMintMeta meta : vMintsToFind) {
        uint256 txHash;
        if (!eshadowDB->ReadCoinMint(meta.hashPubcoin, txHash)) {
            vMissingMints.push_back(meta);
            continue;
        }

        // make sure the txhash and block height meta data are correct for this mint
        CTransaction tx;
        uint256 hashBlock;
        if (!GetTransaction(txHash, tx, hashBlock, true)) {
            LogPrintf("%s : cannot find tx %s\n", __func__, txHash.GetHex());
            vMissingMints.push_back(meta);
            continue;
        }

        if (!mapBlockIndex.count(hashBlock)) {
            LogPrintf("%s : cannot find block %s\n", __func__, hashBlock.GetHex());
            vMissingMints.push_back(meta);
            continue;
        }

        //see if this mint is spent
        uint256 hashTxSpend;
        bool fSpent = eshadowDB->ReadCoinSpend(meta.hashSerial, hashTxSpend);

        //if marked as spent, check that it actually made it into the chain
        CTransaction txSpend;
        uint256 hashBlockSpend;
        if (fSpent && !GetTransaction(hashTxSpend, txSpend, hashBlockSpend, true)) {
            LogPrintf("%s : cannot find spend tx %s\n", __func__, hashTxSpend.GetHex());
            meta.isUsed = false;
            vMintsToUpdate.push_back(meta);
            continue;
        }

        //The mint has been incorrectly labelled as spent in eshadowDB and needs to be undone
        int nHeightTx = 0;
        uint256 hashSerial = meta.hashSerial;
        uint256 txidSpend;
        if (fSpent && !IsSerialInBlockchain(hashSerial, nHeightTx, txidSpend)) {
            LogPrintf("%s : cannot find block %s. Erasing coinspend from eshadowDB.\n", __func__, hashBlockSpend.GetHex());
            meta.isUsed = false;
            vMintsToUpdate.push_back(meta);
            continue;
        }

        // is the denomination correct?
        for (auto& out : tx.vout) {
            if (!out.IsEshadowMint())
                continue;
            libeshadow::PublicCoin pubcoin(Params().GetConsensus().Eshadow_Params(meta.nVersion < libeshadow::PrivateCoin::PUBKEY_VERSION));
            CValidationState state;
            TxOutToPublicCoin(out, pubcoin, state);
            if (GetPubCoinHash(pubcoin.getValue()) == meta.hashPubcoin && pubcoin.getDenomination() != meta.denom) {
                LogPrintf("%s: found mismatched denom pubcoinhash = %s\n", __func__, meta.hashPubcoin.GetHex());
                meta.denom = pubcoin.getDenomination();
                vMintsToUpdate.emplace_back(meta);
            }
        }

        // if meta data is correct, then no need to update
        if (meta.txid == txHash && meta.nHeight == mapBlockIndex[hashBlock]->nHeight && meta.isUsed == fSpent)
            continue;

        //mark this mint for update
        meta.txid = txHash;
        meta.nHeight = mapBlockIndex[hashBlock]->nHeight;
        meta.isUsed = fSpent;
        LogPrintf("%s: found updates for pubcoinhash = %s\n", __func__, meta.hashPubcoin.GetHex());

        vMintsToUpdate.push_back(meta);
    }
}

bool GetEshadowMint(const CBigNum& bnPubcoin, uint256& txHash)
{
    txHash = UINT256_ZERO;
    return eshadowDB->ReadCoinMint(bnPubcoin, txHash);
}

bool IsPubcoinInBlockchain(const uint256& hashPubcoin, uint256& txid)
{
    txid.SetNull();
    return eshadowDB->ReadCoinMint(hashPubcoin, txid);
}

bool IsSerialInBlockchain(const CBigNum& bnSerial, int& nHeightTx)
{
    uint256 txHash;
    // if not in eshadowDB then its not in the blockchain
    if (!eshadowDB->ReadCoinSpend(bnSerial, txHash))
        return false;

    return IsTransactionInChain(txHash, nHeightTx);
}

bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend)
{
    CTransaction tx;
    return IsSerialInBlockchain(hashSerial, nHeightTx, txidSpend, tx);
}

bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend, CTransaction& tx)
{
    txidSpend.SetNull();
    // if not in eshadowDB then its not in the blockchain
    if (!eshadowDB->ReadCoinSpend(hashSerial, txidSpend))
        return false;

    return IsTransactionInChain(txidSpend, nHeightTx, tx);
}

std::string ReindexEshadowDB()
{
    AssertLockHeld(cs_main);

    if (!eshadowDB->WipeCoins("spends") || !eshadowDB->WipeCoins("mints")) {
        return _("Failed to wipe eshadowDB");
    }

    uiInterface.ShowProgress(_("Reindexing eshadow database..."), 0);

    // initialize supply to 0
    mapEshadowSupply.clear();
    for (auto& denom : libeshadow::eshadowDenomList) mapEshadowSupply.insert(std::make_pair(denom, 0));

    const Consensus::Params& consensus = Params().GetConsensus();
    const int zc_start_height = consensus.vUpgrades[Consensus::UPGRADE_ZC].nActivationHeight;
    CBlockIndex* pindex = chainActive[zc_start_height];
    std::vector<std::pair<libeshadow::CoinSpend, uint256> > vSpendInfo;
    std::vector<std::pair<libeshadow::PublicCoin, uint256> > vMintInfo;
    while (pindex) {
        uiInterface.ShowProgress(_("Reindexing eshadow database..."), std::max(1, std::min(99, (int)((double)(pindex->nHeight - zc_start_height) / (double)(chainActive.Height() - zc_start_height) * 100))));

        if (pindex->nHeight % 1000 == 0)
            LogPrintf("Reindexing eshadow : block %d...\n", pindex->nHeight);

        CBlock block;
        if (!ReadBlockFromDisk(block, pindex)) {
            return _("Reindexing eshadow failed");
        }
        // update supply
        UpdateEZEESupplyConnect(block, pindex, true);

        for (const CTransaction& tx : block.vtx) {
            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                if (tx.IsCoinBase())
                    break;

                if (tx.ContainsEshadows()) {
                    uint256 txid = tx.GetHash();
                    //Record Serials
                    if (tx.HasEshadowSpendInputs()) {
                        for (auto& in : tx.vin) {
                            bool isPublicSpend = in.IsEshadowPublicSpend();
                            if (!in.IsEshadowSpend() && !isPublicSpend)
                                continue;
                            if (isPublicSpend) {
                                libeshadow::EshadowParams* params = consensus.Eshadow_Params(false);
                                PublicCoinSpend publicSpend(params);
                                CValidationState state;
                                if (!EZEEModule::ParseEshadowPublicSpend(in, tx, state, publicSpend)){
                                    return _("Failed to parse public spend");
                                }
                                vSpendInfo.push_back(std::make_pair(publicSpend, txid));
                            } else {
                                libeshadow::CoinSpend spend = TxInToEshadowSpend(in);
                                vSpendInfo.push_back(std::make_pair(spend, txid));
                            }
                        }
                    }

                    //Record mints
                    if (tx.HasEshadowMintOutputs()) {
                        for (auto& out : tx.vout) {
                            if (!out.IsEshadowMint())
                                continue;

                            CValidationState state;
                            const bool v1params = !consensus.NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_ZC_V2);
                            libeshadow::PublicCoin coin(consensus.Eshadow_Params(v1params));
                            TxOutToPublicCoin(out, coin, state);
                            vMintInfo.push_back(std::make_pair(coin, txid));
                        }
                    }
                }
            }
        }

        // Flush the eshadowDB to disk every 100 blocks
        if (pindex->nHeight % 100 == 0) {
            if ((!vSpendInfo.empty() && !eshadowDB->WriteCoinSpendBatch(vSpendInfo)) || (!vMintInfo.empty() && !eshadowDB->WriteCoinMintBatch(vMintInfo)))
                return _("Error writing eshadowDB to disk");
            vSpendInfo.clear();
            vMintInfo.clear();
        }

        pindex = chainActive.Next(pindex);
    }
    uiInterface.ShowProgress("", 100);

    // Final flush to disk in case any remaining information exists
    if ((!vSpendInfo.empty() && !eshadowDB->WriteCoinSpendBatch(vSpendInfo)) || (!vMintInfo.empty() && !eshadowDB->WriteCoinMintBatch(vMintInfo)))
        return _("Error writing eshadowDB to disk");

    uiInterface.ShowProgress("", 100);

    return "";
}

bool RemoveSerialFromDB(const CBigNum& bnSerial)
{
    return eshadowDB->EraseCoinSpend(bnSerial);
}

libeshadow::CoinSpend TxInToEshadowSpend(const CTxIn& txin)
{
    CDataStream serializedCoinSpend = EZEEModule::ScriptSigToSerializedSpend(txin.scriptSig);
    return libeshadow::CoinSpend(serializedCoinSpend);
}

bool TxOutToPublicCoin(const CTxOut& txout, libeshadow::PublicCoin& pubCoin, CValidationState& state)
{
    CBigNum publicEshadow;
    std::vector<unsigned char> vchZeroMint;
    vchZeroMint.insert(vchZeroMint.end(), txout.scriptPubKey.begin() + SCRIPT_OFFSET,
                       txout.scriptPubKey.begin() + txout.scriptPubKey.size());
    publicEshadow.setvch(vchZeroMint);

    libeshadow::CoinDenomination denomination = libeshadow::AmountToEshadowDenomination(txout.nValue);
    LogPrint(BCLog::LEGACYZC, "%s : denomination %d for pubcoin %s\n", __func__, denomination, publicEshadow.GetHex());
    if (denomination == libeshadow::ZQ_ERROR)
        return state.DoS(100, error("TxOutToPublicCoin : txout.nValue is not correct"));

    libeshadow::PublicCoin checkPubCoin(Params().GetConsensus().Eshadow_Params(false), publicEshadow, denomination);
    pubCoin = checkPubCoin;

    return true;
}

//return a list of eshadow spends contained in a specific block, list may have many denominations
std::list<libeshadow::CoinDenomination> EshadowSpendListFromBlock(const CBlock& block, bool fFilterInvalid)
{
    std::list<libeshadow::CoinDenomination> vSpends;
    for (const CTransaction& tx : block.vtx) {
        if (!tx.HasEshadowSpendInputs())
            continue;

        for (const CTxIn& txin : tx.vin) {
            bool isPublicSpend = txin.IsEshadowPublicSpend();
            if (!txin.IsEshadowSpend() && !isPublicSpend)
                continue;

            if (fFilterInvalid && !isPublicSpend) {
                libeshadow::CoinSpend spend = TxInToEshadowSpend(txin);
                if (invalid_out::ContainsSerial(spend.getCoinSerialNumber()))
                    continue;
            }

            libeshadow::CoinDenomination c = libeshadow::IntToEshadowDenomination(txin.nSequence);
            vSpends.push_back(c);
        }
    }
    return vSpends;
}

int64_t GetEshadowSupply()
{
    AssertLockHeld(cs_main);

    if (mapEshadowSupply.empty())
        return 0;

    int64_t nTotal = 0;
    for (auto& denom : libeshadow::eshadowDenomList) {
        nTotal += libeshadow::EshadowDenominationToAmount(denom) * mapEshadowSupply.at(denom);
    }
    return nTotal;
}

bool UpdateEZEESupplyConnect(const CBlock& block, CBlockIndex* pindex, bool fJustCheck)
{
    AssertLockHeld(cs_main);

    const Consensus::Params& consensus = Params().GetConsensus();
    if (!consensus.NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_ZC))
        return true;

    //Add mints to eZEE supply (mints are forever disabled after last checkpoint)
    if (pindex->nHeight < consensus.height_last_ZC_AccumCheckpoint) {
        std::list<CEshadowMint> listMints;
        std::set<uint256> setAddedToWallet;
        BlockToEshadowMintList(block, listMints, true);
        for (const CEshadowMint& m : listMints) {
            mapEshadowSupply.at(m.GetDenomination())++;
            //Remove any of our own mints from the mintpool
            if (!fJustCheck && pwalletMain) {
                if (pwalletMain->IsMyMint(m.GetValue())) {
                    pwalletMain->UpdateMint(m.GetValue(), pindex->nHeight, m.GetTxHash(), m.GetDenomination());
                    // Add the transaction to the wallet
                    for (auto& tx : block.vtx) {
                        uint256 txid = tx.GetHash();
                        if (setAddedToWallet.count(txid))
                            continue;
                        if (txid == m.GetTxHash()) {
                            CWalletTx wtx(pwalletMain, tx);
                            wtx.nTimeReceived = block.GetBlockTime();
                            wtx.SetMerkleBranch(block);
                            pwalletMain->AddToWallet(wtx, nullptr);
                            setAddedToWallet.insert(txid);
                        }
                    }
                }
            }
        }
    }

    //Remove spends from eZEE supply
    std::list<libeshadow::CoinDenomination> listDenomsSpent = EshadowSpendListFromBlock(block, true);
    for (const libeshadow::CoinDenomination& denom : listDenomsSpent) {
        mapEshadowSupply.at(denom)--;
        // eshadow failsafe
        if (mapEshadowSupply.at(denom) < 0)
            return error("Block contains eshadows that spend more than are in the available supply to spend");
    }

    // Update Wrapped Serials amount
    // A one-time event where only the eZEE supply was off (due to serial duplication off-chain on main net)
    if (Params().NetworkID() == CBaseChainParams::MAIN && pindex->nHeight == consensus.height_last_ZC_WrappedSerials + 1) {
        for (const libeshadow::CoinDenomination& denom : libeshadow::eshadowDenomList)
            mapEshadowSupply.at(denom) += GetWrapppedSerialInflation(denom);
    }

    for (const libeshadow::CoinDenomination& denom : libeshadow::eshadowDenomList)
        LogPrint(BCLog::LEGACYZC, "%s coins for denomination %d pubcoin %s\n", __func__, denom, mapEshadowSupply.at(denom));

    return true;
}

bool UpdateEZEESupplyDisconnect(const CBlock& block, CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);

    const Consensus::Params& consensus = Params().GetConsensus();
    if (!consensus.NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_ZC))
        return true;

    // Undo Update Wrapped Serials amount
    // A one-time event where only the eZEE supply was off (due to serial duplication off-chain on main net)
    if (Params().NetworkID() == CBaseChainParams::MAIN && pindex->nHeight == consensus.height_last_ZC_WrappedSerials + 1) {
        for (const libeshadow::CoinDenomination& denom : libeshadow::eshadowDenomList)
            mapEshadowSupply.at(denom) -= GetWrapppedSerialInflation(denom);
    }

    // Re-add spends to eZEE supply
    std::list<libeshadow::CoinDenomination> listDenomsSpent = EshadowSpendListFromBlock(block, true);
    for (const libeshadow::CoinDenomination& denom : listDenomsSpent) {
        mapEshadowSupply.at(denom)++;
    }

    // Remove mints from eZEE supply (mints are forever disabled after last checkpoint)
    if (pindex->nHeight < consensus.height_last_ZC_AccumCheckpoint) {
        std::list<CEshadowMint> listMints;
        std::set<uint256> setAddedToWallet;
        BlockToEshadowMintList(block, listMints, true);
        for (const CEshadowMint& m : listMints) {
            const libeshadow::CoinDenomination denom = m.GetDenomination();
            mapEshadowSupply.at(denom)--;
            // eshadow failsafe
            if (mapEshadowSupply.at(denom) < 0)
                return error("Block contains eshadows that spend more than are in the available supply to spend");
        }
    }

    for (const libeshadow::CoinDenomination& denom : libeshadow::eshadowDenomList)
        LogPrint(BCLog::LEGACYZC, "%s coins for denomination %d pubcoin %s\n", __func__, denom, mapEshadowSupply.at(denom));

    return true;
}


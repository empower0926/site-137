// Copyright (c) 2020 The OZEETY developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include "legacy/validation_eshadow_legacy.h"

#include "consensus/eshadow_verify.h"
#include "libeshadow/CoinSpend.h"
#include "wallet/wallet.h"
#include "ezeechain.h"

bool AcceptToMemoryPoolEshadow(const CTransaction& tx, CAmount& nValueIn, int chainHeight, CValidationState& state, const Consensus::Params& consensus)
{
    nValueIn = tx.GetEshadowSpent();

    //Check that txid is not already in the chain
    int nHeightTx = 0;
    if (IsTransactionInChain(tx.GetHash(), nHeightTx))
        return state.Invalid(error("%s : eZEE spend tx %s already in block %d", __func__, tx.GetHash().GetHex(), nHeightTx),
                             REJECT_DUPLICATE, "bad-txns-inputs-spent");

    //Check for double spending of serial #'s
    for (const CTxIn& txIn : tx.vin) {
        // Only allow for public zc spends inputs
        if (!txIn.IsEshadowPublicSpend())
            return state.Invalid(false, REJECT_INVALID, "bad-zc-spend-notpublic");

        libeshadow::EshadowParams* params = consensus.Eshadow_Params(false);
        PublicCoinSpend publicSpend(params);
        if (!EZEEModule::ParseEshadowPublicSpend(txIn, tx, state, publicSpend)){
            return false;
        }
        if (!ContextualCheckEshadowSpend(tx, &publicSpend, chainHeight, UINT256_ZERO))
            return state.Invalid(false, REJECT_INVALID, "bad-zc-spend-contextcheck");

        // Check that the version matches the one enforced with SPORK_18
        if (!CheckPublicCoinSpendVersion(publicSpend.getVersion())) {
            return state.Invalid(false, REJECT_INVALID, "bad-zc-spend-version");
        }
    }

    return true;
}

bool DisconnectEshadowTx(const CTransaction& tx, CAmount& nValueIn, CEshadowDB* eshadowDB)
{
    /** UNDO ESHADOW DATABASING
         * note we only undo eshadow databasing in the following statement, value to and from OZEETY
         * addresses should still be handled by the typical bitcoin based undo code
         * */
    if (tx.ContainsEshadows()) {
        libeshadow::EshadowParams *params = Params().GetConsensus().Eshadow_Params(false);
        if (tx.HasEshadowSpendInputs()) {
            //erase all eshadowspends in this transaction
            for (const CTxIn &txin : tx.vin) {
                bool isPublicSpend = txin.IsEshadowPublicSpend();
                if (txin.scriptSig.IsEshadowSpend() || isPublicSpend) {
                    CBigNum serial;
                    if (isPublicSpend) {
                        PublicCoinSpend publicSpend(params);
                        CValidationState state;
                        if (!EZEEModule::ParseEshadowPublicSpend(txin, tx, state, publicSpend)) {
                            return error("Failed to parse public spend");
                        }
                        serial = publicSpend.getCoinSerialNumber();
                        nValueIn += publicSpend.getDenomination() * COIN;
                    } else {
                        libeshadow::CoinSpend spend = TxInToEshadowSpend(txin);
                        serial = spend.getCoinSerialNumber();
                        nValueIn += spend.getDenomination() * COIN;
                    }

                    if (!eshadowDB->EraseCoinSpend(serial))
                        return error("failed to erase spent eshadow in block");

                    //if this was our spend, then mark it unspent now
                    if (pwalletMain) {
                        if (pwalletMain->IsMyEshadowSpend(serial)) {
                            if (!pwalletMain->SetMintUnspent(serial))
                                LogPrintf("%s: failed to automatically reset mint", __func__);
                        }
                    }
                }

            }
        }

        if (tx.HasEshadowMintOutputs()) {
            //erase all eshadowmints in this transaction
            for (const CTxOut &txout : tx.vout) {
                if (txout.scriptPubKey.empty() || !txout.IsEshadowMint())
                    continue;

                libeshadow::PublicCoin pubCoin(params);
                CValidationState state;
                if (!TxOutToPublicCoin(txout, pubCoin, state))
                    return error("DisconnectBlock(): TxOutToPublicCoin() failed");

                if (!eshadowDB->EraseCoinMint(pubCoin.getValue()))
                    return error("DisconnectBlock(): Failed to erase coin mint");
            }
        }
    }
    return true;
}

// Legacy Eshadow DB: used for performance during IBD
// (between Eshadow_Block_V2_Start and Eshadow_Block_Last_Checkpoint)
void DataBaseAccChecksum(CBlockIndex* pindex, bool fWrite)
{
    const Consensus::Params& consensus = Params().GetConsensus();
    if (!pindex ||
        !consensus.NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_ZC_V2) ||
        pindex->nHeight > consensus.height_last_ZC_AccumCheckpoint ||
        pindex->nAccumulatorCheckpoint == pindex->pprev->nAccumulatorCheckpoint)
        return;

    uint256 accCurr = pindex->nAccumulatorCheckpoint;
    uint256 accPrev = pindex->pprev->nAccumulatorCheckpoint;
    // add/remove changed checksums to/from DB
    for (int i = (int)libeshadow::eshadowDenomList.size()-1; i >= 0; i--) {
        const uint32_t& nChecksum = accCurr.Get32();
        if (nChecksum != accPrev.Get32()) {
            fWrite ?
            eshadowDB->WriteAccChecksum(nChecksum, libeshadow::eshadowDenomList[i], pindex->nHeight) :
            eshadowDB->EraseAccChecksum(nChecksum, libeshadow::eshadowDenomList[i]);
        }
        accCurr >>= 32;
        accPrev >>= 32;
    }
}

// Copyright (c) 2019-2020 The OZEETY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ezee/ezeemodule.h"
#include "ezeechain.h"
#include "libeshadow/Commitment.h"
#include "libeshadow/Coin.h"
#include "hash.h"
#include "main.h"
#include "iostream"

PublicCoinSpend::PublicCoinSpend(libeshadow::EshadowParams* params, const uint8_t version,
        const CBigNum& serial, const CBigNum& randomness, const uint256& ptxHash, CPubKey* pubkey):
            pubCoin(params)
{
    this->coinSerialNumber = serial;
    this->version = version;
    this->spendType = libeshadow::SpendType::SPEND;
    this->ptxHash = ptxHash;
    this->coinVersion = libeshadow::ExtractVersionFromSerial(coinSerialNumber);

    if (!isAllowed()) {
        // v1 coins need at least version 4 spends
        std::string errMsg = strprintf("Unable to create PublicCoinSpend version %d with coin version 1. "
                "Minimum spend version required: %d", version, PUBSPEND_SCHNORR);
        // this should be unreachable code (already checked in createInput)
        // throw runtime error
        throw std::runtime_error(errMsg);
    }

    if (pubkey && getCoinVersion() >= libeshadow::PrivateCoin::PUBKEY_VERSION) {
        // pubkey available only from v2 coins onwards
        this->pubkey = *pubkey;
    }

    if (version < PUBSPEND_SCHNORR)
        this->randomness = randomness;
    else
        this->schnorrSig = libeshadow::CoinRandomnessSchnorrSignature(params, randomness, ptxHash);

}

template <typename Stream>
PublicCoinSpend::PublicCoinSpend(libeshadow::EshadowParams* params, Stream& strm): pubCoin(params) {
    strm >> *this;
    this->spendType = libeshadow::SpendType::SPEND;

    if (this->version < PUBSPEND_SCHNORR) {
        // coinVersion is serialized only from v4 spends
        this->coinVersion = libeshadow::ExtractVersionFromSerial(this->coinSerialNumber);

    } else {
        // from v4 spends, serialNumber is not serialized for v2 coins anymore.
        // in this case, we extract it from the coin public key
        if (this->coinVersion >= libeshadow::PrivateCoin::PUBKEY_VERSION)
            this->coinSerialNumber = libeshadow::ExtractSerialFromPubKey(this->pubkey);

    }

}

bool PublicCoinSpend::Verify() const {
    bool fUseV1Params = getCoinVersion() < libeshadow::PrivateCoin::PUBKEY_VERSION;
    if (version < PUBSPEND_SCHNORR) {
        // spend contains the randomness of the coin
        if (fUseV1Params) {
            // Only v2+ coins can publish the randomness
            std::string errMsg = strprintf("PublicCoinSpend version %d with coin version 1 not allowed. "
                    "Minimum spend version required: %d", version, PUBSPEND_SCHNORR);
            return error("%s: %s", __func__, errMsg);
        }

        // Check that the coin is a commitment to serial and randomness.
        libeshadow::EshadowParams* params = Params().GetConsensus().Eshadow_Params(false);
        libeshadow::Commitment comm(&params->coinCommitmentGroup, getCoinSerialNumber(), randomness);
        if (comm.getCommitmentValue() != pubCoin.getValue()) {
            return error("%s: commitments values are not equal", __func__);
        }

    } else {
        // for v1 coins, double check that the serialized coin serial is indeed a v1 serial
        if (coinVersion < libeshadow::PrivateCoin::PUBKEY_VERSION &&
                libeshadow::ExtractVersionFromSerial(this->coinSerialNumber) != coinVersion) {
            return error("%s: invalid coin version", __func__);
        }

        // spend contains a shnorr signature of ptxHash with the randomness of the coin
        libeshadow::EshadowParams* params = Params().GetConsensus().Eshadow_Params(fUseV1Params);
        if (!schnorrSig.Verify(params, getCoinSerialNumber(), pubCoin.getValue(), getTxOutHash())) {
            return error("%s: schnorr signature does not verify", __func__);
        }

    }

    // Now check that the signature validates with the serial
    if (!HasValidSignature()) {
        return error("%s: signature invalid", __func__);;
    }

    return true;
}

bool PublicCoinSpend::HasValidSignature() const
{
    if (coinVersion < libeshadow::PrivateCoin::PUBKEY_VERSION)
        return true;

    // for spend version 3 we must check that the provided pubkey and serial number match
    if (version < PUBSPEND_SCHNORR) {
        CBigNum extractedSerial = libeshadow::ExtractSerialFromPubKey(this->pubkey);
        if (extractedSerial != this->coinSerialNumber)
            return error("%s: hashedpubkey is not equal to the serial!", __func__);
    }

    return pubkey.Verify(signatureHash(), vchSig);
}


const uint256 PublicCoinSpend::signatureHash() const
{
    CHashWriter h(0, 0);
    h << ptxHash << denomination << getCoinSerialNumber() << randomness << txHash << outputIndex << getSpendType();
    return h.GetHash();
}

namespace EZEEModule {

    // Return stream of CoinSpend from tx input scriptsig
    CDataStream ScriptSigToSerializedSpend(const CScript& scriptSig)
    {
        std::vector<char, zero_after_free_allocator<char> > data;
        // skip opcode and data-len
        uint8_t byteskip = ((uint8_t) scriptSig[1] + 2);
        data.insert(data.end(), scriptSig.begin() + byteskip, scriptSig.end());
        return CDataStream(data, SER_NETWORK, PROTOCOL_VERSION);
    }

    bool createInput(CTxIn &in, CEshadowMint &mint, uint256 hashTxOut, const int spendVersion) {
        // check that this spend is allowed
        const bool fUseV1Params = mint.GetVersion() < libeshadow::PrivateCoin::PUBKEY_VERSION;
        if (!PublicCoinSpend::isAllowed(fUseV1Params, spendVersion)) {
            // v1 coins need at least version 4 spends
            std::string errMsg = strprintf("Unable to create PublicCoinSpend version %d with coin version 1. "
                    "Minimum spend version required: %d", spendVersion, PUBSPEND_SCHNORR);
            return error("%s: %s", __func__, errMsg);
        }

        // create the PublicCoinSpend
        libeshadow::EshadowParams *params = Params().GetConsensus().Eshadow_Params(fUseV1Params);
        PublicCoinSpend spend(params, spendVersion, mint.GetSerialNumber(), mint.GetRandomness(), hashTxOut, nullptr);

        spend.outputIndex = mint.GetOutputIndex();
        spend.txHash = mint.GetTxHash();
        spend.setDenom(mint.GetDenomination());

        // add public key and signature
        if (!fUseV1Params) {
            CKey key;
            if (!mint.GetKeyPair(key))
                return error("%s: failed to set eZEE privkey mint.", __func__);
            spend.setPubKey(key.GetPubKey(), true);

            std::vector<unsigned char> vchSig;
            if (!key.Sign(spend.signatureHash(), vchSig))
                return error("%s: EZEEModule failed to sign signatureHash.", __func__);
            spend.setVchSig(vchSig);

        }

        // serialize the PublicCoinSpend and add it to the input scriptSig
        CDataStream ser(SER_NETWORK, PROTOCOL_VERSION);
        ser << spend;
        std::vector<unsigned char> data(ser.begin(), ser.end());
        CScript scriptSigIn = CScript() << OP_ESHADOWPUBLICSPEND << data.size();
        scriptSigIn.insert(scriptSigIn.end(), data.begin(), data.end());

        // create the tx input
        in = CTxIn(mint.GetTxHash(), mint.GetOutputIndex(), scriptSigIn, mint.GetDenomination());
        in.nSequence = mint.GetDenomination();
        return true;
    }

    PublicCoinSpend parseCoinSpend(const CTxIn &in)
    {
        libeshadow::EshadowParams *params = Params().GetConsensus().Eshadow_Params(false);
        CDataStream serializedCoinSpend = ScriptSigToSerializedSpend(in.scriptSig);
        return PublicCoinSpend(params, serializedCoinSpend);
    }

    bool parseCoinSpend(const CTxIn &in, const CTransaction &tx, const CTxOut &prevOut, PublicCoinSpend &publicCoinSpend) {
        if (!in.IsEshadowPublicSpend() || !prevOut.IsEshadowMint())
            return error("%s: invalid argument/s", __func__);

        PublicCoinSpend spend = parseCoinSpend(in);
        spend.outputIndex = in.prevout.n;
        spend.txHash = in.prevout.hash;
        CMutableTransaction txNew(tx);
        txNew.vin.clear();
        spend.setTxOutHash(txNew.GetHash());

        // Check prev out now
        CValidationState state;
        if (!TxOutToPublicCoin(prevOut, spend.pubCoin, state))
            return error("%s: cannot get mint from output", __func__);

        spend.setDenom(spend.pubCoin.getDenomination());
        publicCoinSpend = spend;
        return true;
    }

    bool validateInput(const CTxIn &in, const CTxOut &prevOut, const CTransaction &tx, PublicCoinSpend &publicSpend) {
        // Now prove that the commitment value opens to the input
        if (!parseCoinSpend(in, tx, prevOut, publicSpend)) {
            return false;
        }
        if (libeshadow::EshadowDenominationToAmount(
                libeshadow::IntToEshadowDenomination(in.nSequence)) != prevOut.nValue) {
            return error("PublicCoinSpend validateInput :: input nSequence different to prevout value");
        }
        return publicSpend.Verify();
    }

    bool ParseEshadowPublicSpend(const CTxIn &txIn, const CTransaction& tx, CValidationState& state, PublicCoinSpend& publicSpend)
    {
        CTxOut prevOut;
        if(!GetOutput(txIn.prevout.hash, txIn.prevout.n ,state, prevOut)){
            return state.DoS(100, error("%s: public eshadow spend prev output not found, prevTx %s, index %d",
                                        __func__, txIn.prevout.hash.GetHex(), txIn.prevout.n));
        }
        if (!EZEEModule::parseCoinSpend(txIn, tx, prevOut, publicSpend)) {
            return state.Invalid(error("%s: invalid public coin spend parse %s\n", __func__,
                                       tx.GetHash().GetHex()), REJECT_INVALID, "bad-txns-invalid-ezee");
        }
        return true;
    }
}

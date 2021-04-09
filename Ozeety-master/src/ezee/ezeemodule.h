// Copyright (c) 2019-2020 The OZEETY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
#ifndef OZEETY_EZEEMODULE_H
#define OZEETY_EZEEMODULE_H

#include "libeshadow/bignum.h"
#include "libeshadow/Denominations.h"
#include "libeshadow/CoinSpend.h"
#include "libeshadow/Coin.h"
#include "libeshadow/CoinRandomnessSchnorrSignature.h"
#include "libeshadow/SpendType.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "serialize.h"
#include "uint256.h"
#include <streams.h>
#include <utilstrencodings.h>
#include "ezee/eshadow.h"
#include "chainparams.h"

static int const PUBSPEND_SCHNORR = 4;

class PublicCoinSpend : public libeshadow::CoinSpend {
public:

    PublicCoinSpend(libeshadow::EshadowParams* params): pubCoin(params) {};
    PublicCoinSpend(libeshadow::EshadowParams* params, const uint8_t version, const CBigNum& serial, const CBigNum& randomness, const uint256& ptxHash, CPubKey* pubkey);
    template <typename Stream> PublicCoinSpend(libeshadow::EshadowParams* params, Stream& strm);

    ~PublicCoinSpend(){};

    const uint256 signatureHash() const override;
    void setVchSig(std::vector<unsigned char> vchSig) { this->vchSig = vchSig; };
    bool HasValidSignature() const;
    bool Verify() const;
    static bool isAllowed(const bool fUseV1Params, const int spendVersion) { return !fUseV1Params || spendVersion >= PUBSPEND_SCHNORR; }
    bool isAllowed() const {
        const bool fUseV1Params = getCoinVersion() < libeshadow::PrivateCoin::PUBKEY_VERSION;
        return isAllowed(fUseV1Params, version);
    }
    int getCoinVersion() const { return this->coinVersion; }

    // Members
    int coinVersion;
    CBigNum randomness;
    libeshadow::CoinRandomnessSchnorrSignature schnorrSig;
    // prev out values
    uint256 txHash;
    unsigned int outputIndex = -1;
    libeshadow::PublicCoin pubCoin;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {

        READWRITE(version);

        if (version < PUBSPEND_SCHNORR) {
            READWRITE(coinSerialNumber);
            READWRITE(randomness);
            READWRITE(pubkey);
            READWRITE(vchSig);

        } else {
            READWRITE(coinVersion);
            if (coinVersion < libeshadow::PrivateCoin::PUBKEY_VERSION) {
                READWRITE(coinSerialNumber);
            }
            else {
                READWRITE(pubkey);
                READWRITE(vchSig);
            }
            READWRITE(schnorrSig);
        }
    }
};


class CValidationState;

namespace EZEEModule {
    CDataStream ScriptSigToSerializedSpend(const CScript& scriptSig);
    bool createInput(CTxIn &in, CEshadowMint& mint, uint256 hashTxOut, const int spendVersion);
    PublicCoinSpend parseCoinSpend(const CTxIn &in);
    bool parseCoinSpend(const CTxIn &in, const CTransaction& tx, const CTxOut &prevOut, PublicCoinSpend& publicCoinSpend);
    bool validateInput(const CTxIn &in, const CTxOut &prevOut, const CTransaction& tx, PublicCoinSpend& ret);

    // Public zc spend parse
    /**
     *
     * @param in --> public zc spend input
     * @param tx --> input parent
     * @param publicCoinSpend ---> return the publicCoinSpend parsed
     * @return true if everything went ok
     */
    bool ParseEshadowPublicSpend(const CTxIn &in, const CTransaction& tx, CValidationState& state, PublicCoinSpend& publicCoinSpend);
};


#endif //OZEETY_EZEEMODULE_H

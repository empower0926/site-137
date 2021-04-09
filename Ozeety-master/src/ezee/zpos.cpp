// Copyright (c) 2017-2020 The OZEETY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ezee/zpos.h"
#include "ezeechain.h"


/*
 * LEGACY: Kept for IBD in order to verify eshadow stakes occurred when zPoS was active
 * Find the first occurrence of a certain accumulator checksum.
 * Return block index pointer or nullptr if not found
 */

uint32_t ParseAccChecksum(uint256 nCheckpoint, const libeshadow::CoinDenomination denom)
{
    int pos = std::distance(libeshadow::eshadowDenomList.begin(),
            find(libeshadow::eshadowDenomList.begin(), libeshadow::eshadowDenomList.end(), denom));
    nCheckpoint = nCheckpoint >> (32*((libeshadow::eshadowDenomList.size() - 1) - pos));
    return nCheckpoint.Get32();
}

bool CLegacyEZeeStake::InitFromTxIn(const CTxIn& txin)
{
    // Construct the stakeinput object
    if (!txin.IsEshadowSpend())
        return error("%s: unable to initialize CLegacyEZeeStake from non zc-spend", __func__);

    // Check spend type
    libeshadow::CoinSpend spend = TxInToEshadowSpend(txin);
    if (spend.getSpendType() != libeshadow::SpendType::STAKE)
        return error("%s : spend is using the wrong SpendType (%d)", __func__, (int)spend.getSpendType());

    *this = CLegacyEZeeStake(spend);

    // Find the pindex with the accumulator checksum
    if (!GetIndexFrom())
        return error("%s : Failed to find the block index for ezee stake origin", __func__);

    // All good
    return true;
}

CLegacyEZeeStake::CLegacyEZeeStake(const libeshadow::CoinSpend& spend)
{
    this->nChecksum = spend.getAccumulatorChecksum();
    this->denom = spend.getDenomination();
    uint256 nSerial = spend.getCoinSerialNumber().getuint256();
    this->hashSerial = Hash(nSerial.begin(), nSerial.end());
}

CBlockIndex* CLegacyEZeeStake::GetIndexFrom()
{
    // First look in the legacy database
    int nHeightChecksum = 0;
    if (eshadowDB->ReadAccChecksum(nChecksum, denom, nHeightChecksum)) {
        return chainActive[nHeightChecksum];
    }

    // Not found. Scan the chain.
    const Consensus::Params& consensus = Params().GetConsensus();
    CBlockIndex* pindex = chainActive[consensus.vUpgrades[Consensus::UPGRADE_ZC].nActivationHeight];
    if (!pindex) return nullptr;
    while (pindex && pindex->nHeight <= consensus.height_last_ZC_AccumCheckpoint) {
        if (ParseAccChecksum(pindex->nAccumulatorCheckpoint, denom) == nChecksum) {
            // Found. Save to database and return
            eshadowDB->WriteAccChecksum(nChecksum, denom, pindex->nHeight);
            return pindex;
        }
        //Skip forward in groups of 10 blocks since checkpoints only change every 10 blocks
        if (pindex->nHeight % 10 == 0) {
            pindex = chainActive[pindex->nHeight + 10];
            continue;
        }
        pindex = chainActive.Next(pindex);
    }
    return nullptr;
}

CAmount CLegacyEZeeStake::GetValue() const
{
    return denom * COIN;
}

CDataStream CLegacyEZeeStake::GetUniqueness() const
{
    CDataStream ss(SER_GETHASH, 0);
    ss << hashSerial;
    return ss;
}

// Verify stake contextual checks
bool CLegacyEZeeStake::ContextCheck(int nHeight, uint32_t nTime)
{
    const Consensus::Params& consensus = Params().GetConsensus();
    if (!consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_ZC_V2) || nHeight >= consensus.height_last_ZC_AccumCheckpoint)
        return error("%s : eZEE stake block: height %d outside range", __func__, nHeight);

    // The checkpoint needs to be from 200 blocks ago
    const int cpHeight = nHeight - 1 - consensus.ZC_MinStakeDepth;
    const libeshadow::CoinDenomination denom = libeshadow::AmountToEshadowDenomination(GetValue());
    if (ParseAccChecksum(chainActive[cpHeight]->nAccumulatorCheckpoint, denom) != GetChecksum())
        return error("%s : accum. checksum at height %d is wrong.", __func__, nHeight);

    // All good
    return true;
}

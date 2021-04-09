// Copyright (c) 2020 The OZEETY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OZEETY_CONSENSUS_ESHADOW_VERIFY_H
#define OZEETY_CONSENSUS_ESHADOW_VERIFY_H

#include "consensus/consensus.h"
#include "main.h"
#include "script/interpreter.h"
#include "ezeechain.h"

/** Context-independent validity checks */
bool CheckEshadowSpend(const CTransaction& tx, bool fVerifySignature, CValidationState& state, bool fFakeSerialAttack = false);
// Fake Serial attack Range
bool isBlockBetweenFakeSerialAttackRange(int nHeight);
// Public coin spend
bool CheckPublicCoinSpendEnforced(int blockHeight, bool isPublicSpend);
int CurrentPublicCoinSpendVersion();
bool CheckPublicCoinSpendVersion(int version);
bool ContextualCheckEshadowSpend(const CTransaction& tx, const libeshadow::CoinSpend* spend, int nHeight, const uint256& hashBlock);
bool ContextualCheckEshadowSpendNoSerialCheck(const CTransaction& tx, const libeshadow::CoinSpend* spend, int nHeight, const uint256& hashBlock);
bool RecalculateOZTGSupply(int nHeightStart, bool fSkipEzee = true);
CAmount GetInvalidUTXOValue();

#endif //OZEETY_CONSENSUS_ESHADOW_VERIFY_H

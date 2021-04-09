// Copyright (c) 2020 The OZEETY developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef VALIDATION_ESHADOW_LEGACY_H
#define VALIDATION_ESHADOW_LEGACY_H

#include "amount.h"
#include "primitives/transaction.h"
#include "txdb.h" // for the eshadowDB implementation.

bool AcceptToMemoryPoolEshadow(const CTransaction& tx, CAmount& nValueIn, int chainHeight, CValidationState& state, const Consensus::Params& consensus);
bool DisconnectEshadowTx(const CTransaction& tx, CAmount& nValueIn, CEshadowDB* eshadowDB);
void DataBaseAccChecksum(CBlockIndex* pindex, bool fWrite);

#endif //VALIDATION_ESHADOW_LEGACY_H

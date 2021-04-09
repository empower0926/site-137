// Copyright (c) 2018 The OZEETY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OZEETY_SPENDTYPE_H
#define OZEETY_SPENDTYPE_H

#include <cstdint>

namespace libeshadow {
    enum SpendType : uint8_t {
        SPEND, // Used for a typical spend transaction, eZEE should be unusable after
        STAKE, // Used for a spend that occurs as a stake
        MN_COLLATERAL, // Used when proving ownership of eZEE that will be used for masternodes (future)
        SIGN_MESSAGE // Used to sign messages that do not belong above (future)
    };
}

#endif //OZEETY_SPENDTYPE_H

/**
* @file       Eshadow.h
*
* @brief      Exceptions and constants for Eshadow
*
* @author     Ian Miers, Christina Garman and Matthew Green
* @date       June 2013
*
* @copyright  Copyright 2013 Ian Miers, Christina Garman and Matthew Green
* @license    This project is released under the MIT license.
**/
// Copyright (c) 2017 The OZEETY developers

#ifndef ESHADOW_DEFINES_H_
#define ESHADOW_DEFINES_H_

#include <stdexcept>

#define ESHADOW_DEFAULT_SECURITYLEVEL      80
#define ESHADOW_MIN_SECURITY_LEVEL         80
#define ESHADOW_MAX_SECURITY_LEVEL         80
#define ACCPROOF_KPRIME                     160
#define ACCPROOF_KDPRIME                    128
#define MAX_COINMINT_ATTEMPTS               10000
#define ESHADOW_MINT_PRIME_PARAM			20
#define ESHADOW_VERSION_STRING             "0.11"
#define ESHADOW_VERSION_INT				11
#define ESHADOW_PROTOCOL_VERSION           "1"
#define HASH_OUTPUT_BITS                    256
#define ESHADOW_COMMITMENT_EQUALITY_PROOF  "COMMITMENT_EQUALITY_PROOF"
#define ESHADOW_ACCUMULATOR_PROOF          "ACCUMULATOR_PROOF"
#define ESHADOW_SERIALNUMBER_PROOF         "SERIALNUMBER_PROOF"

// Activate multithreaded mode for proof verification
#define ESHADOW_THREADING 1

// Uses a fast technique for coin generation. Could be more vulnerable
// to timing attacks. Turn off if an attacker can measure coin minting time.
#define	ESHADOW_FAST_MINT 1

#endif /* ESHADOW_H_ */

// Copyright (c) 2020 The OZEETY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/wallet.h"

#include "init.h"
#include "coincontrol.h"
#include "consensus/eshadow_verify.h"
#include "denomination_functions.h"
#include "primitives/transaction.h"
#include "script/sign.h"
#include "utilmoneystr.h"
#include "ezeechain.h"
#include "ezee/deterministicmint.h"


/*
 * Legacy Eshadow Wallet
 */

bool CWallet::AddDeterministicSeed(const uint256& seed)
{
    CWalletDB db(strWalletFile);
    std::string strErr;
    uint256 hashSeed = Hash(seed.begin(), seed.end());

    if(IsCrypted()) {
        if (!IsLocked()) { //if we have password

            CKeyingMaterial kmSeed(seed.begin(), seed.end());
            std::vector<unsigned char> vchSeedSecret;


            //attempt encrypt
            if (EncryptSecret(vMasterKey, kmSeed, hashSeed, vchSeedSecret)) {
                //write to wallet with hashSeed as unique key
                if (db.WriteEZEESeed(hashSeed, vchSeedSecret)) {
                    return true;
                }
            }
            strErr = "encrypt seed";
        }
        strErr = "save since wallet is locked";
    } else { //wallet not encrypted
        if (db.WriteEZEESeed(hashSeed, ToByteVector(seed))) {
            return true;
        }
        strErr = "save ezeeseed to wallet";
    }
    //the use case for this is no password set seed, mint deZEE,

    return error("s%: Failed to %s\n", __func__, strErr);
}

bool CWallet::GetDeterministicSeed(const uint256& hashSeed, uint256& seedOut)
{

    CWalletDB db(strWalletFile);
    std::string strErr;
    if (IsCrypted()) {
        if(!IsLocked()) { //if we have password

            std::vector<unsigned char> vchCryptedSeed;
            //read encrypted seed
            if (db.ReadEZEESeed(hashSeed, vchCryptedSeed)) {
                uint256 seedRetrieved = uint256S(ReverseEndianString(HexStr(vchCryptedSeed)));
                //this checks if the hash of the seed we just read matches the hash given, meaning it is not encrypted
                //the use case for this is when not crypted, seed is set, then password set, the seed not yet crypted in memory
                if(hashSeed == Hash(seedRetrieved.begin(), seedRetrieved.end())) {
                    seedOut = seedRetrieved;
                    return true;
                }

                CKeyingMaterial kmSeed;
                //attempt decrypt
                if (DecryptSecret(vMasterKey, vchCryptedSeed, hashSeed, kmSeed)) {
                    seedOut = uint256S(ReverseEndianString(HexStr(kmSeed)));
                    return true;
                }
                strErr = "decrypt seed";
            } else { strErr = "read seed from wallet"; }
        } else { strErr = "read seed; wallet is locked"; }
    } else {
        std::vector<unsigned char> vchSeed;
        // wallet not crypted
        if (db.ReadEZEESeed(hashSeed, vchSeed)) {
            seedOut = uint256S(ReverseEndianString(HexStr(vchSeed)));
            return true;
        }
        strErr = "read seed from wallet";
    }

    return error("%s: Failed to %s\n", __func__, strErr);
}

void CWallet::doEZeeRescan(const CBlockIndex* pindex, const CBlock& block,
        std::set<uint256>& setAddedToWallet, const Consensus::Params& consensus, bool fCheckEZEE)
{
    //If this is a zapwallettx, need to read ezee
    if (fCheckEZEE && consensus.NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_ZC)) {
        std::list<CEshadowMint> listMints;
        BlockToEshadowMintList(block, listMints, true);
        CWalletDB walletdb(strWalletFile);

        for (auto& m : listMints) {
            if (IsMyMint(m.GetValue())) {
                LogPrint(BCLog::LEGACYZC, "%s: found mint\n", __func__);
                UpdateMint(m.GetValue(), pindex->nHeight, m.GetTxHash(), m.GetDenomination());

                // Add the transaction to the wallet
                for (auto& tx : block.vtx) {
                    uint256 txid = tx.GetHash();
                    if (setAddedToWallet.count(txid) || mapWallet.count(txid))
                        continue;
                    if (txid == m.GetTxHash()) {
                        CWalletTx wtx(this, tx);
                        wtx.nTimeReceived = block.GetBlockTime();
                        wtx.SetMerkleBranch(block);
                        AddToWallet(wtx, &walletdb);
                        setAddedToWallet.insert(txid);
                    }
                }

                //Check if the mint was ever spent
                int nHeightSpend = 0;
                uint256 txidSpend;
                CTransaction txSpend;
                if (IsSerialInBlockchain(GetSerialHash(m.GetSerialNumber()), nHeightSpend, txidSpend, txSpend)) {
                    if (setAddedToWallet.count(txidSpend) || mapWallet.count(txidSpend))
                        continue;

                    CWalletTx wtx(this, txSpend);
                    CBlockIndex* pindexSpend = chainActive[nHeightSpend];
                    CBlock blockSpend;
                    if (ReadBlockFromDisk(blockSpend, pindexSpend))
                        wtx.SetMerkleBranch(blockSpend);

                    wtx.nTimeReceived = pindexSpend->nTime;
                    AddToWallet(wtx, &walletdb);
                    setAddedToWallet.emplace(txidSpend);
                }
            }
        }
    }
}

//- ZC Mints (Only for regtest)

std::string CWallet::MintEshadow(CAmount nValue, CWalletTx& wtxNew, std::vector<CDeterministicMint>& vDMints, const CCoinControl* coinControl)
{
    if (!Params().IsRegTestNet())
        return _("Eshadow minting available only on regtest");

    // Check amount
    if (nValue <= 0)
        return _("Invalid amount");

    CAmount nBalance = GetBalance();
    const CAmount& nFee = Params().GetConsensus().ZC_MinMintFee;
    if (nValue + nFee > nBalance) {
        LogPrintf("%s: balance=%s fee=%s nValue=%s\n", __func__, FormatMoney(nBalance), FormatMoney(nFee), FormatMoney(nValue));
        return _("Insufficient funds");
    }

    if (IsLocked()) {
        std::string strError = _("Error: Wallet locked, unable to create transaction!");
        LogPrintf("MintEshadow() : %s", strError.c_str());
        return strError;
    }

    std::string strError;
    CReserveKey reservekey(this);
    CMutableTransaction txNew;
    if (!CreateEshadowMintTransaction(nValue, txNew, vDMints, &reservekey, strError, coinControl)) {
        return strError;
    }

    wtxNew = CWalletTx(this, txNew);
    wtxNew.fFromMe = true;
    wtxNew.fTimeReceivedIsTxTime = true;

    // Limit size
    unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
    if (nBytes >= MAX_ESHADOW_TX_SIZE) {
        return _("Error: The transaction is larger than the maximum allowed transaction size!");
    }

    //commit the transaction to the network
    const CWallet::CommitResult& res = CommitTransaction(wtxNew, reservekey);
    if (res.status != CWallet::CommitStatus::OK) {
        return res.ToString();
    } else {
        //update mints with full transaction hash and then database them
        CWalletDB walletdb(strWalletFile);
        for (CDeterministicMint dMint : vDMints) {
            dMint.SetTxHash(wtxNew.GetHash());
            ezeeTracker->Add(dMint, true);
        }
    }

    return "";
}

std::string CWallet::MintEshadowFromOutPoint(CAmount nValue, CWalletTx& wtxNew, std::vector<CDeterministicMint>& vDMints, const std::vector<COutPoint> vOutpts)
{
    CCoinControl* coinControl = new CCoinControl();
    for (const COutPoint& outpt : vOutpts) {
        coinControl->Select(outpt);
    }
    if (!coinControl->HasSelected()){
        std::string strError = _("Error: No valid utxo!");
        LogPrintf("MintEshadow() : %s", strError.c_str());
        return strError;
    }
    std::string strError = MintEshadow(nValue, wtxNew, vDMints, coinControl);
    delete coinControl;
    return strError;
}

bool CWallet::CreateEZEEOutPut(libeshadow::CoinDenomination denomination, CTxOut& outMint, CDeterministicMint& dMint)
{
    // mint a new coin (create Pedersen Commitment) and extract PublicCoin that is shareable from it
    libeshadow::PrivateCoin coin(Params().GetConsensus().Eshadow_Params(false), denomination, false);
    zwalletMain->GenerateDeterministicEZEE(denomination, coin, dMint);

    libeshadow::PublicCoin pubCoin = coin.getPublicCoin();

    // Validate
    if(!pubCoin.validate())
        return error("%s: newly created pubcoin is not valid", __func__);

    zwalletMain->UpdateCount();

    CScript scriptSerializedCoin = CScript() << OP_ESHADOWMINT << pubCoin.getValue().getvch().size() << pubCoin.getValue().getvch();
    outMint = CTxOut(libeshadow::EshadowDenominationToAmount(denomination), scriptSerializedCoin);

    return true;
}

// Given a set of inputs, find the public key that contributes the most coins to the input set
CScript GetLargestContributor(std::set<std::pair<const CWalletTx*, unsigned int> >& setCoins)
{
    std::map<CScript, CAmount> mapScriptsOut;
    for (const std::pair<const CWalletTx*, unsigned int>& coin : setCoins) {
        CTxOut out = coin.first->vout[coin.second];
        mapScriptsOut[out.scriptPubKey] += out.nValue;
    }

    CScript scriptLargest;
    CAmount nLargestContributor = 0;
    for (auto it : mapScriptsOut) {
        if (it.second > nLargestContributor) {
            scriptLargest = it.first;
            nLargestContributor = it.second;
        }
    }

    return scriptLargest;
}

bool CWallet::CreateEshadowMintTransaction(const CAmount nValue,
                                            CMutableTransaction& txNew,
                                            std::vector<CDeterministicMint>& vDMints,
                                            CReserveKey* reservekey,
                                            std::string& strFailReason,
                                            const CCoinControl* coinControl)
{
    if (IsLocked()) {
        strFailReason = _("Error: Wallet locked, unable to create transaction!");
        LogPrintf("SpendEshadow() : %s", strFailReason.c_str());
        return false;
    }

    //add multiple mints that will fit the amount requested as closely as possible
    CAmount nMintingValue = 0;
    CAmount nValueRemaining = 0;
    while (true) {
        //mint a coin with the closest denomination to what is being requested
        nValueRemaining = nValue - nMintingValue;

        libeshadow::CoinDenomination denomination = libeshadow::AmountToClosestDenomination(nValueRemaining, nValueRemaining);
        if (denomination == libeshadow::ZQ_ERROR)
            break;

        CAmount nValueNewMint = libeshadow::EshadowDenominationToAmount(denomination);
        nMintingValue += nValueNewMint;

        CTxOut outMint;
        CDeterministicMint dMint;
        if (!CreateEZEEOutPut(denomination, outMint, dMint)) {
            strFailReason = strprintf("%s: failed to create new ezee output", __func__);
            return error(strFailReason.c_str());
        }
        txNew.vout.push_back(outMint);

        //store as CEshadowMint for later use
        LogPrint(BCLog::LEGACYZC, "%s: new mint %s\n", __func__, dMint.ToString());
        vDMints.emplace_back(dMint);
    }

    // calculate fee
    CAmount nTotalValue = nValue + Params().GetConsensus().ZC_MinMintFee * txNew.vout.size();

    // Get the available coins
    std::vector<COutput> vAvailableCoins;
    AvailableCoins(&vAvailableCoins, coinControl);

    CAmount nValueIn = 0;
    std::set<std::pair<const CWalletTx*, unsigned int> > setCoins;
    // select UTXO's to use
    if (!SelectCoinsToSpend(vAvailableCoins, nTotalValue, setCoins, nValueIn, coinControl)) {
        strFailReason = _("Insufficient or insufficient confirmed funds, you might need to wait a few minutes and try again.");
        return false;
    }

    // Fill vin
    for (const std::pair<const CWalletTx*, unsigned int>& coin : setCoins)
        txNew.vin.push_back(CTxIn(coin.first->GetHash(), coin.second));


    //any change that is less than 0.0100000 will be ignored and given as an extra fee
    //also assume that a eshadowspend that is minting the change will not have any change that goes to Oztg
    CAmount nChange = nValueIn - nTotalValue; // Fee already accounted for in nTotalValue
    if (nChange > 1 * CENT) {
        // Fill a vout to ourself using the largest contributing address
        CScript scriptChange = GetLargestContributor(setCoins);

        //add to the transaction
        CTxOut outChange(nChange, scriptChange);
        txNew.vout.push_back(outChange);
    } else if (reservekey) {
        reservekey->ReturnKey();
    }

    // Sign
    int nIn = 0;
    for (const std::pair<const CWalletTx*, unsigned int>& coin : setCoins) {
        if (!SignSignature(*this, *coin.first, txNew, nIn++)) {
            strFailReason = _("Signing transaction failed");
            return false;
        }
    }

    return true;
}

// - ZC PublicSpends

bool CWallet::SpendEshadow(CAmount nAmount, CWalletTx& wtxNew, CEshadowSpendReceipt& receipt, std::vector<CEshadowMint>& vMintsSelected,
        std::list<std::pair<CTxDestination, CAmount>> addressesTo, CTxDestination* changeAddress)
{
    // Default: assume something goes wrong. Depending on the problem this gets more specific below
    int nStatus = EZEE_SPEND_ERROR;

    if (IsLocked()) {
        receipt.SetStatus("Error: Wallet locked, unable to create transaction!", EZEE_WALLET_LOCKED);
        return false;
    }

    CReserveKey reserveKey(this);
    std::vector<CDeterministicMint> vNewMints;
    if (!CreateZCPublicSpendTransaction(
            nAmount,
            wtxNew,
            reserveKey,
            receipt,
            vMintsSelected,
            vNewMints,
            addressesTo,
            changeAddress
    )) {
        return false;
    }


    CWalletDB walletdb(strWalletFile);
    const CWallet::CommitResult& res = CommitTransaction(wtxNew, reserveKey);
    if (res.status != CWallet::CommitStatus::OK) {
        LogPrintf("%s: failed to commit\n", __func__);
        nStatus = EZEE_COMMIT_FAILED;

        //reset all mints
        for (CEshadowMint mint : vMintsSelected) {
            uint256 hashPubcoin = GetPubCoinHash(mint.GetValue());
            ezeeTracker->SetPubcoinNotUsed(hashPubcoin);
            NotifyEshadowChanged(this, mint.GetValue().GetHex(), "New", CT_UPDATED);
        }

        //erase spends
        for (CEshadowSpend spend : receipt.GetSpends()) {
            if (!walletdb.EraseEshadowSpendSerialEntry(spend.GetSerial())) {
                receipt.SetStatus("Error: It cannot delete coin serial number in wallet", EZEE_ERASE_SPENDS_FAILED);
            }

            //Remove from public eshadowDB
            RemoveSerialFromDB(spend.GetSerial());
        }

        // erase new mints
        for (auto& dMint : vNewMints) {
            if (!walletdb.EraseDeterministicMint(dMint.GetPubcoinHash())) {
                receipt.SetStatus("Error: Unable to cannot delete eshadow mint in wallet", EZEE_ERASE_NEW_MINTS_FAILED);
            }
        }

        receipt.SetStatus("Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.", nStatus);
        return false;
    }

    //Set spent mints as used
    uint256 txidSpend = wtxNew.GetHash();
    for (CEshadowMint mint : vMintsSelected) {
        uint256 hashPubcoin = GetPubCoinHash(mint.GetValue());
        ezeeTracker->SetPubcoinUsed(hashPubcoin, txidSpend);

        CMintMeta metaCheck = ezeeTracker->GetMetaFromPubcoin(hashPubcoin);
        if (!metaCheck.isUsed) {
            receipt.SetStatus("Error, the mint did not get marked as used", nStatus);
            return false;
        }
    }

    // write new Mints to db
    for (auto& dMint : vNewMints) {
        dMint.SetTxHash(txidSpend);
        ezeeTracker->Add(dMint, true);
    }

    receipt.SetStatus("Spend Successful", EZEE_SPEND_OKAY);  // When we reach this point spending eZEE was successful

    return true;
}

bool CWallet::MintsToInputVectorPublicSpend(std::map<CBigNum, CEshadowMint>& mapMintsSelected, const uint256& hashTxOut, std::vector<CTxIn>& vin,
                                    CEshadowSpendReceipt& receipt, libeshadow::SpendType spendType, CBlockIndex* pindexCheckpoint)
{
    // Default error status if not changed below
    receipt.SetStatus(_("Transaction Mint Started"), EZEE_TXMINT_GENERAL);

    // Get the chain tip to determine the active public spend version
    int nHeight = 0;
    {
        LOCK(cs_main);
        nHeight = chainActive.Height();
    }
    if (!nHeight)
        return error("%s: Unable to get chain tip height", __func__);

    int spendVersion = CurrentPublicCoinSpendVersion();

    for (auto &it : mapMintsSelected) {
        CEshadowMint mint = it.second;

        // Create the simple input and the scriptSig -> Serial + Randomness + Private key signature of both.
        // As the mint doesn't have the output index search it..
        CTransaction txMint;
        uint256 hashBlock;
        if (!GetTransaction(mint.GetTxHash(), txMint, hashBlock)) {
            receipt.SetStatus(strprintf(_("Unable to find transaction containing mint %s"), mint.GetTxHash().GetHex()), EZEE_TXMINT_GENERAL);
            return false;
        } else if (mapBlockIndex.count(hashBlock) < 1) {
            // check that this mint made it into the blockchain
            receipt.SetStatus(_("Mint did not make it into blockchain"), EZEE_TXMINT_GENERAL);
            return false;
        }

        int outputIndex = -1;
        for (unsigned long i = 0; i < txMint.vout.size(); ++i) {
            CTxOut out = txMint.vout[i];
            if (out.scriptPubKey.IsEshadowMint()){
                libeshadow::PublicCoin pubcoin(Params().GetConsensus().Eshadow_Params(false));
                CValidationState state;
                if (!TxOutToPublicCoin(out, pubcoin, state))
                    return error("%s: extracting pubcoin from txout failed", __func__);

                if (pubcoin.getValue() == mint.GetValue()){
                    outputIndex = i;
                    break;
                }
            }
        }

        if (outputIndex == -1) {
            receipt.SetStatus(_("Pubcoin not found in mint tx"), EZEE_TXMINT_GENERAL);
            return false;
        }

        mint.SetOutputIndex(outputIndex);
        CTxIn in;
        if(!EZEEModule::createInput(in, mint, hashTxOut, spendVersion)) {
            receipt.SetStatus(_("Cannot create public spend input"), EZEE_TXMINT_GENERAL);
            return false;
        }
        vin.emplace_back(in);
        receipt.AddSpend(CEshadowSpend(mint.GetSerialNumber(), UINT256_ZERO, mint.GetValue(), mint.GetDenomination(), 0));
    }

    receipt.SetStatus(_("Spend Valid"), EZEE_SPEND_OKAY); // Everything okay

    return true;
}

bool CWallet::CreateZCPublicSpendTransaction(
        CAmount nValue,
        CWalletTx& wtxNew,
        CReserveKey& reserveKey,
        CEshadowSpendReceipt& receipt,
        std::vector<CEshadowMint>& vSelectedMints,
        std::vector<CDeterministicMint>& vNewMints,
        std::list<std::pair<CTxDestination,CAmount>> addressesTo,
        CTxDestination * changeAddress)
{
    // Check available funds
    int nStatus = EZEE_TRX_FUNDS_PROBLEMS;
    if (nValue > GetEshadowBalance(true)) {
        receipt.SetStatus(_("You don't have enough Eshadows in your wallet"), nStatus);
        return false;
    }

    if (nValue < 1) {
        receipt.SetStatus(_("Value is below the smallest available denomination (= 1) of eZEE"), nStatus);
        return false;
    }

    // Create transaction
    nStatus = EZEE_TRX_CREATE;

    // If not already given pre-selected mints, then select mints from the wallet
    CWalletDB walletdb(strWalletFile);
    std::set<CMintMeta> setMints;
    CAmount nValueSelected = 0;
    int nCoinsReturned = 0; // Number of coins returned in change from function below (for debug)
    int nNeededSpends = 0;  // Number of spends which would be needed if selection failed
    const int nMaxSpends = Params().GetConsensus().ZC_MaxPublicSpendsPerTx; // Maximum possible spends for one eZEE public spend transaction
    std::vector<CMintMeta> vMintsToFetch;
    if (vSelectedMints.empty()) {
        //  All of the eZEE used in the public coin spend are mature by default (everything is public now.. no need to wait for any accumulation)
        setMints = ezeeTracker->ListMints(true, false, true, true); // need to find mints to spend
        if(setMints.empty()) {
            receipt.SetStatus(_("Failed to find Eshadows in wallet.dat"), nStatus);
            return false;
        }

        // If the input value is not an int, then we want the selection algorithm to round up to the next highest int
        double dValue = static_cast<double>(nValue) / static_cast<double>(COIN);
        bool fWholeNumber = floor(dValue) == dValue;
        CAmount nValueToSelect = nValue;
        if(!fWholeNumber)
            nValueToSelect = static_cast<CAmount>(ceil(dValue) * COIN);

        // Select the eZEE mints to use in this spend
        std::map<libeshadow::CoinDenomination, CAmount> DenomMap = GetMyEshadowDistribution();
        std::list<CMintMeta> listMints(setMints.begin(), setMints.end());
        vMintsToFetch = SelectMintsFromList(nValueToSelect, nValueSelected, nMaxSpends,
                                             nCoinsReturned, listMints, DenomMap, nNeededSpends);
        for (auto& meta : vMintsToFetch) {
            CEshadowMint mint;
            if (!GetMint(meta.hashSerial, mint))
                return error("%s: failed to fetch hashSerial %s", __func__, meta.hashSerial.GetHex());
            vSelectedMints.emplace_back(mint);
        }
    } else {
        unsigned int mintsCount = 0;
        for (const CEshadowMint& mint : vSelectedMints) {
            if (nValueSelected < nValue) {
                nValueSelected += EshadowDenominationToAmount(mint.GetDenomination());
                mintsCount ++;
            }
            else
                break;
        }
        if (mintsCount < vSelectedMints.size()) {
            vSelectedMints.resize(mintsCount);
        }
    }

    int nArchived = 0;
    for (CEshadowMint mint : vSelectedMints) {
        // see if this serial has already been spent
        int nHeightSpend;
        if (IsSerialInBlockchain(mint.GetSerialNumber(), nHeightSpend)) {
            receipt.SetStatus(_("Trying to spend an already spent serial #, try again."), nStatus);
            uint256 hashSerial = GetSerialHash(mint.GetSerialNumber());
            if (!ezeeTracker->HasSerialHash(hashSerial))
                return error("%s: tracker does not have serialhash %s", __func__, hashSerial.GetHex());

            CMintMeta meta = ezeeTracker->Get(hashSerial);
            meta.isUsed = true;
            ezeeTracker->UpdateState(meta);

            return false;
        }

        //check that this mint made it into the blockchain
        CTransaction txMint;
        uint256 hashBlock;
        bool fArchive = false;
        if (!GetTransaction(mint.GetTxHash(), txMint, hashBlock)) {
            receipt.SetStatus(strprintf(_("Unable to find transaction containing mint, txHash: %s"), mint.GetTxHash().GetHex()), nStatus);
            fArchive = true;
        } else if (mapBlockIndex.count(hashBlock) < 1) {
            receipt.SetStatus(_("Mint did not make it into blockchain"), nStatus);
            fArchive = true;
        }

        // archive this mint as an orphan
        if (fArchive) {
            //walletdb.ArchiveMintOrphan(mint);
            //nArchived++;
            //todo
        }
    }
    if (nArchived)
        return false;

    if (vSelectedMints.empty()) {
        if(nNeededSpends > 0){
            // Too much spends needed, so abuse nStatus to report back the number of needed spends
            receipt.SetStatus(_("Too many spends needed"), nStatus, nNeededSpends);
        }
        else {
            receipt.SetStatus(_("Failed to select a eshadow"), nStatus);
        }
        return false;
    }


    if (static_cast<int>(vSelectedMints.size()) > nMaxSpends) {
        receipt.SetStatus(_("Failed to find coin set amongst held coins with less than maxNumber of Spends"), nStatus);
        return false;
    }


    // Create change if needed
    nStatus = EZEE_TRX_CHANGE;

    CMutableTransaction txNew;
    wtxNew.BindWallet(this);
    {
        LOCK2(cs_main, cs_wallet);
        {
            txNew.vin.clear();
            txNew.vout.clear();

            CAmount nChange = nValueSelected - nValue;

            if (nChange < 0) {
                receipt.SetStatus(_("Selected coins value is less than payment target"), nStatus);
                return false;
            }

            if (nChange > 0 && !changeAddress && addressesTo.size() == 0) {
                receipt.SetStatus(_("Need destination or change address because change is not exact"), nStatus);
                return false;
            }

            //if there are addresses to send to then use them, if not generate a new address to send to
            CTxDestination destinationAddr;
            if (addressesTo.size() == 0) {
                CPubKey pubkey;
                assert(reserveKey.GetReservedKey(pubkey)); // should never fail
                destinationAddr = pubkey.GetID();
                addressesTo.push_back(std::make_pair(destinationAddr, nValue));
            }

            for (std::pair<CTxDestination,CAmount> pair : addressesTo){
                CScript scriptEshadowSpend = GetScriptForDestination(pair.first);
                //add output to ozeety address to the transaction (the actual primary spend taking place)
                // TODO: check value?
                CTxOut txOutEshadowSpend(pair.second, scriptEshadowSpend);
                txNew.vout.push_back(txOutEshadowSpend);
            }

            //add change output if we are spending too much (only applies to spending multiple at once)
            if (nChange) {
                CScript scriptChange;
                // Change address
                if(changeAddress){
                    scriptChange = GetScriptForDestination(*changeAddress);
                } else {
                    // Reserve a new key pair from key pool
                    CPubKey vchPubKey;
                    assert(reserveKey.GetReservedKey(vchPubKey)); // should never fail
                    scriptChange = GetScriptForDestination(vchPubKey.GetID());
                }
                //mint change as eshadows
                CTxOut txOutChange(nValueSelected - nValue, scriptChange);
                txNew.vout.push_back(txOutChange);
            }

            //hash with only the output info in it to be used in Signature of Knowledge
            // and in CoinRandomness Schnorr Signature
            uint256 hashTxOut = txNew.GetHash();

            CBlockIndex* pindexCheckpoint = nullptr;
            std::map<CBigNum, CEshadowMint> mapSelectedMints;
            for (const CEshadowMint& mint : vSelectedMints)
                mapSelectedMints.insert(std::make_pair(mint.GetValue(), mint));

            //add all of the mints to the transaction as inputs
            std::vector<CTxIn> vin;
            if (!MintsToInputVectorPublicSpend(mapSelectedMints, hashTxOut, vin, receipt,
                                               libeshadow::SpendType::SPEND, pindexCheckpoint))
                return false;
            txNew.vin = vin;

            // Limit size
            unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
            if (nBytes >= MAX_ESHADOW_TX_SIZE) {
                receipt.SetStatus(_("In rare cases, a spend with 7 coins exceeds our maximum allowable transaction size, please retry spend using 6 or less coins"), EZEE_TX_TOO_LARGE);
                return false;
            }

            //now that all inputs have been added, add full tx hash to eshadowspend records and write to db
            uint256 txHash = txNew.GetHash();
            for (CEshadowSpend spend : receipt.GetSpends()) {
                spend.SetTxHash(txHash);

                if (!CWalletDB(strWalletFile).WriteEshadowSpendSerialEntry(spend)) {
                    receipt.SetStatus(_("Failed to write coin serial number into wallet"), nStatus);
                }
            }

            //turn the finalized transaction into a wallet transaction
            wtxNew = CWalletTx(this, txNew);
            wtxNew.fFromMe = true;
            wtxNew.fTimeReceivedIsTxTime = true;
            wtxNew.nTimeReceived = GetAdjustedTime();
        }
    }

    receipt.SetStatus(_("Transaction Created"), EZEE_SPEND_OKAY); // Everything okay

    return true;
}

// - ZC Balances

CAmount CWallet::GetEshadowBalance(bool fMatureOnly) const
{
    if (fMatureOnly) {
        // This code is not removed just for when we back to use eZEE in the future, for now it's useless,
        // every public coin spend is now spendable without need to have new mints on top.

        //if (chainActive.Height() > nLastMaturityCheck)
        //nLastMaturityCheck = chainActive.Height();

        CAmount nBalance = 0;
        std::vector<CMintMeta> vMints = ezeeTracker->GetMints(true);
        for (auto meta : vMints) {
            // Every public coin spend is now spendable, no need to mint new coins on top.
            //if (meta.nHeight >= mapMintMaturity.at(meta.denom) || meta.nHeight >= chainActive.Height() || meta.nHeight == 0)
            //    continue;
            nBalance += libeshadow::EshadowDenominationToAmount(meta.denom);
        }
        return nBalance;
    }

    return ezeeTracker->GetBalance(false, false);
}

CAmount CWallet::GetUnconfirmedEshadowBalance() const
{
    return ezeeTracker->GetUnconfirmedBalance();
}

CAmount CWallet::GetImmatureEshadowBalance() const
{
    return GetEshadowBalance(false) - GetEshadowBalance(true) - GetUnconfirmedEshadowBalance();
}

// Get a Map pairing the Denominations with the amount of Eshadow for each Denomination
std::map<libeshadow::CoinDenomination, CAmount> CWallet::GetMyEshadowDistribution() const
{
    std::map<libeshadow::CoinDenomination, CAmount> spread;
    for (const auto& denom : libeshadow::eshadowDenomList)
        spread.insert(std::pair<libeshadow::CoinDenomination, CAmount>(denom, 0));
    {
        LOCK(cs_wallet);
        std::set<CMintMeta> setMints = ezeeTracker->ListMints(true, true, true);
        for (auto& mint : setMints)
            spread.at(mint.denom)++;
    }
    return spread;
}

// eZEE wallet

void CWallet::setZWallet(CeZEEWallet* zwallet)
{
    zwalletMain = zwallet;
    ezeeTracker = std::unique_ptr<CeZEETracker>(new CeZEETracker(this));
}

CeZEEWallet* CWallet::getZWallet()
{
    return zwalletMain;
}

bool CWallet::IsMyEshadowSpend(const CBigNum& bnSerial) const
{
    return ezeeTracker->HasSerial(bnSerial);
}

bool CWallet::IsMyMint(const CBigNum& bnValue) const
{
    if (ezeeTracker->HasPubcoin(bnValue))
        return true;

    return zwalletMain->IsInMintPool(bnValue);
}

std::string CWallet::ResetMintEshadow()
{
    long updates = 0;
    long deletions = 0;
    CWalletDB walletdb(strWalletFile);

    std::set<CMintMeta> setMints = ezeeTracker->ListMints(false, false, true);
    std::vector<CMintMeta> vMintsToFind(setMints.begin(), setMints.end());
    std::vector<CMintMeta> vMintsMissing;
    std::vector<CMintMeta> vMintsToUpdate;

    // search all of our available data for these mints
    FindMints(vMintsToFind, vMintsToUpdate, vMintsMissing);

    // Update the meta data of mints that were marked for updating
    for (CMintMeta meta : vMintsToUpdate) {
        updates++;
        ezeeTracker->UpdateState(meta);
    }

    // Delete any mints that were unable to be located on the blockchain
    for (CMintMeta mint : vMintsMissing) {
        deletions++;
        if (!ezeeTracker->Archive(mint))
            LogPrintf("%s: failed to archive mint\n", __func__);
    }

    NotifyeZEEReset();

    std::string strResult = _("ResetMintEshadow finished: ") + std::to_string(updates) + _(" mints updated, ") + std::to_string(deletions) + _(" mints deleted\n");
    return strResult;
}

std::string CWallet::ResetSpentEshadow()
{
    long removed = 0;
    CWalletDB walletdb(strWalletFile);

    std::set<CMintMeta> setMints = ezeeTracker->ListMints(false, false, true);
    std::list<CEshadowSpend> listSpends = walletdb.ListSpentCoins();
    std::list<CEshadowSpend> listUnconfirmedSpends;

    for (CEshadowSpend spend : listSpends) {
        CTransaction tx;
        uint256 hashBlock = UINT256_ZERO;
        if (!GetTransaction(spend.GetTxHash(), tx, hashBlock)) {
            listUnconfirmedSpends.push_back(spend);
            continue;
        }

        //no confirmations
        if (hashBlock.IsNull())
            listUnconfirmedSpends.push_back(spend);
    }

    for (CEshadowSpend spend : listUnconfirmedSpends) {
        for (CMintMeta meta : setMints) {
            if (meta.hashSerial == GetSerialHash(spend.GetSerial())) {
                removed++;
                meta.isUsed = false;
                ezeeTracker->UpdateState(meta);
                walletdb.EraseEshadowSpendSerialEntry(spend.GetSerial());
                continue;
            }
        }
    }

    NotifyeZEEReset();

    std::string strResult = _("ResetSpentEshadow finished: ") + std::to_string(removed) + _(" unconfirmed transactions removed\n");
    return strResult;
}

bool IsMintInChain(const uint256& hashPubcoin, uint256& txid, int& nHeight)
{
    if (!IsPubcoinInBlockchain(hashPubcoin, txid))
        return false;

    uint256 hashBlock;
    CTransaction tx;
    if (!GetTransaction(txid, tx, hashBlock))
        return false;

    if (!mapBlockIndex.count(hashBlock) || !chainActive.Contains(mapBlockIndex.at(hashBlock)))
        return false;

    nHeight = mapBlockIndex.at(hashBlock)->nHeight;
    return true;
}

void CWallet::ReconsiderEshadows(std::list<CEshadowMint>& listMintsRestored, std::list<CDeterministicMint>& listDMintsRestored)
{
    CWalletDB walletdb(strWalletFile);
    std::list<CEshadowMint> listMints = walletdb.ListArchivedEshadows();
    std::list<CDeterministicMint> listDMints = walletdb.ListArchivedDeterministicMints();

    if (listMints.empty() && listDMints.empty())
        return;

    for (CEshadowMint mint : listMints) {
        uint256 txid;
        int nHeight;
        uint256 hashPubcoin = GetPubCoinHash(mint.GetValue());
        if (!IsMintInChain(hashPubcoin, txid, nHeight))
            continue;

        mint.SetTxHash(txid);
        mint.SetHeight(nHeight);
        mint.SetUsed(IsSerialInBlockchain(mint.GetSerialNumber(), nHeight));

        if (!ezeeTracker->UnArchive(hashPubcoin, false)) {
            LogPrintf("%s : failed to unarchive mint %s\n", __func__, mint.GetValue().GetHex());
        } else {
            ezeeTracker->UpdateEshadowMint(mint);
        }
        listMintsRestored.emplace_back(mint);
    }

    for (CDeterministicMint dMint : listDMints) {
        uint256 txid;
        int nHeight;
        if (!IsMintInChain(dMint.GetPubcoinHash(), txid, nHeight))
            continue;

        dMint.SetTxHash(txid);
        dMint.SetHeight(nHeight);
        uint256 txidSpend;
        dMint.SetUsed(IsSerialInBlockchain(dMint.GetSerialHash(), nHeight, txidSpend));

        if (!ezeeTracker->UnArchive(dMint.GetPubcoinHash(), true)) {
            LogPrintf("%s : failed to unarchive deterministic mint %s\n", __func__, dMint.GetPubcoinHash().GetHex());
        } else {
            ezeeTracker->Add(dMint, true);
        }
        listDMintsRestored.emplace_back(dMint);
    }
}

bool CWallet::GetEshadowKey(const CBigNum& bnSerial, CKey& key)
{
    CWalletDB walletdb(strWalletFile);
    CEshadowMint mint;
    if (!GetMint(GetSerialHash(bnSerial), mint))
        return error("%s: could not find serial %s in walletdb!", __func__, bnSerial.GetHex());

    return mint.GetKeyPair(key);
}

bool CWallet::GetMint(const uint256& hashSerial, CEshadowMint& mint)
{
    if (!ezeeTracker->HasSerialHash(hashSerial))
        return error("%s: serialhash %s is not in tracker", __func__, hashSerial.GetHex());

    CWalletDB walletdb(strWalletFile);
    CMintMeta meta = ezeeTracker->Get(hashSerial);
    if (meta.isDeterministic) {
        CDeterministicMint dMint;
        if (!walletdb.ReadDeterministicMint(meta.hashPubcoin, dMint))
            return error("%s: failed to read deterministic mint", __func__);
        if (!zwalletMain->RegenerateMint(dMint, mint))
            return error("%s: failed to generate mint", __func__);

        return true;
    } else if (!walletdb.ReadEshadowMint(meta.hashPubcoin, mint)) {
        return error("%s: failed to read eshadowmint from database", __func__);
    }

    return true;
}

bool CWallet::GetMintFromStakeHash(const uint256& hashStake, CEshadowMint& mint)
{
    CMintMeta meta;
    if (!ezeeTracker->GetMetaFromStakeHash(hashStake, meta))
        return error("%s: failed to find meta associated with hashStake", __func__);
    return GetMint(meta.hashSerial, mint);
}

bool CWallet::DatabaseMint(CDeterministicMint& dMint)
{
    CWalletDB walletdb(strWalletFile);
    ezeeTracker->Add(dMint, true);
    return true;
}

//! Primarily for the scenario that a mint was confirmed and added to the chain and then that block orphaned
bool CWallet::SetMintUnspent(const CBigNum& bnSerial)
{
    uint256 hashSerial = GetSerialHash(bnSerial);
    if (!ezeeTracker->HasSerialHash(hashSerial))
        return error("%s: did not find mint", __func__);

    CMintMeta meta = ezeeTracker->Get(hashSerial);
    ezeeTracker->SetPubcoinNotUsed(meta.hashPubcoin);
    return true;
}

bool CWallet::UpdateMint(const CBigNum& bnValue, const int& nHeight, const uint256& txid, const libeshadow::CoinDenomination& denom)
{
    uint256 hashValue = GetPubCoinHash(bnValue);
    CEshadowMint mint;
    if (ezeeTracker->HasPubcoinHash(hashValue)) {
        CMintMeta meta = ezeeTracker->GetMetaFromPubcoin(hashValue);
        meta.nHeight = nHeight;
        meta.txid = txid;
        return ezeeTracker->UpdateState(meta);
    } else {
        //Check if this mint is one that is in our mintpool (a potential future mint from our deterministic generation)
        if (zwalletMain->IsInMintPool(bnValue)) {
            if (zwalletMain->SetMintSeen(bnValue, nHeight, txid, denom))
                return true;
        }
    }

    return false;
}



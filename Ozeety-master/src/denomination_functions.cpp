// Copyright (c) 2017-2020 The OZEETY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "denomination_functions.h"


// -------------------------------------------------------------------------------------------------------
// Number of coins used for either change or a spend given a map of coins used
// -------------------------------------------------------------------------------------------------------
int getNumberOfCoinsUsed(
    const std::map<libeshadow::CoinDenomination, CAmount>& mapChange)
{
    int nChangeCount = 0;
    for (const auto& denom : libeshadow::eshadowDenomList) {
        nChangeCount += mapChange.at(denom);
    }
    return nChangeCount;
}

// -------------------------------------------------------------------------------------------------------
// Find the max CoinDenomination amongst held coins
// -------------------------------------------------------------------------------------------------------
libeshadow::CoinDenomination getMaxDenomHeld(
    const std::map<libeshadow::CoinDenomination, CAmount>& mapCoinsHeld)
{
    libeshadow::CoinDenomination maxDenom = libeshadow::ZQ_ERROR;
    for (auto& coin : reverse_iterate(libeshadow::eshadowDenomList)) {
        if (mapCoinsHeld.at(coin)) {
            maxDenom = coin;
            break;
        }
    }
    return maxDenom;
}
// -------------------------------------------------------------------------------------------------------
// Get Exact Amount with CoinsHeld
// -------------------------------------------------------------------------------------------------------
std::map<libeshadow::CoinDenomination, CAmount> getSpendCoins(const CAmount nValueTarget,
    const std::map<libeshadow::CoinDenomination, CAmount> mapOfDenomsHeld)

{
    std::map<libeshadow::CoinDenomination, CAmount> mapUsed;
    CAmount nRemainingValue = nValueTarget;
    // Initialize
    for (const auto& denom : libeshadow::eshadowDenomList)
        mapUsed.insert(std::pair<libeshadow::CoinDenomination, CAmount>(denom, 0));

    // Start with the Highest Denomination coin and grab coins as long as the remaining amount is greater than the
    // current denomination value and we have the denom
    for (auto& coin : reverse_iterate(libeshadow::eshadowDenomList)) {
        CAmount nValue = EshadowDenominationToAmount(coin);
        do {
            if ((nRemainingValue >= nValue) && (mapUsed.at(coin) < mapOfDenomsHeld.at(coin))) {
                mapUsed.at(coin)++;
                nRemainingValue -= nValue;
            }
        } while ((nRemainingValue >= nValue) && (mapUsed.at(coin) < mapOfDenomsHeld.at(coin)));
    }
    return mapUsed;
}

// -------------------------------------------------------------------------------------------------------
// Get change (no limits)
// -------------------------------------------------------------------------------------------------------
std::map<libeshadow::CoinDenomination, CAmount> getChange(const CAmount nValueTarget)
{
    std::map<libeshadow::CoinDenomination, CAmount> mapChange;
    CAmount nRemainingValue = nValueTarget;
    // Initialize
    for (const auto& denom : libeshadow::eshadowDenomList)
        mapChange.insert(std::pair<libeshadow::CoinDenomination, CAmount>(denom, 0));

    // Start with the Highest Denomination coin and grab coins as long as the remaining amount is greater than the
    // current denomination value
    for (auto& coin : reverse_iterate(libeshadow::eshadowDenomList)) {
        CAmount nValue = EshadowDenominationToAmount(coin);
        do {
            if (nRemainingValue >= nValue) {
                mapChange.at(coin)++;
                nRemainingValue -= nValue;
            }
        } while (nRemainingValue >= nValue);
    }
    return mapChange;
}

// -------------------------------------------------------------------------------------------------------
// Attempt to use coins held to exactly reach nValueTarget, return mapOfDenomsUsed with the coin set used
// Return false if exact match is not possible
// -------------------------------------------------------------------------------------------------------
bool getIdealSpends(
    const CAmount nValueTarget,
    const std::list<CMintMeta>& listMints,
    const std::map<libeshadow::CoinDenomination, CAmount> mapOfDenomsHeld,
    std::map<libeshadow::CoinDenomination, CAmount>& mapOfDenomsUsed)
{
    CAmount nRemainingValue = nValueTarget;
    // Initialize
    for (const auto& denom : libeshadow::eshadowDenomList)
        mapOfDenomsUsed.insert(std::pair<libeshadow::CoinDenomination, CAmount>(denom, 0));

    // Start with the Highest Denomination coin and grab coins as long as the remaining amount is greater than the
    // current denomination value
    for (auto& coin : reverse_iterate(libeshadow::eshadowDenomList)) {
        for (const CMintMeta& mint : listMints) {
            if (mint.isUsed) continue;
            if (nRemainingValue >= EshadowDenominationToAmount(coin) && coin == mint.denom) {
                mapOfDenomsUsed.at(coin)++;
                nRemainingValue -= EshadowDenominationToAmount(mint.denom);
            }
            if (nRemainingValue < EshadowDenominationToAmount(coin)) break;
        }
    }
    return (nRemainingValue == 0);
}

// -------------------------------------------------------------------------------------------------------
// Return a list of Mint coins based on mapOfDenomsUsed and the overall value in nCoinsSpentValue
// -------------------------------------------------------------------------------------------------------
std::vector<CMintMeta> getSpends(
    const std::list<CMintMeta>& listMints,
    std::map<libeshadow::CoinDenomination, CAmount>& mapOfDenomsUsed,
    CAmount& nCoinsSpentValue)
{
    std::vector<CMintMeta> vSelectedMints;
    nCoinsSpentValue = 0;
    for (auto& coin : reverse_iterate(libeshadow::eshadowDenomList)) {
        do {
            for (const CMintMeta& mint : listMints) {
                if (mint.isUsed) continue;
                if (coin == mint.denom && mapOfDenomsUsed.at(coin)) {
                    vSelectedMints.push_back(mint);
                    nCoinsSpentValue += EshadowDenominationToAmount(coin);
                    mapOfDenomsUsed.at(coin)--;
                }
            }
        } while (mapOfDenomsUsed.at(coin));
    }
    return vSelectedMints;
}
// -------------------------------------------------------------------------------------------------------
// Just for printing/debuggin
// -------------------------------------------------------------------------------------------------------
void listSpends(const std::vector<CEshadowMint>& vSelectedMints)
{
    std::map<libeshadow::CoinDenomination, int64_t> mapEshadowSupply;
    for (auto& denom : libeshadow::eshadowDenomList)
        mapEshadowSupply.insert(std::make_pair(denom, 0));

    for (const CEshadowMint& mint : vSelectedMints) {
        libeshadow::CoinDenomination denom = mint.GetDenomination();
        mapEshadowSupply.at(denom)++;
    }

    CAmount nTotal = 0;
    for (auto& denom : libeshadow::eshadowDenomList) {
        LogPrint(BCLog::LEGACYZC, "%s %d coins for denomination %d used\n", __func__, mapEshadowSupply.at(denom), denom);
        nTotal += libeshadow::EshadowDenominationToAmount(denom);
    }
    LogPrint(BCLog::LEGACYZC, "Total value of coins %d\n", nTotal);
}

// -------------------------------------------------------------------------------------------------------
// Find the CoinDenomination with the most number for a given amount
// -------------------------------------------------------------------------------------------------------
libeshadow::CoinDenomination getDenomWithMostCoins(
    const std::map<libeshadow::CoinDenomination, CAmount>& mapOfDenomsUsed)
{
    libeshadow::CoinDenomination maxCoins = libeshadow::ZQ_ERROR;
    CAmount nMaxNumber = 0;
    for (const auto& denom : libeshadow::eshadowDenomList) {
        CAmount amount = mapOfDenomsUsed.at(denom);
        if (amount > nMaxNumber) {
            nMaxNumber = amount;
            maxCoins = denom;
        }
    }
    return maxCoins;
}
// -------------------------------------------------------------------------------------------------------
// Get the next denomination above the current one. Return ZQ_ERROR if already at the highest
// -------------------------------------------------------------------------------------------------------
libeshadow::CoinDenomination getNextHighestDenom(const libeshadow::CoinDenomination& this_denom)
{
    libeshadow::CoinDenomination nextValue = libeshadow::ZQ_ERROR;
    for (const auto& denom : libeshadow::eshadowDenomList) {
        if (EshadowDenominationToAmount(denom) > EshadowDenominationToAmount(this_denom)) {
            nextValue = denom;
            break;
        }
    }
    return nextValue;
}
// -------------------------------------------------------------------------------------------------------
// Get the next denomination below the current one that is also amongst those held.
// Return ZQ_ERROR if none found
// -------------------------------------------------------------------------------------------------------
libeshadow::CoinDenomination getNextLowerDenomHeld(const libeshadow::CoinDenomination& this_denom,
    const std::map<libeshadow::CoinDenomination, CAmount>& mapCoinsHeld)
{
    libeshadow::CoinDenomination nextValue = libeshadow::ZQ_ERROR;
    for (auto& denom : reverse_iterate(libeshadow::eshadowDenomList)) {
        if ((denom < this_denom) && (mapCoinsHeld.at(denom) != 0)) {
            nextValue = denom;
            break;
        }
    }
    return nextValue;
}

int minimizeChange(
    int nMaxNumberOfSpends,
    int nChangeCount,
    const libeshadow::CoinDenomination nextToMaxDenom,
    const CAmount nValueTarget,
    const std::map<libeshadow::CoinDenomination, CAmount>& mapOfDenomsHeld,
    std::map<libeshadow::CoinDenomination, CAmount>& mapOfDenomsUsed)
{
    // Now find out if possible without using 1 coin such that we have more spends but less change
    // First get set of coins close to value but still less than value (since not exact)
    CAmount nRemainingValue = nValueTarget;
    CAmount AmountUsed = 0;
    int nCoinCount = 0;

    // Re-clear this
    std::map<libeshadow::CoinDenomination, CAmount> savedMapOfDenomsUsed = mapOfDenomsUsed;
    for (const auto& denom : libeshadow::eshadowDenomList)
        mapOfDenomsUsed.at(denom) = 0;

    // Find the amount this is less than total but uses up higher denoms first,
    // starting at the denom that is not greater than the overall total
    for (const auto& denom : reverse_iterate(libeshadow::eshadowDenomList)) {
        if (denom <= nextToMaxDenom) {
            CAmount nValue = EshadowDenominationToAmount(denom);
            do {
                if ((nRemainingValue > nValue) && (mapOfDenomsUsed.at(denom) < mapOfDenomsHeld.at(denom))) {
                    mapOfDenomsUsed.at(denom)++;
                    nRemainingValue -= nValue;
                    AmountUsed += nValue;
                    nCoinCount++;
                }
            } while ((nRemainingValue > nValue) && (mapOfDenomsUsed.at(denom) < mapOfDenomsHeld.at(denom)));
        }
    }

    // Now work way back up from the bottom filling in with the denom that we have that is just
    // bigger than the remaining amount
    // Shouldn't need more than one coin here?
    for (const auto& denom : libeshadow::eshadowDenomList) {
        CAmount nValue = EshadowDenominationToAmount(denom);
        if ((nValue > nRemainingValue) && (mapOfDenomsUsed.at(denom) < mapOfDenomsHeld.at(denom))) {
            mapOfDenomsUsed.at(denom)++;
            nRemainingValue -= nValue;
            AmountUsed += nValue;
            nCoinCount++;
        }
        if (nRemainingValue < 0) break;
    }

    // This can still result in a case where you've used an extra spend than needed.
    // e.g Spend of 26, while having 1*5 + 4*10
    // First stage may be 2*10+5 (i.e < 26)
    // Second stage can be 3*10+5 (no more fives, so add a 10)
    // So 5 is no longer needed and will become change also

    CAmount nAltChangeAmount = AmountUsed - nValueTarget;
    std::map<libeshadow::CoinDenomination, CAmount> mapAltChange = getChange(nAltChangeAmount);

    // Check if there is overlap between change and spend denominations
    // And if so, remove those that overlap
    for (const auto& denom : libeshadow::eshadowDenomList) {
        do {
            if (mapAltChange.at(denom) && mapOfDenomsUsed.at(denom)) {
                mapOfDenomsUsed.at(denom)--;
                mapAltChange.at(denom)--;
                nCoinCount--;
                CAmount nValue = EshadowDenominationToAmount(denom);
                AmountUsed -= nValue;
            }
        } while (mapAltChange.at(denom) && mapOfDenomsUsed.at(denom));
    }

    // Still possible to have wrong mix. So meet exact amount found above - with least number of coins
    mapOfDenomsUsed = getSpendCoins(AmountUsed, mapOfDenomsHeld);
    nCoinCount = getNumberOfCoinsUsed(mapOfDenomsUsed);

    // Re-calculate change
    nAltChangeAmount = AmountUsed - nValueTarget;
    mapAltChange = getChange(nAltChangeAmount);
    int AltChangeCount = getNumberOfCoinsUsed(mapAltChange);

    // Alternative method yields less mints and is less than MaxNumberOfSpends if true
    if ((AltChangeCount < nChangeCount) && (nCoinCount <= nMaxNumberOfSpends)) {
        return AltChangeCount;
    } else {
        // if we don't meet above go back to what we started with
        mapOfDenomsUsed = savedMapOfDenomsUsed;
        return nChangeCount;
    }
}


// -------------------------------------------------------------------------------------------------------
// Couldn't meet amount exactly, will need to generate change
// returning with a 0 means either too many spends or no change
// Latter should never happen since we should only get here if exact is not possible
// -------------------------------------------------------------------------------------------------------
int calculateChange(
    int nMaxNumberOfSpends,
    const CAmount nValueTarget,
    const std::map<libeshadow::CoinDenomination, CAmount>& mapOfDenomsHeld,
    std::map<libeshadow::CoinDenomination, CAmount>& mapOfDenomsUsed)
{
    libeshadow::CoinDenomination minDenomOverTarget = libeshadow::ZQ_ERROR;
    // Initialize
    mapOfDenomsUsed.clear();
    for (const auto& denom : libeshadow::eshadowDenomList)
        mapOfDenomsUsed.insert(std::pair<libeshadow::CoinDenomination, CAmount>(denom, 0));

    for (const auto& denom : libeshadow::eshadowDenomList) {
        if (nValueTarget < EshadowDenominationToAmount(denom) && mapOfDenomsHeld.at(denom)) {
            minDenomOverTarget = denom;
            break;
        }
    }
    // OK so if != ZQ_ERROR we have a solution using 1 coin
    if (minDenomOverTarget != libeshadow::ZQ_ERROR) {
        mapOfDenomsUsed.at(minDenomOverTarget) = 1;

        // Now find out # of coins in change
        CAmount nChangeAmount = EshadowDenominationToAmount(minDenomOverTarget) - nValueTarget;
        std::map<libeshadow::CoinDenomination, CAmount> mapChange = getChange(nChangeAmount);
        int nChangeCount = getNumberOfCoinsUsed(mapChange);

        // always try to minimize change
        libeshadow::CoinDenomination nextToMaxDenom = getNextLowerDenomHeld(minDenomOverTarget, mapOfDenomsHeld);
        int newChangeCount = minimizeChange(nMaxNumberOfSpends, nChangeCount,
                                            nextToMaxDenom, nValueTarget,
                                            mapOfDenomsHeld, mapOfDenomsUsed);

        // Alternative method yields less mints and is less than MaxNumberOfSpends if true
        if (newChangeCount < nChangeCount) return newChangeCount;

        // Reclear
        for (const auto& denom : libeshadow::eshadowDenomList)
            mapOfDenomsUsed.at(denom) = 0;
        // Then reset as before previous clearing
        mapOfDenomsUsed.at(minDenomOverTarget) = 1;

        return nChangeCount;

    } else {
        // Try to meet a different way
        for (const auto& denom : libeshadow::eshadowDenomList)
            mapOfDenomsUsed.at(denom) = 0;
        CAmount nRemainingValue = nValueTarget;
        int nCoinCount = 0;
        CAmount AmountUsed = 0;
        for (const auto& denom : reverse_iterate(libeshadow::eshadowDenomList)) {
            CAmount nValue = EshadowDenominationToAmount(denom);
            do {
                if (mapOfDenomsHeld.at(denom) && nRemainingValue > 0) {
                    mapOfDenomsUsed.at(denom)++;
                    AmountUsed += nValue;
                    nRemainingValue -= nValue;
                    nCoinCount++;
                }
            } while ((nRemainingValue > 0) && (mapOfDenomsUsed.at(denom) < mapOfDenomsHeld.at(denom)));
            if (nRemainingValue < 0) break;
        }

        CAmount nChangeAmount = AmountUsed - nValueTarget;
        std::map<libeshadow::CoinDenomination, CAmount> mapChange = getChange(nChangeAmount);
        int nMaxChangeCount = getNumberOfCoinsUsed(mapChange);

        // Instead get max Denom held
        libeshadow::CoinDenomination maxDenomHeld = getMaxDenomHeld(mapOfDenomsHeld);

        // Assign for size (only)
        std::map<libeshadow::CoinDenomination, CAmount> mapOfMinDenomsUsed = mapOfDenomsUsed;
        int nChangeCount = minimizeChange(nMaxNumberOfSpends, nMaxChangeCount,
                                          maxDenomHeld, nValueTarget,
                                          mapOfDenomsHeld, mapOfMinDenomsUsed);
        mapOfDenomsUsed = mapOfMinDenomsUsed;
        return nChangeCount;
    }
}

// -------------------------------------------------------------------------------------------------------
// Given a Target Spend Amount, attempt to meet it with a set of coins where less than nMaxNumberOfSpends
// 'spends' are required
// -------------------------------------------------------------------------------------------------------
std::vector<CMintMeta> SelectMintsFromList(const CAmount nValueTarget, CAmount& nSelectedValue, int nMaxNumberOfSpends,
                                               int& nCoinsReturned, const std::list<CMintMeta>& listMints,
                                               const std::map<libeshadow::CoinDenomination, CAmount> mapOfDenomsHeld, int& nNeededSpends)
{
    std::vector<CMintMeta> vSelectedMints;
    std::map<libeshadow::CoinDenomination, CAmount> mapOfDenomsUsed;

    nNeededSpends = 0;
    bool fCanMeetExactly = getIdealSpends(nValueTarget, listMints, mapOfDenomsHeld, mapOfDenomsUsed);
    if (fCanMeetExactly) {
        nCoinsReturned = 0;
        nSelectedValue = nValueTarget;
        vSelectedMints = getSpends(listMints, mapOfDenomsUsed, nSelectedValue);
        // If true, we are good and done!
        if (vSelectedMints.size() <= (size_t)nMaxNumberOfSpends) {
            return vSelectedMints;
        }
        else {
            nNeededSpends = vSelectedMints.size();
        }
    }
    // Since either too many spends needed or can not spend the exact amount,
    // calculate the change needed and the map of coins used
    nCoinsReturned = calculateChange(nMaxNumberOfSpends, nValueTarget, mapOfDenomsHeld, mapOfDenomsUsed);
    if (nCoinsReturned == 0) {
        LogPrint(BCLog::LEGACYZC, "%s: Problem getting change (TBD) or Too many spends %d\n", __func__, nValueTarget);
        vSelectedMints.clear();
    } else {
        vSelectedMints = getSpends(listMints, mapOfDenomsUsed, nSelectedValue);
        LogPrint(BCLog::LEGACYZC, "%s: %d coins in change for %d\n", __func__, nCoinsReturned, nValueTarget);
    }
    return vSelectedMints;
}

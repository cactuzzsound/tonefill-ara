#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>

namespace tonefill::licensing
{
// Serial-number activation for ToneFill via Gumroad. Two product families share one key field:
//   • Perpetual (one-time purchase) — verified once, then valid offline forever.
//   • Subscription ($9/mo, $69/yr)  — verified on activation and re-checked periodically; it lapses
//     when the Gumroad membership ends/cancels/fails, with an offline grace window.
// Before activation, a 7-day full-functionality trial runs from first use, then a hard block.
class LicenseManager
{
public:
    enum class Result { Success, InvalidKey, NetworkError };

    static constexpr int kTrialDays             = 7;   // full trial from first run
    static constexpr int kRecheckIntervalDays   = 3;   // how often a subscription phones home
    static constexpr int kSubscriptionGraceDays = 14;  // max offline before a subscription lapses

    // TODO: paste the real Gumroad product ids (product settings -> "product_id"). Two products:
    // a one-time (perpetual) product and a membership (subscription, monthly+yearly tiers).
    static constexpr const char* kProductIdPerpetual    = "rNG7v7I5Z4FtBwpg00grzA==";
    static constexpr const char* kProductIdSubscription = "FE_n6_hoNqu8N625LQpe8w==";
    static constexpr const char* kBuyUrlPerpetual       = "https://cactuzzsound.gumroad.com/l/tonefill_perpetual";
    static constexpr const char* kBuyUrlSubscription    = "https://cactuzzsound.gumroad.com/l/tonefill_subscription";

    static LicenseManager& getInstance();

    bool         isActivated()  const noexcept { return activated_.load(); }
    juce::String getStoredKey() const noexcept { return storedKey_; }

    int  trialDaysLeft() const;                              // 0 once the trial has run out
    bool isTrialActive() const { return ! isActivated() && trialDaysLeft() > 0; }
    bool isUsable()      const { return isActivated() || isTrialActive(); } // may process audio
    bool isExpired()     const { return ! isActivated() && trialDaysLeft() <= 0; } // blocked

    // Async: verify against the perpetual product, then the subscription product; store on success.
    void verifyAndActivate (const juce::String& key, std::function<void (Result, juce::String)> callback);
    void deactivate();

    // Call from a message-thread timer: re-validates a subscription when the recheck interval is due.
    void tickRecheck();

private:
    LicenseManager();
    void loadFromDisk();
    void saveToDisk();
    void ensureFirstRun();
    double daysSince (juce::Time t) const;
    void   activateOnMessageThread (juce::String key, juce::String type,
                                    std::function<void (Result, juce::String)> callback);
    static juce::PropertiesFile::Options storageOptions();

    enum class Verify { NetworkError, NotThisProduct, ValidActive, ValidInactive };
    static Verify verifyProduct (const char* productId, const juce::String& key);

    std::unique_ptr<juce::PropertiesFile> props_;
    std::atomic<bool> activated_ { false };
    std::atomic<bool> checking_  { false };
    juce::String storedKey_, licenseType_; // "perpetual" | "subscription" | ""
    juce::Time   firstRun_, lastVerified_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseManager)
};
} // namespace tonefill::licensing

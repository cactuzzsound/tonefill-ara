#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <functional>

namespace tonefill::licensing
{
// Serial-number activation for ToneFill, verified against Gumroad (same scheme as LostComzz).
// "Demo" = simply not activated: the plugin still runs and auditions, but Export/Render is gated
// (checked at the call sites). Activation is stored locally and survives offline afterwards.
class LicenseManager
{
public:
    enum class Result { Success, InvalidKey, NetworkError };

    // Gumroad product this plugin's license keys belong to. TODO: paste the ToneFill product id
    // (Gumroad product settings -> "product_id", looks like "abcd-XXXXXXXX...==").
    static constexpr const char* kProductId = "REPLACE_WITH_TONEFILL_GUMROAD_PRODUCT_ID";
    // Where "Buy license" sends the user (the ToneFill Gumroad product page).
    static constexpr const char* kBuyUrl    = "https://cactuzzsound.gumroad.com/l/tonefill";

    static constexpr int kTrialDays = 7; // full-functionality trial from first run

    static LicenseManager& getInstance();

    bool         isActivated()  const noexcept { return activated_; }
    juce::String getStoredKey() const noexcept { return storedKey_; }

    // Trial: full functionality for kTrialDays after first run, then a hard block until activated.
    int  trialDaysLeft() const;                              // 0 once the trial has run out
    bool isTrialActive() const { return ! activated_ && trialDaysLeft() > 0; }
    bool isUsable()      const { return activated_ || isTrialActive(); } // may process audio
    bool isExpired()     const { return ! activated_ && trialDaysLeft() <= 0; } // blocked

    // Async: verifies the key (online, Gumroad) and, on success, stores activation. Calls back on the
    // message thread with the result + a user-facing message.
    void verifyAndActivate (const juce::String& key, std::function<void (Result, juce::String)> callback);
    void deactivate();

private:
    LicenseManager();
    void loadFromDisk();
    void saveToDisk (const juce::String& key);
    void ensureFirstRun();   // stamp + persist the first-run date if not already set
    static juce::PropertiesFile::Options storageOptions();

    std::unique_ptr<juce::PropertiesFile> props_;
    bool         activated_ { false };
    juce::String storedKey_;
    juce::Time   firstRun_;  // when the trial clock started

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseManager)
};
} // namespace tonefill::licensing

#include "licensing/LicenseManager.h"

#include <cmath>

#if JUCE_MAC
 #include <unistd.h>
 #include <pwd.h>
#endif

namespace tonefill::licensing
{
// Real-home files (survive plugin reinstall + the Logic AU sandbox). The license file mirrors the
// activation; the trial file holds only the first-run clock (kept separate so it never clobbers it).
static juce::File realHome (const juce::String& leaf)
{
#if JUCE_MAC
    if (struct passwd* pw = getpwuid (getuid()))
        if (pw->pw_dir != nullptr)
            return juce::File (juce::String::fromUTF8 (pw->pw_dir)
                               + "/Library/Application Support/Cactuzz Sound/" + leaf);
#endif
    juce::ignoreUnused (leaf);
    return {};
}
static juce::File realLicenseFile() { return realHome ("ToneFill-License.xml"); }
static juce::File realTrialFile()   { return realHome ("ToneFill-Trial.dat"); }

static juce::Time parseIso (const juce::String& iso) { return iso.isNotEmpty() ? juce::Time::fromISO8601 (iso) : juce::Time (0); }

LicenseManager& LicenseManager::getInstance()
{
    static LicenseManager instance;
    return instance;
}

juce::PropertiesFile::Options LicenseManager::storageOptions()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "ToneFill-License";
    opts.filenameSuffix      = ".xml";
    opts.folderName          = "Cactuzz Sound";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat       = juce::PropertiesFile::storeAsXML;
    return opts;
}

LicenseManager::LicenseManager()
{
    props_ = std::make_unique<juce::PropertiesFile> (storageOptions());
    loadFromDisk();
    ensureFirstRun();
}

double LicenseManager::daysSince (juce::Time t) const
{
    if (t.toMilliseconds() <= 0) return 1.0e9; // never verified -> treat as very old
    return (double) (juce::Time::getCurrentTime().toMilliseconds() - t.toMilliseconds()) / (1000.0 * 60.0 * 60.0 * 24.0);
}

void LicenseManager::loadFromDisk()
{
    storedKey_    = props_->getValue ("licenseKey", "");
    licenseType_  = props_->getValue ("licenseType", "");
    lastVerified_ = parseIso (props_->getValue ("lastVerified", ""));
    bool storedAct = storedKey_.isNotEmpty() && props_->getBoolValue ("activated", false);

    // Real-home mirror (bypasses the Logic AU sandbox).
    if (! storedAct)
    {
        const auto rf = realLicenseFile();
        if (rf.existsAsFile())
            if (auto xml = juce::XmlDocument::parse (rf))
            {
                juce::String key, type, lastV; bool act = false;
                for (auto* e = xml->getFirstChildElement(); e != nullptr; e = e->getNextElement())
                    if (e->hasTagName ("VALUE"))
                    {
                        const auto n = e->getStringAttribute ("name"), v = e->getStringAttribute ("val");
                        if (n == "licenseKey")   key   = v;
                        if (n == "licenseType")  type  = v;
                        if (n == "lastVerified") lastV = v;
                        if (n == "activated")    act   = (v == "1");
                    }
                if (key.isNotEmpty() && act)
                {
                    storedKey_ = key; licenseType_ = type; lastVerified_ = parseIso (lastV); storedAct = true;
                    saveToDisk(); // mirror back into the local props
                }
            }
    }

    // Derive the current activation state. A perpetual license is valid forever; a subscription is
    // valid only within the offline grace window (tickRecheck refreshes it online).
    if (storedAct)
        activated_ = (licenseType_ == "subscription") ? (daysSince (lastVerified_) <= kSubscriptionGraceDays) : true;
}

void LicenseManager::saveToDisk()
{
    props_->setValue ("licenseKey",   storedKey_);
    props_->setValue ("licenseType",  licenseType_);
    props_->setValue ("activated",    activated_.load());
    props_->setValue ("lastVerified", lastVerified_.toISO8601 (true));
    props_->save();

    const auto rf = realLicenseFile();
    if (rf != juce::File{})
    {
        rf.getParentDirectory().createDirectory();
        juce::XmlElement xml ("PROPERTIES");
        auto add = [&xml] (const char* n, const juce::String& v)
        { auto* e = xml.createNewChildElement ("VALUE"); e->setAttribute ("name", n); e->setAttribute ("val", v); };
        add ("licenseKey", storedKey_);
        add ("licenseType", licenseType_);
        add ("activated", activated_.load() ? "1" : "0");
        add ("lastVerified", lastVerified_.toISO8601 (true));
        xml.writeTo (rf);
    }
}

void LicenseManager::ensureFirstRun()
{
    juce::Time eff (0);
    auto consider = [&eff] (juce::Time t)
    { if (t.toMilliseconds() > 0 && (eff.toMilliseconds() == 0 || t < eff)) eff = t; };

    consider (parseIso (props_->getValue ("firstRun", "")));
    const auto tf = realTrialFile();
    if (tf.existsAsFile()) consider (parseIso (tf.loadFileAsString().trim()));

    if (eff.toMilliseconds() == 0) eff = juce::Time::getCurrentTime();
    firstRun_ = eff;

    const auto iso = eff.toISO8601 (true);
    props_->setValue ("firstRun", iso);
    props_->save();
    if (tf != juce::File{}) { tf.getParentDirectory().createDirectory(); tf.replaceWithText (iso); }
}

int LicenseManager::trialDaysLeft() const
{
    if (isActivated()) return kTrialDays;
    return juce::jmax (0, (int) std::ceil ((double) kTrialDays - daysSince (firstRun_)));
}

void LicenseManager::deactivate()
{
    activated_ = false; storedKey_ = {}; licenseType_ = {}; lastVerified_ = juce::Time (0);
    props_->removeValue ("licenseKey");
    props_->removeValue ("licenseType");
    props_->removeValue ("activated");
    props_->removeValue ("lastVerified");
    props_->save();
    const auto rf = realLicenseFile();
    if (rf.existsAsFile()) rf.deleteFile();
}

// Verify one product id against Gumroad. Runs on the CALLING thread (use a background one).
LicenseManager::Verify LicenseManager::verifyProduct (const char* productId, const juce::String& key)
{
    juce::String resp; bool net = false;
    try
    {
        const juce::String body =
            "product_id="   + juce::URL::addEscapeChars (productId, true)
          + "&license_key=" + juce::URL::addEscapeChars (key.trim(), true)
          + "&increment_uses_count=false";
        juce::URL url ("https://api.gumroad.com/v2/licenses/verify");
        url = url.withPOSTData (body);
        if (auto s = url.createInputStream (
                juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                    .withConnectionTimeoutMs (10'000)))
        { resp = s->readEntireStreamAsString(); net = true; }
    }
    catch (...) {}

    if (! net || resp.isEmpty()) return Verify::NetworkError;

    const auto json = juce::JSON::parse (resp);
    if (! static_cast<bool> (json.getProperty ("success", false))) return Verify::NotThisProduct;

    const auto p = json.getProperty ("purchase", juce::var());
    auto isTruthy = [&p] (const char* k) { return static_cast<bool> (p.getProperty (k, false)); };
    auto isDateSet = [&p] (const char* k) { const auto v = p.getProperty (k, juce::var()); return v.isString() && v.toString().isNotEmpty(); };

    if (isTruthy ("refunded") || isTruthy ("chargebacked") || isTruthy ("disputed")) return Verify::ValidInactive;
    // Membership status fields (absent for a perpetual purchase; a date string when the sub has ended).
    if (isDateSet ("subscription_ended_at") || isDateSet ("subscription_cancelled_at") || isDateSet ("subscription_failed_at"))
        return Verify::ValidInactive;

    return Verify::ValidActive;
}

void LicenseManager::activateOnMessageThread (juce::String key, juce::String type,
                                              std::function<void (Result, juce::String)> callback)
{
    juce::MessageManager::callAsync ([this, key, type, callback]
    {
        storedKey_ = key; licenseType_ = type; lastVerified_ = juce::Time::getCurrentTime(); activated_ = true;
        saveToDisk();
        callback (Result::Success, "Activation successful!\nThank you for your purchase.");
    });
}

void LicenseManager::verifyAndActivate (const juce::String& key, std::function<void (Result, juce::String)> callback)
{
    const auto trimmed = key.trim();
    if (trimmed.isEmpty()) { callback (Result::InvalidKey, "Please enter your license key."); return; }

    juce::Thread::launch ([this, trimmed, callback]
    {
        auto finish = [callback] (Result r, juce::String m)
        { juce::MessageManager::callAsync ([callback, r, m] { callback (r, m); }); };
        const juce::String netMsg = "Could not reach the license server.\nCheck your connection and try again.";

        const auto vp = verifyProduct (kProductIdPerpetual, trimmed);
        if (vp == Verify::NetworkError) { finish (Result::NetworkError, netMsg); return; }
        if (vp == Verify::ValidActive)  { activateOnMessageThread (trimmed, "perpetual", callback); return; }
        if (vp == Verify::ValidInactive){ finish (Result::InvalidKey, "This purchase is no longer valid."); return; }

        // Not a perpetual key -> try the subscription product.
        const auto vs = verifyProduct (kProductIdSubscription, trimmed);
        if (vs == Verify::NetworkError) { finish (Result::NetworkError, netMsg); return; }
        if (vs == Verify::ValidActive)  { activateOnMessageThread (trimmed, "subscription", callback); return; }
        if (vs == Verify::ValidInactive){ finish (Result::InvalidKey, "Your subscription is not active. Please renew to continue."); return; }

        finish (Result::InvalidKey, "Invalid license key. Please check and try again.");
    });
}

void LicenseManager::tickRecheck()
{
    if (licenseType_ != "subscription" || checking_.load()) return;
    const double days = daysSince (lastVerified_);
    if (days < (double) kRecheckIntervalDays) return; // not due yet

    checking_ = true;
    const auto key = storedKey_;
    juce::Thread::launch ([this, key, days]
    {
        const auto v = verifyProduct (kProductIdSubscription, key);
        juce::MessageManager::callAsync ([this, v, days]
        {
            if (v == Verify::ValidActive)
            { lastVerified_ = juce::Time::getCurrentTime(); activated_ = true; saveToDisk(); }
            else if (v == Verify::ValidInactive || v == Verify::NotThisProduct)
            { activated_ = false; saveToDisk(); }
            else if (days > (double) kSubscriptionGraceDays) // NetworkError beyond grace -> lapse
            { activated_ = false; }
            checking_ = false;
        });
    });
}
} // namespace tonefill::licensing

#include "licensing/LicenseManager.h"

#if JUCE_MAC
 #include <unistd.h>
 #include <pwd.h>
#endif

namespace tonefill::licensing
{
// Logic Pro sandboxes AU plugins, so JUCE's PropertiesFile path is redirected into the sandbox
// container and can't see the activation the (non-sandboxed) VST3 wrote. getpwuid() reads the real
// /Users/<name> from the passwd DB, unaffected by the sandbox — we also write/read there.
static juce::File realActivationFile()
{
#if JUCE_MAC
    if (struct passwd* pw = getpwuid (getuid()))
        if (pw->pw_dir != nullptr)
            return juce::File (juce::String::fromUTF8 (pw->pw_dir)
                               + "/Library/Application Support/Cactuzz Sound/ToneFill-License.xml");
#endif
    return {};
}

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
}

void LicenseManager::loadFromDisk()
{
    storedKey_ = props_->getValue ("licenseKey", "");
    activated_ = storedKey_.isNotEmpty() && props_->getBoolValue ("activated", false);

    if (! activated_)
    {
        const auto rf = realActivationFile();
        if (rf.existsAsFile())
            if (auto xml = juce::XmlDocument::parse (rf))
            {
                juce::String key; bool act = false;
                for (auto* e = xml->getFirstChildElement(); e != nullptr; e = e->getNextElement())
                    if (e->hasTagName ("VALUE"))
                    {
                        const auto n = e->getStringAttribute ("name");
                        if (n == "licenseKey") key = e->getStringAttribute ("val");
                        if (n == "activated")  act = (e->getStringAttribute ("val") == "1");
                    }
                if (key.isNotEmpty() && act)
                {
                    storedKey_ = key; activated_ = true;
                    props_->setValue ("licenseKey", key);
                    props_->setValue ("activated", true);
                    props_->save();
                }
            }
    }
}

void LicenseManager::saveToDisk (const juce::String& key)
{
    storedKey_ = key; activated_ = true;
    props_->setValue ("licenseKey", key);
    props_->setValue ("activated", true);
    props_->setValue ("activationDate", juce::Time::getCurrentTime().toISO8601 (true));
    props_->save();

    const auto rf = realActivationFile();
    if (rf != juce::File{})
    {
        rf.getParentDirectory().createDirectory();
        juce::XmlElement xml ("PROPERTIES");
        auto add = [&xml] (const char* n, const juce::String& v)
        { auto* e = xml.createNewChildElement ("VALUE"); e->setAttribute ("name", n); e->setAttribute ("val", v); };
        add ("licenseKey", key);
        add ("activated", "1");
        add ("activationDate", juce::Time::getCurrentTime().toISO8601 (true));
        xml.writeTo (rf);
    }
}

void LicenseManager::deactivate()
{
    activated_ = false; storedKey_ = {};
    props_->removeValue ("licenseKey");
    props_->removeValue ("activated");
    props_->removeValue ("activationDate");
    props_->save();
    const auto rf = realActivationFile();
    if (rf.existsAsFile()) rf.deleteFile();
}

void LicenseManager::verifyAndActivate (const juce::String& key,
                                        std::function<void (Result, juce::String)> callback)
{
    const auto trimmed = key.trim();
    if (trimmed.isEmpty()) { callback (Result::InvalidKey, "Please enter your license key."); return; }

    juce::Thread::launch ([trimmed, callback]
    {
        juce::String responseText; bool networkOk = false;
        try
        {
            const juce::String body =
                "product_id="   + juce::URL::addEscapeChars (kProductId, true)
              + "&license_key=" + juce::URL::addEscapeChars (trimmed, true)
              + "&increment_uses_count=false";
            juce::URL url ("https://api.gumroad.com/v2/licenses/verify");
            url = url.withPOSTData (body);
            if (auto stream = url.createInputStream (
                    juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                        .withConnectionTimeoutMs (10'000)))
            {
                responseText = stream->readEntireStreamAsString();
                networkOk = true;
            }
        }
        catch (...) {}

        if (! networkOk || responseText.isEmpty())
        {
            juce::MessageManager::callAsync ([callback]
            { callback (Result::NetworkError, "Could not reach the license server.\nCheck your connection and try again."); });
            return;
        }

        const auto json = juce::JSON::parse (responseText);
        const bool success = static_cast<bool> (json.getProperty ("success", false));
        if (success)
        {
            juce::MessageManager::callAsync ([trimmed, callback]
            {
                LicenseManager::getInstance().saveToDisk (trimmed);
                callback (Result::Success, "Activation successful!\nThank you for your purchase.");
            });
        }
        else
        {
            juce::String msg = json.getProperty ("message", "").toString();
            if (msg.isEmpty()) msg = "Invalid license key. Please check and try again.";
            juce::MessageManager::callAsync ([msg, callback] { callback (Result::InvalidKey, msg); });
        }
    });
}
} // namespace tonefill::licensing

#ifndef CAPTIVE_CONFIG_HEADER
#define CAPTIVE_CONFIG_HEADER

#include <list> // Probably a bit heavy, but ESP8266WiFiGeneric uses list
#include <IPAddress.h>

class ESP8266WebServer;
class DNSServer;

/// IP address of the ESP8266 when we're serving the captive config page
const static IPAddress captiveServeIP(192, 168, 1, 1);

/// Store information on WiFi networks between scanning and serving
struct APType
{
    String ssid;

    // TODO: Encryption type, etc.
};


/// For returning the SSID and Passphrase input by the user
struct APCredentials
{
    String ssid;
    String passphrase;
};


/// Used to provide a "Captive Portal" so the user can set WiFi Passwords, etc.
/*!
 * Should only ever be one of these created at a time; ESP8266WebServer uses
 * callbacks to handle requests, and can only deal with one client at a time.
 *
 * Usage:
 *
 *   CaptiveConfig *configGetter(new CaptiveConfig);
 *
 *   while(!configGetter->haveConfig())
 *       // DNS and HTTP servers start up, user makes config selections.
 *       do_other_stuff();
 * 
 *   // Retrieve the config
 *   auto receivedConfig(configGetter->getConfig());
 *
 *   // done
 *   delete configGetter;
 * */
class CaptiveConfig
{
    public:

        CaptiveConfig();
        ~CaptiveConfig();
        
        /// Advances state machine, handles requests. Returns true when done.
        bool haveConfig();

        /// Returns the SSID + Passphase selected
        APCredentials getConfig() const;
        
    protected:

        enum class CaptiveConfigState {
            SCANNING,
            STARTING_WIFI,
            STARTING_HTTP,
            STARTING_DNS,
            SERVING,
            DONE,
        } state;

        ESP8266WebServer *configHTTPServer;

        DNSServer *configDNSServer;

        std::list<APType> knownAPs;

        static void storePassword();
        
        static void serveConfigPage();

        static CaptiveConfig *instance;

        APCredentials *pickedCreds;
}; // end class CaptiveConfig

#endif // #ifndef CAPTIVE_CONFIG_HEADER


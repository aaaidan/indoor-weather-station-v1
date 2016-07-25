#include "CaptiveConfig.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>

#include <cassert>

CaptiveConfig * CaptiveConfig::instance(nullptr);

CaptiveConfig::CaptiveConfig() :
    configHTTPServer(nullptr),
    configDNSServer(nullptr),
    pickedCreds(nullptr),
    state(CaptiveConfigState::SCANNING),
    numAPsFound(0)
{
    assert(instance == nullptr);
    instance = this;

    // Set WiFi to station mode and disconnect from an AP if it was previously connected
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Scan for WiFi networks that we can see
    auto totalNetworksFound( WiFi.scanNetworks() );

    // Fill knownAPs with a set of different networks we can see.
    // Resolve duplicates by keeping the strongest signals for a given SSID.
    knownAPs = new APType *[totalNetworksFound];
    for(auto i(0); i < totalNetworksFound; ++i) {
        knownAPs[i] = nullptr;

        auto thisSSID( WiFi.SSID(i) );
        auto thisRSSI( WiFi.RSSI(i) );

        auto isNewSSID(true);
        for(auto j(0); j < numAPsFound; ++j) {
            if( knownAPs[j]->ssid == thisSSID ) {
                isNewSSID = false;
                if( knownAPs[j]->rssi > thisRSSI ) {
                    knownAPs[j]->ssid = thisSSID;
                    knownAPs[j]->rssi = thisRSSI;
                    knownAPs[j]->encryptionType = WiFi.encryptionType(i);
                }
                break;
            }
        }

        if(isNewSSID) {
            knownAPs[numAPsFound++] = new APType{ thisSSID,
                                                  thisRSSI,
                                                  WiFi.encryptionType(i) };
        }
    }

    // Calls to haveConfig() will finish the setup
    state = CaptiveConfigState::STARTING_WIFI;
}


CaptiveConfig::~CaptiveConfig()
{
    assert(instance == this);

    configHTTPServer->close();
    delete configHTTPServer;
    configHTTPServer = nullptr;

    configDNSServer->stop();
    delete configDNSServer;
    configDNSServer = nullptr;

    if (pickedCreds) {
        delete pickedCreds;
    }

    for(auto i(0); i < numAPsFound; ++i) {
        if(knownAPs[i] != nullptr) {
            delete knownAPs[i];
            knownAPs[i] = nullptr;
        }
    }
    delete [] knownAPs;

    instance = nullptr;
}


bool CaptiveConfig::haveConfig()
{
    switch (state) {
        case CaptiveConfigState::STARTING_WIFI:
            WiFi.mode(WIFI_AP);
            WiFi.softAPConfig( captiveServeIP, captiveServeIP,
                               IPAddress(255, 255, 255, 0) );
            WiFi.softAP("VCW Indoor Weather Station");

            state = CaptiveConfigState::STARTING_HTTP;
            return false;

        case CaptiveConfigState::STARTING_HTTP:
            configHTTPServer = new ESP8266WebServer(80);
            configHTTPServer->on("/storePassword", storePassword);
            // TODO: Would be better to redirect to a proper start page so we don't see the apple URL for instance.
            configHTTPServer->onNotFound(serveConfigPage);
            configHTTPServer->begin();

            state = CaptiveConfigState::STARTING_DNS;
            return false;

        case CaptiveConfigState::STARTING_DNS:
            configDNSServer = new DNSServer;
            configDNSServer->setTTL(0);
            configDNSServer->start(53, "*", captiveServeIP);

            state = CaptiveConfigState::SERVING;
            return false;

        case CaptiveConfigState::SERVING:
            configDNSServer->processNextRequest();
            configHTTPServer->handleClient();

            // storePassword() advances state to DONE
            return false;

        case CaptiveConfigState::DONE:
            return true;

        default:
            assert(0);
            return false;
    }
}


APCredentials CaptiveConfig::getConfig() const
{
    assert(pickedCreds);

    if (pickedCreds) {
        return *pickedCreds;
    }

    return APCredentials();
}


/*static*/ void CaptiveConfig::storePassword()
{
    assert(instance && instance->configHTTPServer);

    char out[]{
        "<!doctype html>"
        "<html class=\"no-js\" lang=\"en\">"
        "Thanks!"
        "</html>"
        };

    instance->pickedCreds = new APCredentials{
        "This is beginning to look",
        "like web programming"
        };

    instance->state = CaptiveConfigState::DONE;

    instance->configHTTPServer->send(200, "text/html", out);
}

/*static*/ void CaptiveConfig::serveConfigPage()
{
    assert(instance && instance->configHTTPServer);

    String out(
        "<!doctype html>"
        "<html class=\"no-js\" lang=\"en\">"
        "Pretend to <a href=\"http://setup/storePassword\">enter a passphrase</a>."
        "<hr /><table><tr><th>SSID</th><th>RSSI</th></tr>"
        );

    String footer(
        "</table></html>"
        );

    for(auto i(0); i < instance->numAPsFound; ++i) {
        out += "<tr><td>";
        out += instance->knownAPs[i]->ssid;
        out += "</td><td>";
        out += instance->knownAPs[i]->rssi;
        out += "</td></tr>";
    }

    instance->configHTTPServer->send(200, "text/html", out + footer);
}


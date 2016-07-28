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
    state(CaptiveConfigState::START_SCANNING),
    numAPsFound(0)
{
    assert(instance == nullptr);
    instance = this;
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

    tearDownKnownAPs();

    instance = nullptr;
}


bool CaptiveConfig::haveConfig()
{
    switch (state) {
        case CaptiveConfigState::START_SCANNING:
            // Set WiFi to station mode and disconnect from an AP, in case
            // it was previously connected.
            // TODO: This seems to be covered in scanNetworks - double check in the rescanning case
            WiFi.mode(WIFI_STA);
            WiFi.disconnect();

            // Scan asynchronously, don't show hidden networks.
            WiFi.scanNetworks(true, false);

            state = CaptiveConfigState::SCANNING;
            return false;

        case CaptiveConfigState::SCANNING:
        {
            auto scanState(WiFi.scanComplete());

            if (scanState == WIFI_SCAN_RUNNING) {
                return false;
            } else if (scanState == WIFI_SCAN_FAILED) {
                state = CaptiveConfigState::START_SCANNING;
                return false;
            }

            populateKnownAPs(scanState);

            state = CaptiveConfigState::STARTING_WIFI;
            return false;
        }

        case CaptiveConfigState::STARTING_WIFI:
            WiFi.mode(WIFI_AP);
            WiFi.softAPConfig( captiveServeIP, captiveServeIP,
                               IPAddress(255, 255, 255, 0) );
            WiFi.softAP(APNAME);

            // If this is our first time starting up (ie not a rescan)
            if (configHTTPServer == nullptr) {
                state = CaptiveConfigState::STARTING_HTTP;
            } else {
                state = CaptiveConfigState::SERVING;
            }
            return false;

        case CaptiveConfigState::STARTING_HTTP:
            configHTTPServer = new ESP8266WebServer(80);
            configHTTPServer->on("/storePassword", storePassword);
            configHTTPServer->on("/", serveConfigPage);
            configHTTPServer->onNotFound(serveRedirect);
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
    assert(state == CaptiveConfigState::DONE);

    if (pickedCreds) {
        return *pickedCreds;
    }

    return APCredentials();
}


void CaptiveConfig::populateKnownAPs(uint8_t numAPs)
{
    tearDownKnownAPs();

    knownAPs = new APType *[numAPs];
    for(auto i(0); i < numAPs; ++i) {
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
            knownAPs[numAPsFound] = new APType{ thisSSID,
                                                  thisRSSI,
                                                  WiFi.encryptionType(i) };
            ++numAPsFound;
        }
    }
}


void CaptiveConfig::tearDownKnownAPs()
{
    auto numToTearDown(numAPsFound);
    numAPsFound = 0;
    
    for(auto i(0); i < numToTearDown; ++i) {
        if(knownAPs[i] != nullptr) {
            delete knownAPs[i];
            knownAPs[i] = nullptr;
        }
    }

    delete [] knownAPs;

    knownAPs = nullptr;
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
        instance->configHTTPServer->arg("ssid"),
        instance->configHTTPServer->arg("pass")
        };

    instance->state = CaptiveConfigState::DONE;

    instance->configHTTPServer->send(200, "text/html", out);
}


/*static*/ void CaptiveConfig::serveConfigPage()
{
    assert(instance && instance->configHTTPServer);

    // TODO: Javascript reloady thing in case we've rescanned, also condition the network list on the scanning state?
    String out(
        "<!doctype html>"
        "<html class=\"no-js\" lang=\"en\">"
        "<body>"
        "<table><tr><th>SSID</th><th>RSSI</th><th>Encryption Enum</th></tr>"
        );

    String footer(
        "</table></body></html>"
        );

    for(auto i(0); i < instance->numAPsFound; ++i) {
        out += "<tr><td>";
        out += instance->knownAPs[i]->ssid;
        out += "</td><td>";
        out += instance->knownAPs[i]->rssi;
        out += "</td><td>";
        out += instance->knownAPs[i]->encryptionType;
        out += "</td><td>";
        out += "<form action=\"storePassword\"><input type=\"hidden\" name=\"ssid\" value=\"";
        out += instance->knownAPs[i]->ssid;
        out += "\"><input type=\"password\" name=\"pass\"><input type=\"submit\" value=\"Use This one!\">";
        out += "</td></tr></form>";
    }

    instance->configHTTPServer->send(200, "text/html", out + footer);
}


/*static*/ void CaptiveConfig::serveRedirect()
{
    // This only sends one response, with a redirect header and no content.
    instance->configHTTPServer->sendHeader( "Location",
                                            "http://setup/",
                                            true );
    instance->configHTTPServer->send(302, "text/plain", "");
}


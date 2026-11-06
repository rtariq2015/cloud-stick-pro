#include <cstddef>
#include <cstdint>
#include <cstring>
#include "pro_network.h"

#ifndef GUEST_WIFI_NATIVE_TEST
#include "guest_wifi.h"
#include "pro_core.h"
#include "config_loader.h"
#include "setup_network.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#endif

namespace {
constexpr size_t PacketLimit = 1232;
constexpr size_t QuestionLimit = 259; // maximal 255-byte wire name + type/class
constexpr size_t PendingLimit = 4;
constexpr uint32_t DnsTimeoutMs = 3000;
constexpr uint32_t ConnectTimeoutMs = 20000;

uint16_t read16(const uint8_t* p) { return uint16_t(p[0]) << 8 | p[1]; }
void write16(uint8_t* p, uint16_t value) { p[0] = value >> 8; p[1] = value & 255; }

// A single ordinary, uncompressed IN question. Restricting the question format
// makes response matching exact and bounds storage, without decoding answers.
// Additional records (e.g. EDNS) pass through within PacketLimit. No TCP relay.
bool dnsQuestion(const uint8_t* packet, size_t length, size_t& questionLength) {
  if (length < 17 || length > PacketLimit || read16(packet + 4) != 1 ||
      (packet[2] & 0x78)) return false; // ordinary QUERY opcode only
  size_t cursor = 12;
  while (cursor < length) {
    uint8_t label = packet[cursor++];
    if (label > 63) return false; // includes compression pointers
    if (!label) {
      if (cursor - 12 > 255 || cursor + 4 > length ||
          read16(packet + cursor) == 0 || read16(packet + cursor + 2) != 1) return false;
      questionLength = cursor + 4 - 12;
      return questionLength <= QuestionLimit;
    }
    if (cursor + label > length || cursor + label - 12 >= 255) return false;
    cursor += label;
  }
  return false;
}

bool dnsQuery(const uint8_t* packet, size_t length, size_t& questionLength) {
  return dnsQuestion(packet, length, questionLength) && !(packet[2] & 0x80) &&
         !(packet[3] & 15) && !read16(packet + 6) && !read16(packet + 8);
}

bool dnsReply(const uint8_t* packet, size_t length, uint16_t expectedID,
              const uint8_t* question, size_t expectedLength) {
  size_t questionLength = 0;
  return dnsQuestion(packet, length, questionLength) && (packet[2] & 0x80) &&
         read16(packet) == expectedID &&
         questionLength == expectedLength &&
         memcmp(packet + 12, question, expectedLength) == 0;
}

bool validMask(uint32_t mask) {
  return pro::validMask(mask);
}
bool overlap(uint32_t ip, uint32_t mask, uint32_t apIP, uint32_t apMask) {
  return pro::networkOverlap(ip,mask,apIP,apMask);
}
bool elapsed(uint32_t now, uint32_t started, uint32_t interval) {
  return uint32_t(now - started) >= interval;
}

#ifndef GUEST_WIFI_NATIVE_TEST
enum class State : uint8_t { Idle, Connecting, Relocating, Connected, Error };
State state = State::Idle;
const char* message = "Guest Wi-Fi is disconnected";
uint32_t connectStarted = 0;
uint32_t nextSetupAddress=0,relocationSentAt=0;
bool relocationSent=false;
bool retainedDNS=false;
bool connectionOpen=false;
bool forwarding = false;
WiFiUDP apDNS, upstreamDNS;
IPAddress dnsAddress, stationAddress, stationMask, stationGateway;
struct Pending {
  bool used = false;
  IPAddress client;
  uint16_t port = 0, originalID = 0, nonce = 0;
  uint16_t questionLength = 0;
  uint32_t started = 0;
  uint8_t question[QuestionLimit] = {};
};
Pending pending[PendingLimit];
uint8_t packet[PacketLimit]; // main-task scratch; never placed on callback stack

uint32_t ipValue(const IPAddress& ip) {
  return uint32_t(ip[0]) << 24 | uint32_t(ip[1]) << 16 |
         uint32_t(ip[2]) << 8 | ip[3];
}
bool usableIP(const IPAddress& ip) {
  return ip.type() == IPv4 && ip[0] != 0 && ip[0] != 127 && ip[0] < 224 &&
         ipValue(ip) != 0xffffffffU;
}
bool apClient(const IPAddress& ip) {
  return setup_network_client(ip);
}
bool usableDNS(const IPAddress& ip) {
  return usableIP(ip) && (ipValue(ip) & pro::SetupMask) != (ipValue(WiFi.softAPIP())&pro::SetupMask) &&
         ip != stationAddress;
}

void stopRouting() {
  if (forwarding) WiFi.AP.enableNAPT(false);
  forwarding = false;
  apDNS.stop();
  upstreamDNS.stop();
  for (auto& request : pending) request.used = false;
}
void disconnectStation() {
  WiFi.setAutoReconnect(false);
  wifi_mode_t mode;
  if (esp_wifi_get_mode(&mode) == ESP_OK && (mode & WIFI_MODE_STA)) {
    WiFi.disconnectAsync(false, false);
    // This core can retry a first failed connection despite autoReconnect(false).
    // Removing only STA also prevents a queued retry after the owner stops it.
    WiFi.enableSTA(false);
  }
}
void fail(const char* reason) {
  stopRouting();
  disconnectStation();
  state = State::Error;
  nextSetupAddress=0;relocationSent=false;
  message = reason;
}
bool sendPacket(WiFiUDP& socket, const IPAddress& destination, uint16_t port,
                const uint8_t* data, size_t length) {
  return socket.beginPacket(destination, port) && socket.write(data, length) == length &&
         socket.endPacket() == 1;
}
int receive(WiFiUDP& socket) {
  int length = socket.parsePacket();
  if (length <= 0) return 0;
  if (size_t(length) > PacketLimit) { socket.clear(); return -1; }
  int received = socket.read(packet, length);
  socket.clear();
  return received == length ? length : -1;
}
void expireRequests(uint32_t now) {
  for (auto& request : pending) {
    if (request.used && elapsed(now, request.started, DnsTimeoutMs)) request.used = false;
  }
}
uint16_t transactionID() {
  // Fresh random 16-bit DNS nonce, unique among the four outstanding requests.
  uint16_t candidate = uint16_t(esp_random());
  for (;;) {
    bool collision = false;
    for (const auto& request : pending) {
      if (request.used && request.nonce == candidate) { collision = true; break; }
    }
    if (!collision) return candidate;
    ++candidate; // at most four collisions; random starting point remains private
  }
}
void relayDNS() {
  uint32_t now = millis();
  expireRequests(now);
  // A bounded amount of work per portal tick; no name lookups or waiting reads.
  for (size_t n = 0; n < PendingLimit; ++n) {
    int length = receive(upstreamDNS);
    if (!length) break;
    if (length < 0 || upstreamDNS.remoteIP() != dnsAddress || upstreamDNS.remotePort() != 53) continue;
    uint16_t nonce = length >= 2 ? read16(packet) : 0;
    for (auto& request : pending) {
      if (!request.used || request.nonce != nonce ||
          !dnsReply(packet, length, request.nonce, request.question, request.questionLength)) continue;
      write16(packet, request.originalID);
      sendPacket(apDNS, request.client, request.port, packet, length);
      request.used = false;
      break;
    }
  }
  for (size_t n = 0; n < PendingLimit; ++n) {
    int length = receive(apDNS);
    if (!length) break;
    if (length < 0 || !apClient(apDNS.remoteIP()) || apDNS.remotePort() == 0) continue;
    size_t questionLength = 0;
    if (!dnsQuery(packet, length, questionLength)) continue;
    Pending* slot = nullptr;
    bool duplicate = false;
    for (auto& request : pending) {
      if (!request.used) { if (!slot) slot = &request; continue; }
      if (request.client == apDNS.remoteIP() && request.port == apDNS.remotePort() &&
          request.originalID == read16(packet) && request.questionLength == questionLength &&
          memcmp(request.question, packet + 12, questionLength) == 0) duplicate = true;
    }
    if (!slot || duplicate) continue;
    slot->client = apDNS.remoteIP();
    slot->port = apDNS.remotePort();
    slot->originalID = read16(packet);
    slot->nonce = transactionID();
    slot->questionLength = questionLength;
    memcpy(slot->question, packet + 12, questionLength);
    slot->started = now;
    write16(packet, slot->nonce);
    slot->used = sendPacket(upstreamDNS, dnsAddress, 53, packet, length);
  }
}

bool startRouting() {
  // Read IP, mask and gateway from one DHCP snapshot; separate reads can race.
  esp_netif_ip_info_t info;
  if(!WiFi.STA.netif()||esp_netif_get_ip_info(WiFi.STA.netif(),&info)!=ESP_OK)return false;
  stationAddress=IPAddress(info.ip.addr);stationMask=IPAddress(info.netmask.addr);stationGateway=IPAddress(info.gw.addr);
  uint32_t ip = ipValue(stationAddress), mask = ipValue(stationMask);
  // A temporarily incomplete DHCP snapshot is retried until ConnectTimeoutMs.
  if (!usableIP(stationAddress) || !validMask(mask))return false;
  if (!usableIP(stationGateway)) { fail("Guest Wi-Fi did not supply an IPv4 gateway"); return false; }
  if(!retainedDNS){dnsAddress=WiFi.dnsIP(0);if(!usableIP(dnsAddress)||dnsAddress==stationAddress)dnsAddress=WiFi.dnsIP(1);}
  if(overlap(ip,mask,ipValue(WiFi.softAPIP()),pro::SetupMask)||
      (usableIP(dnsAddress)&&((ipValue(dnsAddress)&pro::SetupMask)==(ipValue(WiFi.softAPIP())&pro::SetupMask)))) {
    nextSetupAddress=pro::chooseSetupAddress(ip,mask,ipValue(dnsAddress));
    if(!nextSetupAddress){fail("This guest network leaves no nonconflicting setup address");return false;}
    relocationSent=false;state=State::Relocating;
    message="Changing the setup address to avoid a guest Wi-Fi conflict. Your Wi-Fi name and password stay the same";
    return false;
  }
  if (!usableDNS(dnsAddress)) dnsAddress = WiFi.dnsIP(1);
  if (!usableDNS(dnsAddress)) { fail("Guest Wi-Fi did not supply usable DNS"); return false; }
  if (!apDNS.begin(WiFi.softAPIP(), 53) || !upstreamDNS.begin(stationAddress, 0)) {
    fail("Cannot start guest DNS relay"); return false;
  }
  if (!WiFi.AP.enableNAPT(true)) { fail("Cannot enable guest Wi-Fi forwarding"); return false; }
  forwarding = true;
  state = State::Connected;
  message = "Connected; guest Wi-Fi sign-in may be required. Internet access is not verified";
  return true;
}
#endif
} // namespace

#ifndef GUEST_WIFI_NATIVE_TEST
bool guest_wifi_connect(const String& ssid, const String& password, String& error) {
  if (!pro::validWiFi(std::string(ssid.c_str(), ssid.length()),
                      std::string(password.c_str(), password.length()), password.isEmpty())) {
    error = "Enter a Wi-Fi name and either no password or an 8–63 character password";
    return false;
  }
  for (size_t i = 0; i < password.length(); ++i) {
    if (uint8_t(password[i]) < 32 || uint8_t(password[i]) > 126) {
      error = "Wi-Fi password must use printable ASCII characters"; return false;
    }
  }
  if (guest_wifi_active()) { error = "Disconnect guest Wi-Fi before connecting again"; return false; }
  wifi_mode_t mode;
  if (esp_wifi_get_mode(&mode) != ESP_OK || !(mode & WIFI_MODE_AP) ||
      WiFi.softAPIP() != setup_network_ip() ||
      WiFi.softAPSubnetMask() != IPAddress(255, 255, 255, 0)) {
    error = "Protected setup access point is unavailable"; return false;
  }
  stopRouting();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  // persistent(false) only affects later driver initialization in this core.
  // The AP already initialized the driver; RAM storage prevents credential writes.
  if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK || !WiFi.mode(WIFI_AP_STA)) {
    fail("Cannot prepare guest Wi-Fi station"); error = message; return false;
  }
  WiFi.setMinSecurity(password.isEmpty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK);
  state = State::Connecting;
  nextSetupAddress=0;relocationSent=false;
  retainedDNS=false;
  connectionOpen=password.isEmpty();
  message = "Connecting to guest Wi-Fi; setup remains available";
  connectStarted = millis();
  if (WiFi.begin(ssid.c_str(), password.isEmpty() ? nullptr : password.c_str()) == WL_CONNECT_FAILED) {
    fail("Cannot start guest Wi-Fi connection"); error = message; return false;
  }
  error = "";
  return true;
}

void guest_wifi_poll() {
  if (state == State::Connecting) {
    if (WiFi.status() == WL_CONNECTED && usableIP(WiFi.localIP())) startRouting();
    if(state==State::Connecting&&elapsed(millis(),connectStarted,ConnectTimeoutMs))fail("Wi-Fi connection timed out or DHCP settings are incomplete; check the network and password");
  } else if (state == State::Connected) {
    if (WiFi.status() != WL_CONNECTED || WiFi.localIP() != stationAddress ||
        WiFi.subnetMask() != stationMask) {
      fail("Guest Wi-Fi disconnected or changed; connect again");
      return;
    }
    relayDNS();
  }
}

String guest_wifi_json() {
  guest_wifi_poll();
  uint32_t now = millis();
  JsonDocument doc;
  doc["state"] = state == State::Connecting ? "connecting" :
                 state == State::Relocating ? "address-change" : state == State::Connected ? "connected" : state == State::Error ? "error" : "idle";
  doc["message"] = message;
  doc["internet_verified"] = false;
  if(guest_wifi_active()){doc["ssid"]=WiFi.SSID();doc["wifi_open"]=connectionOpen;}
  doc["station_ip"]=stationAddress.toString();doc["station_mask"]=stationMask.toString();
  if(nextSetupAddress){IPAddress next(nextSetupAddress>>24,(nextSetupAddress>>16)&255,(nextSetupAddress>>8)&255,nextSetupAddress&255);doc["setup_url"]=String("http://")+next.toString()+"/";}
  doc["remaining_ms"] = state == State::Connecting && !elapsed(now, connectStarted, ConnectTimeoutMs)
                         ? ConnectTimeoutMs - uint32_t(now - connectStarted) : 0;
  String out;
  if (!serializeJson(doc, out)) return F("{\"state\":\"error\",\"message\":\"Guest status unavailable\"}");
  return out;
}

void guest_wifi_stop() {
  stopRouting();
  disconnectStation();
  state = State::Idle;
  nextSetupAddress=0;relocationSent=false;
  retainedDNS=false;
  message = "Guest Wi-Fi is disconnected";
}
bool guest_wifi_active() { return state == State::Connecting || state == State::Relocating || state == State::Connected; }
bool guest_wifi_fill_config(CloudConfig& config){
  if(state!=State::Connected||WiFi.status()!=WL_CONNECTED||config.ssid!=WiFi.SSID()||config.wifi_open!=connectionOpen)return false;
  config.wifi_password=connectionOpen?String():WiFi.psk();
  return pro::validWiFi(std::string(config.ssid.c_str(),config.ssid.length()),std::string(config.wifi_password.c_str(),config.wifi_password.length()),config.wifi_open);
}
void guest_wifi_relocation_sent(){if(state==State::Relocating&&!relocationSent){relocationSentAt=millis();relocationSent=true;}}
uint32_t guest_wifi_relocation_ready(){return state==State::Relocating&&relocationSent&&elapsed(millis(),relocationSentAt,2000)?nextSetupAddress:0;}
void guest_wifi_relocated(bool ok){nextSetupAddress=0;relocationSent=false;if(!ok){fail("Cannot change the setup address; reconnect or restart the dongle");return;}retainedDNS=true;state=State::Connecting;connectStarted=millis();}
#endif

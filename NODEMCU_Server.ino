// ======================================================================
//  ESP8266 - Telnet + LittleFS Notes + GitHub + bore.hub Auto-Tunnel
//  FINAL VERSION: Auto tunnel on boot + GitHub port push + Minimal logs
//  Test on hotspot first (port 7889 may be blocked on regular WiFi)
// ======================================================================

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <ESPTelnet.h>
#include <ArduinoJson.h>

// ──────────────────────────────────────────────── WiFi Credentials ─────
const char* ssid          = "Your Wifi ssid";
const char* wifi_password = "Your Wifi's psd";
const char* telnet_user   = "admin";
const char* telnet_pass   = "qwerty";

// ──────────────────────────────────────────────── Static IP ─────
IPAddress local_IP(192, 168, 0, 105);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

// ──────────────────────────────────────────────── GitHub Config ─────
const char* githubRepo    = "Your Github Repo";
const char* githubToken   = "Your Github Token"; // only with repo access
const char* dataBranch    = "Your Github Branch for both log file and port file";
const char* portFile      = "ports.txt";
const char* logFile       = "log.txt";

// ──────────────────────────────────────────────── bore.hub Config ─────
const char* bore_host     = "bore.pub";
const int   local_port    = 23;    // Forward telnet (port 23)

// ──────────────────────────────────────────────── Local Storage ─────
const char* DATA_FILE = "/notes.json";
StaticJsonDocument<4096> doc;

// ──────────────────────────────────────────────── Telnet ─────
ESPTelnet telnet;
bool authenticated = false;

// ──────────────────────────────────────────────── Time (Auto + Manual) ─────
unsigned long timeBaseSec = 0;
unsigned long timeBootMs = 0;
bool timeIsSet = false;
unsigned long lastTimeSync = 0;
const unsigned long TIME_SYNC_INTERVAL = 3600000;

// ──────────────────────────────────────────────── Tunnel State ─────
WiFiClient boreClient;
String borePublicPort = "";
bool boreConnected = false;
unsigned long lastTunnelConnect = 0;
const unsigned long TUNNEL_RECONNECT_INTERVAL = 60000;  // Retry every 1 min

// ──────────────────────────────────────────────── Port Tracking ─────
String currentPort = "";
unsigned long lastPortPush = 0;
const unsigned long PORT_PUSH_INTERVAL = 300000;  // 5 minutes

// ──────────────────────────────────────────────── Base64 ─────
static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64_encode(const String &text) {
  String out = "";
  int val = 0, valb = -6;
  for (unsigned char c : text) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out += b64chars[(val >> valb) & 0x3F];
      valb -= 6;
    }
  }
  if (valb > -6) out += b64chars[((val << 8) >> (valb + 8)) & 0x3F];
  while (out.length() % 4) out += '=';
  return out;
}

String base64_decode(const String &encoded) {
  String out = "";
  int val = 0, valb = -8;
  for (char c : encoded) {
    if (c == '=') break;
    const char *p = strchr(b64chars, c);
    if (p == nullptr) continue;
    val = (val << 6) + (p - b64chars);
    valb += 6;
    if (valb >= 0) {
      out += char((val >> valb) & 0xFF);
      valb -= 8;
    }
  }
  return out;
}

String sanitizeText(const String &input) {
  String out = "";
  for (char c : input) {
    if ((c >= 32 && c <= 126) || c == 10 || c == 13 || c == 9) out += c;
  }
  return out;
}

// ──────────────────────────────────────────────── Time Sync ─────
bool parseHttpDate(const String& dateStr, int& h, int& m, int& s) {
  int tPos = dateStr.indexOf(':');
  if (tPos < 0) return false;
  String timePart = dateStr.substring(tPos - 2, tPos + 6);
  h = timePart.substring(0, 2).toInt();
  m = timePart.substring(3, 5).toInt();
  s = timePart.substring(6, 8).toInt();
  return (h >= 0 && h <= 23 && m >= 0 && m <= 59 && s >= 0 && s <= 59);
}

bool fetchTimeFromServer(const char* host, int& utcH, int& utcM, int& utcS) {
  WiFiClient client;
  client.setTimeout(8000);
  if (!client.connect(host, 80)) return false;
  client.print("GET / HTTP/1.1\r\nHost: " + String(host) + "\r\nUser-Agent: ESP8266\r\nConnection: close\r\n\r\n");
  unsigned long t = millis();
  while (!client.available() && millis() - t < 8000) { delay(50); yield(); }
  if (!client.available()) { client.stop(); return false; }
  bool foundDate = false;
  while (client.available()) {
    String line = client.readStringUntil('\r');
    client.readStringUntil('\n');
    if (line.length() <= 2) break;
    if (line.startsWith("Date:")) {
      String dateVal = line.substring(6);
      dateVal.trim();
      if (parseHttpDate(dateVal, utcH, utcM, utcS)) foundDate = true;
    }
    yield();
  }
  client.stop();
  return foundDate;
}

bool syncTimeHTTP() {
  Serial.print("⏰ Syncing time");
  const char* servers[] = {"httpbin.org", "ifconfig.co", "icanhazip.com"};
  int utcH = 0, utcM = 0, utcS = 0;
  bool success = false;
  for (int retry = 0; retry < 2 && !success; retry++) {
    for (int i = 0; i < 3 && !success; i++) {
      Serial.print(".");
      if (fetchTimeFromServer(servers[i], utcH, utcM, utcS)) {
        success = true;
        Serial.println(" ✓ via " + String(servers[i]));
      }
      delay(200); yield();
    }
    if (!success) delay(500);
  }
  if (!success) { Serial.println(" ✗"); return false; }
  int istH = utcH + 5;
  int istM = utcM + 30;
  if (istM >= 60) { istM -= 60; istH += 1; }
  if (istH >= 24) istH -= 24;
  timeBaseSec = istH * 3600 + istM * 60 + utcS;
  timeBootMs = millis();
  timeIsSet = true;
  lastTimeSync = millis();
  Serial.println("   🇮🇳 IST: " + String(istH) + ":" + String(istM) + ":" + String(utcS));
  return true;
}

String getTimestamp() {
  if (!timeIsSet) {
    unsigned long secs = millis() / 1000;
    unsigned long h = (secs / 3600) % 24;
    unsigned long m = (secs / 60) % 60;
    unsigned long s = secs % 60;
    return String(h < 10 ? "0" : "") + h + ":" +
           String(m < 10 ? "0" : "") + m + ":" +
           String(s < 10 ? "0" : "") + s + "*";
  }
  if (millis() - lastTimeSync > TIME_SYNC_INTERVAL) syncTimeHTTP();
  unsigned long elapsed = (millis() - timeBootMs) / 1000;
  unsigned long total = (timeBaseSec + elapsed) % 86400;
  unsigned long h = total / 3600;
  unsigned long m = (total % 3600) / 60;
  unsigned long s = total % 60;
  return String(h < 10 ? "0" : "") + h + ":" +
         String(m < 10 ? "0" : "") + m + ":" +
         String(s < 10 ? "0" : "") + s;
}

void setTimeManual(int h, int m, int s) {
  timeBaseSec = h * 3600 + m * 60 + s;
  timeBootMs = millis();
  timeIsSet = true;
  lastTimeSync = millis();
  Serial.println("✓ Time set manually: " + getTimestamp());
}

// ──────────────────────────────────────────────── Ctrl+U Clear ─────
void clearLine() {
  telnet.print("\r\033[K> ");
}

// ──────────────────────────────────────────────── GitHub Push ─────
bool resolveGitHubIP(IPAddress &ip) {
  for (int i = 1; i <= 2; i++) {
    if (WiFi.hostByName("api.github.com", ip)) return true;
    delay(300 * i); yield();
  }
  ip = IPAddress(140, 82, 121, 6);
  return true;
}

bool pushToGitHub(const char* file, String content, const char* msg, bool overwrite) {
  if (!githubToken || strlen(githubToken) < 10) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  content = sanitizeText(content);
  IPAddress ip;
  if (!resolveGitHubIP(ip)) return false;
  delay(50); yield();
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);
  client.setBufferSizes(1024, 1024);
  String path = "/repos/" + String(githubRepo) + "/contents/" + String(file) + "?ref=" + String(dataBranch);
  String sha = "";
  String existingContent = "";
  if (!overwrite) {
    if (!client.connect(ip, 443)) return false;
    delay(100); yield();
    client.print("GET " + path + " HTTP/1.1\r\nHost: api.github.com\r\nAuthorization: token " + String(githubToken) + "\r\nAccept: application/vnd.github.v3+json\r\nUser-Agent: ESP8266\r\nConnection: close\r\n\r\n");
    yield();
    unsigned long t = millis();
    while (!client.available() && millis() - t < 10000) { delay(10); yield(); }
    if (client.available()) {
      String status = client.readStringUntil('\r');
      while (client.available()) { String line = client.readStringUntil('\r'); if (line.length() <= 2) break; }
      if (status.indexOf("200") >= 0) {
        String payload = client.readString();
        int sp = payload.indexOf("\"sha\":\"");
        if (sp >= 0) { sp += 7; int ep = payload.indexOf("\"", sp); if (ep > sp) sha = payload.substring(sp, ep); }
        int cp = payload.indexOf("\"content\":\"");
        if (cp >= 0) { cp += 11; int ce = payload.indexOf("\"", cp); if (ce > cp) {
          existingContent = sanitizeText(base64_decode(payload.substring(cp, ce)));
        }}
      }
    }
    client.stop(); yield();
  }
  String finalContent;
  if (overwrite) {
    finalContent = content;
  } else {
    if (existingContent.length() > 0) {
      while (existingContent.endsWith("\n")) existingContent = existingContent.substring(0, existingContent.length() - 1);
      finalContent = existingContent + "\n" + content;
    } else {
      finalContent = content;
    }
  }
  finalContent = sanitizeText(finalContent);
  String b64 = base64_encode(finalContent);
  String safeMsg = msg;
  safeMsg.replace("\\", "\\\\"); safeMsg.replace("\"", "\\\"");
  String json = "{\"message\":\"" + safeMsg + "\",\"content\":\"" + b64 + "\",\"branch\":\"" + String(dataBranch) + "\"";
  if (sha != "") json += ",\"sha\":\"" + sha + "\"";
  json += "}";
  if (!client.connect(ip, 443)) return false;
  delay(100); yield();
  client.print("PUT " + path + " HTTP/1.1\r\nHost: api.github.com\r\nAuthorization: token " + String(githubToken) + "\r\nAccept: application/vnd.github.v3+json\r\nContent-Type: application/json\r\nUser-Agent: ESP8266\r\nContent-Length: " + String(json.length()) + "\r\nConnection: close\r\n\r\n" + json);
  yield();
  unsigned long t = millis();
  while (!client.available() && millis() - t < 15000) { delay(10); yield(); }
  if (!client.available()) { client.stop(); return false; }
  String status = client.readStringUntil('\r');
  while (client.available()) { String line = client.readStringUntil('\r'); if (line.length() <= 2) break; }
  client.readString(); client.stop();
  return (status.indexOf("200") >= 0 || status.indexOf("201") >= 0);
}

bool pushPort(const String& port) {
  String content = port + "  # " + getTimestamp() + " (bore.hub)\n";
  bool ok = pushToGitHub(portFile, content, "Update port", true);
  if (ok) Serial.println("✅ Port pushed: " + port);
  return ok;
}

void logCritical(const String& msg) {
  String entry = getTimestamp() + " | " + msg;
  pushToGitHub(logFile, entry, "Log", false);
  Serial.println("📝 " + entry);
}

// ──────────────────────────────────────────────── bore.hub Tunnel ─────
// ──────────────────────────────────────────────── bore.hub Tunnel (Multi-Port) ─────
bool connectToBore() {
  if (boreConnected && boreClient.connected()) return true;
  
  // Try multiple ports in order of likelihood to work
  const int borePorts[] = {7889, 8080, 443, 80};  // Try standard ports too
  const int numPorts = sizeof(borePorts) / sizeof(borePorts[0]);
  
  for (int i = 0; i < numPorts; i++) {
    int port = borePorts[i];
    Serial.println("\n🔌 Trying bore.hub:" + String(port) + "...");
    
    if (!boreClient.connect(bore_host, port)) {
      Serial.println("   ❌ Connect failed");
      continue;
    }
    
    Serial.println("   ✓ TCP connected");
    delay(100);
    
    // Send tunnel request (bore.hub format)
    String request = String(local_port) + ":localhost:" + String(local_port) + "\n";
    Serial.println("   📤 Sending: " + request);
    boreClient.print(request);
    
    // Wait for response
    Serial.print("   ⏳ Waiting");
    unsigned long timeout = millis();
    while (!boreClient.available() && millis() - timeout < 8000) {
      delay(100);
      Serial.print(".");
      yield();
    }
    Serial.println();
    
    if (boreClient.available()) {
      String response = boreClient.readStringUntil('\n');
      response.trim();
      Serial.println("   📥 Response: " + response);
      
      // Check if it's a valid port assignment (not an HTTP error)
      if (response.indexOf("localhost:") >= 0 || response.toInt() > 0) {
        // Extract public port
        int colonPos = response.lastIndexOf(':');
        if (colonPos >= 0) {
          borePublicPort = response.substring(colonPos + 1);
        } else {
          borePublicPort = response;
        }
        borePublicPort.trim();
        
        if (borePublicPort.length() > 0 && borePublicPort.toInt() > 0) {
          boreConnected = true;
          currentPort = borePublicPort;
          Serial.println("   ✅ bore.hub CONNECTED on port " + String(port) + "!");
          Serial.println("   🌐 Public: telnet " + String(bore_host) + " " + borePublicPort);
          
          // Auto-push to GitHub
          Serial.println("   📤 Pushing port to GitHub...");
          if (pushPort(borePublicPort)) {
            Serial.println("   ✅ Port published!");
          }
          
          logCritical("bore.hub:" + borePublicPort);
          lastPortPush = millis();
          return true;
        }
      } else if (response.startsWith("HTTP/")) {
        Serial.println("   ⚠️ Got HTTP response (wrong protocol for this port)");
      }
    }
    
    boreClient.stop();
    delay(500);
    yield();
  }
  
  Serial.println("\n❌ All bore.hub ports failed");
  Serial.println("💡 Try: 1) Different tunnel service 2) Manual setport");
  boreConnected = false;
  return false;
}

void checkTunnelConnection() {
  if (!boreConnected || !boreClient.connected()) {
    boreConnected = false;
    if (millis() - lastTunnelConnect > TUNNEL_RECONNECT_INTERVAL) {
      connectToBore();
      lastTunnelConnect = millis();
    }
  }
  
  // Re-push port every 5 minutes (in case it changes)
  if (boreConnected && borePublicPort != "" && millis() - lastPortPush > PORT_PUSH_INTERVAL) {
    pushPort(borePublicPort);
    lastPortPush = millis();
  }
}

// ──────────────────────────────────────────────── Setup ─────
void setup() {
  Serial.begin(115200); delay(300);
  Serial.println("\n=== ESP8266 Notes Server + bore.hub ===");
  Serial.println("💾 Heap: " + String(ESP.getFreeHeap()));

  if (!LittleFS.begin()) { LittleFS.format(); LittleFS.begin(); }
  Serial.println("LittleFS mounted");

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, wifi_password);
  Serial.print("📡 WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n✓ IP: " + WiFi.localIP().toString());
  Serial.println("📶 RSSI: " + String(WiFi.RSSI()) + " dBm");

  // Time Sync
  syncTimeHTTP();

  // Telnet Server
  telnet.onConnect([](String){ 
    telnet.println("=== Notes Server ==="); 
    telnet.println("Local: telnet " + WiFi.localIP().toString());
    if (boreConnected) {
      telnet.println("Remote: telnet " + String(bore_host) + " " + borePublicPort);
    }
    telnet.print("User: "); 
    authenticated = false; 
  });
  telnet.onDisconnect([](String){ authenticated = false; logCritical("Logout"); });
  telnet.onInputReceived(onTelnetInput);
  telnet.begin();
  Serial.println("🔌 Telnet ready on port 23");

  // Load Notes
  loadData();

  // Connect to bore.hub (auto-tunnel)
  Serial.println("\n🚀 Attempting bore.hub tunnel...");
  connectToBore();

  // Initial Log
  logCritical("Started");
  
  Serial.println("\n🚀 Setup complete!");
  Serial.println("   Local:  telnet " + WiFi.localIP().toString());
  if (boreConnected) {
    Serial.println("   Remote: telnet " + String(bore_host) + " " + borePublicPort);
  } else {
    Serial.println("   Remote: bore.hub failed (use manual setport)");
  }
  Serial.println("   GitHub: https://raw.githubusercontent.com/" + String(githubRepo) + "/" + dataBranch + "/" + portFile);
  Serial.println("💡 Commands: help, setport, reconnect, status");
  Serial.println("💾 Final heap: " + String(ESP.getFreeHeap()));
}

// ──────────────────────────────────────────────── Loop ─────
void loop() {
  telnet.loop();
  checkTunnelConnection();  // Maintain bore.hub tunnel
  delay(10);
}

// ──────────────────────────────────────────────── Telnet Input ─────
void onTelnetInput(String input) {
  if (input.length() == 1) {
    char c = input[0];
    if (c == 21) { clearLine(); return; }
    if (c == 3 && authenticated) { telnet.println("\n^C"); authenticated = false; telnet.print("User: "); return; }
  }
  if (input.indexOf("\033") >= 0) { if (authenticated) telnet.print("> "); return; }
  
  input.trim();
  if (!input.length()) { if (authenticated) telnet.print("> "); return; }

  if (!authenticated) {
    static bool waitPass = false;
    if (!waitPass) {
      if (input == telnet_user) { waitPass = true; telnet.print("Password: "); }
      else { telnet.println("\nWrong user."); telnet.print("User: "); }
    } else {
      if (input == telnet_pass) {
        authenticated = true; waitPass = false;
        telnet.println("\n✓ OK! Type 'help'"); telnet.print("> ");
        logCritical("Login");
      } else {
        telnet.println("\nWrong password."); telnet.print("User: "); waitPass = false;
      }
    }
  } else {
    handleCommand(input);
  }
}

// ──────────────────────────────────────────────── Commands ─────
void handleCommand(String cmd) {
  cmd.trim();
  if (cmd == "help") {
    telnet.println("Commands:");
    telnet.println("  setport <number>  - Manually set port");
    telnet.println("  reconnect         - Reconnect to bore.hub");
    telnet.println("  settime HH MM SS  - Manual time set");
    telnet.println("  add title:\"..\" content:\"..\" - Add note");
    telnet.println("  list              - Show notes");
    telnet.println("  format            - Clear all notes");
    telnet.println("  status | logout");
    telnet.println("Tips: Ctrl+U = clear line");
  }
  else if (cmd == "reconnect") {
    telnet.println("Reconnecting to bore.hub...");
    boreClient.stop();
    boreConnected = false;
    if (connectToBore()) {
      telnet.println("✓ Reconnected! Port: " + borePublicPort);
    } else {
      telnet.println("✗ Reconnection failed");
    }
  }
  else if (cmd.startsWith("settime ")) {
    String a = cmd.substring(8);
    int h = extractInt(a,0), m = extractInt(a,1), s = extractInt(a,2);
    if (h>=0&&h<=23 && m>=0&&m<=59 && s>=0&&s<=59) {
      setTimeManual(h,m,s);
      telnet.println("✓ Time: " + getTimestamp());
    } else telnet.println("✗ Use: settime HH MM SS");
  }
  else if (cmd.startsWith("setport ")) { 
    currentPort = cmd.substring(8); 
    telnet.println("Port: " + currentPort); 
    if (pushPort(currentPort)) {
      telnet.println("✅ Pushed to GitHub!");
      telnet.println("🔗 https://raw.githubusercontent.com/" + String(githubRepo) + "/" + dataBranch + "/" + portFile);
    }
    lastPortPush = millis(); 
  }
  else if (cmd == "status") {
    telnet.println("Time: " + getTimestamp());
    telnet.println("bore.hub: " + String(boreConnected ? "Connected" : "Disconnected"));
    if (boreConnected) {
      telnet.println("Port: " + borePublicPort);
      telnet.println("Remote: telnet " + String(bore_host) + " " + borePublicPort);
    }
    telnet.println("Heap: " + String(ESP.getFreeHeap()));
  }
  else if (cmd == "format") {
    doc["items"].clear(); 
    saveData();
    telnet.println("✓ Formatted"); 
    logCritical("Format");
  }
  else if (cmd.startsWith("add ")) { addNote(cmd.substring(4)); telnet.println("✓ Added"); }
  else if (cmd == "list") { listNotes(); }
  else if (cmd == "logout") { authenticated = false; telnet.println("Logged out."); telnet.print("User: "); }
  else { telnet.println("Unknown. Type 'help'"); }
  telnet.print("> ");
}

int extractInt(String a, int idx) {
  int p=0, c=0;
  while (c<=idx && p<a.length()) {
    while (p<a.length() && a[p]==' ') p++;
    if (p>=a.length()) break;
    int s=p;
    while (p<a.length() && a[p]!=' ') p++;
    if (c==idx) return a.substring(s,p).toInt();
    c++;
  }
  return -1;
}

void addNote(String args) {
  String t = extract(args, "title:\"", "\"");
  String c = extract(args, "content:\"", "\"");
  String g = extract(args, "tags:\"", "\"");
  if (t=="" || c=="") { telnet.println("Missing title/content"); return; }
  JsonObject e = doc["items"].createNestedObject();
  e["title"]=t; e["content"]=c;
  if (g!="") e["tags"]=g;
  saveData();
}

void listNotes() {
  if (doc["items"].size()==0) { telnet.println("No notes."); return; }
  telnet.println("Notes:");
  int i=1;
  for (JsonObject it : doc["items"].as<JsonArray>())
    telnet.println(String(i++) + ") " + it["title"].as<String>());
}

String extract(String t, String s, String e) {
  int a = t.indexOf(s);
  if (a<0) return "";
  a += s.length();
  int b = t.indexOf(e, a);
  return (b<0) ? "" : t.substring(a,b);
}

void loadData() {
  File f = LittleFS.open(DATA_FILE, "r");
  if (f) { deserializeJson(doc, f); f.close(); }
  if (!doc["items"].is<JsonArray>()) doc["items"].to<JsonArray>();
}

void saveData() {
  LittleFS.remove(DATA_FILE);
  File f = LittleFS.open(DATA_FILE, "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

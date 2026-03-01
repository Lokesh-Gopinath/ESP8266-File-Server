// ======================================================================
//  ESP8266 - Telnet + LittleFS JSON Notes Database
//  Controlled via Termius Telnet
//  New: search by tag, delete by title
// ======================================================================

#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ESPTelnet.h>
#include <ArduinoJson.h>

// ──────────────────────────────────────────────── WiFi & Telnet ─────
const char* ssid          = "Your Wifi ssid";
const char* wifi_password = "Your Wifi psd";

const char* telnet_user   = "Set user";
const char* telnet_pass   = "Set psd";

// Static IP remove the comment only when you need static ip and the ip is free.
// IPAddress local_IP(192, 168, 0, 104);
// IPAddress gateway(192, 168, 0, 1);
// IPAddress subnet(255, 255, 255, 0);

// Storage file
const char* DATA_FILE = "/notes.json";

// JSON document
StaticJsonDocument<4096> doc;

// Telnet
ESPTelnet telnet;
bool authenticated = false;
String currentUser = "";

// ────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nESP8266 - Telnet + LittleFS JSON DB");

  // LittleFS
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed → formatting...");
    LittleFS.format();
    if (!LittleFS.begin()) {
      Serial.println("Format failed → halting");
      while (true) delay(1000);
    }
  }
  Serial.println("LittleFS mounted");

  FSInfo info;
  LittleFS.info(info);
  Serial.printf("Flash storage: %u / %u KB\n", info.usedBytes/1024, info.totalBytes/1024);

  // WiFi + static IP
  WiFi.mode(WIFI_STA);
  Serial.println("Static IP: " + local_IP.toString());
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, wifi_password);

  Serial.print("Connecting WiFi ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  // Telnet setup
  telnet.onConnect([](String ip){
    telnet.println("Welcome to ESP8266 Telnet");
    telnet.print("User: ");
    authenticated = false;
    currentUser = "";
  });
  telnet.onDisconnect([](String ip){
    authenticated = false;
    currentUser = "";
  });
  telnet.onInputReceived(onTelnetInput);

  if (telnet.begin()) {
    Serial.println("Telnet server started on port 23");
  } else {
    Serial.println("Telnet failed");
  }

  // Load notes
  loadData();
}

// ────────────────────────────────────────────────
void loop() {
  telnet.loop();
  delay(10);
}

// ──────────────────────────────────────────────── Telnet input ─────
void onTelnetInput(String input) {
  input.trim();
  if (input.length() == 0) {
    if (authenticated) telnet.print("> ");
    return;
  }

  if (!authenticated) {
    handleLogin(input);
  } else {
    handleCommand(input);
  }
}

void handleLogin(String input) {
  static String enteredUser = "";
  if (enteredUser == "") {
    enteredUser = input;
    telnet.print("Password: ");
  } else {
    if (enteredUser == telnet_user && input == telnet_pass) {
      authenticated = true;
      telnet.println("\nLogin OK! Type 'help'");
      telnet.print("> ");
    } else {
      telnet.println("\nWrong credentials.");
      telnet.print("User: ");
    }
    enteredUser = "";
  }
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();

  if (cmd == "help") {
    telnet.println("Commands:");
    telnet.println("  add title:\"...\" content:\"...\" tags:\"tag1 tag2\"");
    telnet.println("  list");
    telnet.println("  search tag:\"tagname\"");
    telnet.println("  delete title:\"Exact Title\"");
    telnet.println("  status");
    telnet.println("  clear (delete all)");
    telnet.println("  logout");
  }
  else if (cmd.startsWith("add ")) {
    addNote(cmd.substring(4));
    telnet.println("Added.");
  }
  else if (cmd == "list") {
    listNotes();
  }
  else if (cmd.startsWith("search tag:\"")) {
    String tag = extract(cmd, "search tag:\"", "\"");
    searchByTag(tag);
  }
  else if (cmd.startsWith("delete title:\"")) {
    String title = extract(cmd, "delete title:\"", "\"");
    deleteNote(title);
  }
  else if (cmd == "status") {
    FSInfo info;
    LittleFS.info(info);
    telnet.println("Flash storage: " + String(info.usedBytes/1024) + " / " + String(info.totalBytes/1024) + " KB");
    telnet.println("Notes stored: " + String(doc["items"].size()));
    telnet.println("IP: " + WiFi.localIP().toString());
  }
  else if (cmd == "clear") {
    doc["items"].clear();
    saveData();
    telnet.println("All notes deleted.");
  }
  else if (cmd == "logout") {
    authenticated = false;
    telnet.println("Logged out.");
    telnet.print("User: ");
  }
  else {
    telnet.println("Unknown command. Type 'help'");
  }

  telnet.print("> ");
}

// ──────────────────────────────────────────────── New: Search by tag ─────
void searchByTag(String searchTag) {
  searchTag.toLowerCase();
  JsonArray items = doc["items"];
  if (items.size() == 0) {
    telnet.println("No notes yet.");
    return;
  }

  telnet.println("Notes matching tag \"" + searchTag + "\":");
  int found = 0;
  int i = 1;
  for (JsonObject item : items) {
    JsonVariant tags = item["tags"];
    bool match = false;

    if (tags.is<String>()) {
      String t = tags.as<String>();
      t.toLowerCase();
      if (t == searchTag) match = true;
    } else if (tags.is<JsonArray>()) {
      for (const char* t : tags.as<JsonArray>()) {
        String tagStr(t);
        tagStr.toLowerCase();
        if (tagStr == searchTag) {
          match = true;
          break;
        }
      }
    }

    if (match) {
      found++;
      telnet.print(i++);
      telnet.print(") ");
      telnet.print(item["title"].as<String>());
      telnet.print(" - ");
      telnet.println(item["content"].as<String>());
      if (tags.is<String>()) {
        telnet.println("   Tags: " + tags.as<String>());
      } else if (tags.is<JsonArray>()) {
        telnet.print("   Tags: [");
        bool first = true;
        for (const char* t : tags.as<JsonArray>()) {
          if (!first) telnet.print(", ");
          telnet.print(t);
          first = false;
        }
        telnet.println("]");
      }
      telnet.println();
    }
  }

  if (found == 0) {
    telnet.println("No notes found with tag \"" + searchTag + "\"");
  }
}

// ──────────────────────────────────────────────── New: Delete by title ─────
void deleteNote(String titleToDelete) {
  JsonArray items = doc["items"];
  bool found = false;

  for (int i = 0; i < items.size(); i++) {
    JsonObject item = items[i];
    if (item["title"].as<String>() == titleToDelete) {
      items.remove(i);
      found = true;
      break;
    }
  }

  if (found) {
    saveData();
    telnet.println("Deleted note: " + titleToDelete);
  } else {
    telnet.println("Note not found: " + titleToDelete);
  }
}

// ──────────────────────────────────────────────── Add & List (unchanged) ─────
void addNote(String args) {
  String title   = extract(args, "title:\"", "\"");
  String content = extract(args, "content:\"", "\"");
  String tagsStr = extract(args, "tags:\"", "\"");

  if (title == "" || content == "") {
    telnet.println("Missing title or content");
    return;
  }

  JsonArray items = doc["items"];
  JsonObject entry = items.createNestedObject();
  entry["title"] = title;
  entry["content"] = content;

  if (tagsStr != "") {
    int space = tagsStr.indexOf(' ');
    if (space == -1) {
      entry["tags"] = tagsStr;
    } else {
      JsonArray tagArr = entry.createNestedArray("tags");
      int pos = 0;
      while (pos < tagsStr.length()) {
        int next = tagsStr.indexOf(' ', pos);
        if (next == -1) next = tagsStr.length();
        String tag = tagsStr.substring(pos, next);
        tag.trim();
        if (tag != "") tagArr.add(tag);
        pos = next + 1;
      }
    }
  }

  saveData();
}

void listNotes() {
  JsonArray items = doc["items"];
  if (items.size() == 0) {
    telnet.println("No notes yet.");
    return;
  }

  telnet.println("Notes:");
  int i = 1;
  for (JsonObject item : items) {
    telnet.print(i++);
    telnet.print(") ");
    telnet.print(item["title"].as<String>());
    telnet.print(" - ");
    telnet.println(item["content"].as<String>());
    JsonVariant tags = item["tags"];
    if (tags.is<String>()) {
      telnet.println("   Tags: " + tags.as<String>());
    } else if (tags.is<JsonArray>()) {
      telnet.print("   Tags: [");
      bool first = true;
      for (const char* t : tags.as<JsonArray>()) {
        if (!first) telnet.print(", ");
        telnet.print(t);
        first = false;
      }
      telnet.println("]");
    }
  }
}

// Helper
String extract(String text, String start, String end) {
  int s = text.indexOf(start);
  if (s == -1) return "";
  s += start.length();
  int e = text.indexOf(end, s);
  if (e == -1) return "";
  return text.substring(s, e);
}

// ──────────────────────────────────────────────── Storage ─────
void loadData() {
  File f = LittleFS.open(DATA_FILE, "r");
  if (f) {
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      Serial.println("JSON load error: " + String(err.c_str()));
    }
  }
  if (!doc["items"].is<JsonArray>()) {
    doc["items"].to<JsonArray>();
  }
}

void saveData() {
  LittleFS.remove(DATA_FILE);
  File f = LittleFS.open(DATA_FILE, "w");
  if (f) {
    serializeJson(doc, f);
    f.close();
    Serial.println("Saved to LittleFS");
  } else {
    Serial.println("Save failed");
  }
}

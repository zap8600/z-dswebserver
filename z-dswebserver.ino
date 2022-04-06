#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <SPI.h>
#include "SdFat.h"
#include "Adafruit_SPIFlash.h"
#include "Adafruit_TinyUSB.h"
const char *ssid = "thirdpartymexicangrandma";
const char *password = "tacobellwireless";
const char *hostname = "myswimmingpool";
#define EXPOSE_FS_ON_MSD
volatile bool fs_changed = false;
Adafruit_FlashTransport_ESP32 flashTransport;
Adafruit_SPIFlash flash(&flashTransport);
FatFileSystem fatfs;
#if defined(EXPOSE_FS_ON_MSD)
Adafruit_USBD_MSC usb_msc;
#endif
WebServer server(80);
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Serial.println();
  Serial.println("Configuring access point...");
#if defined(EXPOSE_FS_ON_MSD)
  usb_msc.setID("Adafruit", "External Flash", "1.0");
  usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);
  usb_msc.setCapacity(flash.size()/512, 512);
  usb_msc.setUnitReady(true);
  usb_msc.begin();
#endif
  fatfs.begin(&flash);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  Serial.print("AP: ");
  Serial.println(ssid);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  if (MDNS.begin(hostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.print("mDNS: ");
    Serial.print(hostname);
    Serial.println(".local");
  }
  server.on("/list", HTTP_GET, printDirectory);
  server.begin();
  Serial.println("Server started");
}
void loop() {
  server.handleClient();
  delay(2);
}
int32_t msc_read_cb (uint32_t lba, void* buffer, uint32_t bufsize) {
  return flash.readBlocks(lba, (uint8_t*) buffer, bufsize/512) ? bufsize : -1;
}
int32_t msc_write_cb (uint32_t lba, uint8_t* buffer, uint32_t bufsize) {
  digitalWrite(LED_BUILTIN, HIGH);
  return flash.writeBlocks(lba, buffer, bufsize/512) ? bufsize : -1;
}
void msc_flush_cb (void) {
  flash.syncBlocks();
  fatfs.cacheClear();
  fs_changed = true;
  digitalWrite(LED_BUILTIN, LOW);
}
void printDirectory() {
  if (!server.hasArg("dir")) {
    return returnFail("BAD ARGS");
  }
  String path = server.arg("dir");
  if (path != "/" && !fatfs.exists((char *)path.c_str())) {
    return returnFail("BAD PATH");
  }
  File dir = fatfs.open((char *)path.c_str());
  path = String();
  if (!dir.isDir()) {
    dir.close();
    return returnFail("NOT DIR");
  }
  dir.rewindDirectory();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/json", "");
  WiFiClient client = server.client();
  server.sendContent("[");
  for (int cnt = 0; true; ++cnt) {
    File entry;
    if (!entry.openNext(&dir, O_RDONLY)) {
      break;
    }
    String output;
    if (cnt > 0) {
      output = ',';
    }
    output += "{\"type\":\"";
    output += (entry.isDir()) ? "dir" : "file";
    output += "\",\"name\":\"";
    //output += entry.path();
    output += "\"";
    output += "}";
    server.sendContent(output);
    entry.close();
  }
  server.sendContent("]");
  dir.close();
}
void handleNotFound() {
  if (loadFromFlash(server.uri())) {
    return;
  }
  String message = "Internal Flash Not Detected\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " NAME:" + server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  Serial.print(message);
}
bool loadFromFlash(String path) {
  String dataType = "text/plain";
  if (path.endsWith("/")) {
    path += "index.html";
  }
  if (path.endsWith(".src")) {
    path = path.substring(0, path.lastIndexOf("."));
  } else if (path.endsWith(".htm")) {
    dataType = "text/html";
  } else if (path.endsWith(".html")) {
    dataType = "text/html";
  } else if (path.endsWith(".css")) {
    dataType = "text/css";
  } else if (path.endsWith(".js")) {
    dataType = "application/javascript";
  } else if (path.endsWith(".png")) {
    dataType = "image/png";
  } else if (path.endsWith(".gif")) {
    dataType = "image/gif";
  } else if (path.endsWith(".jpg")) {
    dataType = "image/jpeg";
  } else if (path.endsWith(".ico")) {
    dataType = "image/x-icon";
  } else if (path.endsWith(".xml")) {
    dataType = "text/xml";
  } else if (path.endsWith(".pdf")) {
    dataType = "application/pdf";
  } else if (path.endsWith(".zip")) {
    dataType = "application/zip";
  }
  Serial.print(path.c_str());
  if (! fatfs.exists(path.c_str())) {
    Serial.println("..doesnt exist?");
    return false;
  }
  File dataFile = fatfs.open(path.c_str());
  if (! dataFile) {
    Serial.println("..couldn't open?");
    return false;
  }
  if (dataFile.isDir()) {
    path += "/index.html";
    dataType = "text/html";
    dataFile = fatfs.open(path.c_str());
  }
  if (server.hasArg("download")) {
    dataType = "application/octet-stream";
  }
  if (server.streamFile(dataFile, dataType) != dataFile.size()) {
    Serial.println("Sent less data than expected!");
  }
  dataFile.close();
  return true;
}
void returnFail(String msg) {
  server.send(500, "text/plain", msg + "\r\n");
}
void returnOK() {
  server.send(200, "text/plain", "");
}

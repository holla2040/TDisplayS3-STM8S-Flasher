#include <WiFiClient.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <FFat.h>
#include <TFT_eSPI.h>

#include "config.h"
#include "swim.h"
#include "stm8.h"
#include "ihx.h"

#define PROJECTNAME "STM8Flasher"

TFT_eSPI tft = TFT_eSPI();
WiFiManager wifiManager;
WebServer server(80);

// Target image lives in FFat, uploaded over HTTP; survives reboots.
static const char *IMAGE_PATH = "/firmware.ihx";
static const char *UPLOAD_TMP = "/upload.tmp";
static File uploadFile;

static const stm8_device_t *target = &STM8S103;
static uint8_t image[32 * 1024];
static size_t imageLen = 0;
static uint32_t passCount = 0, failCount = 0;

// Firmware build id: images carry "GITHASH:<short-hash>" as a plain string
// (see comet src/firmware/main.c build_id). Empty if the image has none.
static char fwHash[16] = "";

static void findFwHash() {
  fwHash[0] = 0;
  if (imageLen < 9) return;
  for (size_t i = 0; i <= imageLen - 9; i++) {
    if (!memcmp(image + i, "GITHASH:", 8)) {
      size_t j = i + 8, k = 0;
      while (k < sizeof(fwHash) - 1 && j < imageLen && image[j] >= ' ' && image[j] < 127)
        fwHash[k++] = image[j++];
      fwHash[k] = 0;
      return;
    }
  }
}

// WiFi is secondary: the jig flashes with or without it. WiFiManager runs
// non-blocking; OTA + mDNS start whenever a connection eventually appears.
static bool netUp = false;

static uint32_t lastPoll = 0;
static bool autoMode = false;  // default manual; MODE_BUTTON toggles
static const char *detected = "no target";   // live probe result, home page line 1

// Home page: status + waiting for the next target.
static void showHome() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.printf("%s\n", detected);   // live detection, updated by the 1 s probe
  if (imageLen) tft.printf("ihx %u %s\n", (unsigned)imageLen, fwHash);
  else tft.println("no image: browse to");
  tft.printf("pass %lu fail %lu\n", passCount, failCount);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.println(autoMode ? "waiting for target..." : "press button to flash");
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (netUp) tft.println(WiFi.localIP());
  else tft.println("wifi: setup AP " PROJECTNAME);

  // mode letter, upper right (drawString: no print-wrap involved)
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(autoMode ? "A" : "M", tft.width() - 2, 0, 2);
  tft.setTextDatum(TL_DATUM);
}

static bool wantRetest = false;

// Debounced release: returns once the pin has read high for 50 ms straight.
// Every button-triggered test starts from here, so a single press can never
// span into the next page and fire twice.
static void waitRelease(uint8_t pin) {
  uint32_t hi = millis();
  while (millis() - hi < 50) {
    if (!digitalRead(pin)) hi = millis();
    delay(5);
  }
}

// Result line at the bottom of the flash page: hold 10 s, then home.
// FLASH_BUTTON during the hold cuts it short and requests a retest.
static void showResult(bool pass, const char *stage) {
  tft.setCursor(0, 4 * 32, 2);
  tft.setTextColor(TFT_WHITE, pass ? TFT_DARKGREEN : TFT_RED);
  tft.printf(" %s %s ", pass ? "PASS" : "FAIL", stage);
  Serial.printf("%s: %s\n", pass ? "PASS" : "FAIL", stage);
  uint32_t start = millis();
  while (millis() - start < 10000) {
    if (!digitalRead(FLASH_BUTTON)) {
      waitRelease(FLASH_BUTTON);
      wantRetest = true;
      return;
    }
    delay(10);
  }
}

// Parse an ihx file from FFat into the RAM image. On success updates
// imageLen; on failure the image buffer is trashed — caller must reload.
static bool loadImage(const char *path) {
  File f = FFat.open(path);
  if (!f) return false;
  size_t sz = f.size();
  char *text = (char *)malloc(sz + 1);
  if (!text) { f.close(); return false; }
  f.read((uint8_t *)text, sz);
  text[sz] = 0;
  f.close();
  size_t n = ihx_parse(text, target->flash_start, image, sizeof(image));
  free(text);
  if (!n) return false;
  imageLen = n;
  findFwHash();
  return true;
}

static void handleRoot() {
  char page[384];
  snprintf(page, sizeof(page),
    "<h1>" PROJECTNAME "</h1><p>%s, image %u bytes, pass %lu fail %lu</p>"
    "<form method='POST' action='/upload' enctype='multipart/form-data'>"
    "<input type='file' name='ihx'> <input type='submit' value='upload'></form>",
    target->name, (unsigned)imageLen, passCount, failCount);
  server.send(200, "text/html", page);
}

static void handleUploadChunk() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) uploadFile = FFat.open(UPLOAD_TMP, FILE_WRITE);
  else if (up.status == UPLOAD_FILE_WRITE && uploadFile) uploadFile.write(up.buf, up.currentSize);
  else if (up.status == UPLOAD_FILE_END && uploadFile) uploadFile.close();
}

static void handleUploadDone() {
  if (!loadImage(UPLOAD_TMP)) {
    FFat.remove(UPLOAD_TMP);
    if (!loadImage(IMAGE_PATH)) imageLen = 0;   // restore previous image to RAM
    server.send(400, "text/plain", "not a valid ihx, kept previous image\n");
    return;
  }
  FFat.remove(IMAGE_PATH);
  FFat.rename(UPLOAD_TMP, IMAGE_PATH);
  char msg[48];
  snprintf(msg, sizeof(msg), "ok: image %u bytes\n", (unsigned)imageLen);
  server.send(200, "text/plain", msg);
  Serial.printf("new image: %u bytes\n", (unsigned)imageLen);
  showHome();
}

// Programming page: white on black, one line per phase, updated in place.
static const int LINE_H = 32;   // font 2 (16 px) at textSize 2

static void writeProgress(size_t done, size_t total) {
  tft.setCursor(0, 2 * LINE_H, 2);
  tft.printf("write %u/%u  ", (unsigned)done, (unsigned)total);
}

static void readProgress(size_t done, size_t total) {
  tft.setCursor(0, 3 * LINE_H, 2);
  tft.printf("read  %u/%u  ", (unsigned)done, (unsigned)total);
}

// Target is powered and SWIM is up: program, verify, run. Blocking is fine —
// it also keeps the radio quiet while SWIM timing matters.
// Returns false on a communication hiccup: no verdict, caller just retries.
// FAIL means one thing only: the readback didn't match the image.
static bool flashCycle() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 0, 2);
  tft.printf("%s  ihx %u", target->name, (unsigned)imageLen);
  tft.setCursor(0, LINE_H, 2);
  tft.printf("target %s", stm8_uid_present() ? "STM8S103 (uid)" : "STM8S003 (no uid)");
  writeProgress(0, imageLen);

  if (!stm8_program(target, image, imageLen, writeProgress)) {
    Serial.println("program: comm error, retrying");
    swim_reset_target(false);
    return false;
  }
  readProgress(0, imageLen);
  int v = stm8_verify(target, image, imageLen, readProgress);
  if (v < 0) {
    Serial.println("verify: comm error, retrying");
    swim_reset_target(false);
    return false;
  }

  stm8_run();
  v ? passCount++ : failCount++;
  showResult(v, v ? "done" : "verify mismatch");
  return true;
}

// Flash, honor retest presses during the result hold, then home.
static void runTests() {
  flashCycle();
  while (wantRetest) {
    wantRetest = false;
    if (!stm8_connect()) { swim_reset_target(false); break; }
    flashCycle();
  }
  showHome();
}

static void maybeStartNetwork() {
  if (netUp || WiFi.status() != WL_CONNECTED) return;
  netUp = true;
  MDNS.begin(PROJECTNAME);                 // STM8Flasher.local
  ArduinoOTA.setHostname(PROJECTNAME);
  ArduinoOTA.begin();
  server.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.printf("%s @ ", PROJECTNAME);
  Serial.println(WiFi.localIP());
  showHome();                           // refresh the wifi status line
}

void setup() {
  Serial.begin(115200);

  pinMode(MODE_BUTTON, INPUT_PULLUP);
  pinMode(FLASH_BUTTON, INPUT_PULLUP);
  swim_init();

  pinMode(15, OUTPUT);        // T-Display S3: LCD power enable, required on battery
  digitalWrite(15, HIGH);

  tft.init();
  tft.setRotation(3);

  if (!digitalRead(MODE_BUTTON)) {
    Serial.println("wifiManager reset");
    wifiManager.resetSettings();
  }
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.autoConnect(PROJECTNAME);    // returns immediately; flashing never waits on wifi

  server.on("/", HTTP_GET, handleRoot);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadChunk);

  FFat.begin(true);                        // format on first boot
  if (!loadImage(IMAGE_PATH))
    Serial.println("no firmware image; upload at http://" PROJECTNAME ".local/");

  showHome();
}

void loop() {
  wifiManager.process();
  maybeStartNetwork();
  if (netUp) {
    ArduinoOTA.handle();
    server.handleClient();
  }

  // Button edges (buttons idle high), 200 ms debounce lockout
  static bool modePrev = HIGH, flashPrev = HIGH;
  static uint32_t lastEdge = 0;
  bool modeNow = digitalRead(MODE_BUTTON);
  bool flashNow = digitalRead(FLASH_BUTTON);
  bool modePressed = (modeNow == LOW && modePrev == HIGH);
  bool flashPressed = (flashNow == LOW && flashPrev == HIGH);
  modePrev = modeNow;
  flashPrev = flashNow;
  if (millis() - lastEdge < 200) {
    modePressed = flashPressed = false;
  } else if (modePressed || flashPressed) {
    lastEdge = millis();
  }

  if (modePressed) {
    autoMode = !autoMode;
    Serial.printf("mode: %s\n", autoMode ? "auto" : "manual");
    showHome();
    return;
  }

  // Manual test: press = probe + flash right now.
  if (!autoMode && flashPressed) {
    waitRelease(FLASH_BUTTON);   // test starts on release, not press
    if (!stm8_connect()) {
      swim_reset_target(false);
      detected = "no target";
      Serial.println("manual: no target answered");
      showHome();
      tft.setCursor(0, 3 * 32, 2);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.print("no target, check wiring");
      return;
    }
    detected = stm8_uid_present(false) ? "STM8S103" : "STM8S003";
    if (imageLen) runTests();
    else { stm8_run(); showHome(); }
    return;
  }

  // Live detection probe, both modes, once a second. In auto mode a hit
  // rolls straight into the flash cycle; in manual it only updates line 1
  // and releases the target to run again.
  if (millis() - lastPoll < 1000) return;
  lastPoll = millis();

  const char *prev = detected;
  if (!stm8_connect()) {
    swim_reset_target(false);
    detected = "no target";
  } else {
    detected = stm8_uid_present(false) ? "STM8S103" : "STM8S003";
    if (autoMode && imageLen) {
      runTests();
      return;
    }
    stm8_run();   // probe only: reset target and let it run
  }
  if (detected != prev) {
    Serial.printf("detected: %s\n", detected);
    showHome();
  }
}

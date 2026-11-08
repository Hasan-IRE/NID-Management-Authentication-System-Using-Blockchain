#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include <string.h>
#include <Adafruit_Fingerprint.h>

// =====================================================
// NID SECUREGUARD
// ESP32-S3 CAMERA + R307S FINGERPRINT
// FULL UPDATED FIRMWARE
// =====================================================

// ================= WIFI SETTINGS =====================

const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

// ================= CAMERA PINS ======================

#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1

#define XCLK_GPIO_NUM      15
#define SIOD_GPIO_NUM       4
#define SIOC_GPIO_NUM       5

#define Y9_GPIO_NUM        16
#define Y8_GPIO_NUM        17
#define Y7_GPIO_NUM        18
#define Y6_GPIO_NUM        12
#define Y5_GPIO_NUM        10
#define Y4_GPIO_NUM         8
#define Y3_GPIO_NUM         9
#define Y2_GPIO_NUM        11

#define VSYNC_GPIO_NUM      6
#define HREF_GPIO_NUM       7
#define PCLK_GPIO_NUM      13

// ================= FINGERPRINT PINS =================

#define FINGER_RX           1
#define FINGER_TX           2
#define FINGER_BAUD     57600

#define MAX_FINGER_ID      127

HardwareSerial fingerSerial(1);
Adafruit_Fingerprint finger(&fingerSerial);

// ================= HTTP SERVERS ======================

// Dashboard + API: Port 80
httpd_handle_t camera_httpd = NULL;

// Camera Stream: Port 81
httpd_handle_t stream_httpd = NULL;

// ================= SYSTEM STATUS =====================

volatile bool fingerprintConnected = false;

enum FingerStatus {
    FP_IDLE,
    FP_ENROLLING,
    FP_ENROLLED,
    FP_SCANNING,
    FP_VERIFIED,
    FP_DENIED,
    FP_ERROR,
    FP_DELETED
};

volatile FingerStatus fingerprintStatus = FP_IDLE;

volatile uint16_t lastMatchedID = 0;
volatile uint16_t storedTemplates = 0;

// =====================================================
// DASHBOARD HTML
// =====================================================

static const char INDEX_HTML[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html>

<head>

<meta charset="UTF-8">

<meta name="viewport"
content="width=device-width, initial-scale=1.0">

<title>NID SecureGuard</title>

<style>

* {
    box-sizing: border-box;
}

body {
    margin: 0;
    background: #101820;
    color: white;
    font-family: Arial, sans-serif;
    text-align: center;
}

.header {
    padding: 20px;
    background: #172b3a;
}

h1 {
    margin: 0;
    font-size: 25px;
}

.container {
    max-width: 900px;
    margin: 20px auto;
    padding: 10px;
}

.camera {
    width: 95%;
    max-width: 800px;
    min-height: 180px;
    border-radius: 12px;
    border: 2px solid #00b894;
    background: #000;
    object-fit: contain;
}

.card {
    background: #1b2c39;
    margin: 10px;
    padding: 18px;
    border-radius: 12px;
}

.status {
    color: #00ff99;
    font-weight: bold;
}

.grid {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
}

.value {
    font-size: 18px;
    font-weight: bold;
    overflow-wrap: anywhere;
}

.footer {
    padding: 20px;
    color: #00b894;
    font-size: 13px;
}

#updated {
    color: #00b894;
    font-size: 13px;
}

@media(max-width:500px) {

    .grid {
        grid-template-columns: 1fr;
    }

    h1 {
        font-size: 22px;
    }

}

</style>

</head>

<body>

<div class="header">

    <h1>NID SecureGuard</h1>

    <p class="status">
        ESP32-S3 SECURITY SYSTEM
    </p>

</div>

<div class="container">

    <h3>Live Camera</h3>

    <img
        class="camera"
        id="cameraStream"
        alt="Camera Stream"
    >

    <div class="grid">

        <div class="card">

            <h3>Camera</h3>

            <p id="camera" class="status value">
                CONNECTING...
            </p>

        </div>

        <div class="card">

            <h3>Fingerprint Sensor</h3>

            <p id="sensor" class="status value">
                CHECKING...
            </p>

        </div>

        <div class="card">

            <h3>Fingerprint Status</h3>

            <p id="fpstatus" class="value">
                WAITING...
            </p>

        </div>

        <div class="card">

            <h3>Matched Fingerprint ID</h3>

            <p id="matched" class="value">
                None
            </p>

        </div>

        <div class="card">

            <h3>Stored Templates</h3>

            <p id="templates" class="value">
                0
            </p>

        </div>

        <div class="card">

            <h3>Access Result</h3>

            <p id="access" class="value">
                NOT VERIFIED
            </p>

        </div>

    </div>

    <p id="updated">
        Waiting for system data...
    </p>

</div>

<div class="footer">

    IoT-Based Secure NID Data Protection System

    <br>

    NID SecureGuard Prototype

    <br><br>

    Fingerprint matching is a prototype sensor event,
    not official NID identity verification.

</div>

<script>

// =====================================================
// CAMERA STREAM CONNECTION
// =====================================================

const cameraStream =
    document.getElementById("cameraStream");

let cameraRetryTimer = null;

let cameraStreamState = "CONNECTING";

function connectCamera() {

    const host = window.location.hostname;

    const streamURL =
        "http://" + host + ":81/stream?t=" + Date.now();

    cameraStreamState = "CONNECTING";

    document.getElementById("camera").innerText =
        "CONNECTING...";

    cameraStream.onerror = function() {

        cameraStreamState = "STREAM ERROR";

        document.getElementById("camera").innerText =
            cameraStreamState;

        if (cameraRetryTimer !== null) {
            clearTimeout(cameraRetryTimer);
        }

        cameraRetryTimer = setTimeout(
            connectCamera,
            3000
        );
    };

    cameraStream.onload = function() {

        cameraStreamState = "STREAM CONNECTED";

        document.getElementById("camera").innerText =
            "ONLINE - STREAM CONNECTED";
    };

    cameraStream.src = streamURL;
}

// =====================================================
// UPDATE DASHBOARD STATUS
// =====================================================

async function updateStatus() {

    try {

        const response = await fetch(
            "/api/status?t=" + Date.now(),
            {
                method: "GET",
                cache: "no-store"
            }
        );

        if (!response.ok) {

            throw new Error(
                "HTTP " + response.status
            );
        }

        const data = await response.json();

        console.log(
            "NID SecureGuard:",
            data
        );

        // ---------------------------------
        // FINGERPRINT CONNECTION
        // ---------------------------------

        document.getElementById("sensor").innerText =
            data.fingerprint_connected
            ? "CONNECTED"
            : "DISCONNECTED";

        // ---------------------------------
        // FINGERPRINT STATUS
        // ---------------------------------

        document.getElementById("fpstatus").innerText =
            data.fingerprint_status || "UNKNOWN";

        // ---------------------------------
        // MATCHED FINGERPRINT ID
        // ---------------------------------

        document.getElementById("matched").innerText =
            Number(data.matched_id) > 0
            ? data.matched_id
            : "None";

        // ---------------------------------
        // STORED TEMPLATES
        // ---------------------------------

        document.getElementById("templates").innerText =
            data.stored_templates ?? 0;

        // ---------------------------------
        // ACCESS RESULT
        // ---------------------------------

        document.getElementById("access").innerText =
            data.access_result || "NOT VERIFIED";

        // ---------------------------------
        // CAMERA API STATUS
        // ---------------------------------

        if (data.camera === "ONLINE") {

            if (cameraStreamState === "CONNECTING") {

                document.getElementById("camera").innerText =
                    "API ONLINE - CONNECTING STREAM";
            }

            else if (cameraStreamState === "STREAM ERROR") {

                document.getElementById("camera").innerText =
                    "API ONLINE - STREAM ERROR";
            }

            else if (cameraStreamState === "STREAM CONNECTED") {

                document.getElementById("camera").innerText =
                    "ONLINE - STREAM CONNECTED";
            }

        }

        // ---------------------------------
        // LAST UPDATE
        // ---------------------------------

        document.getElementById("updated").innerText =
            "Last updated: " +
            new Date().toLocaleTimeString();

    }

    catch(error) {

        console.error(
            "Dashboard API Error:",
            error
        );

        document.getElementById("updated").innerText =
            "API ERROR: " + error.message;

        document.getElementById("sensor").innerText =
            "API UNAVAILABLE";
    }

}

// =====================================================
// START DASHBOARD
// =====================================================

connectCamera();

updateStatus();

setInterval(
    updateStatus,
    1500
);

</script>

</body>

</html>

)rawliteral";

// =====================================================
// FINGERPRINT STATUS TEXT
// =====================================================

const char* getFingerprintStatusText(
    FingerStatus status
) {

    switch (status) {

        case FP_IDLE:
            return "IDLE";

        case FP_ENROLLING:
            return "ENROLLING";

        case FP_ENROLLED:
            return "ENROLLED";

        case FP_SCANNING:
            return "SCANNING";

        case FP_VERIFIED:
            return "VERIFIED";

        case FP_DENIED:
            return "NOT MATCHED";

        case FP_ERROR:
            return "ERROR";

        case FP_DELETED:
            return "DELETED";

        default:
            return "UNKNOWN";
    }
}

// =====================================================
// HOME PAGE HANDLER
// =====================================================

static esp_err_t index_handler(httpd_req_t *req) {

    httpd_resp_set_type(
        req,
        "text/html"
    );

    httpd_resp_set_hdr(
        req,
        "Cache-Control",
        "no-store, no-cache, must-revalidate"
    );

    httpd_resp_set_hdr(
        req,
        "Pragma",
        "no-cache"
    );

    return httpd_resp_send(
        req,
        INDEX_HTML,
        HTTPD_RESP_USE_STRLEN
    );
}

// =====================================================
// API STATUS HANDLER
// =====================================================

static esp_err_t status_handler(httpd_req_t *req) {

    char json[256];

    // Snapshot current system values

    bool connected = fingerprintConnected;

    FingerStatus currentStatus = fingerprintStatus;

    uint16_t matchedID = lastMatchedID;

    uint16_t templates = storedTemplates;

    const char* accessResult = "NOT VERIFIED";

    if (currentStatus == FP_VERIFIED) {

        accessResult = "GRANTED";

    }

    else if (currentStatus == FP_DENIED) {

        accessResult = "DENIED";

    }

    snprintf(
        json,
        sizeof(json),
        "{"
        "\"camera\":\"ONLINE\","
        "\"fingerprint_connected\":%s,"
        "\"fingerprint_status\":\"%s\","
        "\"matched_id\":%u,"
        "\"stored_templates\":%u,"
        "\"access_result\":\"%s\""
        "}",
        connected ? "true" : "false",
        getFingerprintStatusText(currentStatus),
        matchedID,
        templates,
        accessResult
    );

    httpd_resp_set_type(
        req,
        "application/json"
    );

    httpd_resp_set_hdr(
        req,
        "Cache-Control",
        "no-store, no-cache, must-revalidate"
    );

    httpd_resp_set_hdr(
        req,
        "Pragma",
        "no-cache"
    );

    httpd_resp_set_hdr(
        req,
        "Access-Control-Allow-Origin",
        "*"
    );

    return httpd_resp_sendstr(
        req,
        json
    );
}

// =====================================================
// CAMERA STREAM HANDLER
// =====================================================

static esp_err_t stream_handler(httpd_req_t *req) {

    camera_fb_t *fb = NULL;

    esp_err_t res = ESP_OK;

    char part_buf[64];

    static const char* STREAM_CONTENT_TYPE =
        "multipart/x-mixed-replace;boundary=frame";

    static const char* STREAM_BOUNDARY =
        "\r\n--frame\r\n";

    static const char* STREAM_PART =
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %u\r\n\r\n";

    res = httpd_resp_set_type(
        req,
        STREAM_CONTENT_TYPE
    );

    if (res != ESP_OK) {
        return res;
    }

    httpd_resp_set_hdr(
        req,
        "Access-Control-Allow-Origin",
        "*"
    );

    httpd_resp_set_hdr(
        req,
        "Cache-Control",
        "no-store"
    );

    while (true) {

        fb = esp_camera_fb_get();

        if (fb == NULL) {

            Serial.println(
                "Camera capture failed!"
            );

            res = ESP_FAIL;

            break;
        }

        size_t hlen = snprintf(
            part_buf,
            sizeof(part_buf),
            STREAM_PART,
            (unsigned int)fb->len
        );

        res = httpd_resp_send_chunk(
            req,
            STREAM_BOUNDARY,
            strlen(STREAM_BOUNDARY)
        );

        if (res == ESP_OK) {

            res = httpd_resp_send_chunk(
                req,
                part_buf,
                hlen
            );
        }

        if (res == ESP_OK) {

            res = httpd_resp_send_chunk(
                req,
                (const char*)fb->buf,
                fb->len
            );
        }

        esp_camera_fb_return(fb);

        fb = NULL;

        if (res != ESP_OK) {

            Serial.println(
                "Stream client disconnected"
            );

            break;
        }

        delay(10);
    }

    if (fb != NULL) {

        esp_camera_fb_return(fb);
    }

    return res;
}

// =====================================================
// START WEB SERVERS
// DASHBOARD: PORT 80
// CAMERA STREAM: PORT 81
// =====================================================

void startCameraServer() {

    // =================================
    // SERVER 1: DASHBOARD + API
    // =================================

    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();

    config.server_port = 80;

    config.ctrl_port = 32768;

    config.max_uri_handlers = 8;

    config.stack_size = 8192;

    config.max_open_sockets = 4;

    config.lru_purge_enable = true;

    httpd_uri_t index_uri = {};

    index_uri.uri = "/";

    index_uri.method = HTTP_GET;

    index_uri.handler = index_handler;

    httpd_uri_t status_uri = {};

    status_uri.uri = "/api/status";

    status_uri.method = HTTP_GET;

    status_uri.handler = status_handler;

    esp_err_t err = httpd_start(
        &camera_httpd,
        &config
    );

    if (err != ESP_OK) {

        Serial.printf(
            "Dashboard server failed: 0x%x\n",
            err
        );

        camera_httpd = NULL;

        return;
    }

    httpd_register_uri_handler(
        camera_httpd,
        &index_uri
    );

    httpd_register_uri_handler(
        camera_httpd,
        &status_uri
    );

    Serial.println(
        "Dashboard server started on port 80"
    );

    // =================================
    // SERVER 2: CAMERA STREAM
    // =================================

    httpd_config_t stream_config =
        HTTPD_DEFAULT_CONFIG();

    stream_config.server_port = 81;

    stream_config.ctrl_port = 32769;

    stream_config.max_uri_handlers = 4;

    stream_config.stack_size = 8192;

    stream_config.max_open_sockets = 4;

    stream_config.lru_purge_enable = true;

    httpd_uri_t stream_uri = {};

    stream_uri.uri = "/stream";

    stream_uri.method = HTTP_GET;

    stream_uri.handler = stream_handler;

    err = httpd_start(
        &stream_httpd,
        &stream_config
    );

    if (err != ESP_OK) {

        Serial.printf(
            "Stream server failed: 0x%x\n",
            err
        );

        stream_httpd = NULL;

        return;
    }

    httpd_register_uri_handler(
        stream_httpd,
        &stream_uri
    );

    Serial.println(
        "Camera stream server started on port 81"
    );
}

// =====================================================
// CAMERA INITIALIZATION
// =====================================================

bool initializeCamera() {

    camera_config_t config = {};

    config.ledc_channel = LEDC_CHANNEL_0;

    config.ledc_timer = LEDC_TIMER_0;

    config.pin_d0 = Y2_GPIO_NUM;

    config.pin_d1 = Y3_GPIO_NUM;

    config.pin_d2 = Y4_GPIO_NUM;

    config.pin_d3 = Y5_GPIO_NUM;

    config.pin_d4 = Y6_GPIO_NUM;

    config.pin_d5 = Y7_GPIO_NUM;

    config.pin_d6 = Y8_GPIO_NUM;

    config.pin_d7 = Y9_GPIO_NUM;

    config.pin_xclk = XCLK_GPIO_NUM;

    config.pin_pclk = PCLK_GPIO_NUM;

    config.pin_vsync = VSYNC_GPIO_NUM;

    config.pin_href = HREF_GPIO_NUM;

    config.pin_sccb_sda = SIOD_GPIO_NUM;

    config.pin_sccb_scl = SIOC_GPIO_NUM;

    config.pin_pwdn = PWDN_GPIO_NUM;

    config.pin_reset = RESET_GPIO_NUM;

    config.xclk_freq_hz = 20000000;

    config.pixel_format = PIXFORMAT_JPEG;

    config.frame_size = FRAMESIZE_QVGA;

    config.jpeg_quality = 15;

    if (psramFound()) {

        Serial.println("PSRAM FOUND!");

        config.fb_location = CAMERA_FB_IN_PSRAM;

        config.fb_count = 2;

        config.grab_mode = CAMERA_GRAB_LATEST;

    }

    else {

        Serial.println("PSRAM NOT FOUND!");

        Serial.println(
            "Using internal RAM fallback"
        );

        config.fb_location = CAMERA_FB_IN_DRAM;

        config.fb_count = 1;

        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    }

    Serial.println("Initializing camera...");

    esp_err_t err = esp_camera_init(&config);

    if (err != ESP_OK) {

        Serial.printf(
            "Camera initialization failed: 0x%x\n",
            err
        );

        return false;
    }

    Serial.println(
        "Camera initialized successfully!"
    );

    sensor_t *sensor = esp_camera_sensor_get();

    if (sensor != NULL) {

        sensor->set_framesize(
            sensor,
            FRAMESIZE_QVGA
        );

        sensor->set_quality(
            sensor,
            15
        );
    }

    return true;
}

// =====================================================
// FINGERPRINT INITIALIZATION
// =====================================================

bool initializeFingerprint() {

    Serial.println();

    Serial.println(
        "Initializing R307S..."
    );

    fingerSerial.begin(
        FINGER_BAUD,
        SERIAL_8N1,
        FINGER_RX,
        FINGER_TX
    );

    finger.begin(FINGER_BAUD);

    if (finger.verifyPassword()) {

        fingerprintConnected = true;

        Serial.println(
            "R307S CONNECTED SUCCESSFULLY!"
        );

        if (
            finger.getTemplateCount() ==
            FINGERPRINT_OK
        ) {

            storedTemplates = finger.templateCount;
        }

        fingerprintStatus = FP_IDLE;

        return true;
    }

    fingerprintConnected = false;

    fingerprintStatus = FP_ERROR;

    Serial.println(
        "R307S CONNECTION FAILED!"
    );

    Serial.println(
        "Camera will continue working."
    );

    return false;
}

// =====================================================
// READ FINGERPRINT ID
// =====================================================

uint16_t readFingerprintID() {

    Serial.print(
        "Enter Fingerprint ID (1-"
    );

    Serial.print(MAX_FINGER_ID);

    Serial.println("):");

    while (Serial.available()) {
        Serial.read();
    }

    while (!Serial.available()) {
        delay(10);
    }

    int id = Serial.parseInt();

    while (Serial.available()) {
        Serial.read();
    }

    if (
        id < 1 ||
        id > MAX_FINGER_ID
    ) {

        Serial.println("Invalid ID!");

        return 0;
    }

    return (uint16_t)id;
}

// =====================================================
// CAPTURE FINGERPRINT
// =====================================================

bool captureFingerprint() {

    Serial.println(
        "Place your finger on R307S..."
    );

    unsigned long startTime = millis();

    while (
        millis() - startTime < 15000
    ) {

        uint8_t result = finger.getImage();

        if (result == FINGERPRINT_OK) {

            Serial.println(
                "Fingerprint captured!"
            );

            return true;
        }

        if (
            result != FINGERPRINT_NOFINGER
        ) {

            Serial.print("Capture error: ");

            Serial.println(result);

            return false;
        }

        delay(100);
    }

    Serial.println(
        "Fingerprint scan timeout!"
    );

    return false;
}

// =====================================================
// WAIT FOR FINGER REMOVAL
// =====================================================

bool waitForFingerRemoval() {

    Serial.println(
        "Remove your finger..."
    );

    unsigned long startTime = millis();

    while (
        millis() - startTime < 10000
    ) {

        if (
            finger.getImage() ==
            FINGERPRINT_NOFINGER
        ) {

            Serial.println(
                "Finger removed."
            );

            return true;
        }

        delay(100);
    }

    Serial.println(
        "Finger removal timeout!"
    );

    return false;
}

// =====================================================
// ENROLL FINGERPRINT
// =====================================================

void enrollFingerprint() {

    if (!fingerprintConnected) {

        Serial.println(
            "Fingerprint sensor offline!"
        );

        return;
    }

    Serial.println();

    Serial.println(
        "===== FINGERPRINT ENROLLMENT ====="
    );

    fingerprintStatus = FP_ENROLLING;

    uint16_t id = readFingerprintID();

    if (id == 0) {

        fingerprintStatus = FP_ERROR;

        return;
    }

    // First scan

    Serial.println("FIRST SCAN");

    if (!captureFingerprint()) {

        fingerprintStatus = FP_ERROR;

        return;
    }

    uint8_t result = finger.image2Tz(1);

    if (result != FINGERPRINT_OK) {

        Serial.println(
            "First image conversion failed!"
        );

        fingerprintStatus = FP_ERROR;

        return;
    }

    Serial.println(
        "First scan successful!"
    );

    if (!waitForFingerRemoval()) {

        fingerprintStatus = FP_ERROR;

        return;
    }

    delay(500);

    // Second scan

    Serial.println("SECOND SCAN");

    if (!captureFingerprint()) {

        fingerprintStatus = FP_ERROR;

        return;
    }

    result = finger.image2Tz(2);

    if (result != FINGERPRINT_OK) {

        Serial.println(
            "Second image conversion failed!"
        );

        fingerprintStatus = FP_ERROR;

        return;
    }

    // Create model

    result = finger.createModel();

    if (result != FINGERPRINT_OK) {

        Serial.println(
            "Fingerprint images did not match!"
        );

        fingerprintStatus = FP_ERROR;

        return;
    }

    // Store template

    result = finger.storeModel(id);

    if (result == FINGERPRINT_OK) {

        Serial.println(
            "ENROLLMENT SUCCESSFUL!"
        );

        Serial.print("Fingerprint ID: ");

        Serial.println(id);

        fingerprintStatus = FP_ENROLLED;

        if (
            finger.getTemplateCount() ==
            FINGERPRINT_OK
        ) {

            storedTemplates = finger.templateCount;
        }

    }

    else {

        Serial.println(
            "Fingerprint storage failed!"
        );

        fingerprintStatus = FP_ERROR;
    }

    waitForFingerRemoval();
}

// =====================================================
// VERIFY FINGERPRINT
// =====================================================

void verifyFingerprint() {

    if (!fingerprintConnected) {

        Serial.println(
            "Fingerprint sensor offline!"
        );

        return;
    }

    Serial.println();

    Serial.println(
        "===== FINGERPRINT VERIFICATION ====="
    );

    fingerprintStatus = FP_SCANNING;

    if (!captureFingerprint()) {

        fingerprintStatus = FP_ERROR;

        return;
    }

    uint8_t result = finger.image2Tz();

    if (result != FINGERPRINT_OK) {

        Serial.println(
            "Image conversion failed!"
        );

        fingerprintStatus = FP_ERROR;

        waitForFingerRemoval();

        return;
    }

    result = finger.fingerSearch();

    if (result == FINGERPRINT_OK) {

        lastMatchedID = finger.fingerID;

        fingerprintStatus = FP_VERIFIED;

        Serial.println();

        Serial.println(
            "================================"
        );

        Serial.println(
            "FINGERPRINT VERIFIED!"
        );

        Serial.print("Matched ID: ");

        Serial.println(finger.fingerID);

        Serial.print("Confidence: ");

        Serial.println(finger.confidence);

        Serial.println("ACCESS: GRANTED");

        Serial.println(
            "================================"
        );

    }

    else if (
        result == FINGERPRINT_NOTFOUND
    ) {

        lastMatchedID = 0;

        fingerprintStatus = FP_DENIED;

        Serial.println();

        Serial.println(
            "FINGERPRINT NOT MATCHED!"
        );

        Serial.println("ACCESS: DENIED");

    }

    else {

        fingerprintStatus = FP_ERROR;

        Serial.print(
            "Verification error: "
        );

        Serial.println(result);
    }

    waitForFingerRemoval();
}

// =====================================================
// DELETE ONE FINGERPRINT
// =====================================================

void deleteFingerprint() {

    if (!fingerprintConnected) {

        Serial.println(
            "Fingerprint sensor offline!"
        );

        return;
    }

    uint16_t id = readFingerprintID();

    if (id == 0) return;

    uint8_t result = finger.deleteModel(id);

    if (result == FINGERPRINT_OK) {

        Serial.println(
            "Fingerprint deleted!"
        );

        fingerprintStatus = FP_DELETED;

        if (lastMatchedID == id) {

            lastMatchedID = 0;
        }

        if (
            finger.getTemplateCount() ==
            FINGERPRINT_OK
        ) {

            storedTemplates = finger.templateCount;
        }

    }

    else {

        Serial.println(
            "Fingerprint deletion failed!"
        );

        fingerprintStatus = FP_ERROR;
    }
}

// =====================================================
// DELETE ALL FINGERPRINTS
// =====================================================

void deleteAllFingerprints() {

    if (!fingerprintConnected) {

        Serial.println(
            "Fingerprint sensor offline!"
        );

        return;
    }

    Serial.println(
        "WARNING: This deletes ALL templates."
    );

    Serial.println(
        "Type Y to confirm:"
    );

    while (Serial.available()) {
        Serial.read();
    }

    while (!Serial.available()) {
        delay(10);
    }

    char confirm = Serial.read();

    while (Serial.available()) {
        Serial.read();
    }

    if (
        confirm == 'Y' ||
        confirm == 'y'
    ) {

        if (
            finger.emptyDatabase() ==
            FINGERPRINT_OK
        ) {

            Serial.println(
                "All templates deleted!"
            );

            lastMatchedID = 0;

            storedTemplates = 0;

            fingerprintStatus = FP_DELETED;

        }

        else {

            Serial.println(
                "Database clearing failed!"
            );

            fingerprintStatus = FP_ERROR;
        }

    }

    else {

        Serial.println(
            "Deletion cancelled."
        );
    }
}

// =====================================================
// SERIAL MENU
// =====================================================

void showMenu() {

    Serial.println();

    Serial.println(
        "========== NID SECUREGUARD =========="
    );

    Serial.println(
        "E - Enroll Fingerprint"
    );

    Serial.println(
        "S - Verify Fingerprint"
    );

    Serial.println(
        "D - Delete Fingerprint"
    );

    Serial.println(
        "A - Delete ALL Fingerprints"
    );

    Serial.println(
        "I - Sensor Information"
    );

    Serial.println(
        "H - Show Menu"
    );

    Serial.println(
        "====================================="
    );
}

// =====================================================
// SENSOR INFORMATION
// =====================================================

void sensorInformation() {

    if (!fingerprintConnected) {

        Serial.println(
            "R307S is offline!"
        );

        return;
    }

    Serial.println();

    Serial.println(
        "------ R307S INFORMATION ------"
    );

    if (
        finger.getParameters() ==
        FINGERPRINT_OK
    ) {

        Serial.print("Capacity: ");

        Serial.println(finger.capacity);

        Serial.print("Security Level: ");

        Serial.println(finger.security_level);

        Serial.print("Baud Rate: ");

        Serial.println(finger.baud_rate);
    }

    if (
        finger.getTemplateCount() ==
        FINGERPRINT_OK
    ) {

        storedTemplates = finger.templateCount;

        Serial.print("Stored Templates: ");

        Serial.println(storedTemplates);
    }

    Serial.println(
        "--------------------------------"
    );
}

// =====================================================
// WIFI CONNECTION
// =====================================================

bool connectWiFi() {

    Serial.println();

    Serial.println(
        "Connecting to Wi-Fi..."
    );

    WiFi.mode(WIFI_STA);

    WiFi.setSleep(false);

    WiFi.begin(
        ssid,
        password
    );

    int retry = 0;

    while (
        WiFi.status() != WL_CONNECTED
    ) {

        delay(500);

        Serial.print(".");

        retry++;

        if (retry >= 40) {

            Serial.println();

            Serial.println(
                "Wi-Fi connection failed!"
            );

            return false;
        }
    }

    Serial.println();

    Serial.println(
        "Wi-Fi connected!"
    );

    Serial.print("IP Address: ");

    Serial.println(WiFi.localIP());

    return true;
}

// =====================================================
// SETUP
// =====================================================

void setup() {

    Serial.begin(115200);

    Serial.setDebugOutput(true);

    delay(2000);

    Serial.println();

    Serial.println(
        "===================================="
    );

    Serial.println(
        "NID SECUREGUARD"
    );

    Serial.println(
        "ESP32-S3 + R307S"
    );

    Serial.println(
        "===================================="
    );

    // Step 1: Camera

    if (!initializeCamera()) {

        Serial.println(
            "Camera setup failed!"
        );

        return;
    }

    // Step 2: Fingerprint

    initializeFingerprint();

    // Step 3: Wi-Fi

    if (!connectWiFi()) {

        Serial.println(
            "Wi-Fi setup failed!"
        );

        return;
    }

    // Step 4: Web Servers

    startCameraServer();

    if (camera_httpd == NULL) {

        Serial.println(
            "Dashboard server could not start!"
        );

        return;
    }

    if (stream_httpd == NULL) {

        Serial.println(
            "Stream server could not start!"
        );

        return;
    }

    Serial.println();

    Serial.println(
        "===================================="
    );

    Serial.println(
        "NID SECUREGUARD SYSTEM READY!"
    );

    Serial.print("Dashboard: http://");

    Serial.println(WiFi.localIP());

    Serial.print("Camera Stream: http://");

    Serial.print(WiFi.localIP());

    Serial.println(":81/stream");

    Serial.print("Fingerprint API: http://");

    Serial.print(WiFi.localIP());

    Serial.println("/api/status");

    Serial.println(
        "===================================="
    );

    showMenu();
}

// =====================================================
// LOOP
// =====================================================

void loop() {

    if (
        WiFi.status() != WL_CONNECTED
    ) {

        Serial.println(
            "Wi-Fi disconnected! Reconnecting..."
        );

        WiFi.reconnect();

        delay(1000);
    }

    if (Serial.available()) {

        char command = Serial.read();

        switch (command) {

            case 'E':
            case 'e':

                enrollFingerprint();

                showMenu();

                break;

            case 'S':
            case 's':

                verifyFingerprint();

                showMenu();

                break;

            case 'D':
            case 'd':

                deleteFingerprint();

                showMenu();

                break;

            case 'A':
            case 'a':

                deleteAllFingerprints();

                showMenu();

                break;

            case 'I':
            case 'i':

                sensorInformation();

                showMenu();

                break;

            case 'H':
            case 'h':

                showMenu();

                break;
        }
    }

    delay(10);
}

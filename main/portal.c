#include "portal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <unistd.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "wifi_manager.h"

#define DNS_PORT 53
#define DNS_MAX_LEN 512

static const char *TAG = "PORTAL";

static esp_err_t config_get_handler(httpd_req_t *req);
static esp_err_t config_post_handler(httpd_req_t *req);
static esp_err_t captive_handler(httpd_req_t *req);
static esp_err_t apple_handler(httpd_req_t *req);
static esp_err_t android_handler(httpd_req_t *req);
static esp_err_t windows_handler(httpd_req_t *req);
static void url_decode(char *dst, const char *src, size_t dst_size);
static void parse_field(const char *body, const char *key, char *out, size_t out_size);
static void dns_server_task(void *pvParameters);

// ─── HTTP GET: Serve the form ─────────────────────────────────────
static esp_err_t config_get_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>ESP32 WiFi Setup</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;background:#f4f6f8;margin:0;padding:0;}"
        ".container{max-width:400px;margin:60px auto;background:#fff;padding:24px;"
        "border-radius:16px;box-shadow:0 4px 20px rgba(0,0,0,0.1);}"
        "h2{text-align:center;margin-bottom:20px;color:#333;}"
        "label{display:block;margin-bottom:6px;font-weight:bold;color:#444;}"
        "input[type=text],input[type=password]{width:100%;padding:12px;margin-bottom:16px;"
        "border:1px solid #ccc;border-radius:10px;box-sizing:border-box;font-size:16px;}"
        "input[type=submit]{width:100%;padding:12px;background:#0d6efd;color:white;"
        "border:none;border-radius:10px;font-size:16px;font-weight:bold;cursor:pointer;}"
        "input[type=submit]:hover{background:#0b5ed7;}"
        ".note{text-align:center;font-size:14px;color:#666;margin-top:12px;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='container'>"
        "<h2>LED Matrix WiFi Setup</h2>"
        "<form method='POST' action='/config'>"
        "<label for='ssid'>SSID</label>"
        "<input type='text' id='ssid' name='ssid' placeholder='Enter WiFi name'>"
        "<label for='pass'>Password</label>"
        "<input type='password' id='pass' name='pass' placeholder='Enter WiFi password'>"
        "<input type='submit' value='Connect'>"
        "</form>"
        "<div class='note'>Connect your LED Matrix ESP32 to your home WiFi</div>"
        "</div>"
        "</body>"
        "</html>";

    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ─── HTTP POST: Receive credentials ──────────────────────────────
static esp_err_t config_post_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
        return ESP_FAIL;
    buf[ret] = '\0';

    ESP_LOGI(TAG, "POST body: %s", buf);

    char ssid[64], pass[64];
    parse_field(buf, "ssid", ssid, sizeof(ssid));
    parse_field(buf, "pass", pass, sizeof(pass));

    ESP_LOGI(TAG, "Parsed SSID: %s", ssid);

    if (strlen(ssid) == 0)
    {
        httpd_resp_send(req, "Error: SSID cannot be empty!", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Save to NVS
    nvs_save_credentials(ssid, pass);

    // Respond BEFORE restarting
    httpd_resp_send(req,
                    "<html><body><h1>Saved! Connecting to WiFi...<br>"
                    "You can close this page.</h1></body></html>",
                    HTTPD_RESP_USE_STRLEN);

    // Small delay so response is sent, then restart
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();

    return ESP_OK;
}

static esp_err_t captive_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t apple_handler(httpd_req_t *req)
{
    return captive_handler(req);
}

static esp_err_t android_handler(httpd_req_t *req)
{
    return captive_handler(req);
}

static esp_err_t windows_handler(httpd_req_t *req)
{
    return captive_handler(req);
}

// ─── Start HTTP server ────────────────────────────────────────────
static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t get_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = config_get_handler};

        httpd_uri_t post_uri = {
            .uri = "/config",
            .method = HTTP_POST,
            .handler = config_post_handler};

        httpd_uri_t apple = {
            .uri = "/hotspot-detect.html",
            .method = HTTP_GET,
            .handler = apple_handler};

        httpd_uri_t android = {
            .uri = "/generate_204",
            .method = HTTP_GET,
            .handler = android_handler};

        httpd_uri_t windows = {
            .uri = "/connecttest.txt",
            .method = HTTP_GET,
            .handler = windows_handler};

        httpd_uri_t captive_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = captive_handler};

        httpd_register_uri_handler(server, &get_uri);
        httpd_register_uri_handler(server, &post_uri);
        httpd_register_uri_handler(server, &apple);
        httpd_register_uri_handler(server, &android);
        httpd_register_uri_handler(server, &windows);
        httpd_register_uri_handler(server, &captive_uri);

        ESP_LOGI(TAG, "Webserver started");
    }
}

// ─── URL Decode ──────────────────────────────────────────────────
// Form POST data looks like: ssid=MyNetwork&pass=MyPassword%21
// This converts %21 → '!' and '+' → ' '
static void url_decode(char *dst, const char *src, size_t dst_size)
{
    size_t i = 0;
    while (*src && i < dst_size - 1)
    {
        if (*src == '%' && src[1] && src[2])
        {
            char hex[3] = {src[1], src[2], 0};
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        }
        else if (*src == '+')
        {
            dst[i++] = ' ';
            src++;
        }
        else
        {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

// ─── Parse a field from form body ────────────────────────────────
// From "ssid=MyNet&pass=abc", extract value for key "ssid" → "MyNet"
static void parse_field(const char *body, const char *key, char *out, size_t out_size)
{
    char search[32];
    snprintf(search, sizeof(search), "%s=", key);

    char *start = strstr(body, search);
    if (!start)
    {
        out[0] = '\0';
        return;
    }
    start += strlen(search);

    char *end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    if (len >= out_size)
        len = out_size - 1;

    char encoded[128];
    strncpy(encoded, start, len);
    encoded[len] = '\0';

    url_decode(out, encoded, out_size);
}

static void dns_server_task(void *pvParameters)
{
    int sock = -1;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    uint8_t rx_buffer[DNS_MAX_LEN];
    uint8_t tx_buffer[DNS_MAX_LEN];

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "DNS socket creation failed");
        vTaskDelete(NULL);
        return;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DNS_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        ESP_LOGE(TAG, "DNS bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server started on port %d", DNS_PORT);

    while (1)
    {
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0,
                           (struct sockaddr *)&client_addr, &addr_len);

        if (len < 12)
        {
            continue;
        }

        // Copy request first
        memcpy(tx_buffer, rx_buffer, len);

        // DNS header
        // [0..1] = transaction ID (keep same)
        // [2..3] = flags
        // [4..5] = questions
        // [6..7] = answers
        // [8..9] = authority
        // [10..11] = additional

        // Set response flags: standard query response, no error
        tx_buffer[2] = 0x81;
        tx_buffer[3] = 0x80;

        // QDCOUNT stays same
        // Set ANCOUNT = 1
        tx_buffer[6] = 0x00;
        tx_buffer[7] = 0x01;

        // NSCOUNT = 0
        tx_buffer[8] = 0x00;
        tx_buffer[9] = 0x00;

        // ARCOUNT = 0
        tx_buffer[10] = 0x00;
        tx_buffer[11] = 0x00;

        int pos = 12;

        // Skip over QNAME
        while (pos < len && rx_buffer[pos] != 0x00)
        {
            pos += rx_buffer[pos] + 1;
        }

        // Need null byte + QTYPE(2) + QCLASS(2)
        if (pos + 5 > len)
        {
            continue;
        }

        pos += 1; // skip null terminator
        pos += 2; // QTYPE
        pos += 2; // QCLASS

        int answer_start = pos;

        // Answer section
        // NAME: pointer to domain name at offset 12 => 0xC00C
        tx_buffer[answer_start++] = 0xC0;
        tx_buffer[answer_start++] = 0x0C;

        // TYPE: A
        tx_buffer[answer_start++] = 0x00;
        tx_buffer[answer_start++] = 0x01;

        // CLASS: IN
        tx_buffer[answer_start++] = 0x00;
        tx_buffer[answer_start++] = 0x01;

        // TTL: 60 seconds
        tx_buffer[answer_start++] = 0x00;
        tx_buffer[answer_start++] = 0x00;
        tx_buffer[answer_start++] = 0x00;
        tx_buffer[answer_start++] = 0x3C;

        // RDLENGTH: 4
        tx_buffer[answer_start++] = 0x00;
        tx_buffer[answer_start++] = 0x04;

        // RDATA: 192.168.4.1
        tx_buffer[answer_start++] = 192;
        tx_buffer[answer_start++] = 168;
        tx_buffer[answer_start++] = 4;
        tx_buffer[answer_start++] = 1;

        sendto(sock, tx_buffer, answer_start, 0,
               (struct sockaddr *)&client_addr, sizeof(client_addr));
    }

    // Unreachable in normal flow
    close(sock);
    vTaskDelete(NULL);
}

void portal_start(void)
{
    start_webserver();
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Captive portal started");
}

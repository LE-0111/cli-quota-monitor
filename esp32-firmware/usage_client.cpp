#include "usage_client.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <stdarg.h>

static const char* SERVICE_KEYS[SERVICE_COUNT] = {"claude", "codex", "gemini"};

void UsageClient::init(const String& bridgeHost, uint16_t bridgePort) {
    _bridgeHost = bridgeHost;
    _bridgePort = bridgePort;
    memset(_lastError, 0, sizeof(_lastError));
}

void UsageClient::setError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(_lastError, ERROR_MSG_LEN, fmt, args);
    va_end(args);
    Serial.printf("[Bridge] Error: %s\n", _lastError);
}

bool UsageClient::fetchAll(ServiceUsage services[SERVICE_COUNT], int32_t& serverTimestamp) {
    String url = "http://" + _bridgeHost + ":" + String(_bridgePort) + BRIDGE_ENDPOINT;
    Serial.printf("[Bridge] GET %s\n", url.c_str());

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(10000);
    http.setReuse(false);

    if (!http.begin(client, url)) {
        setError("HTTP begin failed");
        return false;
    }

    int httpCode = http.GET();
    Serial.printf("[Bridge] HTTP %d\n", httpCode);

    if (httpCode != 200) {
        if (httpCode < 0) setError("Bridge unreachable");
        else setError("HTTP %d", httpCode);
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

    if (response.length() == 0) {
        setError("Empty response");
        return false;
    }

    Serial.printf("[Bridge] Response (%d bytes): %.300s\n", response.length(), response.c_str());

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
        setError("JSON parse: %s", err.c_str());
        return false;
    }

    serverTimestamp = doc["timestamp"] | 0;

    for (int i = 0; i < SERVICE_COUNT; i++) {
        memset(&services[i], 0, sizeof(ServiceUsage));
        strncpy(services[i].plan, "NO DATA", PLAN_STR_LEN - 1);

        JsonObject obj = doc[SERVICE_KEYS[i]];
        if (!obj.isNull()) {
            parseServiceUsage(obj, services[i]);
        }
    }

    return true;
}

void UsageClient::parseServiceUsage(JsonObject obj, ServiceUsage& out) {
    if (obj.isNull()) return;

    JsonObject fh = obj["five_hour"];
    if (!fh.isNull()) parseWindow(fh, out.five_hour);

    JsonObject sd = obj["seven_day"];
    if (!sd.isNull()) parseWindow(sd, out.seven_day);

    const char* plan = obj["plan"] | "NO DATA";
    strncpy(out.plan, plan, PLAN_STR_LEN - 1);
    out.plan[PLAN_STR_LEN - 1] = '\0';

    out.last_update_epoch = obj["last_update"] | (uint32_t)0;
    out.valid = out.five_hour.present || out.seven_day.present;
    out.fetched_at = millis();
}

void UsageClient::parseWindow(JsonObject obj, RateLimitWindow& out) {
    if (obj.isNull()) {
        out.present = false;
        return;
    }

    out.present = true;
    out.used_percentage = obj["used_percentage"] | 0.0f;

    if (obj["resets_at"].is<long>()) {
        out.resets_at = obj["resets_at"] | (uint32_t)0;
    } else {
        const char* resetStr = obj["resets_at"] | "";
        if (strlen(resetStr) > 0) {
            struct tm tm = {};
            if (sscanf(resetStr, "%d-%d-%dT%d:%d:%d",
                       &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                       &tm.tm_hour, &tm.tm_min, &tm.tm_sec) >= 6) {
                tm.tm_year -= 1900;
                tm.tm_mon -= 1;
                out.resets_at = mktime(&tm);
            }
        }
    }
}

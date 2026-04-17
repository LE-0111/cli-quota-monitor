#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "data_models.h"

class UsageClient {
public:
    void init(const String& bridgeHost, uint16_t bridgePort);

    // Fetches all services from the bridge. Fills `services[SERVICE_COUNT]`.
    // Returns true if the HTTP call succeeded, regardless of individual service state.
    bool fetchAll(ServiceUsage services[SERVICE_COUNT], int32_t& serverTimestamp);

    const char* getLastError() const { return _lastError; }

private:
    String   _bridgeHost;
    uint16_t _bridgePort;
    char     _lastError[ERROR_MSG_LEN];

    void setError(const char* fmt, ...);
    void parseServiceUsage(JsonObject obj, ServiceUsage& out);
    void parseWindow(JsonObject obj, RateLimitWindow& out);
};

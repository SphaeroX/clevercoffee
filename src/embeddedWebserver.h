/**
 * @file embeddedWebserver.h
 *
 * @brief BLE-based connectivity layer replacing the legacy WiFi web server
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>

#include "Logger.h"
#include "defaults.h"
#include "userConfig.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

enum EditableKind {
    kInteger,
    kUInt8,
    kDouble,
    kDoubletime,
    kCString,
    kFloat
};

struct editable_t {
        String displayName;
        boolean hasHelpText;
        String helpText;
        EditableKind type;
        int section;
        int position;
        std::function<bool()> show;
        int minValue;
        int maxValue;
        void* ptr;
};

extern std::map<String, editable_t> editableVars;
extern const char* hostname;

void setEepromWriteFcn(int (*fcnPtr)(void));
void serverSetup();
void sendTempEvent(double currentTemp, double targetTemp, double heaterPower);
bool bleIsConnected();

// Side-effect hooks implemented in main.cpp
void setPidStatus(int status);
void setSteamMode(int steamMode);
void setBackflush(int backflush);
#if FEATURE_SCALE == 1
void setScaleTare(int tare);
void setScaleCalibration(int calibration);
#endif

namespace EmbeddedBle {
constexpr size_t HISTORY_LENGTH = 600;
constexpr int SECONDS_TO_SKIP = 2;
constexpr size_t kMaxChunkPayload = 96; // payload per BLE notification after metadata overhead

inline double curTemp = 0.0;
inline double tTemp = 0.0;
inline double hPower = 0.0;
inline float tempHistory[3][HISTORY_LENGTH] = {0};
inline int historyCurrentIndex = 0;
inline int historyValueCount = 0;
inline int skippedValues = 0;

inline NimBLEServer* g_server = nullptr;
inline NimBLECharacteristic* g_txCharacteristic = nullptr;
inline NimBLECharacteristic* g_rxCharacteristic = nullptr;
inline bool g_clientConnected = false;
inline int (*g_writeToEeprom)(void) = nullptr;

inline const char* kServiceUuid = "b0c5c0ff-8123-4c5c-a63d-2f6f60adbb89";
inline const char* kTxUuid = "b0c5c100-8123-4c5c-a63d-2f6f60adbb89";
inline const char* kRxUuid = "b0c5c101-8123-4c5c-a63d-2f6f60adbb89";

inline double round2(double value) {
    return (int)(value * 100 + 0.5) / 100.0;
}

inline int mod(int a, int b) {
    int r = a % b;
    return r < 0 ? r + b : r;
}

inline uint8_t flipUintValue(uint8_t value) {
    return (value + 3) % 2;
}

inline String levelToString(Logger::Level level) {
    switch (level) {
        case Logger::Level::TRACE:
            return F("TRACE");
        case Logger::Level::DEBUG:
            return F("DEBUG");
        case Logger::Level::INFO:
            return F("INFO");
        case Logger::Level::WARNING:
            return F("WARNING");
        case Logger::Level::ERROR:
            return F("ERROR");
        case Logger::Level::FATAL:
            return F("FATAL");
        default:
            return F("SILENT");
    }
}

inline void notifyJson(const DynamicJsonDocument& doc) {
    if (!g_clientConnected || g_txCharacteristic == nullptr) {
        return;
    }

    String requestId;
    String command;

    if (doc.containsKey("requestId")) requestId = doc["requestId"].as<String>();
    if (doc.containsKey("command")) command = doc["command"].as<String>();

    String payload;
    serializeJson(doc, payload);

    const size_t mtu = NimBLEDevice::getMTU();
    const size_t maxDirect = (mtu > 3) ? mtu - 3 : 20;

    if (payload.length() <= maxDirect) {
        g_txCharacteristic->setValue(reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length());
        g_txCharacteristic->notify();
        return;
    }

    size_t total = (payload.length() + kMaxChunkPayload - 1) / kMaxChunkPayload;

    for (size_t index = 0; index < total; ++index) {
        size_t start = index * kMaxChunkPayload;
        size_t end = std::min(start + kMaxChunkPayload, static_cast<size_t>(payload.length()));
        String slice = payload.substring(start, end);

        DynamicJsonDocument chunkDoc(256 + slice.length());
        chunkDoc["type"] = F("chunk");
        if (!requestId.isEmpty()) chunkDoc["requestId"] = requestId;
        if (!command.isEmpty()) chunkDoc["command"] = command;
        chunkDoc["index"] = static_cast<uint32_t>(index);
        chunkDoc["count"] = static_cast<uint32_t>(total);
        chunkDoc["data"] = slice;

        String chunk;
        serializeJson(chunkDoc, chunk);
        g_txCharacteristic->setValue(reinterpret_cast<const uint8_t*>(chunk.c_str()), chunk.length());
        g_txCharacteristic->notify();
        delay(4);
    }
}

inline void sendError(const String& requestId, const char* command, const String& message) {
    DynamicJsonDocument doc(256 + message.length());
    doc["type"] = F("response");
    doc["status"] = F("error");
    doc["command"] = command;
    if (!requestId.isEmpty()) doc["requestId"] = requestId;
    doc["error"] = message;
    notifyJson(doc);
}

inline void sendAck(const String& requestId, const char* command, const std::function<void(DynamicJsonDocument&)>& builder = nullptr) {
    DynamicJsonDocument doc(256);
    doc["type"] = F("response");
    doc["status"] = F("ok");
    doc["command"] = command;
    if (!requestId.isEmpty()) doc["requestId"] = requestId;
    if (builder) builder(doc);
    notifyJson(doc);
}

inline void sendTextChunks(const char* type, const char* command, const String& requestId, const String& name, const String& text) {
    if (text.length() == 0) {
        DynamicJsonDocument doc(256);
        doc["type"] = type;
        doc["command"] = command;
        if (!requestId.isEmpty()) doc["requestId"] = requestId;
        if (!name.isEmpty()) doc["name"] = name;
        doc["index"] = 0;
        doc["more"] = false;
        doc["text"] = "";
        notifyJson(doc);
        return;
    }

    size_t offset = 0;
    uint32_t index = 0;

    while (offset < static_cast<size_t>(text.length())) {
        size_t end = std::min(offset + kMaxChunkPayload, static_cast<size_t>(text.length()));
        String slice = text.substring(offset, end);

        DynamicJsonDocument doc(256 + slice.length());
        doc["type"] = type;
        doc["command"] = command;
        if (!requestId.isEmpty()) doc["requestId"] = requestId;
        if (!name.isEmpty()) doc["name"] = name;
        doc["index"] = index;
        doc["more"] = end < static_cast<size_t>(text.length());
        doc["text"] = slice;
        notifyJson(doc);

        offset = end;
        ++index;
    }
}

inline bool applyNumericValue(editable_t& entry, double numericValue, String& error) {
    switch (entry.type) {
        case kInteger: {
            long val = static_cast<long>(std::lround(numericValue));
            if (val < entry.minValue || val > entry.maxValue) {
                error = F("value_out_of_range");
                return false;
            }
            *(int*)entry.ptr = static_cast<int>(val);
            return true;
        }
        case kUInt8: {
            long val = static_cast<long>(std::lround(numericValue));
            if (val < entry.minValue || val > entry.maxValue) {
                error = F("value_out_of_range");
                return false;
            }
            *(uint8_t*)entry.ptr = static_cast<uint8_t>(val);
            return true;
        }
        case kDouble:
        case kDoubletime: {
            if (numericValue < entry.minValue || numericValue > entry.maxValue) {
                error = F("value_out_of_range");
                return false;
            }
            *(double*)entry.ptr = numericValue;
            return true;
        }
        case kFloat: {
            if (numericValue < entry.minValue || numericValue > entry.maxValue) {
                error = F("value_out_of_range");
                return false;
            }
            *(float*)entry.ptr = static_cast<float>(numericValue);
            return true;
        }
        case kCString:
            error = F("read_only");
            return false;
    }

    error = F("unknown_type");
    return false;
}

inline void handleParameterSideEffects(const String& name, editable_t& entry) {
    if (name == F("PID_ON")) {
        setPidStatus(*(uint8_t*)entry.ptr);
    }
    else if (name == F("STEAM_MODE")) {
        setSteamMode(*(uint8_t*)entry.ptr);
    }
    else if (name == F("BACKFLUSH_ON")) {
        setBackflush(*(uint8_t*)entry.ptr);
    }
#if FEATURE_SCALE == 1
    else if (name == F("TARE_ON")) {
        setScaleTare(*(uint8_t*)entry.ptr);
    }
    else if (name == F("CALIBRATION_ON")) {
        setScaleCalibration(*(uint8_t*)entry.ptr);
    }
#endif
}

inline bool setParameterValue(const String& name, const JsonVariantConst& value, String& error) {
    auto it = editableVars.find(name);
    if (it == editableVars.end()) {
        error = F("unknown_parameter");
        return false;
    }

    editable_t& entry = it->second;

    if (entry.type == kCString) {
        error = F("read_only");
        return false;
    }

    double numeric = NAN;

    if (value.is<double>() || value.is<float>() || value.is<long>() || value.is<int>()) {
        numeric = value.as<double>();
    }
    else if (value.is<bool>()) {
        numeric = value.as<bool>() ? 1.0 : 0.0;
    }
    else if (value.is<const char*>()) {
        const char* raw = value.as<const char*>();
        if (raw == nullptr || strlen(raw) == 0) {
            error = F("empty_value");
            return false;
        }
        char* end = nullptr;
        numeric = strtod(raw, &end);
        if (end == raw || (end && *end != '\0')) {
            error = F("invalid_number");
            return false;
        }
    }
    else if (!value.isNull()) {
        String raw = value.as<String>();
        if (raw.length() == 0) {
            error = F("empty_value");
            return false;
        }
        char* end = nullptr;
        numeric = strtod(raw.c_str(), &end);
        if (end == raw.c_str() || (end && *end != '\0')) {
            error = F("invalid_number");
            return false;
        }
    }
    else {
        error = F("missing_value");
        return false;
    }

    if (std::isnan(numeric)) {
        error = F("invalid_number");
        return false;
    }

    if (!applyNumericValue(entry, numeric, error)) {
        return false;
    }

    handleParameterSideEffects(name, entry);
    return true;
}

inline void sendParameterSnapshot(const String& requestId, const char* command, const String& name, editable_t& entry) {
    DynamicJsonDocument doc(384);
    doc["type"] = F("parameter");
    doc["command"] = command;
    if (!requestId.isEmpty()) doc["requestId"] = requestId;
    doc["name"] = name;
    doc["label"] = entry.displayName;
    doc["section"] = entry.section;
    doc["position"] = entry.position;
    doc["show"] = entry.show();
    doc["hasHelp"] = entry.hasHelpText;
    doc["min"] = entry.minValue;
    doc["max"] = entry.maxValue;

    switch (entry.type) {
        case kInteger:
            doc["value"] = *(int*)entry.ptr;
            break;
        case kUInt8:
            doc["value"] = *(uint8_t*)entry.ptr;
            break;
        case kDouble:
        case kDoubletime:
            doc["value"] = round2(*(double*)entry.ptr);
            break;
        case kFloat:
            doc["value"] = round2(*(float*)entry.ptr);
            break;
        case kCString:
            doc["value"] = (const char*)entry.ptr;
            break;
    }

    notifyJson(doc);
}

class ServerCallbacks : public NimBLEServerCallbacks {
        void onConnect(NimBLEServer* pServer) override {
            g_clientConnected = true;
        }

        void onDisconnect(NimBLEServer* pServer) override {
            g_clientConnected = false;
            NimBLEDevice::startAdvertising();
        }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
        void onWrite(NimBLECharacteristic* characteristic) override {
            std::string raw = characteristic->getValue();
            if (raw.empty()) {
                return;
            }

            DynamicJsonDocument doc(2048);
            DeserializationError err = deserializeJson(doc, raw);

            String requestId;
            const char* commandPtr = "unknown";

            if (doc.containsKey("requestId")) {
                requestId = doc["requestId"].as<String>();
            }

            if (doc.containsKey("cmd")) {
                commandPtr = doc["cmd"].as<const char*>();
            }
            else if (doc.containsKey("command")) {
                commandPtr = doc["command"].as<const char*>();
            }

            if (err) {
                sendError(requestId, commandPtr, String(F("invalid_json: ")) + err.c_str());
                return;
            }

            String command(commandPtr);

            auto commitIfNeeded = [&](bool changed) {
                if (changed && g_writeToEeprom) {
                    g_writeToEeprom();
                }
            };

            if (command == F("ping")) {
                sendAck(requestId, commandPtr, [](DynamicJsonDocument& d) { d["message"] = F("pong"); });
            }
            else if (command == F("get_temperatures")) {
                sendAck(requestId, commandPtr, [](DynamicJsonDocument& d) {
                    d["current"] = round2(curTemp);
                    d["target"] = round2(tTemp);
                    d["power"] = round2(hPower);
                });
            }
            else if (command == F("get_parameters")) {
                if (doc.containsKey("names")) {
                    JsonArrayConst names = doc["names"].as<JsonArrayConst>();
                    for (JsonVariantConst nameVariant : names) {
                        String parameterName = nameVariant.as<String>();
                        auto it = editableVars.find(parameterName);
                        if (it != editableVars.end()) {
                            sendParameterSnapshot(requestId, commandPtr, parameterName, it->second);
                        }
                    }
                }
                else {
                    for (auto& pair : editableVars) {
                        sendParameterSnapshot(requestId, commandPtr, pair.first, pair.second);
                    }
                }

                sendAck(requestId, commandPtr);
            }
            else if (command == F("get_parameter_help")) {
                const char* name = doc["name"] | doc["parameter"] | "";
                if (strlen(name) == 0) {
                    sendError(requestId, commandPtr, F("missing_parameter"));
                    return;
                }

                auto it = editableVars.find(name);
                if (it == editableVars.end()) {
                    sendError(requestId, commandPtr, F("unknown_parameter"));
                    return;
                }

                sendTextChunks("help", commandPtr, requestId, it->second.displayName, it->second.helpText);
            }
            else if (command == F("set_parameter")) {
                const char* name = doc["name"] | doc["parameter"] | "";
                if (strlen(name) == 0) {
                    sendError(requestId, commandPtr, F("missing_parameter"));
                    return;
                }

                String error;
                if (!setParameterValue(name, doc["value"], error)) {
                    sendError(requestId, commandPtr, error);
                    return;
                }

                commitIfNeeded(true);

                sendAck(requestId, commandPtr, [&](DynamicJsonDocument& resp) {
                    editable_t& entry = editableVars.at(name);
                    resp["name"] = name;
                    switch (entry.type) {
                        case kInteger:
                            resp["value"] = *(int*)entry.ptr;
                            break;
                        case kUInt8:
                            resp["value"] = *(uint8_t*)entry.ptr;
                            break;
                        case kDouble:
                        case kDoubletime:
                            resp["value"] = round2(*(double*)entry.ptr);
                            break;
                        case kFloat:
                            resp["value"] = round2(*(float*)entry.ptr);
                            break;
                        case kCString:
                            resp["value"] = (const char*)entry.ptr;
                            break;
                    }
                });
            }
            else if (command == F("set_parameters")) {
                if (!doc.containsKey("items")) {
                    sendError(requestId, commandPtr, F("missing_items"));
                    return;
                }

                JsonArray items = doc["items"].as<JsonArray>();
                std::vector<String> updated;

                for (JsonVariant item : items) {
                    const char* name = item["name"] | "";
                    if (strlen(name) == 0) {
                        continue;
                    }

                    String error;
                    if (setParameterValue(name, item["value"], error)) {
                        updated.emplace_back(name);
                    }
                }

                commitIfNeeded(!updated.empty());

                sendAck(requestId, commandPtr, [&](DynamicJsonDocument& resp) {
                    JsonArray changed = resp.createNestedArray("updated");
                    for (const String& name : updated) {
                        changed.add(name);
                    }
                });
            }
            else if (command == F("factory_reset")) {
                if (factoryReset() == 0) {
                    sendAck(requestId, commandPtr);
                }
                else {
                    sendError(requestId, commandPtr, F("factory_reset_failed"));
                }
            }
            else if (command == F("log_level")) {
                const char* levelName = doc["level"] | "";
                if (strlen(levelName) == 0) {
                    sendError(requestId, commandPtr, F("missing_level"));
                    return;
                }

                String lvl(levelName);
                lvl.toUpperCase();

                if (lvl == F("TRACE")) Logger::setLevel(Logger::Level::TRACE);
                else if (lvl == F("DEBUG")) Logger::setLevel(Logger::Level::DEBUG);
                else if (lvl == F("INFO")) Logger::setLevel(Logger::Level::INFO);
                else if (lvl == F("WARNING")) Logger::setLevel(Logger::Level::WARNING);
                else if (lvl == F("ERROR")) Logger::setLevel(Logger::Level::ERROR);
                else if (lvl == F("FATAL")) Logger::setLevel(Logger::Level::FATAL);
                else {
                    sendError(requestId, commandPtr, F("invalid_level"));
                    return;
                }

                sendAck(requestId, commandPtr);
            }
            else {
                sendError(requestId, commandPtr, F("unknown_command"));
            }
        }
};

inline void sendLogMessage(Logger::Level level, const String& message) {
    if (!g_clientConnected || g_txCharacteristic == nullptr) {
        return;
    }

    DynamicJsonDocument doc(256 + message.length());
    doc["type"] = F("log");
    doc["command"] = F("log");
    doc["level"] = levelToString(level);
    doc["message"] = message;
    notifyJson(doc);
}

} // namespace EmbeddedBle

inline bool bleIsConnected() {
    return EmbeddedBle::g_clientConnected;
}

inline void setEepromWriteFcn(int (*fcnPtr)(void)) {
    EmbeddedBle::g_writeToEeprom = fcnPtr;
}

inline void serverSetup() {
    using namespace EmbeddedBle;

    String deviceName = (hostname != nullptr && strlen(hostname) > 0) ? hostname : "CleverCoffee";

    NimBLEDevice::init(deviceName.c_str());    NimBLEDevice::setMTU(247);

    g_server = NimBLEDevice::createServer();
    g_server->setCallbacks(new ServerCallbacks());

    NimBLEService* service = g_server->createService(kServiceUuid);
    g_txCharacteristic = service->createCharacteristic(kTxUuid, NIMBLE_PROPERTY::NOTIFY);
    g_rxCharacteristic = service->createCharacteristic(kRxUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    g_rxCharacteristic->setCallbacks(new CommandCallbacks());

    service->start();

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(service->getUUID());
    advertising->setScanResponse(true);
    advertising->start();

    Logger::setSink([](Logger::Level level, const String& msg) {
        EmbeddedBle::sendLogMessage(level, msg);
    });
}

inline void sendTempEvent(double currentTemp, double targetTemp, double heaterPower) {
    using namespace EmbeddedBle;

    curTemp = currentTemp;
    tTemp = targetTemp;
    hPower = heaterPower;

    if (skippedValues > 0 && skippedValues % SECONDS_TO_SKIP == 0) {
        tempHistory[0][historyCurrentIndex] = static_cast<float>(currentTemp);
        tempHistory[1][historyCurrentIndex] = static_cast<float>(targetTemp);
        tempHistory[2][historyCurrentIndex] = static_cast<float>(heaterPower);
        historyCurrentIndex = (historyCurrentIndex + 1) % HISTORY_LENGTH;
        historyValueCount = std::min(historyValueCount + 1, static_cast<int>(HISTORY_LENGTH - 1));
        skippedValues = 0;
    }
    else {
        skippedValues++;
    }

    if (!g_clientConnected || g_txCharacteristic == nullptr) {
        return;
    }

    DynamicJsonDocument doc(256);
    doc["type"] = F("telemetry");
    doc["command"] = F("temperatures");
    doc["current"] = round2(currentTemp);
    doc["target"] = round2(targetTemp);
    doc["power"] = round2(heaterPower);
    notifyJson(doc);
}


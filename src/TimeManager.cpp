#include "TimeManager.h"

TimeManager timeManager;

TimeManager::TimeManager() {
    synced = false;
    lastSyncTime = 0;
    strcpy(currentTimezone, DEFAULT_TIMEZONE);
    strcpy(ntpServer, DEFAULT_NTP_SERVER);
    sysConfig = nullptr;
}

bool TimeManager::begin(SystemConfig* config) {
    sysConfig = config;

    if (config) {
        if (strlen(config->timezone) > 0) {
            strcpy(currentTimezone, config->timezone);
        }
        if (strlen(config->ntp_server) > 0) {
            strcpy(ntpServer, config->ntp_server);
        }
    }

    Serial.printf("[Time] Configurando zona horaria: %s\n", currentTimezone);
    Serial.printf("[Time] Servidor NTP: %s\n", ntpServer);

    // Buscar la configuración de zona horaria
    const TimezoneInfo* tzInfo = getTimezoneInfo(currentTimezone);
    if (tzInfo) {
        configureTimezone(tzInfo->tzString);
    } else {
        // Usar offset manual si no se encuentra
        char tzStr[32];
        snprintf(tzStr, sizeof(tzStr), "UTC%d", -sysConfig->utc_offset);
        configureTimezone(tzStr);
    }

    return syncTime();
}

void TimeManager::setTimezone(const char* timezone) {
    strncpy(currentTimezone, timezone, 63);
    currentTimezone[63] = '\0';

    const TimezoneInfo* tzInfo = getTimezoneInfo(timezone);
    if (tzInfo) {
        configureTimezone(tzInfo->tzString);
        Serial.printf("[Time] Zona horaria cambiada a: %s\n", timezone);
    }
}

void TimeManager::setNTPServer(const char* server) {
    strncpy(ntpServer, server, 63);
    ntpServer[63] = '\0';
    Serial.printf("[Time] Servidor NTP cambiado a: %s\n", server);
}

void TimeManager::configureTimezone(const char* tzString) {
    setenv("TZ", tzString, 1);
    tzset();
}

bool TimeManager::syncTime() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Time] WiFi no conectado, no se puede sincronizar");
        return false;
    }

    Serial.println("[Time] Sincronizando con NTP...");

    configTime(0, 0, ntpServer, "time.nist.gov", "time.google.com");

    // Esperar sincronización
    int retry = 0;
    while (time(nullptr) < 100000 && retry < 20) {
        delay(500);
        Serial.print(".");
        retry++;
    }
    Serial.println();

    if (time(nullptr) > 100000) {
        synced = true;
        lastSyncTime = millis();
        Serial.printf("[Time] Sincronizado: %s\n", getDateTimeString().c_str());
        return true;
    }

    Serial.println("[Time] Error al sincronizar");
    synced = false;
    return false;
}

String TimeManager::getTimeString() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "--:--:--";
    }

    char buffer[16];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
    return String(buffer);
}

String TimeManager::getDateString() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "--/--/----";
    }

    char buffer[16];
    strftime(buffer, sizeof(buffer), "%d/%m/%Y", &timeinfo);
    return String(buffer);
}

String TimeManager::getDateTimeString() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "--/--/---- --:--:--";
    }

    char buffer[32];
    strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);
    return String(buffer);
}

time_t TimeManager::getEpochTime() {
    return time(nullptr);
}

struct tm TimeManager::getTimeInfo() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        memset(&timeinfo, 0, sizeof(timeinfo));
    }
    return timeinfo;
}

bool TimeManager::isSynced() {
    return synced && time(nullptr) > 100000;
}

unsigned long TimeManager::getLastSync() {
    return lastSyncTime;
}

String TimeManager::formatTime(time_t timestamp, const char* format) {
    struct tm* timeinfo = localtime(&timestamp);
    char buffer[64];
    strftime(buffer, sizeof(buffer), format, timeinfo);
    return String(buffer);
}

const TimezoneInfo* TimeManager::getTimezoneInfo(const char* name) {
    for (size_t i = 0; i < TIMEZONES_COUNT; i++) {
        if (strcmp(TIMEZONES[i].name, name) == 0) {
            return &TIMEZONES[i];
        }
    }
    return nullptr;
}

String TimeManager::listTimezones() {
    String list = "";
    for (size_t i = 0; i < TIMEZONES_COUNT; i++) {
        list += String(TIMEZONES[i].name);
        list += " (UTC";
        if (TIMEZONES[i].utcOffset >= 0) list += "+";
        list += String(TIMEZONES[i].utcOffset);
        list += ")\n";
    }
    return list;
}

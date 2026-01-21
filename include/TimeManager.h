#ifndef TIMEMANAGER_H
#define TIMEMANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"

// Zonas horarias predefinidas
struct TimezoneInfo {
    const char* name;
    const char* tzString;
    int utcOffset;
};

const TimezoneInfo TIMEZONES[] = {
    {"America/Argentina/Buenos_Aires", "ART3", -3},
    {"America/Santiago", "CLT4CLST,M10.2.0/0,M3.2.0/0", -4},
    {"America/Bogota", "COT5", -5},
    {"America/Mexico_City", "CST6CDT,M4.1.0,M10.5.0", -6},
    {"America/Lima", "PET5", -5},
    {"America/Caracas", "VET4", -4},
    {"America/Sao_Paulo", "BRT3BRST,M10.3.0/0,M2.3.0/0", -3},
    {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3", 1},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0", 0},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0", -5},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0", -8},
    {"Asia/Tokyo", "JST-9", 9},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3", 10}
};

#define TIMEZONES_COUNT (sizeof(TIMEZONES) / sizeof(TIMEZONES[0]))

class TimeManager {
public:
    TimeManager();

    // Inicialización
    bool begin(SystemConfig* config);

    // Configuración
    void setTimezone(const char* timezone);
    void setNTPServer(const char* server);
    bool syncTime();

    // Obtener tiempo
    String getTimeString();
    String getDateString();
    String getDateTimeString();
    time_t getEpochTime();
    struct tm getTimeInfo();

    // Estado
    bool isSynced();
    unsigned long getLastSync();

    // Utilidades
    String formatTime(time_t timestamp, const char* format);
    static const TimezoneInfo* getTimezoneInfo(const char* name);
    static String listTimezones();

private:
    SystemConfig* sysConfig;
    bool synced;
    unsigned long lastSyncTime;
    char currentTimezone[64];
    char ntpServer[64];

    void configureTimezone(const char* tzString);
};

// Instancia global
extern TimeManager timeManager;

#endif // TIMEMANAGER_H

# Sistema Cortinas - RF Controller ESP32 + CC1101

## Resumen del Proyecto
Controlador RF universal basado en ESP32 + módulo CC1101 para automatizar cortinas motorizadas y otros dispositivos RF. Soporta múltiples protocolos y se integra con Home Assistant vía MQTT.

**Versión actual:** 1.2.1
**Repositorio:** https://github.com/DiraSmart/sistema-cortinas.git

## Hardware
- **MCU:** ESP32 DevKit
- **Módulo RF:** CC1101 (transceptor multi-frecuencia)
- **Pines SPI:**
  - GDO0: 13 (datos/interrupción)
  - GDO2: 12 (estado)
  - CSN: 5 (Chip Select)
  - SCK: 18, MISO: 19, MOSI: 23

## Protocolos Soportados

### A-OK AC114 (Cortinas motorizadas) ✓ IMPLEMENTADO
- **Frecuencia:** 433.92 MHz
- **Modulación:** ASK/OOK Manchester
- **Timing:** Short=270µs, Long=565µs, AGC=5300µs
- **Frame:** 8 bytes (Start 0xA3 + RemoteID 24-bit + Address 16-bit + Cmd + Checksum)
- **Comandos:** UP=0x11, DOWN=0x33, STOP=0x55, PROG=0xCC
- **Canal 0:** Control de grupo (todos los canales)
- **Características:**
  - Auto-descubrimiento de controles (captura señal → extrae RemoteID)
  - Checksum XOR para verificación
  - Sin rolling code (se puede clonar)

### Somfy RTS ✓ IMPLEMENTADO
- **Frecuencia:** 433.42 MHz (específica Somfy)
- **Modulación:** ASK/OOK con rolling code
- **Frame:** 7 bytes encriptados con XOR
- **Comandos:** My=0x1, Up=0x2, Down=0x4, Prog=0x8
- **IMPORTANTE:** Usa rolling code, NO se puede clonar un control existente
- Se debe emparejar como nuevo control con botón PROG

### Dooya Unidireccional ✓ IMPLEMENTADO
- **Frecuencia:** 433.92 MHz
- **Protocolo:** ASK/OOK simple, 24-28 bits
- **Timing:** Short=350µs, Long=700µs

### Dooya Bidireccional (DDxxxx) ✓ IMPLEMENTADO
- **Frecuencia:** 433.92 MHz
- **Modulación:** 2-FSK (no ASK)
- **Frame:** 10 bytes (compatible RFXCOM)
- **IMPORTANTE:** Usa encriptación, NO se puede clonar

### Genérico (EV1527, PT2262, etc.)
- Captura y reproducción de señales genéricas ASK/OOK
- Frecuencias: 300-915 MHz configurables

## Estructura de Archivos

```
├── src/
│   ├── main.cpp              # Setup y loop principal
│   ├── CC1101_RF.cpp         # Driver CC1101 y captura/transmisión RF
│   ├── WebServer.cpp         # API REST y servidor web
│   ├── Storage.cpp           # Persistencia en LittleFS (JSON)
│   ├── MQTTClient.cpp        # Integración MQTT + Home Assistant
│   ├── AOK_Protocol.cpp      # Protocolo A-OK AC114
│   ├── SomfyRTS.cpp          # Protocolo Somfy RTS
│   ├── DooyaBidir.cpp        # Protocolo Dooya bidireccional
│   └── WiFiManager.cpp       # Gestión WiFi (STA + AP fallback)
│
├── include/
│   ├── config.h              # Configuración global, pines, constantes
│   ├── AOK_Protocol.h
│   ├── CC1101_RF.h
│   ├── Storage.h
│   └── ... (headers)
│
├── data/                     # Interfaz web (LittleFS)
│   ├── index.html            # UI principal
│   ├── app.js                # Lógica frontend
│   └── style.css             # Estilos
│
└── platformio.ini            # Configuración PlatformIO
```

## Comandos de Compilación (PlatformIO)
```bash
pio run                    # Compilar
pio run -t upload          # Subir firmware
pio run -t uploadfs        # Subir filesystem (web UI)
pio device monitor         # Monitor serial
```

## API REST Endpoints

### Dispositivos
- `GET /api/devices` - Listar dispositivos
- `POST /api/devices` - Crear dispositivo
- `PUT /api/devices/{id}` - Actualizar dispositivo
- `DELETE /api/devices/{id}` - Eliminar dispositivo
- `POST /api/devices/{id}/signal/{index}` - Enviar señal

### Captura RF
- `POST /api/capture/start` - Iniciar captura
- `POST /api/capture/stop` - Detener captura
- `GET /api/capture/status` - Estado de captura
- `POST /api/capture/save` - Guardar señal capturada

### A-OK
- `POST /api/aok/detect` - Detectar RemoteID de señal capturada
- `POST /api/aok/send` - Enviar comando A-OK

### Somfy
- `POST /api/somfy/send` - Enviar comando Somfy

### Sistema
- `GET /api/status` - Estado del sistema
- `POST /api/config` - Guardar configuración
- `POST /api/backup` - Crear backup
- `POST /api/restore` - Restaurar backup
- `POST /api/restart` - Reiniciar ESP32

## Integración Home Assistant (MQTT)

### Configuración
- Servidor MQTT configurable desde web UI
- Discovery automático (prefix: `homeassistant`)
- Topic base: `rf_controller`

### Entidades creadas automáticamente
- **Cortinas:** `cover.{device_name}` con OPEN/CLOSE/STOP
- **Switches:** `switch.{device_name}` con ON/OFF
- **Botones:** `button.{device_name}_{signal}` para señales individuales
- **Sensores diagnóstico:** WiFi RSSI, uptime, memoria libre

## Credenciales por Defecto
- **WiFi AP:** RF_Controller / 12345678
- **Web UI:** admin / dirasmart1
- **IP en modo AP:** 192.168.4.1

## Notas Técnicas

### Timing de Transmisión
- Delay entre repeticiones: 0.5ms (500µs)
- Repeticiones por defecto: 6 (configurable por señal)
- El delay largo (50ms) causaba que cada repetición se interpretara como comando separado

### Captura RF
- Se puede capturar sin seleccionar dispositivo primero
- Útil para workflow A-OK: capturar → detectar → crear dispositivo
- Requiere dispositivo seleccionado solo para guardar

### Backup
- Incluye todos los dispositivos y configuración
- Para A-OK guarda: remoteId, channel
- Para Somfy guarda: address, rollingCode, encryptionKey
- Formato JSON descargable desde web UI

## Historial de Cambios

### v1.2.1 (actual)
- Soporte completo protocolo A-OK AC114
- Auto-descubrimiento de controles A-OK
- Captura RF sin requerir dispositivo
- Reducción delay repeticiones (50ms → 0.5ms)
- Canal 0 para control de grupo A-OK

### v1.2.0
- Fixes MQTT y unique client ID
- Integración Home Assistant mejorada
- Sensores diagnóstico

### v1.1.x
- Soporte Somfy RTS
- Soporte Dooya bidireccional
- Mejoras web UI

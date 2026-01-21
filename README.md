# RF Controller - ESP32 + CC1101

Sistema de control RF para dispositivos genéricos (cortinas, interruptores, portones, etc.) con ESP32 y módulo CC1101.

## Características

- **Copy & Replay RF**: Captura y reproduce señales de controles remotos RF
- **Multi-frecuencia**: Soporte para 300-928 MHz (433.92, 315, 868 MHz, etc.)
- **Somfy RTS**: Soporte nativo para cortinas Somfy con rolling code (433.42 MHz)
- **Dooya Bidireccional**: Soporte para motores Dooya DDxxxx con protocolo FSK
- **Detección automática**: Escanea frecuencias para encontrar la señal
- **Interfaz Web**: Portal de configuración en 192.168.4.1
- **MQTT + Home Assistant**: Integración completa con auto-discovery
- **Múltiples dispositivos**: Hasta 50 dispositivos con 4 señales cada uno
- **Backup/Restore**: Sistema completo de respaldo
- **NTP**: Sincronización horaria con múltiples zonas horarias

## Hardware Requerido

- ESP32 (cualquier variante con SPI)
- Módulo CC1101 (para 433 MHz o multi-banda)
- Cables de conexión

### Conexiones ESP32 -> CC1101

| CC1101 | ESP32 | Descripción |
|--------|-------|-------------|
| VCC    | 3.3V  | Alimentación (NO usar 5V) |
| GND    | GND   | Tierra |
| CSN    | GPIO 5| Chip Select |
| SCK    | GPIO 18| SPI Clock |
| MISO   | GPIO 19| SPI MISO |
| MOSI   | GPIO 23| SPI MOSI |
| GDO0   | GPIO 13| Datos/Interrupción |
| GDO2   | GPIO 12| Estado |

## Instalación

### Usando PlatformIO (Recomendado)

1. Instala [PlatformIO](https://platformio.org/)
2. Clona o descarga este proyecto
3. Abre el proyecto en PlatformIO
4. Conecta el ESP32
5. Compila y sube:
   ```bash
   pio run -t upload
   ```
6. Sube los archivos web:
   ```bash
   pio run -t uploadfs
   ```

### Usando Arduino IDE

1. Instala las librerías requeridas:
   - ArduinoJson (v6.x)
   - PubSubClient
   - ESPAsyncWebServer
   - AsyncTCP
   - SmartRC-CC1101-Driver-Lib

2. Configura la partición SPIFFS/LittleFS
3. Sube el sketch
4. Usa el plugin ESP32 Sketch Data Upload para subir la carpeta `data`

## Uso

### Primera Configuración

1. Enciende el ESP32
2. Conéctate a la red WiFi `RF_Controller` (contraseña: `12345678`)
3. Abre http://192.168.4.1 en tu navegador
4. Configura tu red WiFi en la pestaña "Configuración"
5. Una vez conectado, accede desde la IP asignada

### Capturar Señales

1. Ve a la pestaña "Capturar"
2. Selecciona la frecuencia (433.92 MHz es la más común)
3. Activa "Detección automática" si no conoces la frecuencia
4. Presiona "Iniciar Captura"
5. Presiona el botón del control remoto que deseas copiar
6. Una vez capturada, guárdala en un dispositivo

### Agregar Dispositivos

1. Ve a la pestaña "Dispositivos"
2. Presiona "+ Agregar"
3. Ingresa nombre, tipo y habitación
4. Captura las señales necesarias (abrir, cerrar, etc.)

### Tipos de Dispositivos

| Tipo | Señales típicas |
|------|-----------------|
| Cortina | Abrir, Cerrar, Parar |
| Cortina Somfy RTS | Up, Down, Stop, Prog (rolling code) |
| Cortina Dooya DD | Up, Down, Stop, Prog (bidireccional) |
| Interruptor | Encender, Apagar |
| Portón | Abrir/Toggle |
| Luz | Encender, Apagar |
| Botón | Pulso único |

## Protocolos Especiales

### Somfy RTS (Radio Technology Somfy)

Protocolo propietario con rolling code para cortinas y persianas Somfy.

**Características:**
- Frecuencia: 433.42 MHz
- Modulación: ASK/OOK con codificación Manchester
- Frame: 7 bytes con rolling code
- Seguridad: XOR obfuscation + checksum

**Configuración:**
1. Agregar dispositivo tipo "Cortina Somfy"
2. Ingresar dirección del control (24 bits, ej: 0x123456)
3. Ingresar rolling code inicial (16 bits)
4. Para emparejar: poner motor en modo prog y enviar comando PROG

### Dooya Bidireccional (DDxxxx)

Protocolo FSK para motores Dooya de la serie DD (compatible con RFXCOM).

**Características:**
- Frecuencia: 433.92 MHz
- Modulación: 2-FSK
- Frame: 10 bytes (formato RFXCOM)
- Estructura: `09 19 15 00 [ID1] [ID2] [ID3] [ID4+Unit] [CMD] 00`

**Comandos:**
- UP: 0x00
- DOWN: 0x01
- STOP: 0x02
- PROG: 0x03

**Configuración:**
1. Agregar dispositivo tipo "Cortina Dooya DD"
2. Ingresar Device ID (28 bits, capturado del control original)
3. Ingresar Unit Code (0-15)
4. Para emparejar: poner motor en modo prog y enviar comando PROG

## Integración con Home Assistant

### Configuración MQTT

1. Ve a Configuración > MQTT
2. Habilita MQTT
3. Ingresa los datos de tu broker:
   - Servidor: IP del broker (ej: 192.168.1.100)
   - Puerto: 1883 (por defecto)
   - Usuario/Contraseña: si es requerido
4. Activa "Auto-discovery Home Assistant"
5. Guarda la configuración

### Entidades en Home Assistant

Los dispositivos aparecerán automáticamente según su tipo:
- **Cortinas** → `cover.nombre_dispositivo`
- **Interruptores** → `switch.nombre_dispositivo`
- **Otros** → `button.nombre_dispositivo_signal_X`

### Ejemplo de Automatización

```yaml
automation:
  - alias: "Cerrar cortinas al atardecer"
    trigger:
      platform: sun
      event: sunset
    action:
      service: cover.close_cover
      target:
        entity_id: cover.cortina_living
```

## API REST

### Endpoints Disponibles

| Método | Endpoint | Descripción |
|--------|----------|-------------|
| GET | `/api/status` | Estado del sistema |
| GET | `/api/config` | Obtener configuración |
| POST | `/api/config` | Guardar configuración |
| GET | `/api/devices` | Listar dispositivos |
| POST | `/api/devices` | Agregar dispositivo |
| POST | `/api/devices/update` | Actualizar dispositivo |
| GET | `/api/devices/delete?id=X` | Eliminar dispositivo |
| GET | `/api/rf/transmit?id=X&signal=Y` | Transmitir señal |
| GET | `/api/rf/capture/start` | Iniciar captura |
| GET | `/api/rf/capture/stop` | Detener captura |
| GET | `/api/rf/capture/get` | Obtener señal capturada |
| POST | `/api/rf/signal/save` | Guardar señal |
| GET | `/api/rf/frequency?freq=X` | Cambiar frecuencia |
| GET | `/api/rf/scan` | Escanear frecuencias |
| GET | `/api/backup` | Descargar backup |
| POST | `/api/restore` | Restaurar backup |
| GET | `/api/wifi/scan` | Escanear redes WiFi |
| POST | `/api/wifi/connect` | Conectar a WiFi |
| GET | `/api/reboot` | Reiniciar |
| GET | `/api/factory-reset` | Restaurar de fábrica |

### Ejemplo: Transmitir Señal

```bash
curl "http://192.168.1.100/api/rf/transmit?id=abc123&signal=0"
```

### Ejemplo: Capturar Señal

```bash
# Iniciar captura
curl "http://192.168.1.100/api/rf/capture/start?frequency=433.92&auto=true"

# Obtener resultado
curl "http://192.168.1.100/api/rf/capture/get?timeout=10000"
```

## WebSocket

El sistema también expone un WebSocket en `/ws` para comunicación en tiempo real:

```javascript
const ws = new WebSocket('ws://192.168.1.100/ws');

// Enviar comando
ws.send(JSON.stringify({
  cmd: 'transmit',
  deviceId: 'abc123',
  signalIndex: 0
}));

// Recibir estado
ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  console.log('Estado:', data);
};
```

## Frecuencias Soportadas

| Frecuencia | Región | Uso común |
|------------|--------|-----------|
| 300 MHz | - | Industrial |
| 303.87 MHz | USA | Controles de garaje |
| 310 MHz | - | Industrial |
| 315 MHz | USA/Asia | Controles de auto |
| 390 MHz | - | Alarmas |
| 418 MHz | - | Industrial |
| **433.92 MHz** | **Europa/Latam** | **El más común** |
| 868 MHz | Europa | Domótica |
| 915 MHz | USA | ISM band |

## Solución de Problemas

### El CC1101 no se detecta
- Verifica las conexiones SPI
- Asegúrate de usar 3.3V (NO 5V)
- Comprueba que el módulo no esté dañado

### No captura señales
- Verifica la frecuencia correcta
- Usa detección automática
- Acerca el control al módulo
- Algunos controles usan protocolos especiales (rolling code)

### No se conecta a WiFi
- Verifica SSID y contraseña
- Asegúrate de que la red sea 2.4 GHz
- Reinicia el dispositivo

### MQTT no conecta
- Verifica IP y puerto del broker
- Comprueba credenciales
- Asegúrate de que el broker esté accesible

## Limitaciones

- **No soporta rolling code de autos**: Controles de autos modernos con seguridad avanzada
- **Dooya FSK parcial**: La modulación FSK puede requerir ajustes según el módulo CC1101
- **Distancia limitada**: Depende de la antena del CC1101

## Dispositivos Compatibles

Probado con:
- **Cortinas Somfy RTS** (Telis, Situo, etc.)
- **Motores Dooya DD** (DD1710, DD7002B, etc.)
- Cortinas genéricas RF (Zemismart, etc.)
- Controles de garaje universales
- Enchufes RF 433 MHz
- Interruptores de pared RF
- Sensores de apertura RF

## Contribuir

Las contribuciones son bienvenidas. Por favor:
1. Haz fork del proyecto
2. Crea una rama para tu feature
3. Envía un pull request

## Licencia

Este proyecto es de código abierto bajo la licencia MIT.

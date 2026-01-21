// ============================================
// RF Controller - DEMO MODE
// Simulación sin hardware ESP32
// ============================================

// ============================================
// Constantes de tipos de dispositivo
// ============================================
const DEVICE_TYPES = {
    1: { name: 'Cortina', signals: ['Abrir', 'Cerrar', 'Parar'], icon: '🪟' },
    2: { name: 'Interruptor', signals: ['Encender', 'Apagar'], icon: '🔘' },
    3: { name: 'Botón', signals: ['Pulsar'], icon: '⏺' },
    4: { name: 'Portón', signals: ['Toggle', 'Abrir', 'Cerrar'], icon: '🚪' },
    5: { name: 'Luz', signals: ['Encender', 'Apagar'], icon: '💡' },
    6: { name: 'Ventilador', signals: ['Encender', 'Apagar', 'Velocidad'], icon: '🌀' },
    7: { name: 'Dimmer', signals: ['Encender', 'Apagar', 'Subir', 'Bajar'], icon: '🔆' },
    10: { name: 'Cortina Somfy', signals: ['Subir', 'Bajar', 'My/Parar'], icon: '🪟', isSomfy: true },
    11: { name: 'Cortina Dooya DD', signals: ['Subir', 'Bajar', 'Parar'], icon: '🪟', isDooyaBidir: true },
    99: { name: 'Otro', signals: ['Señal 1', 'Señal 2', 'Señal 3', 'Señal 4'], icon: '📡' }
};

const PROTOCOLS = {
    0: 'Desconocido',
    1: 'Genérico ASK/OOK',
    2: 'Dooya',
    3: 'Zemismart',
    4: 'Tuya RF',
    5: 'EV1527',
    6: 'PT2262',
    7: 'Nice Flor-s',
    8: 'Came',
    9: 'Vertilux/VTI',
    10: 'Somfy RTS',
    11: 'Dooya Bidir (FSK)'
};

// Estado global
let devices = [];
let config = {};
let capturedSignal = null;
let currentEditDevice = null;
let demoUptime = 0;
let captureTimeout = null;

// ============================================
// Inicialización
// ============================================

document.addEventListener('DOMContentLoaded', function() {
    logConsole('==============================================', 'info');
    logConsole('   RF Controller - ESP32 + CC1101 (DEMO)', 'info');
    logConsole('   Version 1.0.0', 'info');
    logConsole('==============================================', 'info');
    logConsole('', 'info');

    initTabs();
    loadFromLocalStorage();
    updateTime();
    setInterval(updateTime, 1000);
    setInterval(updateUptime, 1000);

    // Simular inicialización del sistema
    simulateSystemInit();
});

function simulateSystemInit() {
    const steps = [
        { msg: '[System] Iniciando sistema...', delay: 100 },
        { msg: '[1/7] Inicializando almacenamiento...', delay: 300 },
        { msg: '[OK] LittleFS montado', delay: 200, type: 'success' },
        { msg: '[2/7] Cargando configuración...', delay: 300 },
        { msg: '  - Nombre: RF_Controller_Demo', delay: 100 },
        { msg: '  - WiFi configurado: Sí', delay: 100 },
        { msg: '  - MQTT habilitado: Sí', delay: 100 },
        { msg: '  - Frecuencia RF: 433.92 MHz', delay: 100 },
        { msg: '[3/7] Iniciando WiFi y servidor web...', delay: 400 },
        { msg: '[WiFi] Conectado a Demo_Network', delay: 200, type: 'success' },
        { msg: '[WiFi] IP: 192.168.4.1', delay: 100 },
        { msg: '[4/7] Esperando estabilización de WiFi...', delay: 300 },
        { msg: '[OK] WiFi estable', delay: 200, type: 'success' },
        { msg: '[5/7] Inicializando módulo CC1101...', delay: 500 },
        { msg: '[RF] CC1101 conectado!', delay: 200, type: 'success' },
        { msg: '[RF] Frecuencia: 433.92 MHz', delay: 100, type: 'rf' },
        { msg: '[6/7] Configurando hora...', delay: 300 },
        { msg: '[NTP] Hora sincronizada', delay: 200, type: 'success' },
        { msg: '[7/7] Configurando MQTT...', delay: 400 },
        { msg: '[MQTT] Conectando a 192.168.1.100:1883...', delay: 300 },
        { msg: '[MQTT] Conectado!', delay: 200, type: 'success' },
        { msg: '[MQTT] Discovery publicado para Home Assistant', delay: 200 },
        { msg: '', delay: 100 },
        { msg: '==============================================', delay: 100, type: 'success' },
        { msg: '   SISTEMA LISTO - RF CONTROLLER', delay: 100, type: 'success' },
        { msg: '==============================================', delay: 100, type: 'success' },
        { msg: '   IP: 192.168.4.1', delay: 50 },
        { msg: '   Modo: Access Point', delay: 50 },
        { msg: '   RF: 433.92 MHz', delay: 50 },
        { msg: '   MQTT: Conectado (Auto-Discovery activo)', delay: 50 },
        { msg: '==============================================', delay: 100, type: 'success' },
    ];

    let totalDelay = 0;
    steps.forEach(step => {
        totalDelay += step.delay;
        setTimeout(() => {
            logConsole(step.msg, step.type || 'info');
        }, totalDelay);
    });
}

// ============================================
// Consola Demo
// ============================================

function logConsole(message, type = 'info') {
    const output = document.getElementById('console-output');
    const entry = document.createElement('div');
    entry.className = `log-entry ${type}`;
    const time = new Date().toLocaleTimeString();
    entry.textContent = `[${time}] ${message}`;
    output.appendChild(entry);
    output.scrollTop = output.scrollHeight;
}

function toggleConsole() {
    const console = document.getElementById('demo-console');
    const btn = console.querySelector('.demo-console-toggle');
    console.classList.toggle('minimized');
    btn.textContent = console.classList.contains('minimized') ? 'Expandir' : 'Minimizar';
}

// ============================================
// LocalStorage
// ============================================

function loadFromLocalStorage() {
    // Cargar dispositivos
    const savedDevices = localStorage.getItem('rf_demo_devices');
    if (savedDevices) {
        devices = JSON.parse(savedDevices);
    } else {
        // Dispositivos de ejemplo con tipos simplificados
        devices = [
            {
                id: 'demo-001',
                name: 'Cortina Living',
                type: 1, // DEVICE_CURTAIN
                room: 'Living',
                signals: [
                    { valid: true, name: 'Abrir', index: 0, frequency: 433.92, length: 48, protocol: 2, data: generateDemoSignal() },
                    { valid: true, name: 'Cerrar', index: 1, frequency: 433.92, length: 48, protocol: 2, data: generateDemoSignal() },
                    { valid: true, name: 'Parar', index: 2, frequency: 433.92, length: 48, protocol: 2, data: generateDemoSignal() }
                ]
            },
            {
                id: 'demo-002',
                name: 'Luz Dormitorio',
                type: 5, // DEVICE_LIGHT
                room: 'Dormitorio',
                signals: [
                    { valid: true, name: 'Encender', index: 0, frequency: 433.92, length: 32, protocol: 5, data: generateDemoSignal() },
                    { valid: true, name: 'Apagar', index: 1, frequency: 433.92, length: 32, protocol: 5, data: generateDemoSignal() }
                ]
            },
            {
                id: 'demo-003',
                name: 'Portón Garage',
                type: 4, // DEVICE_GATE
                room: 'Garage',
                signals: [
                    { valid: true, name: 'Toggle', index: 0, frequency: 433.92, length: 24, protocol: 6, data: generateDemoSignal() }
                ]
            }
        ];
        saveToLocalStorage();
    }

    // Cargar config
    const savedConfig = localStorage.getItem('rf_demo_config');
    if (savedConfig) {
        config = JSON.parse(savedConfig);
    } else {
        config = {
            mqtt_enabled: true,
            mqtt_server: '192.168.1.100',
            mqtt_port: 1883,
            mqtt_user: 'homeassistant',
            mqtt_discovery: true,
            timezone: 'America/Argentina/Buenos_Aires',
            ntp_server: 'pool.ntp.org',
            default_frequency: 433.92,
            auto_detect_enabled: true,
            device_name: 'RF_Controller_Demo'
        };
    }

    renderDevices();
    updateRoomFilter();
    updateCaptureDeviceList();
}

function saveToLocalStorage() {
    localStorage.setItem('rf_demo_devices', JSON.stringify(devices));
    localStorage.setItem('rf_demo_config', JSON.stringify(config));
}

function generateDemoSignal() {
    // Genera datos simulados de señal RF
    const data = [];
    for (let i = 0; i < 24; i++) {
        data.push(Math.floor(Math.random() * 256));
    }
    return data;
}

// ============================================
// Tabs
// ============================================

function initTabs() {
    const tabs = document.querySelectorAll('.tab-btn');
    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            tabs.forEach(t => t.classList.remove('active'));
            tab.classList.add('active');

            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
            document.getElementById(tab.dataset.tab + '-tab').classList.add('active');

            logConsole(`[UI] Cambiando a pestaña: ${tab.dataset.tab}`, 'info');

            // Actualizar lista de dispositivos en captura si cambiamos a esa pestaña
            if (tab.dataset.tab === 'capture') {
                updateCaptureDeviceList();
            }
        });
    });

    // Selector de frecuencia personalizada
    const freqSelect = document.getElementById('capture-frequency');
    const customFreq = document.getElementById('custom-frequency');
    if (freqSelect && customFreq) {
        freqSelect.addEventListener('change', () => {
            customFreq.style.display = freqSelect.value === 'custom' ? 'block' : 'none';
        });
    }

    // Selector de dispositivo en captura
    const captureDeviceSelect = document.getElementById('capture-device');
    if (captureDeviceSelect) {
        captureDeviceSelect.addEventListener('change', updateCaptureSignalOptions);
    }
}

// ============================================
// Time & Status
// ============================================

function updateTime() {
    const timeDisplay = document.getElementById('time-display');
    if (timeDisplay) {
        const now = new Date();
        timeDisplay.textContent = now.toLocaleTimeString();
    }
}

function updateUptime() {
    demoUptime++;
    const uptimeDisplay = document.getElementById('system-uptime');
    if (uptimeDisplay) {
        const h = Math.floor(demoUptime / 3600);
        const m = Math.floor((demoUptime % 3600) / 60);
        const s = demoUptime % 60;
        uptimeDisplay.textContent = `${h}h ${m}m ${s}s`;
    }
}

// ============================================
// Dispositivos
// ============================================

function renderDevices() {
    const container = document.getElementById('devices-list');
    if (!container) return;

    const filterType = document.getElementById('filter-type')?.value;
    const filterRoom = document.getElementById('filter-room')?.value;

    let filtered = devices;

    if (filterType) {
        filtered = filtered.filter(d => d.type == filterType);
    }
    if (filterRoom) {
        filtered = filtered.filter(d => d.room === filterRoom);
    }

    if (filtered.length === 0) {
        container.innerHTML = `
            <div class="empty-state">
                <h3>No hay dispositivos</h3>
                <p>Agrega tu primer dispositivo para comenzar</p>
            </div>
        `;
        return;
    }

    container.innerHTML = filtered.map(device => {
        const typeInfo = DEVICE_TYPES[device.type] || DEVICE_TYPES[99];
        const isSomfy = typeInfo.isSomfy || false;
        const isDooyaBidir = typeInfo.isDooyaBidir || false;
        const isSpecialProtocol = isSomfy || isDooyaBidir;

        return `
            <div class="device-card ${isSomfy ? 'somfy-device' : ''} ${isDooyaBidir ? 'dooya-device' : ''}" data-id="${device.id}">
                <div class="device-card-header">
                    <span class="device-icon">${typeInfo.icon}</span>
                    <span class="device-name">${escapeHtml(device.name)}</span>
                    <span class="device-type">${typeInfo.name}</span>
                    ${isSomfy ? '<span class="badge-somfy">RTS</span>' : ''}
                    ${isDooyaBidir ? '<span class="badge-dooya">DD</span>' : ''}
                </div>
                ${device.room ? `<div class="device-room">${escapeHtml(device.room)}</div>` : ''}
                <div class="device-signals">
                    ${isSomfy ? `
                        <button class="signal-btn somfy-btn" onclick="sendSomfyCommand('${device.id}', 'up')">
                            ▲ Subir
                        </button>
                        <button class="signal-btn somfy-btn" onclick="sendSomfyCommand('${device.id}', 'stop')">
                            ■ My/Parar
                        </button>
                        <button class="signal-btn somfy-btn" onclick="sendSomfyCommand('${device.id}', 'down')">
                            ▼ Bajar
                        </button>
                    ` : isDooyaBidir ? `
                        <button class="signal-btn dooya-btn" onclick="sendDooyaBidirCommand('${device.id}', 'up')">
                            ▲ Subir
                        </button>
                        <button class="signal-btn dooya-btn" onclick="sendDooyaBidirCommand('${device.id}', 'stop')">
                            ■ Parar
                        </button>
                        <button class="signal-btn dooya-btn" onclick="sendDooyaBidirCommand('${device.id}', 'down')">
                            ▼ Bajar
                        </button>
                    ` : device.signals.map(sig => sig.valid ? `
                        <button class="signal-btn" onclick="transmitSignal('${device.id}', ${sig.index})">
                            ${escapeHtml(sig.name || 'Señal ' + (sig.index + 1))}
                        </button>
                    ` : '').join('')}
                </div>
                <div class="device-actions">
                    <button onclick="editDevice('${device.id}')">Editar</button>
                    ${!isSpecialProtocol ? `<button onclick="goToCaptureForDevice('${device.id}')" class="btn-secondary">Capturar Señal</button>` : ''}
                </div>
            </div>
        `;
    }).join('');
}

function filterDevices() {
    renderDevices();
}

function updateRoomFilter() {
    const select = document.getElementById('filter-room');
    if (!select) return;

    const rooms = [...new Set(devices.map(d => d.room).filter(r => r))];
    select.innerHTML = '<option value="">Todas las habitaciones</option>' +
        rooms.map(r => `<option value="${escapeHtml(r)}">${escapeHtml(r)}</option>`).join('');
}

function getDeviceTypeName(type) {
    return DEVICE_TYPES[type]?.name || 'Desconocido';
}

function getProtocolName(protocol) {
    return PROTOCOLS[protocol] || 'Desconocido';
}

// ============================================
// Captura - Nuevo flujo
// ============================================

function updateCaptureDeviceList() {
    const select = document.getElementById('capture-device');
    if (!select) return;

    if (devices.length === 0) {
        select.innerHTML = '<option value="">-- Primero crea un dispositivo --</option>';
        updateCaptureSignalOptions();
        return;
    }

    select.innerHTML = '<option value="">-- Selecciona un dispositivo --</option>' +
        devices.map(d => {
            const typeInfo = DEVICE_TYPES[d.type] || DEVICE_TYPES[99];
            return `<option value="${d.id}">${typeInfo.icon} ${escapeHtml(d.name)}</option>`;
        }).join('');

    updateCaptureSignalOptions();
}

function updateCaptureSignalOptions() {
    const deviceSelect = document.getElementById('capture-device');
    const signalSelect = document.getElementById('capture-signal-slot');
    if (!deviceSelect || !signalSelect) return;

    const deviceId = deviceSelect.value;
    const device = devices.find(d => d.id === deviceId);

    if (!device) {
        signalSelect.innerHTML = '<option value="">-- Selecciona dispositivo primero --</option>';
        signalSelect.disabled = true;
        return;
    }

    const typeInfo = DEVICE_TYPES[device.type] || DEVICE_TYPES[99];
    signalSelect.disabled = false;

    signalSelect.innerHTML = typeInfo.signals.map((name, idx) => {
        const existing = device.signals.find(s => s.index === idx && s.valid);
        const status = existing ? ' (tiene señal)' : ' (vacío)';
        return `<option value="${idx}">${name}${status}</option>`;
    }).join('');
}

function goToCaptureForDevice(deviceId) {
    // Cambiar a pestaña de captura
    document.querySelectorAll('.tab-btn').forEach(t => t.classList.remove('active'));
    document.querySelector('[data-tab="capture"]').classList.add('active');
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.getElementById('capture-tab').classList.add('active');

    // Seleccionar el dispositivo
    updateCaptureDeviceList();
    const deviceSelect = document.getElementById('capture-device');
    if (deviceSelect) {
        deviceSelect.value = deviceId;
        updateCaptureSignalOptions();
    }

    logConsole(`[UI] Preparando captura para dispositivo`, 'info');
}

function goToCaptureForSignal(deviceId, signalIndex) {
    goToCaptureForDevice(deviceId);

    // Seleccionar el slot de señal
    setTimeout(() => {
        const signalSelect = document.getElementById('capture-signal-slot');
        if (signalSelect) {
            signalSelect.value = signalIndex;
        }
    }, 100);
}

// ============================================
// CRUD Dispositivos
// ============================================

function showAddDeviceModal() {
    document.getElementById('new-device-name').value = '';
    document.getElementById('new-device-type').value = '1';
    document.getElementById('new-device-room').value = '';
    showDeviceTypeInfo();
    openModal('modal-add-device');
}

function showDeviceTypeInfo() {
    const typeSelect = document.getElementById('new-device-type');
    const infoDiv = document.getElementById('device-type-info');
    if (!typeSelect || !infoDiv) return;

    const type = parseInt(typeSelect.value);
    const typeInfo = DEVICE_TYPES[type];

    if (typeInfo) {
        infoDiv.innerHTML = `
            <p><strong>Señales para ${typeInfo.name}:</strong></p>
            <ul>
                ${typeInfo.signals.map(s => `<li>${s}</li>`).join('')}
            </ul>
        `;
        infoDiv.style.display = 'block';
    } else {
        infoDiv.style.display = 'none';
    }
}

function addDevice() {
    const name = document.getElementById('new-device-name').value.trim();
    const type = parseInt(document.getElementById('new-device-type').value);
    const room = document.getElementById('new-device-room').value.trim();

    if (!name) {
        showToast('El nombre es requerido', 'error');
        return;
    }

    // Crear señales vacías según el tipo
    const typeInfo = DEVICE_TYPES[type] || DEVICE_TYPES[99];
    const emptySignals = typeInfo.signals.map((signalName, idx) => ({
        valid: false,
        name: signalName,
        index: idx
    }));

    const newDevice = {
        id: 'dev-' + Date.now(),
        name,
        type,
        room,
        signals: emptySignals
    };

    devices.push(newDevice);
    saveToLocalStorage();
    renderDevices();
    updateRoomFilter();
    updateCaptureDeviceList();

    logConsole(`[Storage] Dispositivo agregado: ${name} (${typeInfo.name})`, 'success');
    showToast('Dispositivo agregado. Ahora puedes capturar las señales.', 'success');
    closeModal('modal-add-device');

    // Ofrecer ir a captura
    if (confirm(`¿Quieres capturar las señales para "${name}" ahora?`)) {
        goToCaptureForDevice(newDevice.id);
    }
}

function editDevice(id) {
    const device = devices.find(d => d.id === id);
    if (!device) return;

    currentEditDevice = device;

    document.getElementById('edit-device-id').value = device.id;
    document.getElementById('edit-device-name').value = device.name;
    document.getElementById('edit-device-type').value = device.type;
    document.getElementById('edit-device-room').value = device.room || '';

    renderDeviceSignals(device);
    openModal('modal-edit-device');
}

function renderDeviceSignals(device) {
    const container = document.getElementById('device-signals');
    if (!container) return;

    const typeInfo = DEVICE_TYPES[device.type] || DEVICE_TYPES[99];

    // Mostrar todos los slots de señal (con o sin datos)
    container.innerHTML = typeInfo.signals.map((signalName, idx) => {
        const sig = device.signals.find(s => s.index === idx);
        const hasSignal = sig && sig.valid;

        return `
            <div class="signal-item ${hasSignal ? '' : 'empty'}">
                <div class="signal-item-info">
                    <div class="signal-item-name">${escapeHtml(signalName)}</div>
                    ${hasSignal ? `
                        <div class="signal-item-details">
                            ${sig.frequency} MHz - ${sig.length} bytes
                            ${sig.protocol ? ` - ${getProtocolName(sig.protocol)}` : ''}
                        </div>
                    ` : `
                        <div class="signal-item-details empty">Sin señal capturada</div>
                    `}
                </div>
                <div class="signal-item-actions">
                    ${hasSignal ? `
                        <button class="btn btn-small btn-success" onclick="transmitSignal('${device.id}', ${idx})">Probar</button>
                        <button class="btn btn-small btn-danger" onclick="deleteSignal('${device.id}', ${idx})">Eliminar</button>
                    ` : `
                        <button class="btn btn-small btn-primary" onclick="goToCaptureForSignal('${device.id}', ${idx}); closeModal('modal-edit-device');">Capturar</button>
                    `}
                </div>
            </div>
        `;
    }).join('');
}

function updateDevice() {
    const id = document.getElementById('edit-device-id').value;
    const name = document.getElementById('edit-device-name').value.trim();
    const type = parseInt(document.getElementById('edit-device-type').value);
    const room = document.getElementById('edit-device-room').value.trim();

    if (!name) {
        showToast('El nombre es requerido', 'error');
        return;
    }

    const device = devices.find(d => d.id === id);
    if (device) {
        const oldType = device.type;
        device.name = name;
        device.type = type;
        device.room = room;

        // Si cambió el tipo, ajustar las señales
        if (oldType !== type) {
            const typeInfo = DEVICE_TYPES[type] || DEVICE_TYPES[99];
            // Mantener señales existentes pero agregar nuevos slots si es necesario
            for (let i = device.signals.length; i < typeInfo.signals.length; i++) {
                device.signals.push({
                    valid: false,
                    name: typeInfo.signals[i],
                    index: i
                });
            }
            // Actualizar nombres de señales
            device.signals.forEach((sig, idx) => {
                if (idx < typeInfo.signals.length) {
                    sig.name = typeInfo.signals[idx];
                }
            });
        }

        saveToLocalStorage();
        renderDevices();
        updateRoomFilter();
        updateCaptureDeviceList();

        logConsole(`[Storage] Dispositivo actualizado: ${name}`, 'success');
        showToast('Dispositivo actualizado', 'success');
    }

    closeModal('modal-edit-device');
}

function deleteDevice() {
    if (!currentEditDevice) return;

    if (!confirm(`¿Eliminar "${currentEditDevice.name}"? Esta acción no se puede deshacer.`)) {
        return;
    }

    devices = devices.filter(d => d.id !== currentEditDevice.id);
    saveToLocalStorage();
    renderDevices();
    updateRoomFilter();
    updateCaptureDeviceList();

    logConsole(`[Storage] Dispositivo eliminado: ${currentEditDevice.name}`, 'warning');
    showToast('Dispositivo eliminado', 'success');
    closeModal('modal-edit-device');
}

// ============================================
// Señales RF
// ============================================

function transmitSignal(deviceId, signalIndex) {
    const device = devices.find(d => d.id === deviceId);
    if (!device) return;

    const signal = device.signals.find(s => s.index === signalIndex);
    if (!signal || !signal.valid) {
        showToast('Señal no válida', 'error');
        return;
    }

    logConsole(`[RF] Transmitiendo señal "${signal.name}" de ${device.name}`, 'rf');
    logConsole(`[RF] Frecuencia: ${signal.frequency} MHz`, 'rf');
    logConsole(`[RF] Protocolo: ${getProtocolName(signal.protocol)}`, 'rf');
    logConsole(`[RF] Longitud: ${signal.length} bytes, 5 repeticiones...`, 'rf');

    // Simular transmisión
    setTimeout(() => {
        logConsole(`[RF] Transmisión completada`, 'success');
        showToast(`Señal "${signal.name}" transmitida`, 'success');

        // Simular publicación MQTT
        logConsole(`[MQTT] Publicando estado: rf_controller/${deviceId}/state`, 'info');
    }, 500);
}

function startCapture() {
    // Verificar selección de dispositivo y señal
    const deviceSelect = document.getElementById('capture-device');
    const signalSelect = document.getElementById('capture-signal-slot');

    if (!deviceSelect.value) {
        showToast('Selecciona un dispositivo primero', 'error');
        return;
    }

    if (signalSelect.value === '') {
        showToast('Selecciona un slot de señal', 'error');
        return;
    }

    const freqSelect = document.getElementById('capture-frequency');
    const modSelect = document.getElementById('capture-modulation');
    const autoDetect = document.getElementById('auto-detect')?.checked || false;

    let frequency = parseFloat(freqSelect.value);
    if (freqSelect.value === 'custom') {
        const customFreq = document.getElementById('custom-frequency');
        frequency = parseFloat(customFreq.value);
    }

    if (!frequency || frequency < 300 || frequency > 928) {
        showToast('Frecuencia inválida', 'error');
        return;
    }

    const modulation = parseInt(modSelect.value);
    const modName = getModulationName(modulation);

    document.getElementById('btn-start-capture').style.display = 'none';
    document.getElementById('btn-stop-capture').style.display = 'inline-flex';
    document.getElementById('capture-status').classList.add('capturing');
    document.getElementById('capture-result').style.display = 'none';
    const signalWave = document.getElementById('signal-wave');
    if (signalWave) signalWave.classList.remove('paused');

    const device = devices.find(d => d.id === deviceSelect.value);
    const signalName = DEVICE_TYPES[device.type]?.signals[parseInt(signalSelect.value)] || 'Señal';

    logConsole(`[RF] Iniciando captura para: ${device.name} - ${signalName}`, 'rf');
    logConsole(`[RF] Frecuencia: ${frequency} MHz`, 'rf');
    logConsole(`[RF] Modulación: ${modName}`, 'rf');

    if (autoDetect) {
        logConsole(`[RF] Modo auto-detección activado`, 'info');
        logConsole(`[RF] Escaneando frecuencias...`, 'rf');
    }

    showToast('Captura iniciada, presiona el control remoto...', 'success');

    // Simular captura después de 2-4 segundos
    const captureDelay = 2000 + Math.random() * 2000;
    captureTimeout = setTimeout(() => {
        simulateCapturedSignal(frequency, modulation);
    }, captureDelay);
}

function stopCapture() {
    if (captureTimeout) {
        clearTimeout(captureTimeout);
        captureTimeout = null;
    }
    resetCaptureUI();
    logConsole(`[RF] Captura detenida`, 'warning');
    showToast('Captura detenida', 'warning');
}

function resetCaptureUI() {
    document.getElementById('btn-start-capture').style.display = 'inline-flex';
    document.getElementById('btn-stop-capture').style.display = 'none';
    document.getElementById('capture-status').classList.remove('capturing');
    const signalWave = document.getElementById('signal-wave');
    if (signalWave) signalWave.classList.add('paused');
}

function simulateCapturedSignal(frequency, modulation) {
    const signalLength = 24 + Math.floor(Math.random() * 40);

    // Simular detección de protocolo basado en frecuencia y longitud
    let detectedProtocol = 1; // Genérico por defecto
    if (frequency === 433.92) {
        if (signalLength > 40) {
            detectedProtocol = Math.random() > 0.5 ? 2 : 9; // Dooya o Vertilux
        } else {
            detectedProtocol = Math.random() > 0.5 ? 5 : 6; // EV1527 o PT2262
        }
    }

    capturedSignal = {
        valid: true,
        data: generateDemoSignal(),
        length: signalLength,
        frequency: frequency,
        modulation: modulation,
        protocol: detectedProtocol,
        analysis: generateSignalAnalysis(signalLength, frequency, modulation, detectedProtocol)
    };

    logConsole(`[RF] ¡Señal capturada!`, 'success');
    logConsole(`[RF] Longitud: ${signalLength} bytes`, 'rf');
    logConsole(`[RF] Protocolo detectado: ${getProtocolName(detectedProtocol)}`, 'success');

    showCapturedSignal(capturedSignal);
    resetCaptureUI();
    showToast('Señal capturada correctamente', 'success');
}

function generateSignalAnalysis(length, frequency, modulation, protocol) {
    const shortPulses = Math.floor(length * 0.6);
    const longPulses = length - shortPulses;

    return `Análisis de señal RF:
────────────────────────────────
Frecuencia: ${frequency} MHz
Modulación: ${getModulationName(modulation)}
Longitud: ${length} bytes
────────────────────────────────
Protocolo detectado: ${getProtocolName(protocol)}
Pulsos cortos: ~${shortPulses}
Pulsos largos: ~${longPulses}
────────────────────────────────
${getProtocolDescription(protocol)}`;
}

function getProtocolDescription(protocol) {
    const descriptions = {
        1: 'Señal genérica ASK/OOK sin patrón específico reconocido.',
        2: 'Protocolo Dooya: Típico de cortinas motorizadas, 24-28 bits.',
        3: 'Protocolo Zemismart: Similar a Tuya, usado en dispositivos IoT.',
        4: 'Protocolo Tuya RF: Común en dispositivos smart económicos.',
        5: 'Protocolo EV1527: Muy común en controles remotos genéricos, 20 bits + 4 datos.',
        6: 'Protocolo PT2262/PT2272: Clásico en controles de garaje y alarmas.',
        7: 'Protocolo Nice Flor-s: Usado en automatización de portones Nice.',
        8: 'Protocolo Came: Común en portones automáticos Came.',
        9: 'Protocolo Vertilux/VTI: Similar a EV1527, pulsos más cortos.'
    };
    return descriptions[protocol] || 'Protocolo no identificado.';
}

function showCapturedSignal(data) {
    document.getElementById('capture-result').style.display = 'block';
    document.getElementById('signal-length').textContent = data.length;
    document.getElementById('signal-frequency').textContent = data.frequency;
    document.getElementById('signal-modulation').textContent = getModulationName(data.modulation);

    const protocolEl = document.getElementById('signal-protocol');
    if (protocolEl) {
        protocolEl.textContent = getProtocolName(data.protocol);
    }

    document.getElementById('signal-analysis').textContent = data.analysis;
}

function getModulationName(mod) {
    const mods = {
        0: '2-FSK',
        1: 'GFSK',
        2: 'ASK/OOK',
        3: '4-FSK',
        4: 'MSK'
    };
    return mods[mod] || 'Desconocida';
}

function testCapturedSignal() {
    if (!capturedSignal || !capturedSignal.valid) {
        showToast('No hay señal para probar', 'error');
        return;
    }

    logConsole(`[RF] Transmitiendo señal de prueba...`, 'rf');
    logConsole(`[RF] Frecuencia: ${capturedSignal.frequency} MHz`, 'rf');
    logConsole(`[RF] Protocolo: ${getProtocolName(capturedSignal.protocol)}`, 'rf');

    setTimeout(() => {
        logConsole(`[RF] Transmisión de prueba completada`, 'success');
        showToast('Señal de prueba transmitida', 'success');
    }, 500);
}

function saveCurrentCapture() {
    if (!capturedSignal || !capturedSignal.valid) {
        showToast('No hay señal capturada para guardar', 'error');
        return;
    }

    const deviceSelect = document.getElementById('capture-device');
    const signalSelect = document.getElementById('capture-signal-slot');

    if (!deviceSelect.value) {
        showToast('Selecciona un dispositivo', 'error');
        return;
    }

    const deviceId = deviceSelect.value;
    const signalIndex = parseInt(signalSelect.value);
    const device = devices.find(d => d.id === deviceId);

    if (!device) {
        showToast('Dispositivo no encontrado', 'error');
        return;
    }

    const typeInfo = DEVICE_TYPES[device.type] || DEVICE_TYPES[99];
    const signalName = typeInfo.signals[signalIndex] || 'Señal ' + (signalIndex + 1);

    // Asegurar que el array de señales tiene el tamaño correcto
    while (device.signals.length <= signalIndex) {
        device.signals.push({ valid: false, index: device.signals.length });
    }

    // Guardar la señal
    device.signals[signalIndex] = {
        valid: true,
        name: signalName,
        index: signalIndex,
        frequency: capturedSignal.frequency,
        length: capturedSignal.length,
        protocol: capturedSignal.protocol,
        modulation: capturedSignal.modulation,
        data: capturedSignal.data
    };

    saveToLocalStorage();
    renderDevices();
    updateCaptureSignalOptions();

    logConsole(`[Storage] Señal "${signalName}" guardada en ${device.name}`, 'success');
    logConsole(`[MQTT] Actualizando discovery para ${device.name}`, 'info');
    showToast(`Señal "${signalName}" guardada en ${device.name}`, 'success');

    // Limpiar captura
    clearCapture();
}

// ============================================
// Identificar señal (sin guardar)
// ============================================

function identifySignal() {
    const freqSelect = document.getElementById('capture-frequency');
    const modSelect = document.getElementById('capture-modulation');
    const autoDetect = document.getElementById('auto-detect')?.checked || false;

    let frequency = parseFloat(freqSelect.value);
    if (freqSelect.value === 'custom') {
        const customFreq = document.getElementById('custom-frequency');
        frequency = parseFloat(customFreq.value);
    }

    if (!frequency || frequency < 300 || frequency > 928) {
        showToast('Frecuencia inválida', 'error');
        return;
    }

    const modulation = parseInt(modSelect.value);

    document.getElementById('btn-identify-signal').disabled = true;
    document.getElementById('btn-identify-signal').textContent = 'Esperando señal...';

    logConsole(`[RF] Modo identificación - esperando señal...`, 'info');
    logConsole(`[RF] Frecuencia: ${frequency} MHz`, 'rf');

    if (autoDetect) {
        logConsole(`[RF] Auto-detección activada`, 'info');
    }

    showToast('Presiona el control remoto para identificar la señal...', 'success');

    // Simular recepción
    setTimeout(() => {
        const signalLength = 24 + Math.floor(Math.random() * 40);
        let detectedProtocol = 1;
        if (frequency === 433.92) {
            detectedProtocol = [2, 5, 6, 9][Math.floor(Math.random() * 4)];
        }

        logConsole(`[RF] ════════════════════════════════════════`, 'success');
        logConsole(`[RF] SEÑAL IDENTIFICADA`, 'success');
        logConsole(`[RF] ════════════════════════════════════════`, 'success');
        logConsole(`[RF] Frecuencia: ${frequency} MHz`, 'rf');
        logConsole(`[RF] Modulación: ${getModulationName(modulation)}`, 'rf');
        logConsole(`[RF] Longitud: ${signalLength} bytes`, 'rf');
        logConsole(`[RF] Protocolo: ${getProtocolName(detectedProtocol)}`, 'success');
        logConsole(`[RF] ${getProtocolDescription(detectedProtocol)}`, 'info');
        logConsole(`[RF] ════════════════════════════════════════`, 'success');

        showToast(`Protocolo detectado: ${getProtocolName(detectedProtocol)}`, 'success');

        document.getElementById('btn-identify-signal').disabled = false;
        document.getElementById('btn-identify-signal').textContent = 'Identificar Señal';
    }, 2000 + Math.random() * 2000);
}

function deleteSignal(deviceId, signalIndex) {
    if (!confirm('¿Eliminar esta señal?')) return;

    const device = devices.find(d => d.id === deviceId);
    if (device) {
        const sig = device.signals.find(s => s.index === signalIndex);
        if (sig) {
            sig.valid = false;
            sig.data = null;
            sig.length = 0;
            sig.frequency = 0;
            sig.protocol = 0;
        }
        saveToLocalStorage();
        renderDeviceSignals(device);
        renderDevices();

        logConsole(`[Storage] Señal eliminada`, 'warning');
        showToast('Señal eliminada', 'success');
    }
}

function clearCapture() {
    capturedSignal = null;
    document.getElementById('capture-result').style.display = 'none';
    document.getElementById('capture-status').classList.remove('capturing');
}

// ============================================
// Configuración
// ============================================

function loadConfig() {
    logConsole('[Config] Recargando configuración...', 'info');
    loadFromLocalStorage();
    showToast('Configuración recargada', 'success');
}

function saveConfig() {
    config = {
        mqtt_enabled: document.getElementById('mqtt-enabled')?.checked || false,
        mqtt_server: document.getElementById('mqtt-server')?.value || '',
        mqtt_port: parseInt(document.getElementById('mqtt-port')?.value) || 1883,
        mqtt_user: document.getElementById('mqtt-user')?.value || '',
        mqtt_password: document.getElementById('mqtt-password')?.value || '',
        mqtt_discovery: document.getElementById('mqtt-discovery')?.checked || false,
        timezone: document.getElementById('timezone')?.value || 'America/Argentina/Buenos_Aires',
        ntp_server: document.getElementById('ntp-server')?.value || 'pool.ntp.org',
        default_frequency: parseFloat(document.getElementById('default-frequency')?.value) || 433.92,
        auto_detect_enabled: document.getElementById('auto-detect-enabled')?.checked || false,
        device_name: document.getElementById('device-name')?.value || 'RF_Controller'
    };

    saveToLocalStorage();
    logConsole('[Config] Configuración guardada', 'success');
    showToast('Configuración guardada', 'success');
}

// ============================================
// WiFi
// ============================================

function scanWiFi() {
    logConsole('[WiFi] Escaneando redes...', 'info');
    showToast('Buscando redes WiFi...', 'success');

    setTimeout(() => {
        logConsole('[WiFi] 3 redes encontradas', 'success');
        showToast('3 redes encontradas', 'success');
    }, 1500);
}

function connectWiFi() {
    const ssid = document.getElementById('wifi-ssid')?.value;
    if (!ssid) {
        showToast('Selecciona una red WiFi', 'error');
        return;
    }

    logConsole(`[WiFi] Conectando a ${ssid}...`, 'info');
    showToast('Conectando a WiFi...', 'success');

    setTimeout(() => {
        logConsole(`[WiFi] Conectado a ${ssid}`, 'success');
        const wifiStatus = document.getElementById('wifi-status');
        if (wifiStatus) {
            wifiStatus.textContent = `WiFi: ${ssid}`;
            wifiStatus.className = 'status-indicator connected';
        }
        showToast('Conectado a WiFi', 'success');
    }, 2000);
}

// ============================================
// Backup/Restore
// ============================================

function downloadBackup() {
    const backup = {
        version: '1.0.0',
        timestamp: new Date().toISOString(),
        config: config,
        devices: devices
    };

    const blob = new Blob([JSON.stringify(backup, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `rf_controller_backup_${new Date().toISOString().split('T')[0]}.json`;
    a.click();
    URL.revokeObjectURL(url);

    logConsole('[Backup] Backup descargado', 'success');
    showToast('Backup descargado', 'success');
}

function restoreBackup() {
    const fileInput = document.getElementById('backup-file');
    if (!fileInput?.files || !fileInput.files[0]) {
        showToast('Selecciona un archivo de backup', 'error');
        return;
    }

    if (!confirm('¿Restaurar backup? Esto reemplazará toda la configuración actual.')) {
        return;
    }

    const reader = new FileReader();
    reader.onload = function(e) {
        try {
            const backup = JSON.parse(e.target.result);

            if (backup.config) config = backup.config;
            if (backup.devices) devices = backup.devices;

            saveToLocalStorage();
            renderDevices();
            updateRoomFilter();
            updateCaptureDeviceList();

            logConsole('[Backup] Backup restaurado correctamente', 'success');
            showToast('Backup restaurado', 'success');
        } catch (err) {
            logConsole('[Backup] Error al restaurar: ' + err.message, 'error');
            showToast('Error al restaurar backup', 'error');
        }
    };
    reader.readAsText(fileInput.files[0]);
}

function confirmReboot() {
    if (confirm('¿Reiniciar el dispositivo?')) {
        logConsole('[System] Reiniciando...', 'warning');
        showToast('Reiniciando... (simulado)', 'warning');

        setTimeout(() => {
            location.reload();
        }, 1500);
    }
}

function confirmFactoryReset() {
    if (confirm('¿Restaurar de fábrica? Se eliminarán TODOS los dispositivos y configuraciones.')) {
        if (confirm('Esta acción NO se puede deshacer. ¿Continuar?')) {
            localStorage.removeItem('rf_demo_devices');
            localStorage.removeItem('rf_demo_config');

            logConsole('[System] Restaurando valores de fábrica...', 'warning');
            showToast('Restaurando valores de fábrica...', 'warning');

            setTimeout(() => {
                location.reload();
            }, 1500);
        }
    }
}

// ============================================
// Modales
// ============================================

function openModal(id) {
    document.getElementById(id).classList.add('active');
}

function closeModal(id) {
    document.getElementById(id).classList.remove('active');
}

// Cerrar modal al hacer click fuera
document.addEventListener('click', (e) => {
    if (e.target.classList.contains('modal')) {
        e.target.classList.remove('active');
    }
});

// ============================================
// Toast Notifications
// ============================================

function showToast(message, type = 'success') {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    toast.textContent = message;

    container.appendChild(toast);

    setTimeout(() => {
        toast.style.animation = 'slideIn 0.3s ease reverse';
        setTimeout(() => toast.remove(), 300);
    }, 3000);
}

// ============================================
// Utilidades
// ============================================

function escapeHtml(text) {
    if (!text) return '';
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// ============================================
// Somfy RTS
// ============================================

function sendSomfyCommand(deviceId, command) {
    const device = devices.find(d => d.id === deviceId);
    if (!device) return;

    const cmdNames = {
        'up': 'Subir',
        'down': 'Bajar',
        'stop': 'My/Parar',
        'prog': 'Programar'
    };

    logConsole(`[Somfy RTS] Enviando comando "${cmdNames[command]}" a ${device.name}`, 'rf');
    logConsole(`[Somfy RTS] Frecuencia: 433.42 MHz`, 'rf');
    logConsole(`[Somfy RTS] Address: 0x${(device.somfy?.address || 0).toString(16).toUpperCase()}`, 'rf');
    logConsole(`[Somfy RTS] Rolling Code: ${device.somfy?.rollingCode || 0}`, 'rf');

    // Simular transmisión
    setTimeout(() => {
        // Incrementar rolling code
        if (device.somfy) {
            device.somfy.rollingCode = (device.somfy.rollingCode || 0) + 1;
            saveToLocalStorage();
        }

        logConsole(`[Somfy RTS] Comando enviado OK`, 'success');
        logConsole(`[Somfy RTS] Rolling code incrementado a ${device.somfy?.rollingCode}`, 'info');
        showToast(`${device.name}: ${cmdNames[command]}`, 'success');

        // Simular publicación MQTT
        logConsole(`[MQTT] Publicando estado: rf_controller/${deviceId}/state -> ${command}`, 'info');
    }, 300);
}

function showAddSomfyModal() {
    document.getElementById('somfy-device-name').value = '';
    document.getElementById('somfy-device-room').value = '';
    document.getElementById('somfy-address').value = generateSomfyAddress();
    document.getElementById('somfy-rolling-code').value = '0';
    openModal('modal-add-somfy');
}

function generateSomfyAddress() {
    // Generar dirección aleatoria de 24 bits
    return Math.floor(Math.random() * 0xFFFFFF).toString(16).toUpperCase().padStart(6, '0');
}

function addSomfyDevice() {
    const name = document.getElementById('somfy-device-name').value.trim();
    const room = document.getElementById('somfy-device-room').value.trim();
    const addressHex = document.getElementById('somfy-address').value.trim();
    const rollingCode = parseInt(document.getElementById('somfy-rolling-code').value) || 0;

    if (!name) {
        showToast('El nombre es requerido', 'error');
        return;
    }

    if (!addressHex || !/^[0-9A-Fa-f]{1,6}$/.test(addressHex)) {
        showToast('Dirección inválida (debe ser hexadecimal, ej: 1A2B3C)', 'error');
        return;
    }

    const address = parseInt(addressHex, 16);

    const newDevice = {
        id: 'somfy-' + Date.now(),
        name,
        type: 10, // DEVICE_CURTAIN_SOMFY
        room,
        signals: [], // Somfy no usa señales capturadas
        somfy: {
            address: address,
            rollingCode: rollingCode,
            encryptionKey: 0xA7
        }
    };

    devices.push(newDevice);
    saveToLocalStorage();
    renderDevices();
    updateRoomFilter();

    logConsole(`[Storage] Dispositivo Somfy agregado: ${name}`, 'success');
    logConsole(`[Somfy RTS] Address: 0x${addressHex.toUpperCase()}, RC: ${rollingCode}`, 'info');
    showToast('Dispositivo Somfy agregado. Ahora debes emparejarlo con el motor.', 'success');
    closeModal('modal-add-somfy');

    // Ofrecer emparejar
    if (confirm(`¿Quieres emparejar "${name}" con el motor Somfy ahora?\n\nPara emparejar:\n1. Mantén presionado el botón PROG del control Somfy original hasta que la cortina se mueva\n2. Luego presiona "Emparejar" en la siguiente pantalla`)) {
        showSomfyPairingModal(newDevice.id);
    }
}

function showSomfyPairingModal(deviceId) {
    const device = devices.find(d => d.id === deviceId);
    if (!device || device.type !== 10) return;

    document.getElementById('somfy-pairing-device').textContent = device.name;
    document.getElementById('somfy-pairing-address').textContent = '0x' + device.somfy.address.toString(16).toUpperCase();
    document.getElementById('somfy-pairing-id').value = deviceId;

    openModal('modal-somfy-pairing');
}

function sendSomfyProgCommand() {
    const deviceId = document.getElementById('somfy-pairing-id').value;
    const device = devices.find(d => d.id === deviceId);
    if (!device) return;

    logConsole(`[Somfy RTS] Enviando comando PROG a ${device.name}`, 'rf');
    logConsole(`[Somfy RTS] Address: 0x${device.somfy.address.toString(16).toUpperCase()}`, 'rf');

    // Simular envío de comando PROG
    setTimeout(() => {
        device.somfy.rollingCode = (device.somfy.rollingCode || 0) + 1;
        saveToLocalStorage();

        logConsole(`[Somfy RTS] Comando PROG enviado`, 'success');
        logConsole(`[Somfy RTS] Si el motor se movió, el emparejamiento fue exitoso!`, 'success');
        showToast('Comando PROG enviado. Si el motor se movió, está emparejado.', 'success');

        closeModal('modal-somfy-pairing');
    }, 500);
}

// ============================================
// Dooya Bidireccional (DDxxxx)
// ============================================

function sendDooyaBidirCommand(deviceId, command) {
    const device = devices.find(d => d.id === deviceId);
    if (!device) return;

    const cmdNames = {
        'up': 'Subir',
        'down': 'Bajar',
        'stop': 'Parar',
        'prog': 'P2 (Emparejar)'
    };

    const cmdCodes = {
        'up': '00',
        'down': '01',
        'stop': '02',
        'prog': '03'
    };

    logConsole(`[Dooya Bidir] Enviando comando "${cmdNames[command]}" a ${device.name}`, 'rf');
    logConsole(`[Dooya Bidir] Frecuencia: 433.92 MHz (FSK)`, 'rf');
    logConsole(`[Dooya Bidir] ID: 0x${(device.dooyaBidir?.deviceId || 0).toString(16).toUpperCase().padStart(7, '0')}`, 'rf');
    logConsole(`[Dooya Bidir] Unit: ${device.dooyaBidir?.unitCode || 0}`, 'rf');

    // Generar frame hex
    const id = device.dooyaBidir?.deviceId || 0x1020304;
    const unit = device.dooyaBidir?.unitCode || 1;
    const frameHex = generateDooyaFrame(id, unit, cmdCodes[command]);
    logConsole(`[Dooya Bidir] Frame: ${frameHex}`, 'rf');

    // Simular transmisión
    setTimeout(() => {
        logConsole(`[Dooya Bidir] Comando enviado OK`, 'success');
        showToast(`${device.name}: ${cmdNames[command]}`, 'success');

        // Simular publicación MQTT
        logConsole(`[MQTT] Publicando estado: rf_controller/${deviceId}/state -> ${command}`, 'info');
    }, 300);
}

function generateDooyaFrame(deviceId, unitCode, cmdCode) {
    // Estructura: 09 19 15 00 [ID1] [ID2] [ID3] [ID4+Unit] [CMD] 00
    const id1 = ((deviceId >> 20) & 0xFF).toString(16).padStart(2, '0');
    const id2 = ((deviceId >> 12) & 0xFF).toString(16).padStart(2, '0');
    const id3 = ((deviceId >> 4) & 0xFF).toString(16).padStart(2, '0');
    const id4Unit = (((deviceId & 0x0F) << 4) | (unitCode & 0x0F)).toString(16).padStart(2, '0');

    return `09191500${id1}${id2}${id3}${id4Unit}${cmdCode}00`.toUpperCase();
}

function showAddDooyaBidirModal() {
    document.getElementById('dooya-device-name').value = '';
    document.getElementById('dooya-device-room').value = '';
    document.getElementById('dooya-device-id').value = generateDooyaDeviceId();
    document.getElementById('dooya-unit-code').value = '1';
    openModal('modal-add-dooya');
}

function generateDooyaDeviceId() {
    // Generar ID aleatorio de 28 bits (7 nibbles hex)
    return Math.floor(Math.random() * 0x0FFFFFFF).toString(16).toUpperCase().padStart(7, '0');
}

function addDooyaBidirDevice() {
    const name = document.getElementById('dooya-device-name').value.trim();
    const room = document.getElementById('dooya-device-room').value.trim();
    const deviceIdHex = document.getElementById('dooya-device-id').value.trim();
    const unitCode = parseInt(document.getElementById('dooya-unit-code').value) || 1;

    if (!name) {
        showToast('El nombre es requerido', 'error');
        return;
    }

    if (!deviceIdHex || !/^[0-9A-Fa-f]{1,7}$/.test(deviceIdHex)) {
        showToast('ID inválido (debe ser hexadecimal de hasta 7 dígitos)', 'error');
        return;
    }

    if (unitCode < 0 || unitCode > 15) {
        showToast('Unit code debe ser entre 0 y 15', 'error');
        return;
    }

    const deviceId = parseInt(deviceIdHex, 16);

    const newDevice = {
        id: 'dooya-' + Date.now(),
        name,
        type: 11, // DEVICE_CURTAIN_DOOYA_BIDIR
        room,
        signals: [],
        dooyaBidir: {
            deviceId: deviceId,
            unitCode: unitCode
        }
    };

    devices.push(newDevice);
    saveToLocalStorage();
    renderDevices();
    updateRoomFilter();

    const frameExample = generateDooyaFrame(deviceId, unitCode, '03');
    logConsole(`[Storage] Dispositivo Dooya Bidir agregado: ${name}`, 'success');
    logConsole(`[Dooya Bidir] ID: 0x${deviceIdHex.toUpperCase()}, Unit: ${unitCode}`, 'info');
    logConsole(`[Dooya Bidir] Frame de emparejamiento: ${frameExample}`, 'info');
    showToast('Dispositivo Dooya agregado. Ahora debes emparejarlo con el motor.', 'success');
    closeModal('modal-add-dooya');

    // Ofrecer emparejar
    if (confirm(`¿Quieres emparejar "${name}" con el motor Dooya ahora?\n\nPara emparejar:\n1. Presiona el botón P2 del motor (o control original) 2 veces hasta que se mueva\n2. Luego presiona "Enviar P2" en la siguiente pantalla`)) {
        showDooyaPairingModal(newDevice.id);
    }
}

function showDooyaPairingModal(deviceId) {
    const device = devices.find(d => d.id === deviceId);
    if (!device || device.type !== 11) return;

    document.getElementById('dooya-pairing-device').textContent = device.name;
    document.getElementById('dooya-pairing-id').value = deviceId;

    const frameHex = generateDooyaFrame(device.dooyaBidir.deviceId, device.dooyaBidir.unitCode, '03');
    document.getElementById('dooya-pairing-frame').textContent = frameHex;

    openModal('modal-dooya-pairing');
}

function sendDooyaProgCommand() {
    const deviceId = document.getElementById('dooya-pairing-id').value;
    const device = devices.find(d => d.id === deviceId);
    if (!device) return;

    const frameHex = generateDooyaFrame(device.dooyaBidir.deviceId, device.dooyaBidir.unitCode, '03');

    logConsole(`[Dooya Bidir] Enviando comando P2 (emparejar) a ${device.name}`, 'rf');
    logConsole(`[Dooya Bidir] Frame: ${frameHex}`, 'rf');

    // Simular envío
    setTimeout(() => {
        logConsole(`[Dooya Bidir] Comando P2 enviado`, 'success');
        logConsole(`[Dooya Bidir] Si el motor se movió, el emparejamiento fue exitoso!`, 'success');
        showToast('Comando P2 enviado. Si el motor se movió, está emparejado.', 'success');

        closeModal('modal-dooya-pairing');
    }, 500);
}

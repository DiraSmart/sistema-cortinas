// ============================================
// RF Controller - ESP32 + CC1101
// JavaScript Application
// ============================================

// Estado global
let devices = [];
let config = {};
let capturedSignal = null;
let currentEditDevice = null;
let identifyMode = false;

// ============================================
// Configuración de tipos de dispositivo
// ============================================

const DEVICE_TYPES = {
    1: { name: 'Cortina', signals: ['Abrir', 'Cerrar', 'Parar'], icon: '🪟' },
    2: { name: 'Interruptor', signals: ['Encender', 'Apagar'], icon: '🔘' },
    3: { name: 'Botón', signals: ['Pulsar'], icon: '⏺' },
    4: { name: 'Portón', signals: ['Toggle', 'Abrir', 'Cerrar'], icon: '🚪' },
    5: { name: 'Luz', signals: ['Encender', 'Apagar'], icon: '💡' },
    6: { name: 'Ventilador', signals: ['Encender', 'Apagar', 'Velocidad'], icon: '🌀' },
    7: { name: 'Dimmer', signals: ['Encender', 'Apagar', 'Subir', 'Bajar'], icon: '🔆' },
    10: { name: 'Cortina Somfy RTS', signals: ['Abrir', 'Cerrar', 'My/Stop', 'Prog'], icon: '🏠', protocol: 'somfy' },
    11: { name: 'Cortina Dooya Bidir', signals: ['Abrir', 'Cerrar', 'Parar', 'Prog'], icon: '🏠', protocol: 'dooya' },
    12: { name: 'Cortina A-OK AC114', signals: ['Abrir', 'Cerrar', 'Parar', 'Prog'], icon: '🏠', protocol: 'aok' },
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
    9: 'Vertilux/VTI'
};

// ============================================
// Inicialización
// ============================================

document.addEventListener('DOMContentLoaded', function() {
    initTabs();
    loadStatus();
    loadDevices();
    loadConfig();
    updateTime();
    setInterval(updateTime, 1000);
    setInterval(loadStatus, 5000);
});

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

            // Actualizar lista de dispositivos en captura cuando cambia a esa pestaña
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
}

// ============================================
// Estado del Sistema
// ============================================

async function loadStatus() {
    try {
        const response = await fetch('/api/status');
        const data = await response.json();
        updateStatusIndicators(data);
    } catch (error) {
        console.error('Error loading status:', error);
    }
}

function updateStatusIndicators(data) {
    const wifiStatus = document.getElementById('wifi-status');
    const rfStatus = document.getElementById('rf-status');

    if (wifiStatus) {
        wifiStatus.textContent = data.wifi_connected ? `WiFi: ${data.wifi_ssid}` : 'WiFi: AP Mode';
        wifiStatus.className = 'status-indicator ' + (data.wifi_connected ? 'connected' : '');
    }

    if (rfStatus) {
        rfStatus.textContent = data.rf_connected ? `RF: ${data.rf_frequency} MHz` : 'RF: No conectado';
        rfStatus.className = 'status-indicator ' + (data.rf_connected ? 'connected' : 'disconnected');
    }

    // Actualizar info del sistema en configuración
    const sysIp = document.getElementById('system-ip');
    const sysUptime = document.getElementById('system-uptime');
    const sysHeap = document.getElementById('system-heap');

    if (sysIp) sysIp.textContent = data.ip || '--';
    if (sysUptime) sysUptime.textContent = formatUptime(data.uptime);
    if (sysHeap) sysHeap.textContent = formatBytes(data.free_heap);
}

function formatUptime(seconds) {
    if (!seconds) return '--';
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return `${h}h ${m}m ${s}s`;
}

function formatBytes(bytes) {
    if (!bytes) return '--';
    return (bytes / 1024).toFixed(1) + ' KB';
}

function updateTime() {
    const timeDisplay = document.getElementById('time-display');
    if (timeDisplay) {
        const now = new Date();
        timeDisplay.textContent = now.toLocaleTimeString();
    }
}

// ============================================
// Dispositivos
// ============================================

async function loadDevices() {
    try {
        const response = await fetch('/api/devices');
        devices = await response.json();
        renderDevices();
        updateRoomFilter();
        updateCaptureDeviceList();
    } catch (error) {
        console.error('Error loading devices:', error);
        showToast('Error al cargar dispositivos', 'error');
    }
}

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

        // Protocol-based devices (Somfy, Dooya, A-OK) have virtual controls
        let signalButtons = '';
        if (device.type === 10 || device.type === 11 || device.type === 12) {
            // Virtual control buttons for protocol devices
            signalButtons = `
                <button class="signal-btn btn-success" onclick="transmitSignal('${device.id}', 0)">Abrir</button>
                <button class="signal-btn btn-primary" onclick="transmitSignal('${device.id}', 2)">Parar</button>
                <button class="signal-btn btn-success" onclick="transmitSignal('${device.id}', 1)">Cerrar</button>
            `;
        } else {
            // Regular devices use captured signals
            signalButtons = (device.signals || [])
                .filter(sig => sig && sig.valid)
                .map(sig => `
                    <button class="signal-btn" onclick="transmitSignal('${device.id}', ${sig.index})">
                        ${escapeHtml(sig.name || typeInfo.signals[sig.index] || 'Señal ' + (sig.index + 1))}
                    </button>
                `).join('');
        }

        return `
            <div class="device-card" data-id="${device.id}">
                <div class="device-card-header">
                    <span class="device-icon">${typeInfo.icon}</span>
                    <span class="device-name">${escapeHtml(device.name)}</span>
                    <span class="device-type">${typeInfo.name}</span>
                </div>
                ${device.room ? `<div class="device-room">${escapeHtml(device.room)}</div>` : ''}
                <div class="device-signals">
                    ${signalButtons || '<span class="no-signals">Sin señales configuradas</span>'}
                </div>
                <div class="device-actions">
                    <button onclick="editDevice('${device.id}')">Editar</button>
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
    return (DEVICE_TYPES[type] || DEVICE_TYPES[99]).name;
}

// ============================================
// Información de tipo de dispositivo
// ============================================

function showDeviceTypeInfo(type) {
    const info = document.getElementById('device-type-info');
    if (!info) return;

    const typeInfo = DEVICE_TYPES[type] || DEVICE_TYPES[99];
    info.textContent = `${typeInfo.signals.length} señales: ${typeInfo.signals.join(', ')}`;

    // Show/hide protocol-specific config
    const somfyConfig = document.getElementById('somfy-config');
    const dooyaConfig = document.getElementById('dooya-config');
    const aokConfig = document.getElementById('aok-config');

    if (somfyConfig) somfyConfig.style.display = (type == '10') ? 'block' : 'none';
    if (dooyaConfig) dooyaConfig.style.display = (type == '11') ? 'block' : 'none';
    if (aokConfig) aokConfig.style.display = (type == '12') ? 'block' : 'none';
}

// Generar dirección Somfy aleatoria (3 bytes = 6 caracteres hex)
function generateRandomSomfyAddress() {
    const addr = Math.floor(Math.random() * 0xFFFFFF).toString(16).toUpperCase().padStart(6, '0');
    document.getElementById('new-somfy-address').value = addr;
}

// Generar Device ID Dooya aleatorio (4 bytes = 8 caracteres hex)
function generateRandomDooyaId() {
    const id = Math.floor(Math.random() * 0xFFFFFFFF).toString(16).toUpperCase().padStart(8, '0');
    document.getElementById('new-dooya-device-id').value = id;
}

// Generar Remote ID A-OK aleatorio (3 bytes = 6 caracteres hex)
function generateRandomAokId() {
    const id = Math.floor(Math.random() * 0xFFFFFF).toString(16).toUpperCase().padStart(6, '0');
    document.getElementById('new-aok-remote-id').value = id;
}

// ============================================
// CRUD Dispositivos
// ============================================

function showAddDeviceModal() {
    document.getElementById('new-device-name').value = '';
    document.getElementById('new-device-type').value = '1';
    document.getElementById('new-device-room').value = '';
    showDeviceTypeInfo('1');
    openModal('modal-add-device');
}

async function addDevice() {
    const name = document.getElementById('new-device-name').value.trim();
    const type = parseInt(document.getElementById('new-device-type').value);
    const room = document.getElementById('new-device-room').value.trim();

    if (!name) {
        showToast('El nombre es requerido', 'error');
        return;
    }

    // Build device data
    const deviceData = { name, type, room };

    // Add Somfy RTS config
    if (type === 10) {
        let addrHex = document.getElementById('new-somfy-address')?.value?.trim() || '';
        // Auto-generar dirección si está vacía
        if (!addrHex) {
            addrHex = Math.floor(Math.random() * 0xFFFFFF).toString(16).toUpperCase().padStart(6, '0');
            showToast(`Dirección Somfy generada: ${addrHex}`, 'success');
        }
        const rolling = parseInt(document.getElementById('new-somfy-rolling')?.value) || 1;
        deviceData.somfy_address = parseInt(addrHex, 16) || 0;
        deviceData.somfy_rolling_code = rolling;
    }

    // Add Dooya Bidir config
    if (type === 11) {
        let devIdHex = document.getElementById('new-dooya-device-id')?.value?.trim() || '';
        // Auto-generar Device ID si está vacío
        if (!devIdHex) {
            devIdHex = Math.floor(Math.random() * 0xFFFFFFFF).toString(16).toUpperCase().padStart(8, '0');
            showToast(`Device ID Dooya generado: ${devIdHex}`, 'success');
        }
        const unitCode = parseInt(document.getElementById('new-dooya-unit')?.value) || 1;
        deviceData.dooya_device_id = parseInt(devIdHex, 16) || 0;
        deviceData.dooya_unit_code = unitCode;
    }

    // Add A-OK AC114 config
    if (type === 12) {
        let remoteIdHex = document.getElementById('new-aok-remote-id')?.value?.trim() || '';
        // Auto-generar Remote ID si está vacío
        if (!remoteIdHex) {
            remoteIdHex = Math.floor(Math.random() * 0xFFFFFF).toString(16).toUpperCase().padStart(6, '0');
            showToast(`Remote ID A-OK generado: ${remoteIdHex}`, 'success');
        }
        const channelValue = document.getElementById('new-aok-channel')?.value;
        const channel = channelValue !== '' && channelValue !== undefined ? parseInt(channelValue) : 1;
        deviceData.aok_remote_id = parseInt(remoteIdHex, 16) || 0;
        deviceData.aok_channel = channel;
    }

    try {
        const response = await fetch('/api/devices', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(deviceData)
        });

        const data = await response.json();
        if (data.success) {
            // Different message for protocol-based devices
            if (type === 10 || type === 11 || type === 12) {
                showToast('Dispositivo agregado. Ya puedes controlarlo desde MQTT/Home Assistant.', 'success');
            } else {
                showToast('Dispositivo agregado. Ahora ve a Capturar para grabar las señales.', 'success');
            }
            closeModal('modal-add-device');
            loadDevices();
        } else {
            showToast(data.error || 'Error al agregar', 'error');
        }
    } catch (error) {
        console.error('Error adding device:', error);
        showToast('Error de conexión', 'error');
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
    const signals = device.signals || [];

    // For Somfy and Dooya, show protocol info and pairing instructions
    if (device.type === 10) {
        // Somfy RTS
        const addr = device.somfy?.address || 0;
        const rolling = device.somfy?.rollingCode || 0;
        container.innerHTML = `
            <div class="protocol-info">
                <h4>Somfy RTS - Control Virtual</h4>
                <p><strong>Dirección:</strong> ${addr.toString(16).toUpperCase().padStart(6, '0')}</p>
                <p><strong>Rolling Code:</strong> ${rolling}</p>

                <div class="pairing-instructions">
                    <h5>Para vincular con tu motor:</h5>
                    <ol>
                        <li>Mantén presionado el botón <strong>PROG</strong> en tu control Somfy original hasta que la cortina se mueva brevemente</li>
                        <li>Presiona el botón <strong>"Programar"</strong> abajo dentro de 2 segundos</li>
                        <li>La cortina se moverá confirmando el emparejamiento</li>
                    </ol>
                </div>

                <div class="signal-slot-actions" style="margin-top: 15px;">
                    <button class="btn btn-warning" onclick="transmitSignal('${device.id}', 3)">Programar</button>
                </div>

                <hr style="margin: 20px 0; border-color: rgba(255,255,255,0.1);">
                <h5>Control</h5>
                <div class="signal-slot-actions">
                    <button class="btn btn-success" onclick="transmitSignal('${device.id}', 0)">Abrir</button>
                    <button class="btn btn-primary" onclick="transmitSignal('${device.id}', 2)">My/Stop</button>
                    <button class="btn btn-success" onclick="transmitSignal('${device.id}', 1)">Cerrar</button>
                </div>
            </div>
        `;
        return;
    }

    if (device.type === 11) {
        // Dooya Bidir
        const devId = device.dooyaBidir?.deviceId || 0;
        const unit = device.dooyaBidir?.unitCode || 0;
        container.innerHTML = `
            <div class="protocol-info">
                <h4>Dooya Bidireccional - Control Virtual</h4>
                <p><strong>Device ID:</strong> ${devId.toString(16).toUpperCase().padStart(8, '0')}</p>
                <p><strong>Unit Code:</strong> ${unit}</p>

                <div class="pairing-instructions">
                    <h5>Para vincular con tu motor:</h5>
                    <ol>
                        <li>Presiona el botón <strong>P2</strong> en tu motor Dooya (o desconecta y reconecta la energía)</li>
                        <li>La cortina hará un movimiento corto indicando modo programación</li>
                        <li>Presiona el botón <strong>"Programar"</strong> abajo dentro de 10 segundos</li>
                        <li>La cortina se moverá confirmando el emparejamiento</li>
                    </ol>
                </div>

                <div class="signal-slot-actions" style="margin-top: 15px;">
                    <button class="btn btn-warning" onclick="transmitSignal('${device.id}', 3)">Programar</button>
                </div>

                <hr style="margin: 20px 0; border-color: rgba(255,255,255,0.1);">
                <h5>Control</h5>
                <div class="signal-slot-actions">
                    <button class="btn btn-success" onclick="transmitSignal('${device.id}', 0)">Abrir</button>
                    <button class="btn btn-primary" onclick="transmitSignal('${device.id}', 2)">Parar</button>
                    <button class="btn btn-success" onclick="transmitSignal('${device.id}', 1)">Cerrar</button>
                </div>
            </div>
        `;
        return;
    }

    if (device.type === 12) {
        // A-OK AC114
        const remoteId = device.aok?.remoteId || 0;
        const channel = device.aok?.channel ?? 1;
        const channelDisplay = channel === 0 ? '0 (Grupo - Todas las cortinas)' : channel;
        container.innerHTML = `
            <div class="protocol-info">
                <h4>A-OK AC114 - Control Virtual</h4>
                <p><strong>Remote ID:</strong> ${remoteId.toString(16).toUpperCase().padStart(6, '0')}</p>
                <p><strong>Canal:</strong> ${channelDisplay}</p>

                <div class="pairing-instructions">
                    <h5>Para vincular con tu motor:</h5>
                    <ol>
                        <li>Mantén presionado el botón <strong>P2/STOP</strong> en tu motor A-OK hasta que haga un movimiento corto</li>
                        <li>Presiona el botón <strong>"Programar"</strong> abajo dentro de 5 segundos</li>
                        <li>El motor se moverá confirmando el emparejamiento</li>
                    </ol>
                </div>

                <div class="signal-slot-actions" style="margin-top: 15px;">
                    <button class="btn btn-warning" onclick="transmitSignal('${device.id}', 3)">Programar</button>
                </div>

                <hr style="margin: 20px 0; border-color: rgba(255,255,255,0.1);">
                <h5>Control</h5>
                <div class="signal-slot-actions">
                    <button class="btn btn-success" onclick="transmitSignal('${device.id}', 0)">Abrir</button>
                    <button class="btn btn-primary" onclick="transmitSignal('${device.id}', 2)">Parar</button>
                    <button class="btn btn-success" onclick="transmitSignal('${device.id}', 1)">Cerrar</button>
                </div>
            </div>
        `;
        return;
    }

    // For regular devices, show signal capture UI
    let html = '<div class="signals-grid">';

    typeInfo.signals.forEach((signalName, idx) => {
        const signal = signals.find(s => s && s.index === idx);
        const hasSignal = signal && signal.valid;

        const freqRounded = hasSignal ? parseFloat(signal.frequency).toFixed(2) : '';
        const repeatCount = hasSignal ? (signal.repeatCount || 5) : 5;
        const isInverted = hasSignal ? (signal.inverted || false) : false;

        html += `
            <div class="signal-slot ${hasSignal ? 'configured' : 'empty'}">
                <div class="signal-slot-header">
                    <span class="signal-slot-name">${signalName}</span>
                    ${hasSignal ? `<span class="signal-slot-info">${freqRounded} MHz${isInverted ? ' (INV)' : ''}</span>` : ''}
                </div>
                ${hasSignal ? `
                <div class="signal-slot-repeat">
                    <label>Repeticiones:</label>
                    <input type="number" class="repeat-input" id="repeat-${device.id}-${idx}"
                           value="${repeatCount}" min="1" max="20"
                           onchange="updateSignalRepeat('${device.id}', ${idx}, this.value)">
                </div>
                <div class="signal-slot-invert">
                    <label>
                        <input type="checkbox" id="invert-${device.id}-${idx}"
                               ${isInverted ? 'checked' : ''}
                               onchange="updateSignalInvert('${device.id}', ${idx}, this.checked)">
                        Invertir señal
                    </label>
                </div>
                ` : ''}
                <div class="signal-slot-actions">
                    ${hasSignal ? `
                        <button class="btn btn-small btn-success" onclick="transmitSignal('${device.id}', ${idx})">Probar</button>
                        <button class="btn btn-small btn-danger" onclick="deleteSignal('${device.id}', ${idx})">Eliminar</button>
                    ` : `
                        <button class="btn btn-small btn-primary" onclick="goToCaptureForSignal('${device.id}', ${idx}, '${signalName}')">Grabar</button>
                    `}
                </div>
            </div>
        `;
    });

    html += '</div>';
    container.innerHTML = html;
}

function goToCaptureForSignal(deviceId, signalIndex, signalName) {
    closeModal('modal-edit-device');

    // Cambiar a pestaña de captura
    document.querySelectorAll('.tab-btn').forEach(t => t.classList.remove('active'));
    document.querySelector('[data-tab="capture"]').classList.add('active');
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.getElementById('capture-tab').classList.add('active');

    // Seleccionar dispositivo y función
    setTimeout(() => {
        updateCaptureDeviceList();
        const deviceSelect = document.getElementById('capture-device');
        if (deviceSelect) {
            deviceSelect.value = deviceId;
            updateCaptureSignalOptions();
            const slotSelect = document.getElementById('capture-signal-slot');
            if (slotSelect) {
                slotSelect.value = signalIndex;
            }
        }
        showToast(`Listo para grabar "${signalName}"`, 'success');
    }, 100);
}

async function updateDevice() {
    const id = document.getElementById('edit-device-id').value;
    const name = document.getElementById('edit-device-name').value.trim();
    const type = parseInt(document.getElementById('edit-device-type').value);
    const room = document.getElementById('edit-device-room').value.trim();

    if (!name) {
        showToast('El nombre es requerido', 'error');
        return;
    }

    try {
        const response = await fetch('/api/devices/update', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id, name, type, room })
        });

        const data = await response.json();
        if (data.success) {
            showToast('Dispositivo actualizado', 'success');
            closeModal('modal-edit-device');
            loadDevices();
        } else {
            showToast(data.error || 'Error al actualizar', 'error');
        }
    } catch (error) {
        console.error('Error updating device:', error);
        showToast('Error de conexión', 'error');
    }
}

async function deleteDevice() {
    if (!currentEditDevice) return;

    if (!confirm(`¿Eliminar "${currentEditDevice.name}"? Esta acción no se puede deshacer.`)) {
        return;
    }

    try {
        const response = await fetch(`/api/devices/delete?id=${currentEditDevice.id}`);
        const data = await response.json();

        if (data.success) {
            showToast('Dispositivo eliminado', 'success');
            closeModal('modal-edit-device');
            loadDevices();
        } else {
            showToast(data.error || 'Error al eliminar', 'error');
        }
    } catch (error) {
        console.error('Error deleting device:', error);
        showToast('Error de conexión', 'error');
    }
}

// ============================================
// Actualizar repeticiones de señal
// ============================================

async function updateSignalRepeat(deviceId, signalIndex, repeatCount) {
    repeatCount = Math.max(1, Math.min(20, parseInt(repeatCount) || 5));

    try {
        const response = await fetch('/api/signal/repeat', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                deviceId,
                signalIndex,
                repeatCount
            })
        });

        const data = await response.json();
        if (data.success) {
            showToast(`Repeticiones actualizadas a ${repeatCount}`, 'success');
            // Update local cache
            const device = devices.find(d => d.id === deviceId);
            if (device && device.signals) {
                const signal = device.signals.find(s => s && s.index === signalIndex);
                if (signal) signal.repeatCount = repeatCount;
            }
        } else {
            showToast(data.error || 'Error al actualizar', 'error');
        }
    } catch (error) {
        console.error('Error updating repeat count:', error);
        showToast('Error de conexión', 'error');
    }
}

async function updateSignalInvert(deviceId, signalIndex, inverted) {
    try {
        const response = await fetch('/api/signal/invert', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                deviceId,
                signalIndex,
                inverted
            })
        });

        const data = await response.json();
        if (data.success) {
            showToast(inverted ? 'Señal invertida' : 'Señal normal', 'success');
            // Update local cache
            const device = devices.find(d => d.id === deviceId);
            if (device && device.signals) {
                const signal = device.signals.find(s => s && s.index === signalIndex);
                if (signal) signal.inverted = inverted;
            }
        } else {
            showToast(data.error || 'Error al actualizar', 'error');
            // Revert checkbox
            const checkbox = document.getElementById(`invert-${deviceId}-${signalIndex}`);
            if (checkbox) checkbox.checked = !inverted;
        }
    } catch (error) {
        console.error('Error updating invert flag:', error);
        showToast('Error de conexión', 'error');
        // Revert checkbox
        const checkbox = document.getElementById(`invert-${deviceId}-${signalIndex}`);
        if (checkbox) checkbox.checked = !inverted;
    }
}

// ============================================
// Captura de Señales RF
// ============================================

function updateCaptureDeviceList() {
    const select = document.getElementById('capture-device');
    if (!select) return;

    select.innerHTML = '<option value="">-- Seleccionar --</option>' +
        devices.map(d => {
            const typeInfo = DEVICE_TYPES[d.type] || DEVICE_TYPES[99];
            return `<option value="${d.id}">${typeInfo.icon} ${escapeHtml(d.name)}</option>`;
        }).join('');
}

function updateCaptureSignalOptions() {
    const deviceSelect = document.getElementById('capture-device');
    const slotSelect = document.getElementById('capture-signal-slot');
    if (!deviceSelect || !slotSelect) return;

    const deviceId = deviceSelect.value;
    const device = devices.find(d => d.id === deviceId);

    if (!device) {
        slotSelect.innerHTML = '<option value="">-- Primero selecciona dispositivo --</option>';
        return;
    }

    const typeInfo = DEVICE_TYPES[device.type] || DEVICE_TYPES[99];
    const signals = device.signals || [];

    slotSelect.innerHTML = typeInfo.signals.map((name, idx) => {
        const existing = signals.find(s => s && s.index === idx && s.valid);
        const status = existing ? ' (ya configurada)' : '';
        return `<option value="${idx}">${name}${status}</option>`;
    }).join('');
}

async function transmitSignal(deviceId, signalIndex) {
    const btn = event?.target;
    const originalText = btn?.textContent;

    try {
        if (btn) {
            btn.disabled = true;
            btn.textContent = 'Enviando...';
        }

        // Agregar timeout de 10 segundos
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 10000);

        const response = await fetch(`/api/rf/transmit?id=${deviceId}&signal=${signalIndex}`, {
            signal: controller.signal
        });
        clearTimeout(timeoutId);

        const data = await response.json();

        if (data.success) {
            showToast('Señal transmitida', 'success');
        } else {
            showToast(data.error || 'Error al transmitir', 'error');
        }
    } catch (error) {
        console.error('Error transmitting signal:', error);
        if (error.name === 'AbortError') {
            showToast('Timeout - el dispositivo no respondió', 'error');
        } else {
            showToast('Error de conexión', 'error');
        }
    } finally {
        if (btn) {
            btn.disabled = false;
            btn.textContent = originalText || 'Enviar';
        }
        // Reload devices to update rolling codes etc
        loadDevices();
    }
}

async function startCapture() {
    // Ya no se requiere dispositivo para capturar - solo para guardar
    // Esto permite capturar señales A-OK para auto-descubrimiento

    const freqSelect = document.getElementById('capture-frequency');
    const customFreq = document.getElementById('custom-frequency');
    const modSelect = document.getElementById('capture-modulation');

    let frequency = parseFloat(freqSelect.value);
    if (freqSelect.value === 'custom') {
        frequency = parseFloat(customFreq.value);
    }

    if (!frequency || frequency < 300 || frequency > 928) {
        showToast('Frecuencia inválida', 'error');
        return;
    }

    const modulation = parseInt(modSelect.value);

    try {
        document.getElementById('btn-start-capture').style.display = 'none';
        document.getElementById('btn-stop-capture').style.display = 'inline-flex';
        document.getElementById('capture-status').classList.add('capturing');
        document.getElementById('capture-result').style.display = 'none';

        const url = `/api/rf/capture/start?frequency=${frequency}&modulation=${modulation}`;
        const response = await fetch(url);
        const data = await response.json();

        if (data.success) {
            showToast('Captura iniciada, presiona el control remoto...', 'success');
            pollForCapture();
        } else {
            showToast(data.error || 'Error al iniciar captura', 'error');
            resetCaptureUI();
        }
    } catch (error) {
        console.error('Error starting capture:', error);
        showToast('Error de conexión', 'error');
        resetCaptureUI();
    }
}

async function pollForCapture() {
    try {
        const response = await fetch('/api/rf/capture/get?timeout=10000');
        const data = await response.json();

        if (data.success && data.valid) {
            capturedSignal = data;
            showCapturedSignal(data);
            showToast('Señal capturada correctamente', 'success');
        } else if (data.capturing) {
            setTimeout(pollForCapture, 1000);
        } else {
            showToast('No se detectó ninguna señal', 'warning');
            resetCaptureUI();
        }
    } catch (error) {
        console.error('Error polling capture:', error);
        resetCaptureUI();
    }
}

async function stopCapture() {
    try {
        await fetch('/api/rf/capture/stop');
    } catch (error) {
        console.error('Error stopping capture:', error);
    }
    resetCaptureUI();
}

function resetCaptureUI() {
    document.getElementById('btn-start-capture').style.display = 'inline-flex';
    document.getElementById('btn-stop-capture').style.display = 'none';
    document.getElementById('capture-status').classList.remove('capturing');
}

function showCapturedSignal(data) {
    resetCaptureUI();
    document.getElementById('capture-result').style.display = 'block';
    document.getElementById('signal-length').textContent = data.length;
    document.getElementById('signal-frequency').textContent = data.frequency;
    document.getElementById('signal-modulation').textContent = getModulationName(data.modulation);

    // Set repeat count from captured signal or default
    const repeatInput = document.getElementById('signal-repeat-count');
    if (repeatInput) {
        repeatInput.value = data.repeatCount || 5;
    }

    // Mostrar protocolo detectado
    const protocolEl = document.getElementById('signal-protocol');
    if (protocolEl) {
        protocolEl.textContent = PROTOCOLS[data.protocol] || PROTOCOLS[0];
    }

    // Análisis detallado
    document.getElementById('signal-analysis').textContent = data.analysis || generateAnalysis(data);

    // Mostrar datos RAW en formato legible
    const rawDataEl = document.getElementById('signal-raw-data');
    if (rawDataEl && data.data) {
        rawDataEl.textContent = formatRawSignal(data.data);
    }
}

function generateAnalysis(data) {
    return `Longitud: ${data.length} bytes
Frecuencia: ${data.frequency} MHz
Modulación: ${getModulationName(data.modulation)}
Protocolo: ${PROTOCOLS[data.protocol] || 'Analizando...'}
Pulsos detectados: ${Math.floor(data.length / 2)}
Timestamp: ${new Date().toLocaleString()}`;
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

function detectRCSwitchProtocol(pulses) {
    // Detectar protocolos RC-Switch/ESPHome
    // Basado en: https://github.com/sui77/rc-switch y ESPHome

    if (pulses.length < 20) return null;

    // Protocolos RC-Switch/ESPHome completos
    // { num, name, base, syncHigh, syncLow, zero[high,low], one[high,low], inverted }
    const protocols = [
        { num: 1, name: 'Protocol 1', base: 350, syncHigh: 1, syncLow: 31, zero: [1,3], one: [3,1], inverted: false },
        { num: 2, name: 'Protocol 2', base: 650, syncHigh: 1, syncLow: 10, zero: [1,2], one: [2,1], inverted: false },
        { num: 3, name: 'Protocol 3', base: 100, syncHigh: 30, syncLow: 71, zero: [4,11], one: [9,6], inverted: false },
        { num: 4, name: 'Protocol 4', base: 380, syncHigh: 1, syncLow: 6, zero: [1,3], one: [3,1], inverted: false },
        { num: 5, name: 'Protocol 5', base: 500, syncHigh: 6, syncLow: 14, zero: [1,2], one: [2,1], inverted: false },
        { num: 6, name: 'Protocol 6', base: 450, syncHigh: 23, syncLow: 1, zero: [1,2], one: [2,1], inverted: true },
        { num: 7, name: 'Protocol 7', base: 150, syncHigh: 2, syncLow: 62, zero: [1,6], one: [6,1], inverted: false },
        { num: 8, name: 'Protocol 8', base: 200, syncHigh: 3, syncLow: 130, zero: [7,16], one: [3,16], inverted: false },
        { num: 9, name: 'Protocol 9', base: 365, syncHigh: 18, syncLow: 1, zero: [3,1], one: [1,3], inverted: true },
        { num: 10, name: 'Protocol 10', base: 270, syncHigh: 36, syncLow: 1, zero: [1,2], one: [2,1], inverted: true },
        { num: 11, name: 'Protocol 11', base: 320, syncHigh: 1, syncLow: 36, zero: [1,2], one: [2,1], inverted: false },
        { num: 12, name: 'Protocol 12', base: 250, syncHigh: 1, syncLow: 44, zero: [1,4], one: [4,1], inverted: false },
    ];

    // Encontrar el pulso más largo (sync)
    let maxPulse = 0, maxIndex = -1;
    for (let i = 0; i < pulses.length; i++) {
        if (pulses[i] > maxPulse) {
            maxPulse = pulses[i];
            maxIndex = i;
        }
    }
    if (maxIndex < 0 || maxPulse < 2000) return null;

    // Determinar si el sync es HIGH (índice par) o LOW (índice impar)
    const syncIsHigh = maxIndex % 2 === 0;

    // Calcular base estimada
    const shortPulses = pulses.filter(p => p > 100 && p < 1000);
    const estimatedBase = shortPulses.length > 0 ? Math.min(...shortPulses) : 300;

    // Probar cada protocolo
    for (const proto of protocols) {
        // Calcular sync esperado
        const expectedSyncLong = (proto.inverted ? proto.syncHigh : proto.syncLow) * proto.base;

        // Verificar si el patrón de sync coincide
        const syncMatches = (syncIsHigh === proto.inverted) &&
                           Math.abs(maxPulse - expectedSyncLong) < expectedSyncLong * 0.4;

        if (!syncMatches) continue;

        // Calcular base real desde el sync
        const actualBase = maxPulse / (proto.inverted ? proto.syncHigh : proto.syncLow);

        // Intentar decodificar bits
        const bits = decodeProtocolBits(pulses, maxIndex, actualBase, proto);
        if (bits && bits.length >= 12) {
            return {
                protocol: proto.name,
                protocolNum: proto.num,
                syncPulse: maxPulse,
                baseUnit: Math.round(actualBase),
                syncRatio: Math.round(maxPulse / actualBase),
                inverted: proto.inverted,
                bits: bits,
                code: parseInt(bits, 2),
                codeHex: '0x' + parseInt(bits, 2).toString(16).toUpperCase()
            };
        }
    }

    // No coincide con ningún protocolo conocido
    return {
        protocol: 'Desconocido',
        protocolNum: null,
        syncPulse: maxPulse,
        baseUnit: estimatedBase,
        syncRatio: Math.round(maxPulse / estimatedBase),
        inverted: syncIsHigh,
        bits: null,
        code: null
    };
}

function decodeProtocolBits(pulses, syncIndex, baseUnit, proto) {
    let bits = '';
    const tolerance = baseUnit * 0.5;
    const startIndex = (syncIndex + 1) % pulses.length;

    for (let i = startIndex; i < pulses.length - 1 && bits.length < 32; i += 2) {
        let high = pulses[i];
        let low = pulses[i + 1];

        // Para protocolos invertidos, el primer pulso es LOW
        if (proto.inverted) {
            [high, low] = [low, high];
        }

        // Verificar '0'
        const isZero = Math.abs(high - proto.zero[0] * baseUnit) < tolerance &&
                       Math.abs(low - proto.zero[1] * baseUnit) < tolerance;

        // Verificar '1'
        const isOne = Math.abs(high - proto.one[0] * baseUnit) < tolerance &&
                      Math.abs(low - proto.one[1] * baseUnit) < tolerance;

        if (isZero) bits += '0';
        else if (isOne) bits += '1';
        else break;
    }

    return bits.length >= 12 ? bits : null;
}

function formatRawSignal(hexData) {
    if (!hexData || hexData.length < 4) return 'Sin datos';

    // Convert hex string to pulse durations (each pulse is 2 bytes = 4 hex chars)
    const pulses = [];
    for (let i = 0; i < hexData.length - 3; i += 4) {
        const highByte = parseInt(hexData.substr(i, 2), 16);
        const lowByte = parseInt(hexData.substr(i + 2, 2), 16);
        const duration = (highByte << 8) | lowByte;
        if (duration > 0 && duration < 50000) {
            pulses.push(duration);
        }
    }

    if (pulses.length === 0) return 'Sin pulsos válidos';

    // ESPHome format: positive = HIGH, negative = LOW
    const esphomePulses = pulses.map((p, i) => i % 2 === 0 ? p : -p);

    let output = `Total: ${pulses.length} pulsos\n`;
    output += `================================================\n\n`;

    // Detectar protocolo RC-Switch
    const rcswitch = detectRCSwitchProtocol(pulses);
    if (rcswitch) {
        output += `PROTOCOLO RC-SWITCH:\n`;
        output += `------------------------------------------------\n`;
        if (rcswitch.protocolNum) {
            output += `Tipo: ${rcswitch.protocol} (protocol=${rcswitch.protocolNum})\n`;
        } else {
            output += `Tipo: ${rcswitch.protocol}\n`;
        }
        output += `Sync: ${rcswitch.syncPulse}us (ratio: ${rcswitch.syncRatio}x)${rcswitch.inverted ? ' [INVERTIDO]' : ''}\n`;
        output += `Base: ${rcswitch.baseUnit}us\n`;
        if (rcswitch.bits) {
            output += `Bits: ${rcswitch.bits} (${rcswitch.bits.length} bits)\n`;
            output += `Código: ${rcswitch.code} / ${rcswitch.codeHex}\n`;
            // Separar address y data para 24 bits (20+4)
            if (rcswitch.bits.length === 24) {
                const address = parseInt(rcswitch.bits.substring(0, 20), 2);
                const data = parseInt(rcswitch.bits.substring(20), 2);
                output += `  → Address: ${address} (0x${address.toString(16).toUpperCase()})\n`;
                output += `  → Data: ${data} (0x${data.toString(16).toUpperCase()})\n`;
            }
        }
        output += `\n`;
    }

    // Timing analysis
    output += `ANALISIS DE TIEMPOS:\n`;
    output += `------------------------------------------------\n`;

    const highPulses = pulses.filter((_, i) => i % 2 === 0);
    const lowPulses = pulses.filter((_, i) => i % 2 === 1);

    if (highPulses.length > 0) {
        const avgHigh = Math.round(highPulses.reduce((a, b) => a + b, 0) / highPulses.length);
        const minHigh = Math.min(...highPulses);
        const maxHigh = Math.max(...highPulses);
        output += `HIGH: min=${minHigh}us, max=${maxHigh}us, avg=${avgHigh}us\n`;
    }

    if (lowPulses.length > 0) {
        const avgLow = Math.round(lowPulses.reduce((a, b) => a + b, 0) / lowPulses.length);
        const minLow = Math.min(...lowPulses);
        const maxLow = Math.max(...lowPulses);
        output += `LOW:  min=${minLow}us, max=${maxLow}us, avg=${avgLow}us\n`;
    }

    output += `\n`;

    // ESPHome raw format (copy-paste ready)
    output += `FORMATO ESPHOME (copiar/pegar):\n`;
    output += `------------------------------------------------\n`;
    output += `code: [`;

    // Format in rows of 8 values
    for (let i = 0; i < esphomePulses.length; i++) {
        if (i > 0) output += ', ';
        if (i > 0 && i % 8 === 0) output += '\n       ';
        output += esphomePulses[i];
    }
    output += `]\n`;

    return output;
}

async function testCapturedSignal() {
    if (!capturedSignal || !capturedSignal.data) {
        showToast('No hay señal para probar', 'error');
        return;
    }

    // Get repeat count from UI
    const repeatInput = document.getElementById('signal-repeat-count');
    const repeatCount = repeatInput ? parseInt(repeatInput.value) || 5 : 5;

    try {
        const response = await fetch('/api/rf/test', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                data: capturedSignal.data,
                frequency: capturedSignal.frequency,
                modulation: capturedSignal.modulation,
                repeatCount: repeatCount
            })
        });

        const data = await response.json();
        if (data.success) {
            showToast('Señal de prueba transmitida', 'success');
        } else {
            showToast(data.error || 'Error al probar', 'error');
        }
    } catch (error) {
        console.error('Error testing signal:', error);
        showToast('Error al probar señal', 'error');
    }
}

async function saveCurrentCapture() {
    const deviceId = document.getElementById('capture-device')?.value;
    const signalSlot = document.getElementById('capture-signal-slot')?.value;

    if (!deviceId || signalSlot === '') {
        showToast('Selecciona un dispositivo y función', 'error');
        return;
    }

    if (!capturedSignal || !capturedSignal.data) {
        showToast('No hay señal para guardar', 'error');
        return;
    }

    const device = devices.find(d => d.id === deviceId);
    const typeInfo = DEVICE_TYPES[device?.type] || DEVICE_TYPES[99];
    const signalName = typeInfo.signals[parseInt(signalSlot)] || 'Señal';

    // Get repeat count from UI
    const repeatInput = document.getElementById('signal-repeat-count');
    const repeatCount = repeatInput ? parseInt(repeatInput.value) || 5 : 5;

    try {
        const response = await fetch('/api/rf/signal/save', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                deviceId,
                signalIndex: parseInt(signalSlot),
                signalName,
                data: capturedSignal.data,
                frequency: capturedSignal.frequency,
                modulation: capturedSignal.modulation,
                protocol: capturedSignal.protocol,
                repeatCount: repeatCount
            })
        });

        const data = await response.json();

        if (data.success) {
            showToast(`Señal "${signalName}" guardada correctamente`, 'success');
            loadDevices();
            clearCapture();
        } else {
            showToast(data.error || 'Error al guardar', 'error');
        }
    } catch (error) {
        console.error('Error saving signal:', error);
        showToast('Error de conexión', 'error');
    }
}

function identifySignal() {
    identifyMode = true;
    startCapture();
}

async function deleteSignal(deviceId, signalIndex) {
    if (!confirm('¿Eliminar esta señal?')) return;

    try {
        const response = await fetch('/api/rf/signal/delete', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ deviceId, signalIndex })
        });

        const data = await response.json();
        if (data.success) {
            showToast('Señal eliminada', 'success');
            loadDevices();
            if (currentEditDevice && currentEditDevice.id === deviceId) {
                const device = devices.find(d => d.id === deviceId);
                if (device) renderDeviceSignals(device);
            }
        } else {
            showToast(data.error || 'Error al eliminar', 'error');
        }
    } catch (error) {
        console.error('Error deleting signal:', error);
        showToast('Error al eliminar', 'error');
    }
}

function clearCapture() {
    capturedSignal = null;
    identifyMode = false;
    document.getElementById('capture-result').style.display = 'none';
    document.getElementById('capture-status').classList.remove('capturing');
}

// Decodificar señal capturada como A-OK y crear dispositivo
async function decodeAsAOK() {
    if (!capturedSignal || !capturedSignal.data) {
        showToast('Primero captura una señal', 'error');
        return;
    }

    showToast('Analizando señal A-OK...', 'info');

    try {
        const response = await fetch('/api/rf/decode-aok', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({})
        });

        const data = await response.json();

        if (data.success) {
            const remoteIdHex = data.remote_id_hex;
            const channel = data.channel;

            showToast(`¡A-OK detectado! ID: ${remoteIdHex}, Canal: ${channel}`, 'success');

            // Ask user if they want to create the device
            if (confirm(`Señal A-OK decodificada:\n\nRemote ID: ${remoteIdHex}\nCanal: ${channel}\n\n¿Desea crear un dispositivo A-OK con estos datos?`)) {
                await createAOKDevice(remoteIdHex, channel);
            }
        } else {
            showToast(data.message || 'No es una señal A-OK válida', 'warning');
        }
    } catch (error) {
        console.error('Error decoding A-OK:', error);
        showToast('Error al decodificar', 'error');
    }
}

// Crear dispositivo A-OK automáticamente
async function createAOKDevice(remoteIdHex, channel) {
    const name = prompt('Nombre para el dispositivo:', 'Cortina A-OK');
    if (!name) return;

    const room = prompt('Habitación (opcional):', '');

    try {
        const response = await fetch('/api/devices', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                name: name,
                type: 12,  // DEVICE_CURTAIN_AOK
                room: room || '',
                aok_remote_id: parseInt(remoteIdHex, 16),
                aok_channel: channel
            })
        });

        const data = await response.json();
        if (data.success) {
            showToast('Dispositivo A-OK creado! Puedes controlarlo desde la pestaña Dispositivos.', 'success');
            loadDevices();
            // Switch to devices tab
            document.querySelector('[data-tab="devices"]')?.click();
        } else {
            showToast(data.error || 'Error al crear dispositivo', 'error');
        }
    } catch (error) {
        console.error('Error creating A-OK device:', error);
        showToast('Error de conexión', 'error');
    }
}

// ============================================
// Configuración
// ============================================

async function loadConfig() {
    try {
        const response = await fetch('/api/config');
        config = await response.json();

        // WiFi
        if (config.wifi_ssid) {
            const wifiSelect = document.getElementById('wifi-ssid');
            if (wifiSelect) {
                const option = document.createElement('option');
                option.value = config.wifi_ssid;
                option.textContent = config.wifi_ssid;
                option.selected = true;
                wifiSelect.appendChild(option);
            }
        }

        // MQTT
        document.getElementById('mqtt-enabled').checked = config.mqtt_enabled;
        document.getElementById('mqtt-server').value = config.mqtt_server || '';
        document.getElementById('mqtt-port').value = config.mqtt_port || 1883;
        document.getElementById('mqtt-user').value = config.mqtt_user || '';
        document.getElementById('mqtt-discovery').checked = config.mqtt_discovery !== false;

        // Zona horaria
        if (config.timezone) {
            document.getElementById('timezone').value = config.timezone;
        }
        document.getElementById('ntp-server').value = config.ntp_server || 'pool.ntp.org';

        // RF
        if (config.default_frequency) {
            document.getElementById('default-frequency').value = config.default_frequency;
        }
        document.getElementById('auto-detect-enabled').checked = config.auto_detect_enabled !== false;

        // Sistema
        document.getElementById('device-name').value = config.device_name || 'RF_Controller';

    } catch (error) {
        console.error('Error loading config:', error);
    }
}

async function saveConfig() {
    const newConfig = {
        mqtt_enabled: document.getElementById('mqtt-enabled').checked,
        mqtt_server: document.getElementById('mqtt-server').value,
        mqtt_port: parseInt(document.getElementById('mqtt-port').value),
        mqtt_user: document.getElementById('mqtt-user').value,
        mqtt_password: document.getElementById('mqtt-password').value,
        mqtt_discovery: document.getElementById('mqtt-discovery').checked,
        timezone: document.getElementById('timezone').value,
        ntp_server: document.getElementById('ntp-server').value,
        default_frequency: parseFloat(document.getElementById('default-frequency').value),
        auto_detect_enabled: document.getElementById('auto-detect-enabled').checked,
        device_name: document.getElementById('device-name').value
    };

    try {
        const response = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(newConfig)
        });

        const data = await response.json();
        if (data.success) {
            showToast('Configuración guardada', 'success');
        } else {
            showToast(data.error || 'Error al guardar', 'error');
        }
    } catch (error) {
        console.error('Error saving config:', error);
        showToast('Error de conexión', 'error');
    }
}

async function mqttRediscover() {
    try {
        showToast('Publicando discovery MQTT...', 'success');
        const response = await fetch('/api/mqtt/rediscover', { method: 'POST' });
        const data = await response.json();
        if (data.success) {
            showToast('Discovery publicado correctamente', 'success');
        } else {
            showToast(data.error || 'Error en discovery', 'error');
        }
    } catch (error) {
        console.error('Error MQTT rediscover:', error);
        showToast('Error de conexión', 'error');
    }
}

// ============================================
// WiFi
// ============================================

function toggleManualSSID() {
    const select = document.getElementById('wifi-ssid');
    const manualGroup = document.getElementById('manual-ssid-group');

    if (select.value === '__manual__') {
        manualGroup.style.display = 'block';
        document.getElementById('wifi-ssid-manual').focus();
    } else {
        manualGroup.style.display = 'none';
    }
}

async function scanWiFi() {
    try {
        showToast('Buscando redes WiFi...', 'success');
        const response = await fetch('/api/wifi/scan');
        const data = await response.json();

        const select = document.getElementById('wifi-ssid');
        select.innerHTML = '<option value="">Seleccionar red...</option><option value="__manual__">Ingresar manualmente...</option>';

        if (data.networks && data.networks.length > 0) {
            data.networks.forEach(net => {
                const option = document.createElement('option');
                option.value = net.ssid;
                option.textContent = `${net.ssid} (${net.rssi} dBm)${net.encrypted ? ' 🔒' : ''}`;
                select.appendChild(option);
            });
            showToast(`${data.networks.length} redes encontradas`, 'success');
        } else {
            showToast('No se encontraron redes. Ingresa el SSID manualmente.', 'warning');
            select.value = '__manual__';
            toggleManualSSID();
        }
    } catch (error) {
        console.error('Error scanning WiFi:', error);
        showToast('Error al buscar redes. Ingresa el SSID manualmente.', 'error');
    }
}

async function connectWiFi() {
    const selectValue = document.getElementById('wifi-ssid').value;
    const manualSSID = document.getElementById('wifi-ssid-manual')?.value?.trim();
    const password = document.getElementById('wifi-password').value;

    // Usar SSID manual si está seleccionada esa opción
    const ssid = (selectValue === '__manual__') ? manualSSID : selectValue;

    if (!ssid) {
        showToast('Ingresa o selecciona una red WiFi', 'error');
        return;
    }

    try {
        showToast('Conectando a ' + ssid + '...', 'success');
        const response = await fetch('/api/wifi/connect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssid, password })
        });

        const data = await response.json();
        if (data.success) {
            showToast('Guardado. El dispositivo se reiniciará para conectar.', 'success');
            setTimeout(loadStatus, 5000);
        } else {
            showToast(data.error || 'Error al conectar', 'error');
        }
    } catch (error) {
        console.error('Error connecting WiFi:', error);
        showToast('Error de conexión', 'error');
    }
}

// ============================================
// Backup/Restore
// ============================================

function downloadBackup() {
    window.location.href = '/api/backup';
    showToast('Descargando backup...', 'success');
}

async function restoreBackup() {
    const fileInput = document.getElementById('backup-file');
    if (!fileInput.files || !fileInput.files[0]) {
        showToast('Selecciona un archivo de backup', 'error');
        return;
    }

    if (!confirm('¿Restaurar backup? Esto reemplazará toda la configuración actual.')) {
        return;
    }

    try {
        const file = fileInput.files[0];
        const content = await file.text();

        const response = await fetch('/api/restore', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: content
        });

        const data = await response.json();
        if (data.success) {
            showToast(data.message || 'Backup restaurado', 'success');
            loadDevices();
            loadConfig();
        } else {
            showToast(data.error || 'Error al restaurar', 'error');
        }
    } catch (error) {
        console.error('Error restoring backup:', error);
        showToast('Error al restaurar backup', 'error');
    }
}

function confirmReboot() {
    if (confirm('¿Reiniciar el dispositivo?')) {
        fetch('/api/reboot');
        showToast('Reiniciando...', 'success');
    }
}

function confirmFactoryReset() {
    if (confirm('¿Restaurar de fábrica? Se eliminarán TODOS los dispositivos y configuraciones.')) {
        if (confirm('Esta acción NO se puede deshacer. ¿Continuar?')) {
            fetch('/api/factory-reset');
            showToast('Restaurando valores de fábrica...', 'warning');
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
// Identificación de Señales RF
// ============================================

let identifiedSignalData = null;

async function identifySignal() {
    const btnIdentify = document.getElementById('btn-identify');
    const statusDiv = document.getElementById('identify-status');
    const statusText = document.getElementById('identify-status-text');
    const resultDiv = document.getElementById('identify-result');

    // Mostrar estado de escaneo
    btnIdentify.style.display = 'none';
    statusDiv.style.display = 'flex';
    resultDiv.style.display = 'none';
    identifiedSignalData = null;

    // Lista de frecuencias que escanea el backend (12 frecuencias × 3 modulaciones)
    const frequencies = [300, 303.87, 310, 315, 390, 418, 433, 433.42, 433.92, 434, 868, 915];
    const modulations = ['ASK/OOK', '2-FSK', 'GFSK'];
    const progressText = document.getElementById('identify-progress');

    let freqIndex = 0;
    let modIndex = 0;
    const stepInterval = setInterval(() => {
        if (freqIndex < frequencies.length) {
            statusText.textContent = `Escaneando ${frequencies[freqIndex]} MHz (${modulations[modIndex]})...`;
            progressText.textContent = `${freqIndex + 1} / ${frequencies.length} frecuencias`;
            freqIndex++;
        } else if (modIndex < modulations.length - 1) {
            // Pasar a la siguiente modulación
            modIndex++;
            freqIndex = 0;
            statusText.textContent = `Cambiando a modulación ${modulations[modIndex]}...`;
        } else {
            statusText.textContent = 'Analizando señal detectada...';
        }
    }, 1000);

    try {
        const response = await fetch('/api/rf/identify', {
            method: 'GET',
            headers: { 'Accept': 'application/json' }
        });

        clearInterval(stepInterval);

        const data = await response.json();

        if (data.success) {
            // Guardar datos identificados
            identifiedSignalData = data;

            // Mostrar resultados
            document.getElementById('identify-freq').textContent = data.frequency + ' MHz';
            document.getElementById('identify-mod').textContent = data.modulation_name || 'ASK/OOK';
            document.getElementById('identify-protocol').textContent = data.protocol || 'Genérico';
            document.getElementById('identify-rssi').textContent = data.rssi + ' dBm';
            document.getElementById('identify-length').textContent = data.length + ' bytes';
            document.getElementById('identify-analysis-text').textContent = data.analysis || 'Sin análisis disponible';

            statusDiv.style.display = 'none';
            resultDiv.style.display = 'block';

            showToast('Señal identificada correctamente', 'success');
        } else {
            statusDiv.style.display = 'none';
            btnIdentify.style.display = 'block';

            if (data.frequency) {
                showToast(`Actividad RF detectada en ${data.frequency} MHz pero no se pudo capturar. Mantén presionado el botón.`, 'warning');
            } else {
                showToast(data.message || 'No se detectó ninguna señal RF', 'error');
            }
        }
    } catch (error) {
        clearInterval(stepInterval);
        console.error('Error identifying signal:', error);
        statusDiv.style.display = 'none';
        btnIdentify.style.display = 'block';
        showToast('Error de conexión al identificar', 'error');
    }
}

function useIdentifiedSettings() {
    if (!identifiedSignalData) {
        showToast('No hay señal identificada', 'error');
        return;
    }

    // Establecer la frecuencia detectada en el selector de captura
    const freqSelect = document.getElementById('capture-frequency');
    const customFreq = document.getElementById('custom-frequency');

    // Buscar si la frecuencia está en las opciones predefinidas
    const freqValue = identifiedSignalData.frequency.toString();
    let found = false;
    for (let option of freqSelect.options) {
        if (option.value === freqValue || Math.abs(parseFloat(option.value) - identifiedSignalData.frequency) < 0.1) {
            freqSelect.value = option.value;
            found = true;
            break;
        }
    }

    if (!found) {
        freqSelect.value = 'custom';
        customFreq.style.display = 'block';
        customFreq.value = identifiedSignalData.frequency;
    }

    // Establecer la modulación
    const modSelect = document.getElementById('capture-modulation');
    modSelect.value = identifiedSignalData.modulation.toString();

    // Cambiar a la pestaña de Captura
    document.querySelectorAll('.tab-btn').forEach(btn => btn.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));

    const captureTabBtn = document.querySelector('[data-tab="capture"]');
    const captureTab = document.getElementById('capture-tab');
    if (captureTabBtn) captureTabBtn.classList.add('active');
    if (captureTab) captureTab.classList.add('active');

    showToast(`Ajustes aplicados: ${identifiedSignalData.frequency} MHz, ${identifiedSignalData.modulation_name}`, 'success');
}

function clearIdentifyResult() {
    identifiedSignalData = null;
    document.getElementById('identify-result').style.display = 'none';
    document.getElementById('btn-identify').style.display = 'block';
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

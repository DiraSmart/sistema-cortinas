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
        const signalButtons = (device.signals || [])
            .filter(sig => sig && sig.valid)
            .map(sig => `
                <button class="signal-btn" onclick="transmitSignal('${device.id}', ${sig.index})">
                    ${escapeHtml(sig.name || typeInfo.signals[sig.index] || 'Señal ' + (sig.index + 1))}
                </button>
            `).join('');

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

    try {
        const response = await fetch('/api/devices', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name, type, room })
        });

        const data = await response.json();
        if (data.success) {
            showToast('Dispositivo agregado. Ahora ve a Capturar para grabar las señales.', 'success');
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

    // Mostrar todas las señales posibles para este tipo
    let html = '<div class="signals-grid">';

    typeInfo.signals.forEach((signalName, idx) => {
        const signal = signals.find(s => s && s.index === idx);
        const hasSignal = signal && signal.valid;

        const freqRounded = hasSignal ? parseFloat(signal.frequency).toFixed(2) : '';
        const repeatCount = hasSignal ? (signal.repeatCount || 5) : 5;

        html += `
            <div class="signal-slot ${hasSignal ? 'configured' : 'empty'}">
                <div class="signal-slot-header">
                    <span class="signal-slot-name">${signalName}</span>
                    ${hasSignal ? `<span class="signal-slot-info">${freqRounded} MHz</span>` : ''}
                </div>
                ${hasSignal ? `
                <div class="signal-slot-repeat">
                    <label>Repeticiones:</label>
                    <input type="number" class="repeat-input" id="repeat-${device.id}-${idx}"
                           value="${repeatCount}" min="1" max="20"
                           onchange="updateSignalRepeat('${device.id}', ${idx}, this.value)">
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
    try {
        if (btn) {
            btn.disabled = true;
            btn.textContent = 'Enviando...';
        }

        const response = await fetch(`/api/rf/transmit?id=${deviceId}&signal=${signalIndex}`);
        const data = await response.json();

        if (data.success) {
            showToast('Señal transmitida', 'success');
        } else {
            showToast(data.error || 'Error al transmitir', 'error');
        }
    } catch (error) {
        console.error('Error transmitting signal:', error);
        showToast('Error de conexión', 'error');
    } finally {
        if (btn) {
            btn.disabled = false;
            loadDevices();
        }
    }
}

async function startCapture() {
    const deviceId = document.getElementById('capture-device')?.value;
    const signalSlot = document.getElementById('capture-signal-slot')?.value;

    if (!identifyMode && (!deviceId || signalSlot === '')) {
        showToast('Selecciona un dispositivo y función primero', 'error');
        return;
    }

    const freqSelect = document.getElementById('capture-frequency');
    const customFreq = document.getElementById('custom-frequency');
    const modSelect = document.getElementById('capture-modulation');
    const autoDetect = document.getElementById('auto-detect').checked;

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

        const url = `/api/rf/capture/start?frequency=${frequency}&modulation=${modulation}&auto=${autoDetect}`;
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
    output += `]\n\n`;

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

    // Visual pulse table
    output += `\nPULSOS DETALLADOS:\n`;
    output += `------------------------------------------------\n`;
    for (let i = 0; i < Math.min(pulses.length, 50); i++) {
        const isHigh = i % 2 === 0;
        output += `${(i).toString().padStart(2)}) ${isHigh ? 'HIGH' : 'LOW '} ${pulses[i].toString().padStart(5)}us\n`;
    }
    if (pulses.length > 50) {
        output += `... y ${pulses.length - 50} pulsos mas\n`;
    }

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
// Utilidades
// ============================================

function escapeHtml(text) {
    if (!text) return '';
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

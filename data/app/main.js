const SERVICE_UUID = "b0c5c0ff-8123-4c5c-a63d-2f6f60adbb89";
const TX_UUID = "b0c5c100-8123-4c5c-a63d-2f6f60adbb89";
const RX_UUID = "b0c5c101-8123-4c5c-a63d-2f6f60adbb89";

const SECTION_NAMES = {
    0: "PID Regelung",
    1: "Temperatur",
    2: "Brew PID",
    3: "Bruehkontrolle",
    4: "Waage",
    5: "Display",
    6: "Wartung",
    7: "Energie"
};

const state = {
    connected: false,
    deviceName: "-",
    firmware: "unbekannt",
    parameters: new Map(),
    helpTexts: new Map(),
    logs: [],
    pendingParameters: new Set(),
    parameterErrors: new Map()
};

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const chunkBuffer = new Map();
const helpBuffer = new Map();
const pendingRequests = new Map();
let requestCounter = 0;
let device;
let server;
let txCharacteristic;
let rxCharacteristic;
let toastTimer;

const dom = {};

function initDom() {
    dom.connectBtn = document.getElementById("connectBtn");
    dom.connectionState = document.getElementById("connectionState");
    dom.deviceName = document.getElementById("deviceName");
    dom.firmwareInfo = document.getElementById("firmwareInfo");
    dom.telemetry = {
        current: document.getElementById("tempCurrent"),
        target: document.getElementById("tempTarget"),
        power: document.getElementById("tempPower")
    };
    dom.refreshTelemetry = document.getElementById("refreshTelemetry");
    dom.reloadParameters = document.getElementById("reloadParameters");
    dom.parameterContainer = document.getElementById("parameterContainer");
    dom.factoryReset = document.getElementById("factoryReset");
    dom.toggleLogs = document.getElementById("toggleLogs");
    dom.logsCard = document.getElementById("logsCard");
    dom.logContainer = document.getElementById("logContainer");
    dom.clearLogs = document.getElementById("clearLogs");
    dom.logLevelSelect = document.getElementById("logLevelSelect");
    dom.toast = document.getElementById("toast");
    dom.parameterTemplate = document.getElementById("parameterRowTemplate");
}

function nextRequestId() {
    requestCounter += 1;
    return `req-${Date.now()}-${requestCounter}`;
}

async function connect() {
    if (!navigator.bluetooth) {
        showToast("Web Bluetooth wird nicht unterstuetzt", true);
        return;
    }

    try {
        dom.connectBtn.disabled = true;
        dom.connectBtn.textContent = "Verbinden...";

        device = await navigator.bluetooth.requestDevice({
            filters: [{ services: [SERVICE_UUID] }]
        });

        device.addEventListener("gattserverdisconnected", handleDisconnectEvent);

        server = await device.gatt.connect();
        const service = await server.getPrimaryService(SERVICE_UUID);
        txCharacteristic = await service.getCharacteristic(TX_UUID);
        rxCharacteristic = await service.getCharacteristic(RX_UUID);

        await txCharacteristic.startNotifications();
        txCharacteristic.addEventListener("characteristicvaluechanged", handleNotification);

        state.connected = true;
        state.deviceName = device.name || "Silvia";
        updateConnectionUi();
        enableControls(true);

        await sendCommand("ping");
        requestParameters();
        requestTemperatures();
        showToast("Verbunden mit Silvia");
    } catch (error) {
        showToast(error.message || "Verbindung fehlgeschlagen", true);
        await disconnect();
    } finally {
        dom.connectBtn.disabled = false;
        dom.connectBtn.textContent = state.connected ? "Trennen" : "Verbinden";
    }
}

async function disconnect() {
    if (txCharacteristic) {
        try {
            await txCharacteristic.stopNotifications();
            txCharacteristic.removeEventListener("characteristicvaluechanged", handleNotification);
        } catch (_) {
            // ignore
        }
    }

    if (device && device.gatt.connected) {
        device.gatt.disconnect();
    }

    state.connected = false;
    server = null;
    txCharacteristic = null;
    rxCharacteristic = null;
    state.parameters.clear();
    state.pendingParameters.clear();
    state.parameterErrors.clear();
    state.helpTexts.clear();
    state.firmware = 'unbekannt';
    clearTelemetry();
    updateConnectionUi();
    renderParameters();
    enableControls(false);
    dom.connectBtn.textContent = "Verbinden";
}

function handleDisconnectEvent() {
    showToast("Verbindung getrennt", true);
    disconnect();
}

function clearTelemetry() {
    dom.telemetry.current.textContent = "--";
    dom.telemetry.target.textContent = "--";
    dom.telemetry.power.textContent = "--";
}

function enableControls(connected) {
    dom.refreshTelemetry.disabled = !connected;
    dom.reloadParameters.disabled = !connected;
    dom.factoryReset.disabled = !connected;
    dom.toggleLogs.disabled = !connected;
    dom.logLevelSelect.disabled = !connected;
}

function updateConnectionUi() {
    dom.connectionState.textContent = state.connected ? "Verbunden" : "Getrennt";
    dom.deviceName.textContent = state.deviceName;
    dom.firmwareInfo.textContent = state.firmware;
    dom.connectBtn.textContent = state.connected ? "Trennen" : "Verbinden";
}

function showToast(message, isError = false) {
    if (!dom.toast) return;
    dom.toast.textContent = message;
    dom.toast.classList.toggle("error", isError);
    dom.toast.classList.add("visible");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => {
        dom.toast.classList.remove("visible");
    }, 2600);
}

async function sendCommand(command, payload = {}) {
    if (!state.connected || !rxCharacteristic) {
        throw new Error("Keine Verbindung");
    }

    const requestId = nextRequestId();
    const body = { ...payload, command, requestId };
    const encoded = encoder.encode(JSON.stringify(body));
    await rxCharacteristic.writeValue(encoded);

    return new Promise((resolve, reject) => {
        const timeout = setTimeout(() => {
            pendingRequests.delete(requestId);
            reject(new Error("Zeitueberschreitung"));
        }, 6000);

        pendingRequests.set(requestId, {
            resolve,
            reject,
            command,
            timeout
        });
    });
}

function handleNotification(event) {
    const data = event.target.value;
    if (!data || data.byteLength === 0) {
        return;
    }

    const json = decoder.decode(data);

    try {
        const message = JSON.parse(json);
        processMessage(message);
    } catch (error) {
        console.error("Unbekannte Nachricht", json, error);
    }
}

function processMessage(message) {
    switch (message.type) {
        case "chunk":
            handleChunk(message);
            break;
        case "telemetry":
            updateTelemetry(message);
            break;
        case "parameter":
            updateParameterState(message);
            break;
        case "response":
            handleResponse(message);
            break;
        case "log":
            appendLog(message);
            break;
        case "help":
            handleHelp(message);
            break;
        default:
            console.warn("Unbekannter Nachrichtentyp", message);
    }
}

function chunkKey(message) {
    const cmd = message.command || "";
    const req = message.requestId || "";
    return `${cmd}|${req}`;
}

function handleChunk(message) {
    const key = chunkKey(message);
    let entry = chunkBuffer.get(key);
    if (!entry) {
        entry = { count: message.count, chunks: [] };
        chunkBuffer.set(key, entry);
    }

    entry.count = message.count;
    entry.chunks[message.index] = message.data;

    const filled = entry.chunks.filter((part) => typeof part === "string").length;
    if (filled >= entry.count) {
        const combined = entry.chunks.join("");
        chunkBuffer.delete(key);
        try {
            const parsed = JSON.parse(combined);
            if (!parsed.command && message.command) parsed.command = message.command;
            if (!parsed.requestId && message.requestId) parsed.requestId = message.requestId;
            processMessage(parsed);
        } catch (error) {
            console.error("Fehler beim Zusammenfuehren von Chunk-Daten", error);
            showToast("Fehler beim Dekodieren der Antwort", true);
        }
    }
}

function handleResponse(message) {
    const requestId = message.requestId;
    if (requestId && pendingRequests.has(requestId)) {
        const waiter = pendingRequests.get(requestId);
        clearTimeout(waiter.timeout);
        pendingRequests.delete(requestId);
        if (message.status === "ok") {
            waiter.resolve(message);
        } else {
            waiter.reject(new Error(message.error || "Fehler"));
        }
    }

    if (message.status !== "ok") {
        showToast(message.error || "Fehler", true);
        return;
    }

    switch (message.command) {
        case "set_parameter":
            if (message.name) {
                applyParameterAck(message.name, message.value);
            }
            break;
        case "set_parameters":
            requestParameters();
            break;
        case "factory_reset":
            showToast("Factory Reset ausgefuehrt");
            break;
        case "log_level":
            showToast("Log-Level gesetzt");
            break;
        default:
            break;
    }
}

function applyParameterAck(name, value) {
    const entry = state.parameters.get(name);
    if (entry) {
        entry.value = value;
        state.parameters.set(name, entry);
    }
    state.pendingParameters.delete(name);
    state.parameterErrors.delete(name);
    renderParameters();
}

function updateTelemetry(payload) {
    if (typeof payload.current === "number") {
        dom.telemetry.current.textContent = payload.current.toFixed(1);
    }
    if (typeof payload.target === "number") {
        dom.telemetry.target.textContent = payload.target.toFixed(1);
    }
    if (typeof payload.power === "number") {
        dom.telemetry.power.textContent = payload.power.toFixed(1);
    }
}

function updateParameterState(message) {
    const entry = {
        name: message.name,
        label: message.label || message.name,
        section: Number(message.section || 0),
        position: Number(message.position || 0),
        value: message.value,
        show: message.show !== false,
        hasHelp: Boolean(message.hasHelp),
        min: message.min,
        max: message.max
    };

    state.parameters.set(entry.name, entry);

    if (entry.name === "VERSION" && typeof entry.value === "string") {
        state.firmware = entry.value;
        updateConnectionUi();
    }

    renderParameters();
}

function appendLog(message) {
    const entry = {
        level: message.level || "INFO",
        text: message.message || "",
        time: new Date().toLocaleTimeString()
    };

    state.logs.push(entry);
    if (state.logs.length > 250) {
        state.logs.splice(0, state.logs.length - 250);
    }

    renderLogs();
}

function handleHelp(message) {
    const name = message.name;
    if (!name) return;

    let entry = helpBuffer.get(name);
    if (!entry) {
        entry = { chunks: [] };
        helpBuffer.set(name, entry);
    }

    entry.chunks[message.index] = message.text || "";

    if (!message.more) {
        const full = entry.chunks.join("");
        helpBuffer.delete(name);
        state.helpTexts.set(name, full);
        renderHelpText(name, full);
    }
}

function renderHelpText(name, text) {
    const row = dom.parameterContainer.querySelector(`[data-parameter="${name}"]`);
    if (!row) return;
    const helpBlock = row.querySelector(".help-text");
    if (!helpBlock) return;
    helpBlock.textContent = text || "Keine Beschreibung";
    helpBlock.classList.toggle("hidden", !text);
}

function renderLogs() {
    if (!dom.logsCard || !dom.logContainer) return;
    if (dom.logsCard.classList.contains("hidden")) {
        return;
    }

    dom.logContainer.textContent = state.logs
        .map((log) => `[${log.time}] ${log.level.padEnd(7)} ${log.text}`)
        .join("\n");
}

function renderParameters() {
    const container = dom.parameterContainer;
    if (!container) return;

    if (!state.connected) {
        container.innerHTML = '<p class="placeholder">Verbinde dich mit der Silvia, um Parameter zu laden.</p>';
        return;
    }

    const entries = Array.from(state.parameters.values())
        .filter((item) => item.show !== false)
        .sort((a, b) => {
            if (a.section === b.section) {
                return a.position - b.position;
            }
            return a.section - b.section;
        });

    if (entries.length === 0) {
        container.innerHTML = '<p class="placeholder">Keine Parameter verfuegbar.</p>';
        return;
    }

    container.innerHTML = "";
    const sections = new Map();

    entries.forEach((entry) => {
        if (!sections.has(entry.section)) {
            sections.set(entry.section, []);
        }
        sections.get(entry.section).push(entry);
    });

    sections.forEach((items, sectionId) => {
        const wrapper = document.createElement("div");
        wrapper.className = "parameter-section";
        const title = document.createElement("h3");
        title.textContent = SECTION_NAMES[sectionId] || `Bereich ${sectionId}`;
        wrapper.appendChild(title);

        items.forEach((entry) => {
            const row = dom.parameterTemplate.content.firstElementChild.cloneNode(true);
            row.dataset.parameter = entry.name;

            const labelEl = row.querySelector(".parameter-label");
            const nameEl = row.querySelector(".parameter-name");
            const controlEl = row.querySelector(".parameter-control");
            const rangeEl = row.querySelector(".range");
            const helpBtn = row.querySelector(".help");
            const helpBlock = row.querySelector(".help-text");
            const errorBlock = row.querySelector(".error-text");

            if (labelEl) labelEl.textContent = entry.label;
            if (nameEl) nameEl.textContent = entry.name;
            if (rangeEl) rangeEl.textContent = `Min ${formatNumber(entry.min)} / Max ${formatNumber(entry.max)}`;

            if (helpBtn) {
                helpBtn.disabled = !entry.hasHelp;
                helpBtn.addEventListener("click", () => requestHelp(entry.name));
            }

            if (helpBlock) {
                const cached = state.helpTexts.get(entry.name);
                if (cached) {
                    helpBlock.textContent = cached;
                    helpBlock.classList.remove("hidden");
                } else {
                    helpBlock.classList.add("hidden");
                }
            }

            if (errorBlock) {
                const err = state.parameterErrors.get(entry.name);
                if (err) {
                    errorBlock.textContent = err;
                    errorBlock.classList.remove("hidden");
                    row.classList.add("error");
                } else {
                    errorBlock.classList.add("hidden");
                    errorBlock.textContent = "";
                    row.classList.remove("error");
                }
            }

            if (isBooleanParameter(entry)) {
                const wrapperLabel = document.createElement("label");
                wrapperLabel.className = "switch";
                const input = document.createElement("input");
                input.type = "checkbox";
                input.checked = Boolean(Number(entry.value));
                input.addEventListener("change", () => {
                    updateParameter(entry.name, input.checked ? 1 : 0);
                });
                const span = document.createElement("span");
                span.textContent = input.checked ? "An" : "Aus";
                input.addEventListener("change", () => {
                    span.textContent = input.checked ? "An" : "Aus";
                });
                wrapperLabel.appendChild(input);
                wrapperLabel.appendChild(span);
                controlEl.appendChild(wrapperLabel);
            } else {
                const input = document.createElement("input");
                input.type = "number";
                input.value = formatNumber(entry.value, true);
                if (isFinite(entry.min)) input.min = entry.min;
                if (isFinite(entry.max)) input.max = entry.max;
                input.step = inferStep(entry.value);
                input.inputMode = isFloat(entry.value) ? "decimal" : "numeric";
                input.addEventListener("change", () => {
                    const raw = input.value;
                    if (raw === "") return;
                    const numeric = isFloat(entry.value) ? parseFloat(raw) : parseInt(raw, 10);
                    if (Number.isNaN(numeric)) {
                        state.parameterErrors.set(entry.name, "Ungueltiger Wert");
                        renderParameters();
                        return;
                    }
                    updateParameter(entry.name, numeric);
                });
                controlEl.appendChild(input);
            }

            if (state.pendingParameters.has(entry.name)) {
                row.classList.add("pending");
            } else {
                row.classList.remove("pending");
            }

            wrapper.appendChild(row);
        });

        container.appendChild(wrapper);
    });
}

function isBooleanParameter(entry) {
    return Number(entry.min) === 0 && Number(entry.max) === 1 && Number.isInteger(Number(entry.value));
}

function isFloat(value) {
    return typeof value === "number" && !Number.isInteger(value);
}

function inferStep(value) {
    if (isFloat(value)) {
        return "0.1";
    }
    return "1";
}

function formatNumber(value, keepDecimals = false) {
    if (value === null || value === undefined) return "-";
    if (typeof value === "number") {
        if (!keepDecimals || Number.isInteger(value)) {
            return value.toString();
        }
        return value.toFixed(2);
    }
    return String(value);
}

function requestParameters() {
    state.parameters.clear();
    state.helpTexts.clear();
    renderParameters();
    sendCommand("get_parameters").catch((error) => {
        showToast(error.message, true);
    });
}

function requestTemperatures() {
    sendCommand("get_temperatures").catch((error) => {
        showToast(error.message, true);
    });
}

function requestHelp(name) {
    if (!name) return;
    if (state.helpTexts.has(name)) {
        renderHelpText(name, state.helpTexts.get(name));
        return;
    }
    sendCommand("get_parameter_help", { name }).catch((error) => {
        showToast(error.message, true);
    });
}

async function updateParameter(name, value) {
    if (!state.connected) {
        showToast("Keine aktive Verbindung", true);
        return;
    }

    const current = state.parameters.get(name);
    if (current && current.value === value) {
        return;
    }

    state.pendingParameters.add(name);
    renderParameters();

    try {
        await sendCommand("set_parameter", { name, value });
    } catch (error) {
        state.parameterErrors.set(name, error.message);
        state.pendingParameters.delete(name);
        renderParameters();
        showToast(error.message, true);
    }
}

function setupEventHandlers() {
    dom.connectBtn.addEventListener("click", () => {
        if (state.connected) {
            disconnect();
        } else {
            connect();
        }
    });

    dom.refreshTelemetry.addEventListener("click", () => requestTemperatures());
    dom.reloadParameters.addEventListener("click", () => requestParameters());

    dom.factoryReset.addEventListener("click", () => {
        if (!state.connected) return;
        const confirmed = window.confirm("Factory Reset wirklich ausfuehren?");
        if (!confirmed) return;
        sendCommand("factory_reset").catch((error) => showToast(error.message, true));
    });

    dom.toggleLogs.addEventListener("click", () => {
        dom.logsCard.classList.toggle("hidden");
        dom.toggleLogs.textContent = dom.logsCard.classList.contains("hidden") ? "Logs anzeigen" : "Logs verbergen";
        renderLogs();
    });

    dom.clearLogs.addEventListener("click", () => {
        state.logs = [];
        renderLogs();
    });

    dom.logLevelSelect.addEventListener("change", (event) => {
        const level = event.target.value;
        sendCommand("log_level", { level }).catch((error) => {
            showToast(error.message, true);
        });
    });
}

document.addEventListener("DOMContentLoaded", () => {
    initDom();
    setupEventHandlers();
    enableControls(false);
    renderParameters();
});




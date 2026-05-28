/*
 * web/src/app.js — Scriba Web Flasher application logic.
 * Copyright (C) 2025-2026 Josh at WLTechBlog <wltechblog@wanderlounge.net>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * Scriba Web Flasher - Application Logic
 *
 * Loads the WASM module, drives the scriba C API from JS,
 * and manages the UI state machine.
 */

/* ------------------------------------------------------------------ */
/*  State                                                              */
/* ------------------------------------------------------------------ */

var Module = null;
var scribaReady = false;
var currentState = 'idle';
var firmwareData = null;
var firmwareFileName = '';
var flashSize = 0;
var wasmBusy = false;
var logEntries = [];
var hadFlashOp = false;

var BUSY_STATES = ['connecting', 'detecting', 'reading', 'writing', 'erasing'];
var FLASH_OP_STATES = ['reading', 'writing', 'erasing'];

function isBusy() { return BUSY_STATES.indexOf(currentState) !== -1; }
function isFlashOp() { return FLASH_OP_STATES.indexOf(currentState) !== -1; }

/* Prevent accidental navigation during active operations */
window.addEventListener('beforeunload', function(e) {
    if (isFlashOp()) {
        e.preventDefault();
        e.returnValue = 'A flash operation is in progress. Are you sure?';
    }
});

async function wasmCall(name, returnType, argTypes, args) {
    var deadline = Date.now() + 10000;
    while (wasmBusy) {
        if (Date.now() > deadline) {
            wasmBusy = false;
            log('WASM call timed out waiting for previous operation', 'error');
            throw new Error('wasmCall timeout - previous operation hung');
        }
        await new Promise(function(r) { setTimeout(r, 50); });
    }
    wasmBusy = true;
    try {
        return await Module.ccall(name, returnType, argTypes, args, {async: true});
    } finally {
        wasmBusy = false;
    }
}

/* ------------------------------------------------------------------ */
/*  Logging                                                            */
/* ------------------------------------------------------------------ */

function log(msg, level) {
    level = level || 'info';
    logEntries.push({ time: new Date().toISOString(), level: level, msg: msg });
    appendToLog(document.getElementById('log'), msg, level);
    appendToLog(document.getElementById('op-log'), msg, level);
}

function appendToLog(el, msg, level) {
    if (!el) return;
    var line = document.createElement('div');
    line.className = level;
    line.textContent = msg;
    el.appendChild(line);
    el.scrollTop = el.scrollHeight;
}

function toggleEraseDebug() {
    window.__eraseDebug = !window.__eraseDebug;
    var els = ['btn-erase-debug', 'op-btn-debug'];
    for (var i = 0; i < els.length; i++) {
        var btn = document.getElementById(els[i]);
        if (!btn) continue;
        if (window.__eraseDebug) {
            btn.classList.remove('btn-outline-secondary');
            btn.classList.add('btn-warning');
        } else {
            btn.classList.remove('btn-warning');
            btn.classList.add('btn-outline-secondary');
        }
    }
    if (window.__eraseDebug)
        log('USB debug logging ENABLED', 'warn');
    else
        log('USB debug logging disabled', 'info');
}

Object.assign(window, { toggleEraseDebug: toggleEraseDebug });

/* Route USB debug messages from browser console to the visible log */
(function() {
    var origLog = console.log;
    console.log = function() {
        var text = Array.prototype.join.call(arguments, ' ');
        origLog.apply(console, arguments);
        if (text.indexOf('[USB-DBG]') !== -1 || text.indexOf('[ERASE-DBG]') !== -1)
            log(text, 'debug');
    };
})();

function saveLog() {
    var text = logEntries.map(function(e) { return e.time + ' [' + e.level + '] ' + e.msg; }).join('\n');
    var blob = new Blob([text], { type: 'text/plain;charset=utf-8' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = 'scriba-log.txt';
    a.click();
    URL.revokeObjectURL(url);
    log('Log saved to scriba-log.txt', 'info');
}

/* ------------------------------------------------------------------ */
/*  Progress                                                           */
/* ------------------------------------------------------------------ */

function setProgress(elId, percent, label) {
    var fill = document.getElementById(elId + '-fill');
    var lbl = document.getElementById(elId + '-label');
    if (fill) fill.style.width = percent + '%';
    if (lbl) lbl.textContent = label || '';
}

function showProgress(percent, label) {
    var c = document.getElementById('progress');
    if (c) c.classList.remove('d-none');
    setProgress('progress', percent, label);
    setProgress('op-progress', percent, label);
    var opFill = document.getElementById('op-progress-fill');
    if (opFill && percent === 100) {
        opFill.classList.remove('progress-bar-animated');
        opFill.classList.add('bg-success');
    }
}

function hideProgress() {
    var c = document.getElementById('progress');
    if (c) c.classList.add('d-none');
    var opEl = document.getElementById('op-progress');
    if (opEl) {
        var fill = document.getElementById('op-progress-fill');
        if (fill) {
            fill.style.width = '0%';
            fill.classList.add('progress-bar-animated');
            fill.classList.remove('bg-success');
        }
    }
}

/* ------------------------------------------------------------------ */
/*  UI State                                                           */
/* ------------------------------------------------------------------ */

function setState(state) {
    currentState = state;
    var badge = document.getElementById('status-badge');

    var labels = {
        idle: ['Idle', 'secondary'],
        connecting: ['Connecting...', 'warning'],
        detecting: ['Detecting...', 'warning'],
        reading: ['Reading...', 'warning'],
        writing: ['Writing...', 'warning'],
        erasing: ['Erasing...', 'warning'],
        done: ['Ready', 'success'],
        error: ['Error', 'danger'],
    };
    var info = labels[state] || ['Unknown', 'secondary'];
    badge.textContent = info[0];
    badge.className = 'badge bg-' + info[1] + ' ms-auto';

    var busy = isBusy();
    var flashOp = isFlashOp();
    if (flashOp) hadFlashOp = true;
    var warn = document.getElementById('op-warning');
    if (busy) warn.classList.remove('d-none');
    else warn.classList.add('d-none');

    var modal = document.getElementById('op-modal');
    if (flashOp) modal.classList.remove('d-none');
    else modal.classList.add('d-none');

    var saveBtn = document.getElementById('btn-save-log');
    if (state === 'idle') {
        hadFlashOp = false;
        if (saveBtn) saveBtn.classList.add('d-none');
    } else if ((state === 'done' || state === 'error') && hadFlashOp) {
        if (saveBtn) saveBtn.classList.remove('d-none');
    }

    document.getElementById('btn-connect').disabled = busy;
    document.getElementById('btn-read-id').disabled = busy;
    document.getElementById('btn-read').disabled = busy || state === 'idle';
    document.getElementById('btn-write').disabled = busy || state === 'idle';
    document.getElementById('btn-erase').disabled = busy || state === 'idle';
}

function showChipInfo(name, size, programmer) {
    document.getElementById('chip-disconnected').classList.add('d-none');
    document.getElementById('chip-info').classList.remove('d-none');
    document.getElementById('info-chip').textContent = name || 'Unknown';
    document.getElementById('info-size').textContent = size ? formatSize(size) : 'Unknown';
    var progNames = {0: 'CH341A', 1: 'EZP2019', 2: 'Auto'};
    document.getElementById('info-programmer').textContent = progNames[programmer] || 'Unknown';
}

function formatSize(bytes) {
    if (bytes >= 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
    if (bytes >= 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return bytes + ' B';
}

/* ------------------------------------------------------------------ */
/*  WASM Module Init                                                   */
/* ------------------------------------------------------------------ */

async function initModule() {
    console.log('Loading Scriba WASM module...');

    try {
        Module = await createScribaModule({
            printErr: function(text) {
                if (typeof text === 'string') {
                    if (text.indexOf('[ERASE-DBG]') !== -1) log(text, 'warn');
                    else if (text.startsWith('[DEBUG]')) log(text, 'debug');
                    else if (text.startsWith('[WARN]') || text.startsWith('[EZP]')) log(text, 'warn');
                    else if (text.startsWith('[ERROR]')) log(text, 'error');
                    else log(text, 'info');
                }
            },
            print: function(text) {
                if (typeof text === 'string') log(text, 'info');
            },
        });

        console.log('WASM module loaded (' +
            (Module.HEAPU8.length / 1024 / 1024).toFixed(1) + ' MB heap)');

        scribaReady = true;
        log('Ready - click Connect to select your SPI programmer');
    } catch (e) {
        log('Failed to initialize - check console for details', 'error');
        console.error(e);
    }
}

/* ------------------------------------------------------------------ */
/*  Connect                                                            */
/* ------------------------------------------------------------------ */

async function connectDevice() {
    if (!scribaReady) {
        log('Module not ready', 'warn');
        return;
    }

    setState('connecting');
    log('Requesting USB device...');

    try {
        var filters = [
            { vendorId: 0x1A86, productId: 0x5512 },
            { vendorId: 0x1FC8, productId: 0x310B },
            { vendorId: 0x1FC8, productId: 0x310C },
            { vendorId: 0x1FC8, productId: 0x310D },
        ];

        var device;
        try {
            device = await navigator.usb.requestDevice({ filters: filters });
        } catch (e) {
            log('No device selected', 'warn');
            setState('idle');
            return;
        }

        if (!window._webusb_devices) window._webusb_devices = [];
        var already = window._webusb_devices.some(function(d) { return d === device; });
        if (!already) window._webusb_devices.push(device);

        console.log('Device selected: VID=0x' + device.vendorId.toString(16) +
            ' PID=0x' + device.productId.toString(16));

        log('Connecting to programmer...');
        var result = await wasmCall('scriba_init', 'number', [], []);
        if (result !== 0) {
            log('Failed to connect to programmer', 'error');
            setState('error');
            return;
        }

        log('Programmer connected');

        setState('detecting');
        showProgress(50, 'Detecting flash chip...');

        var detectResult = await wasmCall('scriba_detect_chip', 'number', [], []);
        if (detectResult !== 0) {
            log('No flash chip detected - check wiring', 'error');
            hideProgress();
            setState('error');
            return;
        }

        flashSize = Module.ccall('scriba_get_flash_size', 'number', [], []);
        var namePtr = Module.ccall('scriba_get_chip_name', 'number', [], []);
        var chipName = namePtr ? Module.UTF8ToString(namePtr) : 'Unknown';
        var progType = Module.ccall('scriba_get_programmer_type', 'number', [], []);

        showChipInfo(chipName, flashSize, progType);
        log('Detected: ' + chipName + ' (' + formatSize(flashSize) + ')');

        showProgress(100, 'Detection complete');
        setTimeout(hideProgress, 1000);
        setState('done');
    } catch (e) {
        log('Connection error: ' + e.message, 'error');
        console.error(e);
        hideProgress();
        setState('error');
    }
}

/* ------------------------------------------------------------------ */
/*  Read Chip ID                                                       */
/* ------------------------------------------------------------------ */

async function readChipId() {
    if (!scribaReady) return;

    if (currentState === 'idle') {
        await connectDevice();
        return;
    }

    setState('detecting');
    log('Re-detecting flash chip...');

    try {
        var detectResult = await wasmCall('scriba_detect_chip', 'number', [], []);
        if (detectResult !== 0) {
            log('No flash chip detected', 'error');
            setState('error');
            return;
        }

        flashSize = Module.ccall('scriba_get_flash_size', 'number', [], []);
        var namePtr = Module.ccall('scriba_get_chip_name', 'number', [], []);
        var chipName = namePtr ? Module.UTF8ToString(namePtr) : 'Unknown';
        var progType = Module.ccall('scriba_get_programmer_type', 'number', [], []);

        showChipInfo(chipName, flashSize, progType);
        log('Detected: ' + chipName + ' (' + formatSize(flashSize) + ')');
        setState('done');
    } catch (e) {
        log('Detection error: ' + e.message, 'error');
        console.error(e);
        setState('error');
    }
}

/* ------------------------------------------------------------------ */
/*  Read Flash                                                         */
/* ------------------------------------------------------------------ */

async function doRead() {
    if (!scribaReady || flashSize <= 0) return;

    setState('reading');
    log('Reading flash (' + formatSize(flashSize) + ')...');

    try {
        var bufPtr = Module._malloc(flashSize);
        if (!bufPtr) {
            log('Failed to allocate memory', 'error');
            hideProgress();
            setState('error');
            return;
        }

        var chunkSize = 65536;
        var offset = 0;

        while (offset < flashSize) {
            var len = Math.min(chunkSize, flashSize - offset);
            var pct = Math.round((offset / flashSize) * 100);
            showProgress(pct, 'Reading... ' + pct + '% (' + formatSize(offset) + ' / ' + formatSize(flashSize) + ')');

            var result = await wasmCall('scriba_read_flash', 'number',
                ['number', 'number', 'number'],
                [bufPtr + offset, offset, len]);

            if (result < 0) {
                log('Read failed at offset 0x' + offset.toString(16) + ': error ' + result, 'error');
                Module._free(bufPtr);
                hideProgress();
                setState('error');
                return;
            }
            offset += len;
        }

        showProgress(95, 'Preparing download...');

        var data = Module.HEAPU8.slice(bufPtr, bufPtr + flashSize);
        Module._free(bufPtr);

        var blob = new Blob([data], { type: 'application/octet-stream' });
        var url = URL.createObjectURL(blob);
        var a = document.createElement('a');
        a.href = url;
        a.download = 'flash_dump.bin';
        a.click();
        URL.revokeObjectURL(url);

        showProgress(100, 'Read complete');
        log('Flash read complete - downloaded as flash_dump.bin (' + formatSize(flashSize) + ')');
        setTimeout(hideProgress, 1500);
        setState('done');
    } catch (e) {
        log('Read error: ' + e.message, 'error');
        console.error(e);
        hideProgress();
        setState('error');
    }
}

/* ------------------------------------------------------------------ */
/*  Write Flash                                                        */
/* ------------------------------------------------------------------ */

function selectFirmware() {
    document.getElementById('firmware-file').click();
}

function firmwareSelected(input) {
    if (!input.files || !input.files[0]) return;

    var file = input.files[0];
    firmwareFileName = file.name;

    var reader = new FileReader();
    reader.onload = function(e) {
        firmwareData = new Uint8Array(e.target.result);
        var sizeMB = (firmwareData.length / (1024 * 1024)).toFixed(2);
        document.getElementById('file-info').textContent =
            firmwareFileName + ' (' + sizeMB + ' MB)';
        log('Firmware loaded: ' + firmwareFileName + ' (' + sizeMB + ' MB)');

        if (firmwareData.length > flashSize) {
            log('Warning: firmware (' + formatSize(firmwareData.length) +
                ') is larger than flash (' + formatSize(flashSize) + ')', 'warn');
        }

        document.getElementById('btn-start-write').classList.remove('d-none');
    };
    reader.readAsArrayBuffer(file);
}

async function doWrite() {
    if (!scribaReady || !firmwareData) {
        log('No firmware file selected', 'warn');
        return;
    }

    if (!confirm('Write ' + firmwareFileName + ' to flash?\nThis will erase and overwrite the selected region.')) return;

    document.getElementById('btn-start-write').classList.add('d-none');

    var writeLen = Math.min(firmwareData.length, flashSize);

    setState('erasing');
    showProgress(0, 'Erasing flash...');
    log('Erasing flash (' + formatSize(writeLen) + ')...');

    try {
        log('Erasing entire chip...');
        var eraseResult = await wasmCall('scriba_erase_flash', 'number',
            ['number', 'number'],
            [0, flashSize]);

        if (eraseResult !== 0) {
            log('Erase failed: error ' + eraseResult, 'error');
            hideProgress();
            setState('error');
            return;
        }
        log('Erase complete');

        setState('writing');
        log('Writing ' + firmwareFileName + ' (' + formatSize(writeLen) + ')...');

        var dataPtr = Module._malloc(writeLen);
        if (!dataPtr) {
            log('Failed to allocate WASM memory', 'error');
            hideProgress();
            setState('error');
            return;
        }
        Module.HEAPU8.set(firmwareData.subarray(0, writeLen), dataPtr);

        var writeChunk = 65536;
        var writeOffset = 0;

        while (writeOffset < writeLen) {
            var len = Math.min(writeChunk, writeLen - writeOffset);
            var pct = Math.round((writeOffset / writeLen) * 100);
            showProgress(pct, 'Writing... ' + pct + '% (' + formatSize(writeOffset) + ' / ' + formatSize(writeLen) + ')');

            var writeResult = await wasmCall('scriba_write_flash', 'number',
                ['number', 'number', 'number'],
                [dataPtr + writeOffset, writeOffset, len]);

            if (writeResult <= 0) {
                log('Write failed at offset 0x' + writeOffset.toString(16) + ': error ' + writeResult, 'error');
                Module._free(dataPtr);
                hideProgress();
                setState('error');
                return;
            }
            writeOffset += len;
            await new Promise(function(r) { setTimeout(r, 10); });
        }

        Module._free(dataPtr);
        log('Write complete');

        showProgress(85, 'Verifying...');
        log('Verifying write...');

        var verifyBuf = Module._malloc(writeLen);
        if (verifyBuf) {
            var verifyOk = true;
            var verifyChunk = 65536;
            var verifyOffset = 0;

            while (verifyOffset < writeLen) {
                var len = Math.min(verifyChunk, writeLen - verifyOffset);
                var pct = 85 + Math.round((verifyOffset / writeLen) * 14);
                showProgress(pct, 'Verifying... ' + Math.round((verifyOffset / writeLen) * 100) + '%');

                var readResult = await wasmCall('scriba_read_flash', 'number',
                    ['number', 'number', 'number'],
                    [verifyBuf + verifyOffset, verifyOffset, len]);

                if (readResult >= 0) {
                    for (var i = 0; i < len; i++) {
                        if (Module.HEAPU8[verifyBuf + verifyOffset + i] !== firmwareData[verifyOffset + i]) {
                            verifyOk = false;
                            log('Verify mismatch at offset 0x' + (verifyOffset + i).toString(16), 'error');
                            break;
                        }
                    }
                    if (!verifyOk) break;
                } else {
                    log('Verify read failed, skipping verification', 'warn');
                    verifyOk = false;
                    break;
                }
                verifyOffset += len;
            }

            Module._free(verifyBuf);
            if (verifyOk) log('Verify: OK - data matches');
        }

        showProgress(100, 'Write complete');
        log('Firmware written successfully!');
        setTimeout(hideProgress, 1500);
        setState('done');
    } catch (e) {
        log('Write error: ' + e.message, 'error');
        console.error(e);
        hideProgress();
        setState('error');
    }
}

/* ------------------------------------------------------------------ */
/*  Erase Flash                                                        */
/* ------------------------------------------------------------------ */

async function doErase() {
    if (!scribaReady || flashSize <= 0) return;

    if (!confirm('Erase the entire flash chip? This cannot be undone.')) return;

    setState('erasing');
    log('Erasing entire flash (' + formatSize(flashSize) + ')...');

    try {
        showProgress(0, 'Erasing entire chip (BE command)...');

        var result = await wasmCall('scriba_erase_flash', 'number',
            ['number', 'number'],
            [0, flashSize]);

        if (result !== 0) {
            log('Full chip erase failed: error ' + result, 'error');
            hideProgress();
            setState('error');
            return;
        }

        showProgress(100, 'Verifying erase...');
        var verifyPtr = Module._malloc(256);
        if (verifyPtr) {
            var readResult = await wasmCall('scriba_read_flash', 'number',
                ['number', 'number', 'number'],
                [verifyPtr, 0, 256]);
            if (readResult >= 0) {
                var allFF = true;
                var firstNonFF = -1;
                for (var i = 0; i < 256; i++) {
                    if (Module.HEAPU8[verifyPtr + i] !== 0xFF) {
                        allFF = false;
                        if (firstNonFF < 0) firstNonFF = i;
                    }
                }
                if (allFF) {
                    log('Erase verified: first 256 bytes are all 0xFF', 'success');
                } else {
                    log('Erase FAILED: byte ' + firstNonFF + ' is 0x' + Module.HEAPU8[verifyPtr + firstNonFF].toString(16) + ' (expected 0xFF)', 'error');
                    var hex = '';
                    for (var i = 0; i < 32; i++) hex += ('0' + Module.HEAPU8[verifyPtr + i].toString(16)).slice(-2) + ' ';
                    log('First 32 bytes: ' + hex.trim(), 'warn');
                }
            } else {
                log('Post-erase read failed', 'warn');
            }
            Module._free(verifyPtr);
        }

        showProgress(100, 'Erase complete');
        log('Flash erased successfully');
        setTimeout(hideProgress, 1500);
        setState('done');
    } catch (e) {
        log('Erase error: ' + e.message, 'error');
        console.error(e);
        hideProgress();
        setState('error');
    }
}

/* ------------------------------------------------------------------ */
/*  Init                                                               */
/* ------------------------------------------------------------------ */

Object.assign(window, {
    connectDevice, readChipId, selectFirmware, firmwareSelected,
    doRead, doWrite, doErase, saveLog
});

(function() {
    if (!navigator.usb) {
        document.getElementById('browser-warning').classList.remove('d-none');
        document.querySelector('.flasher-card').classList.add('d-none');
        return;
    }

    navigator.usb.addEventListener('connect', function(e) {
        console.log('USB device connected: VID=0x' + e.device.vendorId.toString(16) +
            ' PID=0x' + e.device.productId.toString(16));
    });
    navigator.usb.addEventListener('disconnect', function(e) {
        console.log('USB device disconnected');
        wasmBusy = false;
    });

    setState('idle');
    initModule();
})();

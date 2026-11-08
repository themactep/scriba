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

async function wasmCall(name, returnType, argTypes, args) {
    var waited = 0;
    while (wasmBusy) {
        if (waited > 60000) throw new Error('wasmCall timeout waiting for previous operation');
        await new Promise(function(r) { setTimeout(r, 50); });
        waited += 50;
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

/* Wall-clock HH:MM:SS.mmm, so operation times can be read off the log */
function timestamp() {
    var d = new Date();
    function pad(n, w) { return String(n).padStart(w || 2, '0'); }
    return pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' +
        pad(d.getSeconds()) + '.' + pad(d.getMilliseconds(), 3);
}

/* Columns of text after the timestamp; index.html sizes the page to fit
 * them (--log-cols there is this plus the 14-column timestamp and one
 * spare). Longer messages are broken at spaces into continuation lines. */
var LOG_COLS = 80;

function wrapLogText(msg) {
    var lines = [], indent = msg.match(/^ */)[0] + '  ';
    while (msg.length > LOG_COLS) {
        var cut = msg.lastIndexOf(' ', LOG_COLS);
        if (cut <= indent.length) break;     /* one long word: the page wraps it */
        lines.push(msg.slice(0, cut));
        msg = indent + msg.slice(cut + 1).replace(/^ +/, '');
    }
    lines.push(msg);
    return lines;
}

function log(msg, level) {
    level = level || 'info';
    var el = document.getElementById('log');
    var ts = timestamp() + '  ';
    msg = String(msg).replace(/^\r+/, '');   /* C progress lines start with \r */
    msg.split('\n').forEach(function(part) {
        (part.trim() ? wrapLogText(part) : [part]).forEach(function(text) {
            var line = document.createElement('div');
            line.className = level;
            if (text.trim()) {
                var span = document.createElement('span');
                span.className = 'ts';
                span.textContent = ts;
                line.appendChild(span);
            }
            line.appendChild(document.createTextNode(text));
            el.appendChild(line);
        });
    });
    el.scrollTop = el.scrollHeight;
}

/* Log overall progress at every 10% step. The C flash layers only see one
 * 64 KB chunk per call, so their own completion lines are suppressed in the
 * WASM build; with timestamps these lines also show the transfer rate. */
function hexAddr(n) {
    return '0x' + n.toString(16).toUpperCase().padStart(6, '0');
}

/* Where the data in an image is, by 256-byte page: the ranges of pages that
 * are not all 0xFF, how many pages are blank, and where the last non-0xFF
 * byte is. The NOR layer skips programming exactly these blank pages
 * (programming 0xFF changes nothing). */
var PAGE_SIZE = 256;

function imageExtent(data) {
    var pages = Math.ceil(data.length / PAGE_SIZE), blank = 0, ranges = [], start = -1;
    for (var p = 0; p < pages; p++) {
        var s = p * PAGE_SIZE, e = Math.min(s + PAGE_SIZE, data.length), isBlank = true;
        for (var i = s; i < e; i++) {
            if (data[i] !== 0xFF) { isBlank = false; break; }
        }
        if (isBlank) {
            blank++;
            if (start >= 0) { ranges.push([start, s]); start = -1; }
        } else if (start < 0) {
            start = s;
        }
    }
    if (start >= 0) ranges.push([start, data.length]);
    var dataEnd = 0;
    for (var j = data.length - 1; j >= 0; j--) {
        if (data[j] !== 0xFF) { dataEnd = j + 1; break; }
    }
    return { pages: pages, blank: blank, ranges: ranges, dataEnd: dataEnd };
}

function rangeText(r) {
    return hexAddr(r[0]) + '-' + hexAddr(r[1] - 1);
}

/* "7.3 MB of data in 29881 of 65536 pages" */
function extentAmount(x) {
    var used = x.pages - x.blank;
    return formatSize(used * PAGE_SIZE) + ' of data in ' + used + ' of ' + x.pages + ' pages';
}

/* "last data byte at 0xFF000B, then a 64.0 KB blank (0xFF) tail" */
function extentTail(x, len) {
    return 'last data byte at ' + hexAddr(x.dataEnd - 1) + (x.dataEnd < len ?
        ', then a ' + formatSize(len - x.dataEnd) + ' blank (0xFF) tail' : '');
}

/* Log where an image holds data, in lines that fit the log's width: the
 * amount, the ranges (three per line, or first to last when fragmented)
 * and where the blank tail starts. */
function logImageExtent(label, data) {
    var x = imageExtent(data), n = x.ranges.length;
    if (!n) {
        log(label + ' is entirely blank (0xFF)');
        return x;
    }
    log(label + ': ' + extentAmount(x) + ', in ' + n + (n === 1 ? ' range' : ' ranges'));
    if (n <= 6) {
        for (var i = 0; i < n; i += 3)
            log('  ' + x.ranges.slice(i, i + 3).map(rangeText).join(', '));
    } else {
        log('  from ' + hexAddr(x.ranges[0][0]) + ' to ' + hexAddr(x.ranges[n - 1][1] - 1));
    }
    log('  ' + extentTail(x, data.length));
    return x;
}

/* Partition sizes as mtdparts writes them */
function partSize(v) {
    return v % 1024 ? v + ' B' : (v / 1024) + ' KB';
}

/* Fill the Partitions card from the layout showLayout got from C, and the
 * image's data extent. */
function renderLayout(title, layout, extent, len) {
    document.getElementById('layout-image').textContent = title;
    var note = document.getElementById('layout-note');
    note.textContent = layout.note ? 'Note: ' + layout.note : '';
    note.classList.toggle('d-none', !layout.note);
    document.getElementById('layout-source').textContent = layout.parts.length ?
        'Layout from ' + layout.source : 'No partitions found (' + layout.source + ')';

    var rows = document.getElementById('layout-rows');
    rows.textContent = '';
    layout.parts.forEach(function(p) {
        var tr = document.createElement('tr');
        if (/^(blank|not in this image)/.test(p.what)) tr.className = 'dim';
        [['range', hexAddr(p.start) + '-' + hexAddr(p.start + p.size - 1)],
         ['size', partSize(p.size)],
         ['name', p.name || '-'],
         ['what', p.what]].forEach(function(c) {
            var td = document.createElement('td');
            td.className = c[0];
            td.textContent = c[1];
            tr.appendChild(td);
        });
        rows.appendChild(tr);
    });

    document.getElementById('layout-extent').textContent = !extent.ranges.length ?
        'Entirely blank (0xFF)' :
        extentAmount(extent) + ' (' + extent.ranges.length +
        (extent.ranges.length === 1 ? ' range); ' : ' ranges); ') + extentTail(extent, len);
    document.getElementById('layout-card').classList.remove('d-none');
}

/* Show the partition layout of an image in the Partitions card, from
 * src/flash_layout.c as JSON. ptr: the image already in WASM memory, or 0
 * to copy `data` in. Skipped while a USB call is suspended, and on an older
 * cached WASM build. */
function showLayout(title, ptr, len, data, extent) {
    if (typeof Module['_scriba_layout_json'] !== 'function' || wasmBusy || !len) return;
    var own = !ptr, outLen = 32768, out = Module._malloc(outLen), layout = null;
    if (!out) return;
    try {
        if (own) {
            ptr = Module._malloc(len);
            if (!ptr) return;
            Module.HEAPU8.set(data, ptr);
        }
        var n = Module.ccall('scriba_layout_json', 'number',
            ['number', 'number', 'number', 'number', 'number'],
            [ptr, len, flashSize > 0 ? flashSize : 0, out, outLen]);
        if (n > 0) layout = JSON.parse(Module.UTF8ToString(out, n));
    } finally {
        if (own && ptr) Module._free(ptr);
        Module._free(out);
    }
    if (!layout) return;
    renderLayout(title, layout, extent, len);
    log('Layout: ' + layout.parts.length + (layout.parts.length === 1 ? ' partition' : ' partitions') +
        ' from ' + layout.source);
    if (layout.note) log('Layout note: ' + layout.note, 'warn');
}

function progressLogger(label, total) {
    var lastStep = 0;
    return function(done) {
        var step = Math.floor(done * 10 / total);
        if (step > lastStep) {
            lastStep = step;
            log(label + ' ' + (step * 10) + '% (' + formatSize(done) + ' / ' + formatSize(total) + ')');
        }
    };
}

function toggleEraseDebug() {
    window.__eraseDebug = !window.__eraseDebug;
    var btn = document.getElementById('btn-erase-debug');
    if (window.__eraseDebug) {
        btn.classList.remove('btn-outline-secondary');
        btn.classList.add('btn-warning');
        log('Erase debug logging ENABLED (check browser console for USB traffic)', 'warn');
    } else {
        btn.classList.remove('btn-warning');
        btn.classList.add('btn-outline-secondary');
        log('Erase debug logging disabled', 'info');
    }
}

Object.assign(window, { toggleEraseDebug: toggleEraseDebug });

/* ------------------------------------------------------------------ */
/*  CH347 SPI clock                                                    */
/* ------------------------------------------------------------------ */

var lastSpiSpeed = '15000';

function spiSpeedKhz() {
    return parseInt(document.getElementById('spi-speed').value, 10) || 0;
}

/* wasm/scriba.js keeps one name across builds, so a browser can pair this
 * script with a cached older WASM build that lacks the export; ccall would
 * then abort the whole page. Warn and carry on at the default clock. */
function applySpiSpeed() {
    if (typeof Module['_scriba_set_spi_speed_khz'] !== 'function') {
        log('Old cached WASM build without SPI clock control: reload with Ctrl+Shift+R', 'warn');
        return false;
    }
    Module.ccall('scriba_set_spi_speed_khz', null, ['number'], [spiSpeedKhz()]);
    return true;
}

/* Sent before Connect; while a CH347 is connected it is applied at once. */
async function spiSpeedChanged(sel) {
    if (!scribaReady) return;
    var busy = ['connecting', 'detecting', 'reading', 'writing', 'erasing'];
    if (busy.indexOf(currentState) !== -1) {
        sel.value = lastSpiSpeed;
        log('Wait for the current operation to finish before changing the SPI clock', 'warn');
        return;
    }
    lastSpiSpeed = sel.value;
    if (!applySpiSpeed()) return;
    if (currentState === 'idle') return;
    if (Module.ccall('scriba_get_programmer_type', 'number', [], []) !== 3) return;
    var r = await wasmCall('scriba_reinit', 'number', [], []);
    if (r !== 0)
        log('Failed to apply the new SPI clock', 'error');
    else
        log('Click Detect to re-detect at the new clock');
}

/* CH347 chip-select flags: on by default (used when the firmware supports
 * them); unchecking sets SPI_OPT_NO_CS_FLAGS = 1. Sent before Connect; while
 * a CH347 is connected, a change re-runs the driver setup. */
function applyOptions() {
    if (typeof Module['_scriba_set_options'] !== 'function') {
        log('Old cached WASM build without CH347 options: reload with Ctrl+Shift+R', 'warn');
        return false;
    }
    var fast = document.getElementById('ch347-csflags').checked;
    Module.ccall('scriba_set_options', null, ['number'], [fast ? 0 : 1]);
    return true;
}

async function csFlagsChanged(box) {
    if (!scribaReady) return;
    var busy = ['connecting', 'detecting', 'reading', 'writing', 'erasing'];
    if (busy.indexOf(currentState) !== -1) {
        box.checked = !box.checked;
        log('Wait for the current operation to finish before changing Fast CS', 'warn');
        return;
    }
    if (!applyOptions()) return;
    if (currentState === 'idle') return;
    if (Module.ccall('scriba_get_programmer_type', 'number', [], []) !== 3) return;
    var r = await wasmCall('scriba_reinit', 'number', [], []);
    if (r !== 0)
        log('Failed to apply the Fast CS change - reconnect the programmer', 'error');
}

Object.assign(window, { spiSpeedChanged: spiSpeedChanged, csFlagsChanged: csFlagsChanged });

/* The clock selector and Fast CS only apply to a CH347: show them once one
 * is connected (before chip detection, so a clock too fast for the wiring
 * can still be lowered), hide them otherwise. Their values are still sent
 * at every Connect. */
function showCh347Controls(show) {
    document.getElementById('ch347-controls').hidden = !show;
}

/* ------------------------------------------------------------------ */
/*  Progress                                                           */
/* ------------------------------------------------------------------ */

function showProgress(percent, label) {
    var container = document.getElementById('progress');
    container.classList.remove('d-none');
    document.getElementById('progress-fill').style.width = percent + '%';
    document.getElementById('progress-label').textContent = label || '';
}

function hideProgress() {
    document.getElementById('progress').classList.add('d-none');
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

    var busy = ['connecting', 'detecting', 'reading', 'writing', 'erasing'].indexOf(state) !== -1;
    var warn = document.getElementById('op-warning');
    if (busy) warn.classList.remove('d-none');
    else warn.classList.add('d-none');

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
    var progNames = {0: 'CH341A', 1: 'EZP2019', 2: 'Auto', 3: 'CH347'};
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
            { vendorId: 0x1A86, productId: 0x55DB },  // CH347T, mode 1
            { vendorId: 0x1A86, productId: 0x55DE },  // CH347F
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
        applySpiSpeed();
        applyOptions();
        var result = await wasmCall('scriba_init', 'number', [], []);
        if (result !== 0) {
            log('Failed to connect to programmer', 'error');
            setState('error');
            return;
        }

        log('Programmer connected');
        showCh347Controls(Module.ccall('scriba_get_programmer_type', 'number', [], []) === 3);

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

/* Read the whole chip, 64 KB at a time, into WASM memory at dest(offset).
 * after(offset, len), when given, runs after each chunk. The progress bar
 * covers [pctFrom, pctTo]. Returns false, having logged why, on a read
 * error. */
async function readPass(label, pctFrom, pctTo, dest, after) {
    var chunkSize = 65536, offset = 0;
    var logProgress = progressLogger(label, flashSize);
    while (offset < flashSize) {
        var len = Math.min(chunkSize, flashSize - offset);
        var done = Math.round((offset / flashSize) * 100);
        showProgress(Math.round(pctFrom + (pctTo - pctFrom) * offset / flashSize),
            label + '... ' + done + '% (' + formatSize(offset) + ' / ' + formatSize(flashSize) + ')');

        var result = await wasmCall('scriba_read_flash', 'number',
            ['number', 'number', 'number'], [dest(offset), offset, len]);
        if (result < 0) {
            log('Read failed at offset 0x' + offset.toString(16) + ': error ' + result, 'error');
            return false;
        }
        if (after) after(offset, len);
        offset += len;
        logProgress(offset);
    }
    return true;
}

async function sha256Hex(data) {
    if (!window.crypto || !crypto.subtle) return '';
    var d = new Uint8Array(await crypto.subtle.digest('SHA-256', data));
    return Array.prototype.map.call(d, function(b) { return ('0' + b.toString(16)).slice(-2); }).join('');
}

async function doRead() {
    if (!scribaReady || flashSize <= 0) return;

    /* Read twice: like the CLI's -R, read the chip a second time and save
     * only if both reads match. The second read compares 64 KB at a time,
     * so it needs no second full-size buffer. */
    var twice = document.getElementById('read-twice').checked;
    setState('reading');
    log('Reading flash (' + formatSize(flashSize) + ')' + (twice ? ' twice, to compare' : '') + '...');

    var bufPtr = 0, chunkPtr = 0;
    try {
        bufPtr = Module._malloc(flashSize);
        chunkPtr = twice ? Module._malloc(65536) : 0;
        if (!bufPtr || (twice && !chunkPtr)) {
            log('Failed to allocate memory', 'error');
            hideProgress();
            setState('error');
            return;
        }

        var ok = await readPass(twice ? 'Read 1/2' : 'Read', 0, twice ? 47 : 95,
            function(offset) { return bufPtr + offset; });

        var diffBytes = 0, diffBlocks = 0, firstDiff = -1;
        if (ok && twice) {
            ok = await readPass('Read 2/2', 47, 95,
                function() { return chunkPtr; },
                function(offset, len) {
                    var heap = Module.HEAPU8, before = diffBytes;
                    for (var i = 0; i < len; i++) {
                        if (heap[bufPtr + offset + i] !== heap[chunkPtr + i]) {
                            if (firstDiff < 0) firstDiff = offset + i;
                            diffBytes++;
                        }
                    }
                    if (diffBytes > before) diffBlocks++;
                });
        }
        if (!ok) {
            hideProgress();
            setState('error');
            return;
        }
        if (diffBytes) {
            log('The two reads differ: ' + diffBytes + (diffBytes === 1 ? ' byte' : ' bytes') +
                ' in ' + diffBlocks + ' of ' + Math.ceil(flashSize / 65536) +
                ' blocks of 64 KB, first at ' + hexAddr(firstDiff), 'error');
            log('Nothing saved. Re-seat the clip (CH347: try a lower clock) and read again.', 'error');
            hideProgress();
            setState('error');
            return;
        }
        if (twice) log('Both reads match');

        showProgress(95, 'Preparing download...');

        var data = Module.HEAPU8.slice(bufPtr, bufPtr + flashSize);

        /* Trimming the blank tail loses nothing for a later write: the
         * page's write erases the whole chip first, which leaves the tail
         * blank again. Offsets are unchanged; blank gaps inside stay. */
        var extent = logImageExtent('Flash', data);
        showLayout('Flash read at ' + timestamp().slice(0, 8), bufPtr, flashSize, null, extent);
        var name = 'flash_dump.bin';
        if (document.getElementById('trim-tail').checked && extent.dataEnd > 0 &&
            extent.dataEnd < data.length) {
            data = data.slice(0, extent.dataEnd);
            name = 'flash_dump_trimmed.bin';
        }

        var blob = new Blob([data], { type: 'application/octet-stream' });
        var url = URL.createObjectURL(blob);
        var a = document.createElement('a');
        a.href = url;
        a.download = name;
        a.click();
        URL.revokeObjectURL(url);

        showProgress(100, 'Read complete');
        log('Flash read complete - downloaded as ' + name + ' (' + formatSize(data.length) +
            (data.length < flashSize ? ', blank tail trimmed' : '') + ')');
        var hash = await sha256Hex(data);
        if (hash) log('  SHA-256 ' + hash);
        setTimeout(hideProgress, 1500);
        setState('done');
    } catch (e) {
        log('Read error: ' + e.message, 'error');
        console.error(e);
        hideProgress();
        setState('error');
    } finally {
        if (chunkPtr) Module._free(chunkPtr);
        if (bufPtr) Module._free(bufPtr);
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
        var extent = logImageExtent('Image', firmwareData);
        showLayout(firmwareFileName, 0, firmwareData.length, firmwareData, extent);

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
        var writeExtent = imageExtent(firmwareData.subarray(0, writeLen));
        var isNor = document.getElementById('info-chip').textContent.indexOf('NOR') === 0;
        log('Writing ' + firmwareFileName + ' (' + formatSize(writeLen) + ')...');
        if (isNor && writeExtent.blank)
            log('  programming ' + (writeExtent.pages - writeExtent.blank) + ' pages, skipping ' +
                writeExtent.blank + ' blank (0xFF) pages');

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
        var writeProgress = progressLogger('Written', writeLen);

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
            writeProgress(writeOffset);
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
            var verifyProgress = progressLogger('Verified', writeLen);

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
                verifyProgress(verifyOffset);
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

        var eraseStart = Date.now();
        var result = await wasmCall('scriba_erase_flash', 'number',
            ['number', 'number'],
            [0, flashSize]);
        var eraseSec = (Date.now() - eraseStart) / 1000;

        if (result !== 0) {
            log('Full chip erase failed: error ' + result, 'error');
            hideProgress();
            setState('error');
            return;
        }

        if (eraseSec < 3) {
            log('WARNING: Erase completed in ' + eraseSec.toFixed(1) +
                's which is too fast for this chip.', 'error');
            log('  The command may not have been accepted.', 'error');
            log('  Try re-seating the flash chip and reloading the page.', 'error');
            hideProgress();
            setState('error');
            return;
        }

        showProgress(100, 'Verifying erase...');
        var verifyLen = 65536; // one sector
        var verifyPtr = Module._malloc(verifyLen);
        if (verifyPtr) {
            var readResult = await wasmCall('scriba_read_flash', 'number',
                ['number', 'number', 'number'],
                [verifyPtr, 0, verifyLen]);
            if (readResult >= 0) {
                var allFF = true;
                var firstNonFF = -1;
                for (var i = 0; i < verifyLen; i++) {
                    if (Module.HEAPU8[verifyPtr + i] !== 0xFF) {
                        allFF = false;
                        if (firstNonFF < 0) firstNonFF = i;
                        break;
                    }
                }
                if (allFF) {
                    log('Erase verified: first ' + verifyLen + ' bytes are all 0xFF', 'success');
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
    doRead, doWrite, doErase
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
        showCh347Controls(false);
        setState('idle');
    });

    setState('idle');
    initModule();
})();

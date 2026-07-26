/**
 * libusb → WebUSB shim for Emscripten
 *
 * Adapted from thingino-cloner for scriba SPI flash programmer.
 * Supports CH341A (0x1A86:0x5512) and EZP2019 family (0x1FC8:0x310B-310D).
 *
 * Uses Asyncify.handleAsync() to properly pause/resume WASM
 * when calling async WebUSB APIs.
 */

mergeInto(LibraryManager.library, {

    $webusb_state: {
        devices: [],
        handles: [],
        device_list: null,
        device_descriptors: [],
        next_handle_id: 1,
        handle_device_map: {},
    },

    $webusb_state__deps: [],
    $webusb_state__postset: '',

    /* ------------------------------------------------------------------ */
    /*  Init / Exit                                                        */
    /* ------------------------------------------------------------------ */

    libusb_init__deps: ['$webusb_state'],
    libusb_init: function(ctx_ptr) {
        if (ctx_ptr) {{{ makeSetValue('ctx_ptr', '0', '0', 'i32') }}};
        return 0;
    },

    libusb_exit__deps: ['$webusb_state'],
    libusb_exit: function(ctx) {
        webusb_state.devices = [];
        webusb_state.handles = [];
        webusb_state.handle_device_map = {};
    },

    /* ------------------------------------------------------------------ */
    /*  Device enumeration                                                 */
    /* ------------------------------------------------------------------ */

    libusb_get_device_list__deps: ['$webusb_state', 'malloc'],
    libusb_get_device_list__async: true,
    libusb_get_device_list: function(ctx, list_ptr) {
        return Asyncify.handleAsync(function() {
            var SCRIBA_VIDS = [0x1A86, 0x1FC8];
            var SCRIBA_PIDS = [0x5512, 0x310B, 0x310C, 0x310D];

            return navigator.usb.getDevices().then(function(allDevices) {
                var extra = (typeof window !== 'undefined' && window._webusb_devices) || [];
                for (var i = 0; i < extra.length; i++) {
                    if (allDevices.indexOf(extra[i]) === -1) allDevices.push(extra[i]);
                }
                var devices = allDevices.filter(function(d) {
                    return SCRIBA_VIDS.indexOf(d.vendorId) !== -1 &&
                           SCRIBA_PIDS.indexOf(d.productId) !== -1;
                });

                webusb_state.devices = devices;
                webusb_state.device_descriptors = [];
                for (var i = 0; i < devices.length; i++) {
                    webusb_state.device_descriptors.push({
                        idVendor: devices[i].vendorId,
                        idProduct: devices[i].productId,
                        bNumConfigurations: devices[i].configurations ? devices[i].configurations.length : 1,
                    });
                }

                var count = devices.length;
                var arr = _malloc((count + 1) * 4);
                if (!arr) return -11;

                for (var i = 0; i < count; i++) {
                    {{{ makeSetValue('arr', 'i * 4', 'i + 1', 'i32') }}};
                }
                {{{ makeSetValue('arr', 'count * 4', '0', 'i32') }}};
                {{{ makeSetValue('list_ptr', '0', 'arr', 'i32') }}};

                webusb_state.device_list = arr;
                return count;
            });
        });
    },

    libusb_free_device_list__deps: ['$webusb_state', 'free'],
    libusb_free_device_list: function(list, unref_devices) {
        if (list) _free(list);
        if (list === webusb_state.device_list) webusb_state.device_list = null;
    },

    /* ------------------------------------------------------------------ */
    /*  Device descriptor / addressing                                     */
    /* ------------------------------------------------------------------ */

    libusb_get_device_descriptor__deps: ['$webusb_state'],
    libusb_get_device_descriptor: function(dev_ptr, desc_ptr) {
        var idx = dev_ptr - 1;
        if (idx < 0 || idx >= webusb_state.device_descriptors.length) return -5;
        var d = webusb_state.device_descriptors[idx];
        for (var i = 0; i < 18; i++) {{{ makeSetValue('desc_ptr', 'i', '0', 'i8') }}};
        {{{ makeSetValue('desc_ptr', '8', 'd.idVendor', 'i16') }}};
        {{{ makeSetValue('desc_ptr', '10', 'd.idProduct', 'i16') }}};
        {{{ makeSetValue('desc_ptr', '17', 'd.bNumConfigurations', 'i8') }}};
        return 0;
    },

    libusb_get_bus_number__deps: [],
    libusb_get_bus_number: function(dev_ptr) { return 1; },

    libusb_get_device_address__deps: [],
    libusb_get_device_address: function(dev_ptr) { return dev_ptr; },

    libusb_get_device__deps: ['$webusb_state'],
    libusb_get_device: function(handle_ptr) {
        var idx = webusb_state.handle_device_map[handle_ptr];
        if (idx === undefined) return 0;
        return idx + 1;
    },

    /* ------------------------------------------------------------------ */
    /*  Open / Close / Ref                                                 */
    /* ------------------------------------------------------------------ */

    libusb_open__deps: ['$webusb_state'],
    libusb_open__async: true,
    libusb_open: function(dev_ptr, handle_ptr) {
        var idx = dev_ptr - 1;
        if (idx < 0 || idx >= webusb_state.devices.length) return -5;
        var device = webusb_state.devices[idx];

        return Asyncify.handleAsync(function() {
            var p = device.opened ? Promise.resolve() : device.open();
            return p.then(function() {
                var handle_id = webusb_state.next_handle_id++;
                webusb_state.handles[handle_id] = device;
                webusb_state.handle_device_map[handle_id] = idx;
                {{{ makeSetValue('handle_ptr', '0', 'handle_id', 'i32') }}};
                return 0;
            }).catch(function(e) {
                console.error('libusb_open error:', e);
                return -3;
            });
        });
    },

    libusb_close__deps: ['$webusb_state'],
    libusb_close: function(handle_ptr) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return;
        delete webusb_state.handles[handle_ptr];
        delete webusb_state.handle_device_map[handle_ptr];
    },

    libusb_ref_device__deps: [],
    libusb_ref_device: function(dev_ptr) { return dev_ptr; },

    libusb_unref_device__deps: [],
    libusb_unref_device: function(dev_ptr) {},

    /* ------------------------------------------------------------------ */
    /*  open_device_with_vid_pid                                           */
    /* ------------------------------------------------------------------ */

    libusb_open_device_with_vid_pid__deps: ['$webusb_state'],
    libusb_open_device_with_vid_pid__async: true,
    libusb_open_device_with_vid_pid: function(ctx, vid, pid) {
        return Asyncify.handleAsync(function() {
            return navigator.usb.getDevices().then(function(allDevices) {
                var extra = (typeof window !== 'undefined' && window._webusb_devices) || [];
                for (var i = 0; i < extra.length; i++) {
                    if (allDevices.indexOf(extra[i]) === -1) allDevices.push(extra[i]);
                }

                var match = null;
                for (var i = 0; i < allDevices.length; i++) {
                    if (allDevices[i].vendorId === vid && allDevices[i].productId === pid) {
                        match = allDevices[i];
                        break;
                    }
                }

                if (!match) {
                    return 0;
                }

                /* Register in device list if not present */
                var idx = webusb_state.devices.indexOf(match);
                if (idx === -1) {
                    webusb_state.devices.push(match);
                    webusb_state.device_descriptors.push({
                        idVendor: match.vendorId,
                        idProduct: match.productId,
                        bNumConfigurations: match.configurations ? match.configurations.length : 1,
                    });
                    idx = webusb_state.devices.length - 1;
                }

                var p = match.opened ? Promise.resolve() : match.open();
                return p.then(function() {
                    return match.claimInterface(0);
                }).then(function() {
                    var handle_id = webusb_state.next_handle_id++;
                    webusb_state.handles[handle_id] = match;
                    webusb_state.handle_device_map[handle_id] = idx;
                    return handle_id;
                }).catch(function(e) {
                    console.error('open_device_with_vid_pid error:', e);
                    return 0;
                });
            });
        });
    },

    /* ------------------------------------------------------------------ */
    /*  Configuration / Interface                                          */
    /* ------------------------------------------------------------------ */

    libusb_set_configuration__deps: ['$webusb_state'],
    libusb_set_configuration__async: true,
    libusb_set_configuration: function(handle_ptr, configuration) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;
        return Asyncify.handleAsync(function() {
            return device.selectConfiguration(configuration).then(function() {
                return 0;
            }).catch(function() { return 0; });
        });
    },

    libusb_get_configuration__deps: ['$webusb_state'],
    libusb_get_configuration: function(handle_ptr, config_ptr) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;
        var v = device.configuration ? device.configuration.configurationValue : 1;
        {{{ makeSetValue('config_ptr', '0', 'v', 'i32') }}};
        return 0;
    },

    libusb_claim_interface__deps: ['$webusb_state'],
    libusb_claim_interface__async: true,
    libusb_claim_interface: function(handle_ptr, iface) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;
        return Asyncify.handleAsync(function() {
            return device.claimInterface(iface).then(function() { return 0; })
                .catch(function(e) { console.error('claimInterface:', e); return -6; });
        });
    },

    libusb_release_interface__deps: ['$webusb_state'],
    libusb_release_interface__async: true,
    libusb_release_interface: function(handle_ptr, iface) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;
        return Asyncify.handleAsync(function() {
            return device.releaseInterface(iface).then(function() { return 0; })
                .catch(function() { return 0; });
        });
    },

    libusb_kernel_driver_active__deps: [],
    libusb_kernel_driver_active: function() { return 0; },

    libusb_detach_kernel_driver__deps: [],
    libusb_detach_kernel_driver: function() { return 0; },

    libusb_set_auto_detach_kernel_driver__deps: [],
    libusb_set_auto_detach_kernel_driver: function() { return 0; },

    libusb_set_option__deps: [],
    libusb_set_option: function() { return 0; },

    libusb_set_debug__deps: [],
    libusb_set_debug: function() {},

    /* ------------------------------------------------------------------ */
    /*  Transfers                                                          */
    /* ------------------------------------------------------------------ */

    libusb_control_transfer__deps: ['$webusb_state'],
    libusb_control_transfer__async: true,
    libusb_control_transfer: function(handle_ptr, bmRequestType, bRequest,
                                      wValue, wIndex, data_ptr, wLength, timeout) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;

        var isIn = (bmRequestType & 0x80) !== 0;
        var setup = { requestType: 'standard', recipient: 'device',
                      request: bRequest, value: wValue, index: wIndex };
        if (bRequest === 0x06) setup.requestType = 'standard';
        else setup.requestType = 'vendor';
        var timeoutMs = (timeout && timeout > 0) ? timeout : 5000;

        return Asyncify.handleAsync(function() {
            var transferPromise;
            if (isIn) {
                transferPromise = device.controlTransferIn(setup, wLength).then(function(result) {
                    if (result.status !== 'ok') return -9;
                    var received = new Uint8Array(result.data.buffer);
                    for (var i = 0; i < received.length && i < wLength; i++) {
                        {{{ makeSetValue('data_ptr', 'i', 'received[i]', 'i8') }}};
                    }
                    return received.length;
                });
            } else {
                var sendData = new Uint8Array(0);
                if (wLength > 0 && data_ptr) {
                    sendData = new Uint8Array(wLength);
                    for (var i = 0; i < wLength; i++) {
                        sendData[i] = {{{ makeGetValue('data_ptr', 'i', 'i8') }}} & 0xFF;
                    }
                }
                transferPromise = device.controlTransferOut(setup, sendData).then(function(result) {
                    if (result.status !== 'ok') return -9;
                    return result.bytesWritten;
                });
            }
            var timeoutPromise = new Promise(function(_, reject) {
                setTimeout(function() { reject({name: 'TimeoutError'}); }, timeoutMs);
            });
            return Promise.race([transferPromise, timeoutPromise]).catch(function(e) {
                if (e.name === 'TimeoutError') return -7;
                if (e.name === 'NotFoundError') return -4;
                if (e.name === 'NetworkError') return -9;
                console.error('control_transfer error:', e);
                return -1;
            });
        });
    },

    libusb_bulk_transfer__deps: ['$webusb_state'],
    libusb_bulk_transfer__async: true,
    libusb_bulk_transfer: function(handle_ptr, endpoint, data_ptr, length,
                                    transferred_ptr, timeout) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;

        var isIn = (endpoint & 0x80) !== 0;
        var epNum = endpoint & 0x0F;
        var timeoutMs = (timeout && timeout > 0) ? timeout : 30000;

        return Asyncify.handleAsync(function() {
            if (typeof window !== 'undefined' && window.__eraseDebug) {
                if (!window.__usbDbgCount) window.__usbDbgCount = 0;
                window.__usbDbgCount++;
                if (window.__usbDbgCount % 100 === 1) {
                    var dir = isIn ? 'IN' : 'OUT';
                    console.log('[USB-DBG] bulk ' + dir + ' ep=0x' + endpoint.toString(16) +
                        ' len=' + length + ' (seq ' + window.__usbDbgCount + ')');
                }
            }

            var transferPromise;
            if (isIn) {
                transferPromise = device.transferIn(epNum, length).then(function(result) {
                    if (result.status !== 'ok') {
                        if (transferred_ptr) {{{ makeSetValue('transferred_ptr', '0', '0', 'i32') }}};
                        return -9;
                    }
                    var received = new Uint8Array(result.data.buffer);
                    var count = Math.min(received.length, length);
                    HEAPU8.set(received.subarray(0, count), data_ptr);
                    if (transferred_ptr) {{{ makeSetValue('transferred_ptr', '0', 'count', 'i32') }}};
                    if (typeof window !== 'undefined' && window.__eraseDebug && count <= 40) {
                        var hex = '';
                        for (var i = 0; i < count; i++) {
                            hex += ('0' + (received[i] & 0xFF).toString(16)).slice(-2) + ' ';
                        }
                        console.log('[USB-DBG] bulk IN result: ' + count + ' bytes data=' + hex.trim());
                    }
                    return 0;
                });
            } else {
                var sendData = HEAPU8.slice(data_ptr, data_ptr + length);
                transferPromise = device.transferOut(epNum, sendData).then(function(result) {
                    if (result.status !== 'ok') {
                        if (transferred_ptr) {{{ makeSetValue('transferred_ptr', '0', '0', 'i32') }}};
                        return -9;
                    }
                    if (transferred_ptr) {{{ makeSetValue('transferred_ptr', '0', 'result.bytesWritten', 'i32') }}};
                    return 0;
                });
            }
            var timeoutPromise = new Promise(function(_, reject) {
                setTimeout(function() { reject({name: 'TimeoutError'}); }, timeoutMs);
            });
            return Promise.race([transferPromise, timeoutPromise]).catch(function(e) {
                if (transferred_ptr) {{{ makeSetValue('transferred_ptr', '0', '0', 'i32') }}};
                if (e.name === 'TimeoutError') {
                    console.warn('bulk_transfer TIMEOUT: ep=0x' + endpoint.toString(16) +
                        ' len=' + length + ' dir=' + (isIn ? 'IN' : 'OUT'));
                    return -7;
                }
                if (e.name === 'NotFoundError') return -4;
                console.error('bulk_transfer error:', e);
                return -1;
            });
        });
    },

    usb_clear_halt__deps: ['$webusb_state'],
    usb_clear_halt__async: true,
    usb_clear_halt: function(handle_ptr, endpoint) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;
        var isIn = (endpoint & 0x80) !== 0;
        var epNum = endpoint & 0x0F;
        var direction = isIn ? 'in' : 'out';
        return Asyncify.handleAsync(function() {
            return device.clearHalt(direction, epNum).then(function() {
                return 0;
            }).catch(function(e) {
                console.warn('clearHalt failed:', e);
                return -1;
            });
        });
    },

    libusb_interrupt_transfer__deps: ['$webusb_state'],
    libusb_interrupt_transfer__async: true,
    libusb_interrupt_transfer: function(handle_ptr, endpoint, data_ptr, length,
                                        transferred_ptr, timeout) {
        return _libusb_bulk_transfer(handle_ptr, endpoint, data_ptr, length,
                                     transferred_ptr, timeout);
    },

    /* ------------------------------------------------------------------ */
    /*  Reset / Error names                                                */
    /* ------------------------------------------------------------------ */

    libusb_reset_device__deps: ['$webusb_state'],
    libusb_reset_device__async: true,
    libusb_reset_device: function(handle_ptr) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;
        return Asyncify.handleAsync(function() {
            return device.reset().then(function() { return 0; })
                .catch(function() { return 0; });
        });
    },

    libusb_error_name__deps: ['malloc'],
    libusb_error_name: function(errcode) {
        var names = {
            0: "LIBUSB_SUCCESS", '-1': "LIBUSB_ERROR_IO",
            '-2': "LIBUSB_ERROR_INVALID_PARAM", '-3': "LIBUSB_ERROR_ACCESS",
            '-4': "LIBUSB_ERROR_NO_DEVICE", '-5': "LIBUSB_ERROR_NOT_FOUND",
            '-6': "LIBUSB_ERROR_BUSY", '-7': "LIBUSB_ERROR_TIMEOUT",
            '-8': "LIBUSB_ERROR_OVERFLOW", '-9': "LIBUSB_ERROR_PIPE",
            '-10': "LIBUSB_ERROR_INTERRUPTED", '-11': "LIBUSB_ERROR_NO_MEM",
            '-12': "LIBUSB_ERROR_NOT_SUPPORTED", '-99': "LIBUSB_ERROR_OTHER",
        };
        var name = names[String(errcode)] || "LIBUSB_UNKNOWN_ERROR";
        if (!_libusb_error_name._cache) _libusb_error_name._cache = {};
        if (!_libusb_error_name._cache[errcode]) {
            var len = name.length + 1;
            var ptr = _malloc(len);
            stringToUTF8(name, ptr, len);
            _libusb_error_name._cache[errcode] = ptr;
        }
        return _libusb_error_name._cache[errcode];
    },

    /* ------------------------------------------------------------------ */
    /*  Bidirectional bulk transfer (concurrent OUT + IN)                  */
    /* ------------------------------------------------------------------ */

    usb_bulk_pair__deps: ['$webusb_state'],
    usb_bulk_pair__async: true,
    usb_bulk_pair: function(handle_ptr, ep_out, data_out_ptr, out_len,
                            ep_in, data_in_ptr, in_len, timeout) {
        var device = webusb_state.handles[handle_ptr];
        if (!device) return -4;

        var epOutNum = ep_out & 0x0F;
        var epInNum = ep_in & 0x0F;
        var timeoutMs = (timeout && timeout > 0) ? timeout : 30000;

        return Asyncify.handleAsync(function() {
            var sendData = HEAPU8.slice(data_out_ptr, data_out_ptr + out_len);

            var outP = device.transferOut(epOutNum, sendData);
            var inP = device.transferIn(epInNum, in_len);

            var timeoutP = new Promise(function(_, reject) {
                setTimeout(function() { reject({name: 'TimeoutError'}); }, timeoutMs);
            });

            return Promise.race([
                Promise.all([outP, inP]),
                timeoutP
            ]).then(function(results) {
                var outResult = results[0];
                var inResult = results[1];

                if (outResult.status !== 'ok') {
                    console.error('usb_bulk_pair OUT failed:', outResult.status);
                    return -9;
                }
                if (inResult.status !== 'ok') {
                    console.error('usb_bulk_pair IN failed:', inResult.status);
                    return -9;
                }

                var received = new Uint8Array(inResult.data.buffer);
                var count = Math.min(received.length, in_len);
                HEAPU8.set(received.subarray(0, count), data_in_ptr);

                if (count < in_len) {
                    console.warn('usb_bulk_pair short read: got ' + count + ' expected ' + in_len);
                }
                return 0;
            }).catch(function(e) {
                if (e.name === 'TimeoutError') {
                    console.error('usb_bulk_pair TIMEOUT after ' + timeoutMs + 'ms');
                    return -7;
                }
                console.error('usb_bulk_pair error:', e);
                return -1;
            });
        });
    },
});

'use strict';

function assertRejectsWithError(promise, name, message) {
  return promise.then(() => {
    assert_unreached('expected promise to reject with ' + name);
  }, error => {
    assert_equals(error.name, name);
    if (message !== undefined)
      assert_equals(error.message, message);
  });
}

// TODO(reillyg): Remove when jyasskin upstreams this to testharness.js:
// https://crbug.com/509058.
function callWithKeyDown(functionCalledOnKeyPress) {
  return new Promise(resolve => {
    function onKeyPress() {
      document.removeEventListener('keypress', onKeyPress, false);
      resolve(functionCalledOnKeyPress());
    }
    document.addEventListener('keypress', onKeyPress, false);

    eventSender.keyDown(' ', []);
  });
}

function runGarbageCollection() {
  // Run gc() as a promise.
  return new Promise((resolve, reject) => {
    GCController.collect();
    setTimeout(resolve, 0);
  });
}

function usbMocks(mojo) {
  return define('USB Mocks', [
    'mojo/public/js/bindings',
    'mojo/public/js/connection',
    'device/usb/public/interfaces/chooser_service.mojom',
    'device/usb/public/interfaces/device_manager.mojom',
    'device/usb/public/interfaces/device.mojom',
  ], (bindings, connection, chooserService, deviceManager, device) => {
    function assertDeviceInfoEquals(device, info) {
      assert_equals(device.usbVersionMajor, info.usb_version_major);
      assert_equals(device.usbVersionMinor, info.usb_version_minor);
      assert_equals(device.usbVersionSubminor, info.usb_version_subminor);
      assert_equals(device.deviceClass, info.class_code);
      assert_equals(device.deviceSubclass, info.subclass_code);
      assert_equals(device.deviceProtocol, info.protocol_code);
      assert_equals(device.vendorId, info.vendor_id);
      assert_equals(device.productId, info.product_id);
      assert_equals(device.deviceVersionMajor, info.device_version_major);
      assert_equals(device.deviceVersionMinor, info.device_version_minor);
      assert_equals(device.deviceVersionSubminor,
                    info.device_version_subminor);
      assert_equals(device.manufacturerName, info.manufacturer_name);
      assert_equals(device.productName, info.product_name);
      assert_equals(device.serialNumber, info.serial_number);
      assert_equals(device.configurations.length,
                    info.configurations.length);
      for (var i = 0; i < device.configurations.length; ++i) {
        assertConfigurationInfoEquals(device.configurations[i],
                                      info.configurations[i]);
      }
    };

    function assertConfigurationInfoEquals(configuration, info) {
      assert_equals(configuration.configurationValue,
                    info.configuration_value);
      assert_equals(configuration.configurationName,
                    info.configuration_name);
      assert_equals(configuration.interfaces.length,
                    info.interfaces.length);
      for (var i = 0; i < configuration.interfaces.length; ++i) {
        assertInterfaceInfoEquals(configuration.interfaces[i],
                                  info.interfaces[i]);
      }
    };

    function assertInterfaceInfoEquals(iface, info) {
      assert_equals(iface.interfaceNumber, info.interface_number);
      assert_equals(iface.alternates.length, info.alternates.length);
      for (var i = 0; i < iface.alternates.length; ++i) {
        assertAlternateInfoEquals(iface.alternates[i], info.alternates[i]);
      }
    };

    function assertAlternateInfoEquals(alternate, info) {
      assert_equals(alternate.alternateSetting, info.alternate_setting);
      assert_equals(alternate.interfaceClass, info.class_code);
      assert_equals(alternate.interfaceSubclass, info.subclass_code);
      assert_equals(alternate.interfaceProtocol, info.protocol_code);
      assert_equals(alternate.interfaceName, info.interface_name);
      assert_equals(alternate.endpoints.length, info.endpoints.length);
      for (var i = 0; i < alternate.endpoints.length; ++i) {
        assertEndpointInfoEquals(alternate.endpoints[i], info.endpoints[i]);
      }
    }

    function assertEndpointInfoEquals(endpoint, info) {
      var direction;
      switch (info.direction) {
        case device.TransferDirection.INBOUND:
          direction = 'in';
          break;
        case device.TransferDirection.OUTBOUND:
          direction = 'out';
          break;
      }

      var type;
      switch (info.type) {
        case device.EndpointType.BULK:
          type = 'bulk';
          break;
        case device.EndpointType.INTERRUPT:
          type = 'interrupt';
          break;
        case device.EndpointType.ISOCHRONOUS:
          type = 'isochronous';
          break;
      }

      assert_equals(endpoint.endpointNumber, info.endpoint_number);
      assert_equals(endpoint.direction, direction);
      assert_equals(endpoint.type, type);
      assert_equals(endpoint.packetSize, info.packet_size);
    }

    class MockDevice {
      constructor(info) {
        this.info_ = info;
        this.opened_ = false;
        this.currentConfiguration_ = undefined;
        this.claimedInterfaces_ = new Map();
      }

      getDeviceInfo() {
        return Promise.resolve({ info: this.info_ });
      }

      getConfiguration() {
        if (this.currentConfiguration_ === undefined) {
          return Promise.resolve({ value: 0 });
        } else {
          return Promise.resolve({
              value: this.currentConfiguration_.configuration_value });
        }
      }

      open() {
        assert_false(this.opened_);
        this.opened_ = true;
        return Promise.resolve({ error: device.OpenDeviceError.OK });
      }

      close() {
        assert_true(this.opened_);
        this.opened_ = false;
        return Promise.resolve({ error: device.OpenDeviceError.OK });
      }

      setConfiguration(value) {
        assert_true(this.opened_);

        let selected_configuration = this.info_.configurations.find(
            configuration => configuration.configuration_value == value);
        if (selected_configuration !== undefined) {
          this.currentConfiguration_ = selected_configuration;
          return Promise.resolve({ success: true });
        } else {
          return Promise.resolve({ success: false });
        }
      }

      claimInterface(interfaceNumber) {
        assert_true(this.opened_);
        assert_true(this.currentConfiguration_ !== undefined,
                    'device configured');

        if (this.claimedInterfaces_.has(interfaceNumber))
          return Promise.resolve({ success: false });

        if (this.currentConfiguration_.interfaces.some(
                iface => iface.interface_number == interfaceNumber)) {
          this.claimedInterfaces_.set(interfaceNumber, 0);
          return Promise.resolve({ success: true });
        } else {
          return Promise.resolve({ success: false });
        }
      }

      releaseInterface(interfaceNumber) {
        assert_true(this.opened_);
        assert_true(this.currentConfiguration_ !== undefined,
                    'device configured');

        if (this.claimedInterfaces_.has(interfaceNumber)) {
          this.claimedInterfaces_.delete(interfaceNumber);
          return Promise.resolve({ success: true });
        } else {
          return Promise.resolve({ success: false });
        }
      }

      setInterfaceAlternateSetting(interfaceNumber, alternateSetting) {
        assert_true(this.opened_);
        assert_true(this.currentConfiguration_ !== undefined,
                    'device configured');

        if (!this.claimedInterfaces_.has(interfaceNumber))
          return Promise.resolve({ success: false });

        let iface = this.currentConfiguration_.interfaces.find(
            iface => iface.interface_number == interfaceNumber);
        if (iface === undefined)
          return Promise.resolve({ success: false });

        if (iface.alternates.some(
               x => x.alternate_setting == alternateSetting)) {
          this.claimedInterfaces_.set(interfaceNumber, alternateSetting);
          return Promise.resolve({ success: true });
        } else {
          return Promise.resolve({ success: false });
        }
      }

      reset() {
        assert_true(this.opened_);
        return Promise.resolve({ success: true });
      }

      clearHalt(endpoint) {
        assert_true(this.opened_);
        assert_true(this.currentConfiguration_ !== undefined,
                    'device configured');
        // TODO(reillyg): Check that endpoint is valid.
        return Promise.resolve({ success: true });
      }

      controlTransferIn(params, length, timeout) {
        assert_true(this.opened_);
        assert_true(this.currentConfiguration_ !== undefined,
                    'device configured');
        return Promise.resolve({
          status: device.TransferStatus.OK,
          data: [length >> 8, length & 0xff, params.request, params.value >> 8,
                 params.value & 0xff, params.index >> 8, params.index & 0xff]
        });
      }

      controlTransferOut(params, data, timeout) {
        assert_true(this.opened_);
        assert_true(this.currentConfiguration_ !== undefined,
                    'device configured');
        return Promise.resolve({
          status: device.TransferStatus.OK,
          bytesWritten: data.byteLength
        });
      }

      genericTransferIn(endpointNumber, length, timeout) {
        assert_true(this.opened_);
        assert_true(this.currentConfiguration_ !== undefined,
                    'device configured');
        // TODO(reillyg): Check that endpoint is valid.
        let data = new Array(length);
        for (let i = 0; i < length; ++i)
          data[i] = i & 0xff;
        return Promise.resolve({
          status: device.TransferStatus.OK,
          data: data
        });
      }

      genericTransferOut(endpointNumber, data, timeout) {
        assert_true(this.opened_);
        assert_true(this.currentConfiguration_ !== undefined,
                    'device configured');
        // TODO(reillyg): Check that endpoint is valid.
        return Promise.resolve({
          status: device.TransferStatus.OK,
          bytesWritten: data.byteLength
        });
      }

      isochronousTransferIn(endpointNumber, packetLengths, timeout) {
        assert_true(this.opened_);
        assert_true(this.currentConfiguration_ !== undefined,
                    'device configured');
        // TODO(reillyg): Check that endpoint is valid.
        let data = new Array(packetLengths.reduce((a, b) => a + b, 0));
        let dataOffset = 0;
        let packets = new Array(packetLengths.length);
        for (let i = 0; i < packetLengths.length; ++i) {
          for (let j = 0; j < packetLengths[i]; ++j)
            data[dataOffset++] = j & 0xff;
          packets[i] = {
            length: packetLengths[i],
            transferred_length: packetLengths[i],
            status: device.TransferStatus.OK
          };
        }
        return Promise.resolve({ data: data, packets: packets });
      }

      isochronousTransferOut(endpointNumber, data, packetLengths, timeout) {
        assert_true(this.opened_);
        assert_true(this.currentConfiguration_ !== undefined,
                    'device configured');
        // TODO(reillyg): Check that endpoint is valid.
        let packets = new Array(packetLengths.length);
        for (let i = 0; i < packetLengths.length; ++i) {
          packets[i] = {
            length: packetLengths[i],
            transferred_length: packetLengths[i],
            status: device.TransferStatus.OK
          };
        }
        return Promise.resolve({ packets: packets });
      }
    };

    class MockDeviceManager {
      constructor() {
        this.mockDevices_ = new Map();
        this.deviceCloseHandler_ = null;
        this.client_ = null;
      }

      bindToPipe(pipe) {
        this.stub_ = connection.bindHandleToStub(
            pipe, deviceManager.DeviceManager);
        bindings.StubBindings(this.stub_).delegate = this;
      }

      reset() {
        this.mockDevices_.forEach(device => {
          for (var stub of device.stubs)
            bindings.StubBindings(stub).close();
          this.client_.onDeviceRemoved(device.info);
        });
        this.mockDevices_.clear();
      }

      addMockDevice(info) {
        let device = {
          info: info,
          stubs: []
        };
        this.mockDevices_.set(info.guid, device);
        if (this.client_)
          this.client_.onDeviceAdded(info);
      }

      removeMockDevice(info) {
        let device = this.mockDevices_.get(info.guid);
        for (var stub of device.stubs)
          bindings.StubBindings(stub).close();
        this.mockDevices_.delete(info.guid);
        if (this.client_)
          this.client_.onDeviceRemoved(info);
      }

      setDeviceCloseHandler(handler) {
        this.deviceCloseHandler_ = handler;
      }

      getDevices(options) {
        let devices = [];
        this.mockDevices_.forEach(device => {
          devices.push(device.info);
        });
        return Promise.resolve({ results: devices });
      }

      getDevice(guid, stub) {
        let device = this.mockDevices_.get(guid);
        if (device === undefined) {
          bindings.StubBindings(stub).close();
        } else {
          var mock = new MockDevice(device.info);
          bindings.StubBindings(stub).delegate = mock;
          bindings.StubBindings(stub).connectionErrorHandler = () => {
            if (this.deviceCloseHandler_)
              this.deviceCloseHandler_(device.info);
          };
          device.stubs.push(stub);
        }
      }

      setClient(client) {
        this.client_ = client;
      }
    }

    class MockChooserService {
      constructor() {
        this.chosenDevice_ = null;
      }

      bindToPipe(pipe) {
        this.stub_ = connection.bindHandleToStub(
            pipe, chooserService.ChooserService);
        bindings.StubBindings(this.stub_).delegate = this;
      }

      setChosenDevice(deviceInfo) {
        this.chosenDeviceInfo_ = deviceInfo;
      }

      getPermission(deviceFilters) {
        return Promise.resolve({ result: this.chosenDeviceInfo_ });
      }
    }

    let mockDeviceManager = new MockDeviceManager;
    mojo.frameInterfaces.addInterfaceOverrideForTesting(
        deviceManager.DeviceManager.name,
        pipe => {
          mockDeviceManager.bindToPipe(pipe);
        });

    let mockChooserService = new MockChooserService;
    mojo.frameInterfaces.addInterfaceOverrideForTesting(
        chooserService.ChooserService.name,
        pipe => {
          mockChooserService.bindToPipe(pipe);
        });

    return fakeUsbDevices().then(fakeDevices => Promise.resolve({
      DeviceManager: deviceManager.DeviceManager,
      Device: device.Device,
      MockDeviceManager: MockDeviceManager,
      mockDeviceManager: mockDeviceManager,
      mockChooserService: mockChooserService,
      fakeDevices: fakeDevices,
      assertDeviceInfoEquals: assertDeviceInfoEquals,
      assertConfigurationInfoEquals: assertConfigurationInfoEquals,
    }));
  });
}

function usb_test(func, name, properties) {
  mojo_test(mojo => usbMocks(mojo).then(usb => {
    let result = Promise.resolve(func(usb));
    let cleanUp = () => usb.mockDeviceManager.reset();
    result.then(cleanUp, cleanUp);
    return result;
  }), name, properties);
}

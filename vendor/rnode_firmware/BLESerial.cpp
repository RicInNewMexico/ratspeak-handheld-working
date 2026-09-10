// Copyright (C) 2024, Mark Qvist

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "Boards.h"

#if PLATFORM != PLATFORM_NRF52
#if HAS_BLE

#include "BLESerial.h"

void bt_passkey_notify_callback(uint32_t passkey, uint32_t beganAt);
void bt_authentication_complete_callback(esp_ble_auth_cmpl_t auth_result);
void bt_connect_callback(BLEServer *server);
void bt_disconnect_callback(BLEServer *server);
void bt_session_retired_callback(bool detached);

namespace {
struct SessionLock {
  portMUX_TYPE &mux;
  explicit SessionLock(portMUX_TYPE &value) : mux(value) { portENTER_CRITICAL(&mux); }
  ~SessionLock() { portEXIT_CRITICAL(&mux); }
};
}

std::atomic<BLESerial *> BLESerial::instance{nullptr};
void BLESerial::onGattsEvent(esp_gatts_cb_event_t event, esp_gatt_if_t interface,
                             esp_ble_gatts_cb_param_t *param) {
  BLESerial *self = instance.load(std::memory_order_acquire);
  if (!self) return;
  if (event == ESP_GATTS_REG_EVT && param->reg.status == ESP_GATT_OK) {
    SessionLock lock(self->sessionMux);
    self->gattsInterface = interface;
  } else if (event == ESP_GATTS_WRITE_EVT && self->subscription &&
             param->write.handle == self->subscription->getHandle()) {
    // The SDK processes the descriptor first; copy the exact connection's
    // CCCD fact instead of racing its mutable descriptor value on the owner.
    SessionLock lock(self->sessionMux);
    if (self->started && self->live && self->connection == param->write.conn_id) {
      self->subscribed = param->write.len == 2 && !param->write.is_prep &&
                         (param->write.value[0] & 1) != 0;
    }
  }
}

void BLESerial::clearBuffersLocked() {
  rx_buffer.clear();
  transmitBufferLength = 0;
  authorized = false;
  disconnectTimer = false;
}

bool BLESerial::sameSession(uint32_t expected) {
  SessionLock lock(sessionMux);
  return started && live && session == expected;
}

bool BLESerial::connected() { SessionLock lock(sessionMux); return started && live; }
bool BLESerial::authenticated() {
  SessionLock lock(sessionMux);
  return started && live && authorized && !retiring;
}
void BLESerial::setPairingPolicy(bool allowed, uint32_t value, uint32_t beganAt, uint32_t timeout) {
  SessionLock lock(sessionMux); pairingAllowed = allowed; passkey = value;
  pairingBegan = beganAt; pairingTimeout = timeout;
}
uint32_t BLESerial::onPassKeyRequest() {
  { SessionLock lock(sessionMux); if (started && live && pairingAllowed && static_cast<uint32_t>(millis() - pairingBegan) < pairingTimeout) return passkey; }
  disconnect(); return 0;
}
bool BLESerial::onConfirmPIN(uint32_t) {
  SessionLock lock(sessionMux); return started && live && pairingAllowed && static_cast<uint32_t>(millis() - pairingBegan) < pairingTimeout;
}
bool BLESerial::onSecurityRequest() {
  bool allowed;
  { SessionLock lock(sessionMux); allowed = started && live && pairingAllowed && static_cast<uint32_t>(millis() - pairingBegan) < pairingTimeout; }
  // Keep the existing bonded reconnect policy after the pairing window closes.
  if (connected() && (allowed || esp_ble_get_bond_device_num() > 0)) return true;
  disconnect(); return false;
}
void BLESerial::onPassKeyNotify(uint32_t value) {
  { SessionLock lock(sessionMux);
    if (started && live && pairingAllowed && static_cast<uint32_t>(millis() - pairingBegan) < pairingTimeout) {
      pendingPasskey = value; pendingPasskeyAt = pairingBegan = millis(); passkeyPending = true; return;
    }
  }
  disconnect();
}
void BLESerial::onAuthenticationComplete(esp_ble_auth_cmpl_t result) {
  SessionLock lock(sessionMux);
  // GAP identifies authentication by peer address, not GATT connection ID.
  // Disconnected/other-peer completions cannot authorize the current session.
  if (!started || !live || closing || memcmp(peerAddress, result.bd_addr, sizeof(peerAddress))) return;
  authSuccess = result.success;
  authPending = true;
  authorized = result.success;
  if (!result.success) { clearBuffersLocked(); retiring = true; detached = true; }
}
void BLESerial::onConnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) {
  bool accepted = false;
  { SessionLock lock(sessionMux);
    if (started && !live && !retiring && !flushing && session != UINT32_MAX) {
      ++session;
      clearBuffersLocked();
      connection = param->connect.conn_id;
      memcpy(peerAddress, param->connect.remote_bda, sizeof(peerAddress));
      live = true; subscribed = false; closing = false;
      retiring = true; connectPending = true;
      passkeyPending = authPending = false;
      advertising = false; accepted = true;
    }
  }
  // A second/unretired peer is never allowed into the serial owner. The
  // controller stops connectable advertising on connect; only poll restarts it.
  if (!accepted) server->disconnect(param->connect.conn_id);
}
void BLESerial::onDisconnect(BLEServer *, esp_ble_gatts_cb_param_t *param) {
  { SessionLock lock(sessionMux);
    if (!live || connection != param->disconnect.conn_id ||
        memcmp(peerAddress, param->disconnect.remote_bda, sizeof(peerAddress))) return;
    clearBuffersLocked();
    live = false; subscribed = false; retiring = true; detached = true;
    connectPending = passkeyPending = authPending = false;
  }
  // Descriptor writes and this GATT callback are serialized by the SDK BT
  // task. Never reset its value from the sketch while that task may write it.
  subscription->setNotifications(false);
  // Do not advertise here: the owner still has to retire downstream bytes.
}

void BLESerial::pollSession() {
  uint32_t expected, key = 0, keyAt = 0;
  bool retire, wasDetached, announceConnect, gotPasskey, gotAuth, success;
  { SessionLock lock(sessionMux);
    if (flushing) return;
    expected = session;
    retire = retiring; wasDetached = detached; announceConnect = connectPending;
    gotPasskey = passkeyPending; key = pendingPasskey; keyAt = pendingPasskeyAt;
    gotAuth = authPending; success = authSuccess;
    detached = connectPending = passkeyPending = authPending = false;
  }
  if (retire) {
    bt_session_retired_callback(wasDetached);
    if (wasDetached && TxCharacteristic) {
      uint8_t empty = 0;
      TxCharacteristic->setValue(&empty, 0);
    }
    { SessionLock lock(sessionMux);
      // A disconnect during owner cleanup must itself still be retired.
      if (session == expected && !detached) retiring = false;
    }
  }
  if (wasDetached) bt_disconnect_callback(ble_server);
  if (announceConnect && sameSession(expected)) bt_connect_callback(ble_server);
  if (gotPasskey && sameSession(expected)) bt_passkey_notify_callback(key, keyAt);
  if (gotAuth && sameSession(expected)) {
    bool deliver;
    { SessionLock lock(sessionMux);
      deliver = started && live && !closing && !retiring && !authPending && session == expected;
      if (deliver) authorized = success;
    }
    if (deliver) {
      esp_ble_auth_cmpl_t result = {};
      result.success = success;
      bt_authentication_complete_callback(result);
    }
  }
  bool timedDisconnect = false, advertise = false;
  { SessionLock lock(sessionMux);
    if (disconnectTimer && live && static_cast<int32_t>(millis() - disconnectAt) >= 0) {
      disconnectTimer = false; timedDisconnect = true;
    }
    if (started && !live && !retiring && !advertising && session != UINT32_MAX) {
      advertising = true; advertise = true;
    }
  }
  if (timedDisconnect) disconnect();
  if (advertise) ble_adv->start();
}

void BLESerial::disconnectAfter(uint32_t delayMs) {
  SessionLock lock(sessionMux);
  if (started && live) { disconnectAt = millis() + delayMs; disconnectTimer = true; }
}
bool BLESerial::flushDue(uint32_t now, uint32_t interval) {
  SessionLock lock(sessionMux);
  return transmitBufferLength && static_cast<uint32_t>(now - lastFlushTime) >= interval;
}
int BLESerial::read() {
  SessionLock lock(sessionMux);
  return started && live && authorized && !retiring ? rx_buffer.pop() : -1;
}
size_t BLESerial::readBytes(uint8_t *buffer, size_t bufferSize) {
  size_t count = 0;
  SessionLock lock(sessionMux);
  if (started && live && authorized && !retiring)
    while (count < bufferSize && rx_buffer.getLength()) buffer[count++] = rx_buffer.pop();
  return count;
}
int BLESerial::peek() {
  SessionLock lock(sessionMux);
  return started && live && authorized && !retiring ? rx_buffer.get(0) : -1;
}
int BLESerial::available() {
  SessionLock lock(sessionMux);
  return started && live && authorized && !retiring ? rx_buffer.getLength() : 0;
}
size_t BLESerial::print(const char *str) {
  return write(reinterpret_cast<const uint8_t *>(str), strlen(str));
}
size_t BLESerial::write(const uint8_t *buffer, size_t bufferSize) {
  size_t written = 0;
  for (; written < bufferSize; ++written) if (!write(buffer[written])) break;
  flush(); return written;
}
size_t BLESerial::write(uint8_t byte) {
  bool full;
  { SessionLock lock(sessionMux);
    if (!started || !live || !authorized || retiring || flushing) return 0;
    transmitBuffer[transmitBufferLength++] = byte;
    full = transmitBufferLength == BLE_BUFFER_SIZE;
  }
  if (full) flush();
  return 1;
}
void BLESerial::flush() {
  uint8_t bytes[BLE_BUFFER_SIZE];
  uint16_t target;
  esp_gatt_if_t interface;
  size_t length;
  uint32_t expected;
  { SessionLock lock(sessionMux);
    if (!started || !live || !authorized || retiring || flushing || !transmitBufferLength) return;
    length = transmitBufferLength;
    memcpy(bytes, transmitBuffer, length);
    transmitBufferLength = 0; lastFlushTime = millis();
    target = connection; interface = gattsInterface; expected = session; flushing = true;
  }
  TxCharacteristic->setValue(bytes, length);
  bool permitted;
  { SessionLock lock(sessionMux);
    permitted = started && live && authorized && !retiring && subscribed &&
                interface != ESP_GATT_IF_NONE && session == expected;
  }
  if (permitted) {
    // Same characteristic and CCCD gate as SDK notify(true), but never its
    // broadcast map. Advertising is held until this SDK submission returns;
    // a disconnected target cannot be recycled for another accepted session.
    esp_ble_gatts_send_indicate(interface, target,
                               TxCharacteristic->getHandle(), length, bytes, false);
  }
  { SessionLock lock(sessionMux); flushing = false; }
}
void BLESerial::disconnect() {
  uint16_t target;
  { SessionLock lock(sessionMux);
    if (!started || !live) return;
    clearBuffersLocked(); retiring = true; detached = true; closing = true;
    target = connection;
  }
  ble_server->disconnect(target);
}

void BLESerial::begin(const char *name) {
  { SessionLock lock(sessionMux);
    clearBuffersLocked(); live = false; retiring = false; detached = false;
    gattsInterface = ESP_GATT_IF_NONE;
    connectPending = passkeyPending = authPending = false;
  }
  instance.store(this, std::memory_order_release);
  BLEDevice::setCustomGattsHandler(onGattsEvent);
  BLEDevice::init(name);

  #if BOARD_MODEL == BOARD_CARDPUTER_ADV
    const esp_power_level_t ble_power = ESP_PWR_LVL_P6;
  #else
    const esp_power_level_t ble_power = ESP_PWR_LVL_P9;
  #endif
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ble_power);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ble_power);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ble_power);

  ble_server = BLEDevice::createServer();
  ble_server->setCallbacks(this);
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
  BLEDevice::setSecurityCallbacks(this);

  SetupSerialService();
  { SessionLock lock(sessionMux); started = true; }
  this->startAdvertising();
}

void BLESerial::startAdvertising() {
  ble_adv = BLEDevice::getAdvertising();
  ble_adv->addServiceUUID(BLE_SERIAL_SERVICE_UUID);
  ble_adv->setMinPreferred(0x20);
  ble_adv->setMaxPreferred(0x40);
  ble_adv->setScanResponse(true);
  { SessionLock lock(sessionMux); advertising = true; }
  ble_adv->start();
}

void BLESerial::stopAdvertising() {
  ble_adv = BLEDevice::getAdvertising();
  ble_adv->stop();
}

void BLESerial::end() {
  { SessionLock lock(sessionMux);
    const bool hadSession = live || retiring || detached;
    started = false; live = false; subscribed = false; advertising = false;
    clearBuffersLocked(); retiring = hadSession; detached = hadSession;
    connectPending = passkeyPending = authPending = false;
  }
  // begin/end/poll/flush all belong to the sketch owner, so no own flush can
  // overlap SDK deinit. Callbacks only touch the synchronized facts above.
  pollSession();
  BLEDevice::deinit();
  instance.store(nullptr, std::memory_order_release);
  BLEDevice::setCustomGattsHandler(nullptr);
  TxCharacteristic = RxCharacteristic = nullptr; subscription = nullptr;
}
void BLESerial::onWrite(BLECharacteristic *characteristic, esp_ble_gatts_cb_param_t *param) {
  if (characteristic != RxCharacteristic) return;
  uint32_t expected;
  { SessionLock lock(sessionMux);
    if (!started || !live || !authorized || closing || detached || param->write.conn_id != connection) return;
    expected = session;
  }
  // An authenticated early write may queue before the main connect notice;
  // reads stay closed until that owner has retired the prior KISS session.
  auto value = characteristic->getValue(); // SDK also assembles prepared writes.
  SessionLock lock(sessionMux);
  if (!started || !live || !authorized || closing || detached || session != expected) return;
  for (size_t i = 0; i < value.length(); ++i) rx_buffer.push(value[i]);
}

void BLESerial::SetupSerialService() {
  SerialService = ble_server->createService(BLE_SERIAL_SERVICE_UUID);

  RxCharacteristic = SerialService->createCharacteristic(BLE_RX_UUID, BLECharacteristic::PROPERTY_WRITE);
  RxCharacteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
  RxCharacteristic->addDescriptor(new BLE2902());
  RxCharacteristic->setWriteProperty(true);
  RxCharacteristic->setCallbacks(this);

  TxCharacteristic = SerialService->createCharacteristic(BLE_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  TxCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  subscription = new BLE2902();
  subscription->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
  TxCharacteristic->addDescriptor(subscription);
  TxCharacteristic->setNotifyProperty(true);
  TxCharacteristic->setReadProperty(true);

  SerialService->start();
}

BLESerial::BLESerial() : ble_server(nullptr), ble_adv(nullptr), SerialService(nullptr),
    TxCharacteristic(nullptr), RxCharacteristic(nullptr) { }

#endif
#endif

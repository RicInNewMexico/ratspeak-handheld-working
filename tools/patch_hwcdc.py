#!/usr/bin/env python3
"""Build-local repair for Arduino ESP32 HWCDC transmit truncation.

The reviewed driver discards a ring item even when the hardware FIFO accepted
only part of it. Task-side FIFO flushes can also interrupt the ISR's copy on the
other core. Keep the SDK notice, serialize FIFO access, and retain the ring item
until every byte is copied. Never modify the installed framework.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re

SOURCE_SHA256 = "c5362242196b6fcebaf8c83588de8bba509d711e9c216d7e61fe745c9d080d89"

STATE = b'''static xSemaphoreHandle tx_lock = NULL;
// FIFO copy and flush must not race across the Arduino and Service cores.
static portMUX_TYPE tx_fifo_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t *tx_pending_buf = NULL;
static size_t tx_pending_size = 0;
static size_t tx_pending_offset = 0;

static void hwcdc_flush_tx_fifo() {
    portENTER_CRITICAL(&tx_fifo_lock);
    usb_serial_jtag_ll_txfifo_flush();
    portEXIT_CRITICAL(&tx_fifo_lock);
}
'''

TX_OLD = b'''        if (tx_ring_buf != NULL && usb_serial_jtag_ll_txfifo_writable() == 1) {
            // We disable the interrupt here so that the interrupt won't be triggered if there is no data to send.
            usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
            size_t queued_size;
            uint8_t *queued_buff = (uint8_t *)xRingbufferReceiveUpToFromISR(tx_ring_buf, &queued_size, 64);
            // If the hardware fifo is avaliable, write in it. Otherwise, do nothing.
            if (queued_buff != NULL) {  //Although tx_queued_bytes may be larger than 0. We may have interrupt before xRingbufferSend() was called.
                //Copy the queued buffer into the TX FIFO
                usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
                usb_serial_jtag_ll_write_txfifo(queued_buff, queued_size);
                usb_serial_jtag_ll_txfifo_flush();
                vRingbufferReturnItemFromISR(tx_ring_buf, queued_buff, &xTaskWoken);
                if(connected) usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
                //send event?
                //ets_printf("TX:%u\\n", queued_size);
                event.tx.len = queued_size;
                arduino_hw_cdc_event_post(ARDUINO_HW_CDC_EVENTS, ARDUINO_HW_CDC_TX_EVENT, &event, sizeof(arduino_hw_cdc_event_data_t), &xTaskWoken);
            }
        } else {
            usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
        }'''

TX_NEW = b'''        size_t copied = 0;
        portENTER_CRITICAL_ISR(&tx_fifo_lock);
        if (tx_ring_buf != NULL && usb_serial_jtag_ll_txfifo_writable() == 1) {
            usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
            if (tx_pending_buf == NULL) {
                tx_pending_buf = (uint8_t *)xRingbufferReceiveUpToFromISR(
                    tx_ring_buf, &tx_pending_size, 64);
                tx_pending_offset = 0;
            }
            if (tx_pending_buf != NULL) {
                usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
                copied = usb_serial_jtag_ll_write_txfifo(
                    tx_pending_buf + tx_pending_offset, tx_pending_size - tx_pending_offset);
                tx_pending_offset += copied;
                usb_serial_jtag_ll_txfifo_flush();
                // A short (including zero) hardware write is not consumption.
                // Keep this byte-buffer item owned until its suffix is copied.
                if (tx_pending_offset == tx_pending_size) {
                    vRingbufferReturnItemFromISR(tx_ring_buf, tx_pending_buf, &xTaskWoken);
                    tx_pending_buf = NULL;
                    tx_pending_size = tx_pending_offset = 0;
                }
                if(connected) usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
            }
        } else {
            usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
        }
        portEXIT_CRITICAL_ISR(&tx_fifo_lock);
        if (copied) {
            event.tx.len = copied;
            arduino_hw_cdc_event_post(ARDUINO_HW_CDC_EVENTS, ARDUINO_HW_CDC_TX_EVENT, &event, sizeof(arduino_hw_cdc_event_data_t), &xTaskWoken);
        }'''

BUFFER_OLD = b'''size_t HWCDC::setTxBufferSize(size_t tx_queue_len){
    if(tx_ring_buf){
        vRingbufferDelete(tx_ring_buf);
        tx_ring_buf = NULL;
    }
    if(!tx_queue_len){
        return 0;
    }
    tx_ring_buf = xRingbufferCreate(tx_queue_len, RINGBUF_TYPE_BYTEBUF);
    if(!tx_ring_buf){
        return 0;
    }
    return tx_queue_len;
}'''

BUFFER_NEW = b'''size_t HWCDC::setTxBufferSize(size_t tx_queue_len){
    RingbufHandle_t replacement = tx_queue_len
        ? xRingbufferCreate(tx_queue_len, RINGBUF_TYPE_BYTEBUF) : NULL;
    if (tx_queue_len && !replacement) return 0;
    if (tx_lock && xSemaphoreTake(tx_lock, tx_timeout_ms / portTICK_PERIOD_MS) != pdPASS) {
        if (replacement) vRingbufferDelete(replacement);
        return 0;
    }
    // Stop the ISR seeing an old item before deleting its owning ring. This
    // also discards an unfinished suffix on end/rebegin instead of replaying it.
    portENTER_CRITICAL(&tx_fifo_lock);
    RingbufHandle_t previous = tx_ring_buf;
    tx_ring_buf = replacement;
    tx_pending_buf = NULL;
    tx_pending_size = tx_pending_offset = 0;
    portEXIT_CRITICAL(&tx_fifo_lock);
    if (previous) vRingbufferDelete(previous);
    if (tx_lock) xSemaphoreGive(tx_lock);
    return tx_queue_len;
}'''


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def patched_source(source: Path) -> bytes:
    original = source.read_bytes()
    if source.name != "HWCDC.cpp" or sha(original) != SOURCE_SHA256:
        raise ValueError(f"unreviewed selected SDK HWCDC source: {source}")
    # Replace only task-side flushes before introducing the serialized ISR copy.
    if original.count(b"usb_serial_jtag_ll_txfifo_flush();") != 7:
        raise ValueError("HWCDC flush boundaries changed")
    if original.count(TX_OLD) != 1 or original.count(BUFFER_OLD) != 1:
        raise ValueError("HWCDC ownership boundaries changed")
    patched = original.replace(TX_OLD, b"/* HWCDC_TX_ISR */")
    patched = patched.replace(b"usb_serial_jtag_ll_txfifo_flush();", b"hwcdc_flush_tx_fifo();")
    patched = patched.replace(b"static xSemaphoreHandle tx_lock = NULL;", STATE)
    patched = patched.replace(b"/* HWCDC_TX_ISR */", TX_NEW)
    return patched.replace(BUFFER_OLD, BUFFER_NEW)


def write_patched(source: Path, output: Path) -> dict:
    source, output = source.resolve(), output.resolve()
    if output == source or output.is_relative_to(source.parent):
        raise ValueError("patched output must be outside the selected SDK source directory")
    data = patched_source(source)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_bytes() != data:
        output.write_bytes(data)
    result = {"patch": "hwcdc-retain-short-write-v1", "source": str(source),
              "source_sha256": SOURCE_SHA256, "output_sha256": sha(data)}
    output.with_suffix(output.suffix + ".json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def verify_map(path: Path) -> None:
    text = path.read_text()
    for symbol in ("_ZN5HWCDC5writeEPKhj", "_ZL18hw_cdc_isr_handlerPv"):
        matches = re.findall(r"\.text\." + re.escape(symbol) +
                             r"\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+([^\n]+)", text)
        live = [provider for address, size, provider in matches if int(address, 16) and int(size, 16)]
        if len(live) != 1 or not re.search(r"(?:[/\\]|\()HWCDCBackport\.cpp\.o\)?$", live[0].strip()):
            raise ValueError(f"{symbol} must link exactly once from HWCDCBackport.cpp.o")

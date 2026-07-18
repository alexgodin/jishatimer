#!/usr/bin/env python3
"""
Hardware-in-the-loop test suite for the jishatimer device.

Drives the device over its USB serial command interface and checks
display presence, timer state transitions, battery, sleep, RTC
drift/calibration, NTP error paths, and NTP sync.
Requires device connected via USB serial.

Usage: python3 hardware_test_suite.py [port]
  port defaults to /dev/cu.usbmodem*
"""

import sys
import glob
import serial
import time
import re
from datetime import datetime, timezone


class SerialDevice:
    """Manages serial connection to device with reconnect support."""

    def __init__(self, port=None, baud=115200):
        self.baud = baud
        self.port = port or self._find_port()
        self.ser = None
        self.connect()

    @staticmethod
    def _find_port():
        ports = glob.glob("/dev/cu.usbmodem*")
        if not ports:
            print("No /dev/cu.usbmodem* found. Pass port as argument.")
            sys.exit(1)
        return ports[0]

    def connect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        print(f"  Connecting to {self.port}...")
        self.ser = serial.Serial(self.port, self.baud, timeout=2)
        time.sleep(0.5)
        self.ser.reset_input_buffer()

    def reconnect(self, timeout=15):
        """Reconnect after reboot — re-glob port since it may change."""
        if self.ser and self.ser.is_open:
            self.ser.close()
        print(f"  Waiting for device to reboot...")
        time.sleep(3)
        for _ in range(timeout):
            ports = glob.glob("/dev/cu.usbmodem*")
            if ports:
                self.port = ports[0]
                try:
                    self.connect()
                    return True
                except serial.SerialException:
                    time.sleep(1)
                    continue
            time.sleep(1)
        return False

    def send(self, cmd):
        """Send a command string."""
        self.ser.reset_input_buffer()
        self.ser.write(cmd.encode())
        time.sleep(0.1)

    def read_lines(self, timeout=5):
        """Read all lines until idle for timeout seconds."""
        lines = []
        last_activity = time.time()
        while time.time() - last_activity < timeout:
            if self.ser.in_waiting:
                line = self.ser.readline().decode(errors='replace').strip()
                if line:
                    lines.append(line)
                    last_activity = time.time()
            else:
                time.sleep(0.05)
        return lines

    def send_and_read(self, cmd, timeout=5, marker=None):
        """Send command and collect response lines.
        If marker is set, stops reading once a line contains it."""
        self.ser.reset_input_buffer()
        self.ser.write(cmd.encode())
        time.sleep(0.1)
        if marker:
            lines, _ = self.read_until(marker, timeout)
            return lines
        return self.read_lines(timeout)

    def read_until(self, marker, timeout=30):
        """Read lines until one contains marker. Returns (all_lines, found)."""
        lines = []
        start = time.time()
        while time.time() - start < timeout:
            if self.ser.in_waiting:
                line = self.ser.readline().decode(errors='replace').strip()
                if line:
                    lines.append(line)
                    if marker in line:
                        return lines, True
            else:
                time.sleep(0.05)
        return lines, False

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()


def get_computer_time_12h():
    """Return (hour_12h, minute) for comparison with device."""
    now = datetime.now().astimezone()
    h = now.hour % 12 or 12
    return h, now.minute


def time_diff_minutes(dev_h, dev_m, comp_h, comp_m):
    """Compute absolute minute difference on a 12-hour clock (max 360)."""
    diff = abs((dev_h * 60 + dev_m) - (comp_h * 60 + comp_m))
    if diff > 360:
        diff = 720 - diff
    return diff


# =============================================================================
# TEST FUNCTIONS
# =============================================================================

def send_with_retry(dev, cmd, marker=None, timeout=5, retries=3):
    """Send command with retries on empty response."""
    for attempt in range(retries):
        lines = dev.send_and_read(cmd, timeout=timeout, marker=marker)
        if lines:
            return lines
        if attempt < retries - 1:
            print(f"  Empty response (attempt {attempt + 1}/{retries}), retrying...")
            time.sleep(0.5)
    return lines


def test_query_state(dev):
    """Test 1: E command returns STATE: response."""
    lines = send_with_retry(dev, "E", marker="STATE:")
    for line in lines:
        if line.startswith("STATE:"):
            return True, f"Got: {line}"
    return False, f"No STATE: line in response: {lines}"


def test_time_display_state(dev):
    """Test 2: After reset, state should be TIME with elapsed ~0."""
    dev.send("R")
    time.sleep(0.2)
    lines = send_with_retry(dev, "E", marker="STATE:")
    for line in lines:
        m = re.search(r'STATE: (\w+) elapsed=(\d+)s', line)
        if m:
            state, elapsed = m.group(1), int(m.group(2))
            if state == "TIME" and elapsed < 10:
                return True, f"state={state} elapsed={elapsed}s"
            return False, f"Expected TIME/<10s, got state={state} elapsed={elapsed}s"
    return False, f"No STATE: line: {lines}"


def test_query_time_format(dev):
    """Test 3: Q returns time in valid 12h format (h:mm, h in 1-12).
    Accuracy is tested later after NTP sync."""
    lines = send_with_retry(dev, "Q", marker="TIME:")
    for line in lines:
        m = re.search(r'TIME: (\d+):(\d+)', line)
        if m:
            dev_h, dev_m = int(m.group(1)), int(m.group(2))
            if 1 <= dev_h <= 12 and 0 <= dev_m <= 59:
                return True, f"Valid format: {dev_h}:{dev_m:02d}"
            return False, f"Invalid time: h={dev_h} m={dev_m}"
    return False, f"No TIME: line: {lines}"


def test_display_present(dev):
    """Probe SH1106 OLED on I2C. Catches 'screen shows nothing' from
    wiring/address/power issues — device responds on serial but the
    display never ACKs at 0x3C."""
    lines = send_with_retry(dev, "X", marker="DISPLAY:")
    for line in lines:
        if line.startswith("DISPLAY: ok"):
            return True, line
        if line.startswith("DISPLAY:"):
            return False, line
    return False, f"No DISPLAY: line: {lines}"


def test_query_battery(dev):
    """Test 4: B returns valid battery percentage."""
    lines = send_with_retry(dev, "B", marker="BATTERY:")
    for line in lines:
        m = re.search(r'BATTERY: (\d+)%', line)
        if m:
            pct = int(m.group(1))
            if 0 <= pct <= 100:
                return True, f"{pct}%"
            return False, f"Invalid battery: {pct}%"
    return False, f"No BATTERY: line: {lines}"


def test_elapsed_minutes_transition(dev):
    """Test 5: Fast-forward to 65s, state should be ELAPSED_MINUTES."""
    lines = send_with_retry(dev, "F65\n", marker="FASTFWD:")
    for line in lines:
        m = re.search(r'FASTFWD: elapsed=(\d+)s state=(\w+)', line)
        if m:
            elapsed, state = int(m.group(1)), m.group(2)
            if state == "ELAPSED_MINUTES" and 60 <= elapsed <= 70:
                return True, f"elapsed={elapsed}s state={state}"
            return False, f"Expected ELAPSED_MINUTES/~65s, got state={state} elapsed={elapsed}s"
    return False, f"No FASTFWD: line: {lines}"


def test_timer_reset(dev):
    """Test 6: Reset timer, verify back to TIME state."""
    lines = send_with_retry(dev, "R", marker="RESET:")
    for line in lines:
        m = re.search(r'RESET: timer reset, state=(\w+) elapsed=(\d+)s', line)
        if m:
            state, elapsed = m.group(1), int(m.group(2))
            if state == "TIME" and elapsed < 5:
                return True, f"state={state} elapsed={elapsed}s"
            return False, f"Expected TIME/<5s, got state={state} elapsed={elapsed}s"
    return False, f"No RESET: line: {lines}"


def test_dry_sleep(dev):
    """Test 7: D command returns DRYSLEEP response."""
    lines = send_with_retry(dev, "D", marker="DRYSLEEP:")
    for line in lines:
        if line.startswith("DRYSLEEP:"):
            return True, f"Got: {line}"
    return False, f"No DRYSLEEP: line: {lines}"


def test_wifi_failure(dev):
    """Test 8: W command injects WiFi failure, device recovers."""
    dev.send("W")
    lines, _ = dev.read_until("FAILED", timeout=15)
    # Also drain any remaining output
    time.sleep(0.5)
    while dev.ser.in_waiting:
        line = dev.ser.readline().decode(errors='replace').strip()
        if line:
            lines.append(line)
    all_text = "\n".join(lines)
    has_injected = "INJECTED" in all_text
    if not has_injected:
        return False, f"Missing injected WiFi failure.\n{all_text}"
    # Verify device still responds
    check = send_with_retry(dev, "E", marker="STATE:")
    for line in check:
        if "STATE:" in line:
            return True, f"WiFi failure injected, device recovered. {line}"
    return False, f"Device not responding after WiFi failure"


def test_ntp_timeout(dev):
    """Test 9: N command injects NTP timeout, device recovers."""
    dev.send("N")
    # Wait for either "FAILED" or the injected timeout message
    lines = []
    start = time.time()
    found_injected = False
    found_failed = False
    while time.time() - start < 25:
        if dev.ser.in_waiting:
            line = dev.ser.readline().decode(errors='replace').strip()
            if line:
                lines.append(line)
                if "INJECTED" in line:
                    found_injected = True
                if "FAILED" in line:
                    found_failed = True
                if found_injected and found_failed:
                    break
        else:
            # If we already have the injected message, wait a bit more for FAILED then stop
            if found_injected and time.time() - start > 5:
                time.sleep(1)
                # Drain remaining
                while dev.ser.in_waiting:
                    line = dev.ser.readline().decode(errors='replace').strip()
                    if line:
                        lines.append(line)
                        if "FAILED" in line:
                            found_failed = True
                break
            time.sleep(0.05)
    all_text = "\n".join(lines)
    if not found_injected:
        return False, f"Missing injected timeout message.\n{all_text}"
    # Verify device still responds
    check = send_with_retry(dev, "Q", marker="TIME:")
    for line in check:
        if "TIME:" in line:
            return True, f"NTP timeout injected, device recovered. {line}"
    return False, f"Device not responding after NTP timeout"


def test_calibration_end_to_end(dev):
    """L command — backdate lastNtpSync by 2hrs, sync, verify calibration runs."""
    lines = dev.send_and_read("L", timeout=40)
    all_text = "\n".join(lines)
    if "calibration drift" in all_text:
        m = re.search(r'calibration drift=(\-?\d+)s over (\d+)s \(([\-\d.]+) ppm\), offset=(\-?\d+)', all_text)
        if m:
            drift, elapsed, ppm, offset = int(m.group(1)), int(m.group(2)), float(m.group(3)), int(m.group(4))
            return True, f"drift={drift}s over {elapsed}s ({ppm:.1f} ppm) offset={offset}"
        return False, f"Calibration line unparseable: {all_text}"
    if "FAILED" in all_text:
        return False, f"Sync failed (even after firmware retries): {all_text}"
    return False, f"No calibration output: {all_text}"


def test_ntp_first_sync_reboot(dev):
    """Test 14: T command — corrupt RTC, reboot, full NTP sync."""
    comp_h_before, comp_m_before = get_computer_time_12h()
    dev.send("T")
    lines, found = dev.read_until("Activity timer started", timeout=30)
    if not found:
        # Try reconnecting — device may have rebooted and port changed
        if not dev.reconnect():
            return False, "Device did not come back after reboot"
        lines, found = dev.read_until("Activity timer started", timeout=20)
    comp_h_after, comp_m_after = get_computer_time_12h()
    all_text = "\n".join(lines)
    first_sync = "first sync, skipping calibration" in all_text
    if "display will show" in all_text:
        m = re.search(r'display will show (\d+):(\d+)', all_text)
        if m:
            dev_h, dev_m = int(m.group(1)), int(m.group(2))
            # Use whichever computer sample is closer to device time
            diff_before = time_diff_minutes(dev_h, dev_m, comp_h_before, comp_m_before)
            diff_after = time_diff_minutes(dev_h, dev_m, comp_h_after, comp_m_after)
            diff = min(diff_before, diff_after)
            comp_h, comp_m = (comp_h_before, comp_m_before) if diff_before <= diff_after else (comp_h_after, comp_m_after)
            if not first_sync:
                return False, f"Missing 'first sync, skipping calibration' in output"
            if diff <= 2:
                return True, f"NTP first sync (no calibration): Device={dev_h}:{dev_m:02d} Computer={comp_h}:{comp_m:02d}"
            return False, f"Time mismatch after reboot: Device={dev_h}:{dev_m:02d} Computer={comp_h}:{comp_m:02d} diff={diff}min"
    if found:
        return True, "Device rebooted and started (no display time in output)"
    return False, f"Timeout waiting for reboot. Output: {all_text}"


def test_ntp_resync_calibration(dev):
    """Test 15: S command — re-sync with calibration data."""
    comp_h_before, comp_m_before = get_computer_time_12h()
    lines = dev.send_and_read("S", timeout=40)
    comp_h_after, comp_m_after = get_computer_time_12h()
    all_text = "\n".join(lines)
    if "display will show" in all_text:
        m = re.search(r'display will show (\d+):(\d+)', all_text)
        if m:
            dev_h, dev_m = int(m.group(1)), int(m.group(2))
            diff_before = time_diff_minutes(dev_h, dev_m, comp_h_before, comp_m_before)
            diff_after = time_diff_minutes(dev_h, dev_m, comp_h_after, comp_m_after)
            diff = min(diff_before, diff_after)
            comp_h, comp_m = (comp_h_before, comp_m_before) if diff_before <= diff_after else (comp_h_after, comp_m_after)
            ok = diff <= 2
            cal = "calibration drift" in all_text
            return ok, f"Device={dev_h}:{dev_m:02d} Computer={comp_h}:{comp_m:02d} calibration={'yes' if cal else 'no'}"
    if "FAILED" in all_text:
        return False, f"Sync failed (even after firmware retries): {all_text}"
    return False, f"No 'display will show' in output: {all_text}"


# =============================================================================
# TEST RUNNER
# =============================================================================

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else None
    dev = SerialDevice(port)

    # Ensure timer is reset at start
    dev.send_and_read("R")

    tests = [
        # Group 1: State & Display Tests
        ("Display Present (I2C probe)", test_display_present),
        ("Query State Command", test_query_state),
        ("Time Display State", test_time_display_state),
        ("Query Time Format", test_query_time_format),
        ("Query Battery", test_query_battery),
        ("Elapsed Minutes Transition", test_elapsed_minutes_transition),
        ("Timer Reset", test_timer_reset),
        ("Sleep Timeout (dry run)", test_dry_sleep),
        # Group 2: Calibration (before error tests — needs clean SNTP state)
        ("Calibration End-to-End", test_calibration_end_to_end),
        # Group 3: NTP Error Path Tests
        ("WiFi Connection Failure", test_wifi_failure),
        ("NTP Timeout", test_ntp_timeout),
        # Group 4: NTP Sync Tests (cause reboots — run last)
        ("NTP First Sync (reboot)", test_ntp_first_sync_reboot),
        ("NTP Re-sync with Calibration", test_ntp_resync_calibration),
    ]

    results = []
    for i, (name, func) in enumerate(tests, 1):
        print(f"\n{'='*60}")
        print(f"TEST {i}/{len(tests)}: {name}")
        print(f"{'='*60}")
        try:
            passed, msg = func(dev)
        except Exception as e:
            passed, msg = False, f"Exception: {e}"
        status = "PASS" if passed else "FAIL"
        print(f"  [{status}] {msg}")
        results.append((name, passed, msg))

    # Summary
    print(f"\n{'='*60}")
    print("SUMMARY")
    print(f"{'='*60}")
    passed_count = sum(1 for _, p, _ in results if p)
    total = len(results)
    for name, passed, msg in results:
        status = "PASS" if passed else "FAIL"
        print(f"  [{status}] {name}: {msg}")
    print(f"\n{passed_count}/{total} tests passed")

    dev.close()
    sys.exit(0 if passed_count == total else 1)


if __name__ == "__main__":
    main()

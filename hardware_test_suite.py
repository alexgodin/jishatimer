#!/usr/bin/env python3
"""
Hardware-in-the-loop test suite for the jishatimer device.

Drives the device over its USB serial command interface and checks
display presence, timer state transitions, battery, sleep, RTC
drift/calibration, NTP error paths, and NTP sync.
Requires device connected via USB serial.

The RTC calibration arithmetic is covered separately and without hardware by
`pio test -e native`; the calibration test here only checks the integration.

Usage: python3 hardware_test_suite.py [port] [--no-display]
  port           defaults to /dev/cu.usbmodem*
  --no-display   skip the OLED probe when no panel is attached
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

def skipped(reason):
    """Build a test that reports SKIP — passed is None, not False."""
    def _test(dev):
        return None, reason
    return _test


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


def test_rtc_health(dev):
    """RTC holds a believable time — oscillator running and part configured.

    Runs before the time tests because every one of them is meaningless if
    the RTC lost its count; this makes that the reported failure rather than
    a confusing parse error further down."""
    lines = send_with_retry(dev, "V", marker="RTCHEALTH:")
    for line in lines:
        m = re.search(r'RTCHEALTH: present=(\w+) valid=(\w+) boot_oscillator_stopped=(\d) '
                      r'boot_configured=(\d) offset=(-?\d+)', line)
        if m:
            present, valid = m.group(1) == "yes", m.group(2) == "yes"
            os_stopped, configured = m.group(3) == "1", m.group(4) == "1"
            offset = int(m.group(5))
            # Check presence first: with no part on the bus every field below
            # reads zero, which is indistinguishable from a configured RTC
            # reporting good news, and the old code called that a dead cell.
            if not present:
                return False, ("RTC not found on the I2C bus — check wiring, "
                               "not the backup cell. Confirm with the I command.")
            if valid:
                # Bound the stored offset by what the firmware itself is
                # willing to apply. Tighter than this fails devices whose
                # genuinely poor crystal legitimately needs a large correction,
                # and tells the operator to erase a correct calibration.
                if abs(offset) > 49:  # MAX_PLAUSIBLE_PPM / PPM_PER_LSB
                    return False, (f"RTC valid but offset={offset} "
                                   f"({offset * 4.069:.0f} ppm) looks like a bad "
                                   f"calibration — clear it with the Z command")
                # Flag a part that recovered this boot but came up cold: it
                # will lose time again on the next power cycle.
                if not configured:
                    return True, ("RTC time valid, but came up unconfigured — "
                                  "backup power is not holding across power cycles")
                return True, "RTC time valid"
            if not configured:
                return False, ("RTC came up unconfigured — backup cell dead or "
                               "VBAT not connected. Time shows --:-- until a sync.")
            if os_stopped:
                return False, ("RTC configured but oscillator stopped — crystal "
                               "or backup supply fault. Time shows --:-- until a sync.")
            return False, "RTC reports invalid time for an unexpected reason"
    return False, f"No RTCHEALTH: line: {lines}"


def test_query_time_format(dev):
    """Test 3: Q returns time in valid 12h format (h:mm, h in 1-12).
    Accuracy is tested later after NTP sync."""
    lines = send_with_retry(dev, "Q", marker="TIME:")
    for line in lines:
        if "TIME: --:--" in line:
            return False, "Device reports --:-- (no valid RTC time) — see RTC Health"
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


def test_query_charging(dev):
    """Test: C returns charge status and last charge-triggered sync outcome."""
    lines = send_with_retry(dev, "C", marker="CHARGING:")
    for line in lines:
        m = re.search(r'CHARGING: (yes|no) SYNC: (ok|failed)', line)
        if m:
            return True, f"charging={m.group(1)} sync={m.group(2)}"
    return False, f"No well-formed CHARGING: line: {lines}"


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
    """L command — backdate lastNtpSync by 7 days, sync, verify the calibration
    path is reached and its numbers are self-consistent.

    This is an *integration* check: that the gate fires, the drift/elapsed
    values come off the RTC sanely, and the math runs. The arithmetic itself
    is covered exhaustively and without hardware by `pio test -e native`
    (test/test_calibration) — don't duplicate it here, since real drift on the
    bench isn't a controllable input.

    L runs the firmware's dry-run path: the 7d window is fabricated, so the
    ppm it produces is not a crystal measurement and must not be written to
    the offset register. Seeing DRYRUN is part of what this test asserts.
    """
    lines = dev.send_and_read("L", timeout=40)
    all_text = "\n".join(lines)

    if "FAILED" in all_text:
        return False, f"Sync failed (even after firmware retries): {all_text}"

    if re.search(r'calibration drift=', all_text):
        return False, ("L wrote a REAL calibration — dry-run flag is broken, the "
                       "offset register now holds a fabricated correction. "
                       "Clear it with the Z command.")

    m = re.search(r'calibration DRYRUN drift=(-?\d+)s over (\d+)s \((-?[\d.]+) ppm\), offset=(-?\d+)', all_text)
    if m:
        drift, elapsed, ppm, offset = int(m.group(1)), int(m.group(2)), float(m.group(3)), int(m.group(4))
        # elapsed = 604800 - drift + syncDuration, so a fast RTC lands BELOW
        # the nominal 604800. The firmware accepts |drift| up to 121s (200 ppm),
        # so the window has to be at least that wide on both sides or a
        # perfectly healthy fast RTC fails here.
        if not (604600 <= elapsed <= 604980):
            return False, f"elapsed={elapsed}s, expected ~604800s from the L backdate"
        if elapsed and abs(drift / elapsed * 1e6 - ppm) > 1.0:
            return False, f"ppm={ppm} inconsistent with drift={drift}s over {elapsed}s"
        # Deliberately NOT recomputing the offset here: that is
        # test/test_calibration's job, and doing it in Python compares
        # banker's rounding against the firmware's lroundf() using a ppm
        # already truncated to one decimal by printf. Just bound it.
        if abs(offset) > 49:  # MAX_PLAUSIBLE_PPM / PPM_PER_LSB
            return False, f"offset={offset} outside the plausible register range"
        return True, f"dry-run calibration: drift={drift}s over {elapsed}s ({ppm:.1f} ppm) offset={offset}"

    m = re.search(r'calibration skipped drift=(-?\d+)s over (\d+)s \((-?[\d.]+) ppm\) - (.+)', all_text)
    if m:
        drift, elapsed, ppm, reason = int(m.group(1)), int(m.group(2)), float(m.group(3)), m.group(4)
        # Reaching the skip branch still proves the gate fired and the math ran.
        # An implausible ppm here means the RTC is not merely drifting — most
        # likely it was reseeded from firmware compile time (see setupTime).
        if abs(ppm) > 200.0:
            return False, (f"RTC drift {ppm:.0f} ppm is not crystal error — "
                           f"RTC likely lost power and reseeded to compile time. "
                           f"drift={drift}s over {elapsed}s")
        return False, f"Calibration skipped unexpectedly: {reason} ({ppm:.1f} ppm)"

    if "first sync, skipping calibration" in all_text:
        return False, f"lastNtpSync was not backdated — L hook is broken: {all_text}"
    return False, f"No calibration output: {all_text}"


def test_ntp_first_sync_reboot(dev):
    """Test 14: T command — corrupt RTC, reboot, full NTP sync.

    Sync is now charge-triggered (edge on the D7 charge-status pin), not a
    48h-timer fallback, so this only fires if the DUT is actively charging
    (not just USB-connected) at reboot time — some charge ICs de-assert
    their status line once the battery reaches full even with USB still
    connected. If this test stops seeing a sync, check whether the test
    battery was topped off before assuming a firmware regression."""
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
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    no_display = "--no-display" in sys.argv
    port = args[0] if args else None
    dev = SerialDevice(port)

    # Ensure timer is reset at start
    dev.send_and_read("R")

    tests = [
        # Group 1: State & Display Tests
        # --no-display: the probe is correct, the bench just has no OLED
        # attached. Report SKIP rather than a FAIL that has to be remembered.
        ("Display Present (I2C probe)",
         skipped("no OLED on this bench (--no-display)") if no_display
         else test_display_present),
        ("RTC Health", test_rtc_health),
        ("Query State Command", test_query_state),
        ("Time Display State", test_time_display_state),
        ("Query Time Format", test_query_time_format),
        ("Query Battery", test_query_battery),
        ("Query Charging", test_query_charging),
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
        status = "SKIP" if passed is None else ("PASS" if passed else "FAIL")
        print(f"  [{status}] {msg}")
        results.append((name, passed, msg))

    # Summary
    print(f"\n{'='*60}")
    print("SUMMARY")
    print(f"{'='*60}")
    passed_count = sum(1 for _, p, _ in results if p is True)
    failed_count = sum(1 for _, p, _ in results if p is False)
    skipped_count = sum(1 for _, p, _ in results if p is None)
    ran = passed_count + failed_count
    for name, passed, msg in results:
        status = "SKIP" if passed is None else ("PASS" if passed else "FAIL")
        print(f"  [{status}] {name}: {msg}")
    summary = f"\n{passed_count}/{ran} tests passed"
    if skipped_count:
        summary += f", {skipped_count} skipped"
    print(summary)

    dev.close()
    sys.exit(0 if failed_count == 0 else 1)


if __name__ == "__main__":
    main()

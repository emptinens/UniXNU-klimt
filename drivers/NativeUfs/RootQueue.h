// SPDX-License-Identifier: BSD-2-Clause-Patent
// Root queue takeover; shared with the host fault-injection harness.
static bool hostWaitRegister (unsigned offset, uint32_t mask, uint32_t value)
{
  uint64_t deadline;
  clock_interval_to_deadline (100, kMillisecondScale, &deadline);
  do {
    if ((read32 (gHost.hc, offset) & mask) == value) return true;
    IODelay (10);
  } while (mach_absolute_time () < deadline);
  return false;
}

static bool hostStopQueue (const char **why)
{
  uint32_t cap = read32 (gHost.hc, 0x00);
  uint32_t ver = read32 (gHost.hc, 0x08);
  uint32_t is  = read32 (gHost.hc, 0x20);
  uint32_t hcs = read32 (gHost.hc, 0x30);
  uint32_t hce = read32 (gHost.hc, 0x34);
  uint32_t utrldbr = read32 (gHost.hc, 0x58);
  uint32_t utmrdbr = read32 (gHost.hc, 0x78);
  uint32_t utrlrun = read32 (gHost.hc, 0x60);
  uint32_t utmrlrun = read32 (gHost.hc, 0x70);

  char diag[160];
  snprintf (diag, sizeof (diag),
            "NATIVE_UFS: takeover CAP=%08x VER=%08x HCS=%08x HCE=%08x IS=%08x DBR=%08x/%08x RUN=%x/%x\n",
            cap, ver, hcs, hce, is, utrldbr, utmrdbr, utrlrun, utmrlrun);
  dali_bootstrap_log (diag);

  // 1. Controller must be enabled.
  if (!(hce & 1)) {
    dali_bootstrap_log ("NATIVE_UFS: HCE disabled, enabling controller...\n");
    hostWrite (0x34, 1);
    if (!hostWaitRegister (0x34, 1, 1)) {
      recordTransport (kIOReturnNotReady, 15);
      *why = "controller enable (HCE) failed";
      return false;
    }
  }

  // 2. Device Present (DP): bit 0 of HCS
  if (!hostWaitRegister (0x30, 1, 1)) {
    recordTransport (kIOReturnNotReady, 15);
    *why = "UFS device not present (HCS.DP=0)";
    return false;
  }

  // 3. Confirm the firmware has no active transfer/task in flight.
  if (read32 (gHost.hc, 0x58) || read32 (gHost.hc, 0x78)) {
    uint64_t deadline;
    clock_interval_to_deadline (50, kMillisecondScale, &deadline);
    while ((read32 (gHost.hc, 0x58) || read32 (gHost.hc, 0x78)) && mach_absolute_time () < deadline) {
      IODelay (50);
    }
    if (read32 (gHost.hc, 0x58) || read32 (gHost.hc, 0x78)) {
      recordTransport (kIOReturnBusy, 15);
      *why = "firmware request outstanding";
      return false;
    }
  }

  // 4. Controller fatal error
  if (read32 (gHost.hc, 0x20) & kUfsFatalStatus) {
    recordTransport (kIOReturnIOError, 15);
    *why = "firmware controller error";
    return false;
  }

  // 5. Mask interrupts and stop active request list if running
  hostWrite (0x24, 0);
  if (read32 (gHost.hc, 0x60) & 1) {
    hostWrite (0x60, 0);
    if (!hostWaitRegister (0x60, 1, 0)) {
      recordTransport (kIOReturnTimeout, 15);
      *why = "request list stop";
      return false;
    }
  }
  if (read32 (gHost.hc, 0x70) & 1) {
    hostWrite (0x70, 0);
    if (!hostWaitRegister (0x70, 1, 0)) {
      recordTransport (kIOReturnTimeout, 15);
      *why = "task management list stop";
      return false;
    }
  }

  dali_bootstrap_log ("NATIVE_UFS: host controller stopped, ready for DMA list takeover\n");
  return true;
}

static bool hostStartQueue (const char **why)
{
  hostWrite (0x50, static_cast<uint32_t> (gHost.phys));
  hostWrite (0x54, static_cast<uint32_t> (gHost.phys >> 32));
  if (read32 (gHost.hc, 0x50) != uint32_t (gHost.phys) ||
      read32 (gHost.hc, 0x54) != uint32_t (gHost.phys >> 32)) {
    recordTransport (kIOReturnIOError, 15);
    *why = "request list address";
    return false;
  }
  gRootDiagnostic.isBefore = read32 (gHost.hc, 0x20);
  hostWrite (0x20, 0xffffffffU); // clear all stale status interrupts
  hostWrite (0x60, 1);
  if (!hostWaitRegister (0x60, 1, 1) || !hostWaitRegister (0x30, 2, 2)) {
    char err[80];
    snprintf (err, sizeof (err), "NATIVE_UFS: startQueue wait failed: RUN=%x HCS=%x\n",
              read32 (gHost.hc, 0x60), read32 (gHost.hc, 0x30));
    dali_bootstrap_log (err);
    recordTransport (kIOReturnTimeout, 15);
    *why = "request list ready";
    return false;
  }
  dali_bootstrap_log ("NATIVE_UFS: request list started (UTRLRDY=1)\n");
  return true;
}

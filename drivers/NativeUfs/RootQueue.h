// SPDX-License-Identifier: BSD-2-Clause-Patent
// Root queue takeover; shared with the host fault-injection harness.
static bool hostWaitRegister (unsigned offset, uint32_t mask, uint32_t value, unsigned loops = 10000)
{
  for (unsigned i = 0; i < loops; i++) {
    if ((read32 (gHost.hc, offset) & mask) == value) return true;
    IODelay (10);
  }
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
    dali_bootstrap_log ("NATIVE_UFS: HCE disabled, enabling controller (0x34=1)...\n");
    hostWrite (0x34, 1);
    if (!hostWaitRegister (0x34, 1, 1, 100000)) {
      char err[80];
      snprintf (err, sizeof (err), "NATIVE_UFS: HCE enable timeout, read=%08x\n", read32 (gHost.hc, 0x34));
      dali_bootstrap_log (err);
      recordTransport (kIOReturnNotReady, 15);
      *why = "controller enable (HCE) failed";
      return false;
    }
    dali_bootstrap_log ("NATIVE_UFS: HCE enabled (0x34=1)\n");
  }

  // 2. Link startup if Device Present (DP) is not set
  hcs = read32 (gHost.hc, 0x30);
  if (!(hcs & 1)) {
    dali_bootstrap_log ("NATIVE_UFS: DP=0, issuing DME_LINKSTARTUP (0x90=0x16)...\n");
    // Wait for UCRDY (bit 3) before writing UICCMD
    if (!hostWaitRegister (0x30, 8, 8, 50000)) {
      dali_bootstrap_log ("NATIVE_UFS: warning: UCRDY not set before LINKSTARTUP\n");
    }
    hostWrite (0x90, 0x16);
    if (!hostWaitRegister (0x30, 1, 1, 100000)) {
      char err[80];
      snprintf (err, sizeof (err), "NATIVE_UFS: link startup timeout, HCS=%08x\n", read32 (gHost.hc, 0x30));
      dali_bootstrap_log (err);
      recordTransport (kIOReturnNotReady, 15);
      *why = "UFS link startup failed (HCS.DP=0)";
      return false;
    }
    char msg[80];
    snprintf (msg, sizeof (msg), "NATIVE_UFS: link established, HCS=%08x\n", read32 (gHost.hc, 0x30));
    dali_bootstrap_log (msg);
  }

  // 3. Confirm the firmware has no active transfer/task in flight.
  for (unsigned wait = 0; wait < 5000; wait++) {
    if (!read32 (gHost.hc, 0x58) && !read32 (gHost.hc, 0x78)) break;
    IODelay (10);
  }
  if (read32 (gHost.hc, 0x58) || read32 (gHost.hc, 0x78)) {
    recordTransport (kIOReturnBusy, 15);
    *why = "firmware request outstanding";
    return false;
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
    if (!hostWaitRegister (0x60, 1, 0, 10000)) {
      recordTransport (kIOReturnTimeout, 15);
      *why = "request list stop";
      return false;
    }
  }
  if (read32 (gHost.hc, 0x70) & 1) {
    hostWrite (0x70, 0);
    if (!hostWaitRegister (0x70, 1, 0, 10000)) {
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
  if (!hostWaitRegister (0x60, 1, 1, 10000) || !hostWaitRegister (0x30, 2, 2, 50000)) {
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

// SPDX-License-Identifier: BSD-2-Clause-Patent
// Root queue takeover; shared with the host fault-injection harness.
static inline void ufsDelay (unsigned iters = 10000)
{
  for (volatile unsigned i = 0; i < iters; i++) {
    __asm__ volatile ("yield");
  }
}

static bool hostWaitRegister (unsigned offset, uint32_t mask, uint32_t value, unsigned loops = 10000)
{
  for (unsigned i = 0; i < loops; i++) {
    if ((read32 (gHost.hc, offset) & mask) == value) return true;
    ufsDelay (10000);
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
    // 1a. MediaTek vendor reference clock handshake (0x144)
    uint32_t refclk = read32 (gHost.hc, 0x144);
    char refMsg[96];
    snprintf (refMsg, sizeof (refMsg), "NATIVE_UFS: MTK REFCLK initial=0x%08x\n", refclk);
    dali_bootstrap_log (refMsg);

    if (!(refclk & 2)) {
      dali_bootstrap_log ("NATIVE_UFS: requesting MTK REFCLK (0x144=1)...\n");
      hostWrite (0x144, 1);
      bool refAck = false;
      for (unsigned i = 0; i < 5000; i++) {
        if (read32 (gHost.hc, 0x144) & 2) {
          refAck = true;
          break;
        }
        ufsDelay (10000);
      }
      snprintf (refMsg, sizeof (refMsg), "NATIVE_UFS: MTK REFCLK ack=%d (0x144=0x%08x)\n",
                refAck ? 1 : 0, read32 (gHost.hc, 0x144));
      dali_bootstrap_log (refMsg);
    }

    // 1b. Disable Auto-Hibernate idle timer (0x18) during bringup
    hostWrite (0x18, 0);

    // 1c. Enable Host Controller (0x34 = 1)
    dali_bootstrap_log ("NATIVE_UFS: issuing HCE write (0x34=1)...\n");
    hostWrite (0x34, 1);
    dali_bootstrap_log ("NATIVE_UFS: HCE write issued, pausing 600us...\n");
    ufsDelay (200000); // 600us pause
    dali_bootstrap_log ("NATIVE_UFS: pause complete, reading 0x34...\n");

    uint32_t curHce = read32 (gHost.hc, 0x34);
    char hceMsg[96];
    snprintf (hceMsg, sizeof (hceMsg), "NATIVE_UFS: first read 0x34=0x%08x HCS=0x%08x\n",
              curHce, read32 (gHost.hc, 0x30));
    dali_bootstrap_log (hceMsg);

    bool hceReady = (curHce & 1) != 0;
    for (unsigned i = 0; !hceReady && i < 2000; i++) { // up to 50 ms
      ufsDelay (20000);
      curHce = read32 (gHost.hc, 0x34);
      if (curHce & 1) {
        hceReady = true;
        break;
      }
      if ((i % 200) == 0) {
        snprintf (hceMsg, sizeof (hceMsg), "NATIVE_UFS: HCE poll i=%u 0x34=0x%08x HCS=0x%08x\n",
                  i, curHce, read32 (gHost.hc, 0x30));
        dali_bootstrap_log (hceMsg);
      }
    }

    snprintf (hceMsg, sizeof (hceMsg), "NATIVE_UFS: HCE ready=%d (0x34=0x%08x, HCS=0x%08x)\n",
              hceReady ? 1 : 0, read32 (gHost.hc, 0x34), read32 (gHost.hc, 0x30));
    dali_bootstrap_log (hceMsg);

    if (!hceReady) {
      recordTransport (kIOReturnNotReady, 15);
      *why = "controller enable (HCE) failed";
      return false;
    }
  }

  // 2. Link startup if Device Present (DP) is not set
  hcs = read32 (gHost.hc, 0x30);
  if (!(hcs & 1)) {
    dali_bootstrap_log ("NATIVE_UFS: DP=0, preparing DME_LINKSTARTUP sequence...\n");

    bool dpReady = false;
    for (unsigned attempt = 1; attempt <= 4; attempt++) {
      // 2a. Wait for UCRDY (bit 3) before writing UICCMD
      for (unsigned i = 0; i < 5000; i++) {
        if (read32 (gHost.hc, 0x30) & 8) break;
        ufsDelay (1000);
      }

      // 2b. Clear previous UIC completion status in IS (0x20): bit 10 (UCCS) and bit 8 (ULSS)
      hostWrite (0x20, 0x500);

      // 2c. Write UIC command arguments (0x94, 0x98, 0x9C = 0 for DME_LINKSTARTUP)
      hostWrite (0x94, 0);
      hostWrite (0x98, 0);
      hostWrite (0x9C, 0);

      // 2d. Issue DME_LINKSTARTUP (0x90 = 0x16)
      hostWrite (0x90, 0x16);

      // 2e. Wait for UIC completion: IS.UCCS (bit 10) or HCS.UCRDY (bit 3)
      for (unsigned i = 0; i < 20000; i++) {
        if ((read32 (gHost.hc, 0x20) & 0x400) || (read32 (gHost.hc, 0x30) & 8)) break;
        ufsDelay (1000);
      }

      uint32_t isVal = read32 (gHost.hc, 0x20);
      uint32_t hcsVal = read32 (gHost.hc, 0x30);
      uint32_t uicResult = read32 (gHost.hc, 0x98) & 0xff;

      char attMsg[128];
      snprintf (attMsg, sizeof (attMsg),
                "NATIVE_UFS: DME_LINKSTARTUP att=%u: res=%02x IS=%08x HCS=%08x\n",
                attempt, uicResult, isVal, hcsVal);
      dali_bootstrap_log (attMsg);

      // 2f. Poll for DP (Device Present)
      for (unsigned i = 0; i < 10000; i++) {
        if (read32 (gHost.hc, 0x30) & 1) {
          dpReady = true;
          break;
        }
        ufsDelay (2000);
      }

      if (dpReady) {
        snprintf (attMsg, sizeof (attMsg),
                  "NATIVE_UFS: Link established on attempt %u! HCS=%08x\n",
                  attempt, read32 (gHost.hc, 0x30));
        dali_bootstrap_log (attMsg);
        break;
      }

      // Inter-attempt pause to allow M-PHY state machine to settle
      ufsDelay (100000);
    }

    if (!dpReady) {
      recordTransport (kIOReturnNotReady, 15);
      *why = "UFS link startup failed (HCS.DP=0)";
      return false;
    }
  }

  // 3. Confirm the firmware has no active transfer/task in flight.
  for (unsigned wait = 0; wait < 5000; wait++) {
    if (!read32 (gHost.hc, 0x58) && !read32 (gHost.hc, 0x78)) break;
    ufsDelay (10000);
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

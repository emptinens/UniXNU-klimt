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

static bool ufsSendUicCmd (uint32_t cmd, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t *outArg2 = nullptr)
{
  for (unsigned i = 0; i < 50000; i++) {
    if (read32 (gHost.hc, 0x30) & 8) break;
    ufsDelay (1000);
  }
  if (!(read32 (gHost.hc, 0x30) & 8)) {
    dali_bootstrap_log ("NATIVE_UFS: ufsSendUicCmd: UCRDY timeout before write\n");
    return false;
  }

  hostWrite (0x20, 0x504);
  hostWrite (0x94, arg1);
  hostWrite (0x98, arg2);
  hostWrite (0x9C, arg3);
  hostWrite (0x90, cmd);

  bool done = false;
  for (unsigned i = 0; i < 100000; i++) {
    uint32_t is = read32 (gHost.hc, 0x20);
    if (is & 0x404) {
      done = true;
      break;
    }
    ufsDelay (1000);
  }

  uint32_t isVal = read32 (gHost.hc, 0x20);
  uint32_t resVal = read32 (gHost.hc, 0x98);
  if (outArg2) *outArg2 = resVal;

  if (!done || (isVal & 4) || ((resVal & 0xff) != 0)) {
    char err[128];
    snprintf (err, sizeof (err),
              "NATIVE_UFS: UIC cmd 0x%02x failed: done=%d IS=%08x res=%02x\n",
              cmd, done ? 1 : 0, isVal, resVal & 0xff);
    dali_bootstrap_log (err);
    return false;
  }
  return true;
}

static bool ufsDmeSet (uint32_t mib, uint32_t val)
{
  return ufsSendUicCmd (0x02, (mib & 0xffff) << 16, 0, val);
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

    // 2a. Disable TX LCC before link startup (MediaTek recommendation)
    bool lccOk = ufsDmeSet (0x155e, 0);
    char lccMsg[80];
    snprintf (lccMsg, sizeof (lccMsg), "NATIVE_UFS: TX_LCC disable res=%d\n", lccOk ? 1 : 0);
    dali_bootstrap_log (lccMsg);

    bool dpReady = false;
    for (unsigned attempt = 1; attempt <= 3; attempt++) {
      // 2b. Wait for UCRDY (bit 3) before writing UICCMD
      for (unsigned i = 0; i < 50000; i++) {
        if (read32 (gHost.hc, 0x30) & 8) break;
        ufsDelay (1000);
      }

      // 2c. Clear previous UIC completion status and errors: UCCS (0x400), ULSS (0x100), UE (0x04)
      hostWrite (0x20, 0x504);

      // 2d. Write UIC command arguments (0 for DME_LINKSTARTUP)
      hostWrite (0x94, 0);
      hostWrite (0x98, 0);
      hostWrite (0x9C, 0);

      // 2e. Issue DME_LINKSTARTUP (0x16)
      hostWrite (0x90, 0x16);

      // 2f. Wait strictly for IS.UCCS (0x400) or IS.UE (0x04)
      // Link startup negotiation takes 50-500 ms (up to 1000 ms per UFSHCI spec).
      // Each step: ufsDelay(300000) ~ 1 ms.
      bool uicDone = false;
      for (unsigned step = 0; step < 1200; step++) {
        uint32_t is = read32 (gHost.hc, 0x20);
        if (is & 0x404) {
          uicDone = true;
          break;
        }
        if (step == 50 || step == 150 || step == 300 || step == 600 || step == 1000) {
          char pollMsg[128];
          snprintf (pollMsg, sizeof (pollMsg),
                    "NATIVE_UFS: link poll %u ms: HCS=%08x IS=%08x\n",
                    step, read32 (gHost.hc, 0x30), is);
          dali_bootstrap_log (pollMsg);
        }
        ufsDelay (300000);
      }

      uint32_t isVal = read32 (gHost.hc, 0x20);
      uint32_t hcsVal = read32 (gHost.hc, 0x30);
      uint32_t uicRes = read32 (gHost.hc, 0x98) & 0xff;
      uint32_t uecpa = read32 (gHost.hc, 0x38);
      uint32_t uecdl = read32 (gHost.hc, 0x3c);

      char attMsg[160];
      snprintf (attMsg, sizeof (attMsg),
                "NATIVE_UFS: DME_LINKSTARTUP att=%u: done=%d res=%02x IS=%08x HCS=%08x UECPA=%08x UECDL=%08x\n",
                attempt, uicDone ? 1 : 0, uicRes, isVal, hcsVal, uecpa, uecdl);
      dali_bootstrap_log (attMsg);

      // 2g. Check if DP (Device Present) is established
      if (hcsVal & 1) {
        dpReady = true;
      } else {
        // Poll for DP up to 200 ms if UCCS arrived before DP assertion
        for (unsigned p = 0; p < 200; p++) {
          if (read32 (gHost.hc, 0x30) & 1) {
            dpReady = true;
            break;
          }
          ufsDelay (300000);
        }
      }

      if (dpReady) {
        snprintf (attMsg, sizeof (attMsg),
                  "NATIVE_UFS: Link established on attempt %u! HCS=%08x\n",
                  attempt, read32 (gHost.hc, 0x30));
        dali_bootstrap_log (attMsg);
        break;
      }

      // Settle pause before retry: clear IS and pause 100 ms
      hostWrite (0x20, 0x504);
      ufsDelay (30000000);
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

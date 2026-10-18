#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * @file diagnostics.h
 * @brief Hardware sanity-check routine for the FootHoled H1 (Waveshare
 *        ESP32-S3-Touch-LCD-3.5B).
 *
 * This is a SIMPLE diagnostics pass: it asks each piece of hardware "are you
 * there and responding?" and reports a pass/fail/not-present result. It does
 * NOT fabricate measurements — every result comes from a real probe (an I2C
 * ACK, a driver init that verifies a chip ID, a filesystem mount, a radio init
 * returning a count). When a probe cannot be run (no driver, feature disabled)
 * it reports SKIPPED, never a fake "OK".
 *
 * The routine is UI-driven: the Expand -> Diagnostics screen runs it on open
 * (and offers a Re-run button). It is deliberately read-only and non-
 * destructive — safe to run at any time. Nothing here starts a long scan or
 * writes to storage; the Wi-Fi/BLE checks use a brief init-and-count only.
 *
 * TODO(HW): none of this is compiled or run on real silicon in this repo (no
 * ESP-IDF toolchain available where it was authored). The probes call real
 * ESP-IDF / driver APIs, but the first true verification is an on-board run.
 * The chip presence checks double as a bring-up checklist: see
 * docs/Build_Log.md "Diagnostics self-test".
 */

#ifdef __cplusplus
extern "C" {
#endif

/// Outcome of a single diagnostic check. Kept deliberately coarse — this is a
/// sanity routine, not a test harness.
typedef enum {
    FH_DIAG_OK = 0,       ///< Hardware present and responded as expected.
    FH_DIAG_FAIL,         ///< Hardware expected but did not respond / failed.
    FH_DIAG_NOT_PRESENT,  ///< Hardware optional and not detected (not an error).
    FH_DIAG_SKIPPED,      ///< Check could not be run (no driver / disabled).
} fh_diag_status_t;

/// One line of the diagnostics report.
typedef struct {
    char             name[24];    ///< Short component label, e.g. "IMU (QMI8658)".
    fh_diag_status_t status;      ///< Result of the probe.
    char             detail[48];  ///< Human-readable detail, e.g. "WHO_AM_I 0x05" or "0x51 ACK".
} fh_diag_result_t;

/// Maximum number of checks the routine can report (sizes the caller's array).
#define FH_DIAG_MAX_RESULTS 16

/**
 * @brief Run the full hardware sanity pass and fill the caller's result array.
 *
 * Blocking and quick (a few hundred ms worst case — the radio checks dominate).
 * Intended to be called from the UI task; it does not spawn its own task.
 *
 * @param out       Caller-provided array, at least FH_DIAG_MAX_RESULTS long.
 * @param max       Capacity of @p out.
 * @param[out] ok   Optional — set to the number of checks that returned OK.
 * @param[out] fail Optional — set to the number of checks that returned FAIL.
 * @return Number of result rows written (<= max).
 */
size_t footholed_diagnostics_run(fh_diag_result_t *out, size_t max,
                                 size_t *ok, size_t *fail);

/// Short human string for a status (for UI rendering): "OK" / "FAIL" / etc.
const char *footholed_diagnostics_status_text(fh_diag_status_t s);

#ifdef __cplusplus
}
#endif

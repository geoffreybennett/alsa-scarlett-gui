// SPDX-FileCopyrightText: 2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

// Test program for biquad coefficient analysis
// Build from src/: make test
// Run: ./tests/test-biquad

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "biquad.h"

#define SAMPLE_RATE 48000.0

static int test_count = 0;
static int pass_count = 0;
static int fail_count = 0;

static void test_filter(
  BiquadFilterType type,
  double freq,
  double q,
  double gain_db
) {
  struct biquad_params orig = {
    .type = type,
    .freq = freq,
    .q = q,
    .gain_db = gain_db
  };

  // Calculate coefficients
  struct biquad_coeffs coeffs;
  biquad_calculate(&orig, SAMPLE_RATE, &coeffs);

  // Analyze back
  struct biquad_params result;
  biquad_analyze(&coeffs, SAMPLE_RATE, &result);

  // Check results
  int type_ok = (result.type == orig.type);
  // Freq is irrelevant for gain filter
  int freq_ok = (type == BIQUAD_TYPE_GAIN) ||
                fabs(result.freq - orig.freq) < orig.freq * 0.000000001;
  // Q is irrelevant for gain and first-order filters
  int q_ok = !biquad_type_uses_q(type) ||
             fabs(result.q - orig.q) < orig.q * 0.000000001;
  int gain_ok = !biquad_type_uses_gain(type) ||
                fabs(result.gain_db - orig.gain_db) < 0.000000001;

  int pass = type_ok && freq_ok && q_ok && gain_ok;

  test_count++;
  if (pass) {
    pass_count++;
  } else {
    fail_count++;
    printf("FAIL: %s freq=%.1f Q=%.2f gain=%.1f\n",
           biquad_type_name(type), freq, q, gain_db);
    printf("  -> got: %s freq=%.1f Q=%.2f gain=%.1f\n",
           biquad_type_name(result.type), result.freq, result.q, result.gain_db);
    printf("  coeffs: b0=%.6f b1=%.6f b2=%.6f a1=%.6f a2=%.6f\n",
           coeffs.b0, coeffs.b1, coeffs.b2, coeffs.a1, coeffs.a2);
    if (!type_ok) printf("  type mismatch!\n");
    if (!freq_ok) printf("  freq error: %.1f%%\n",
                         100.0 * fabs(result.freq - orig.freq) / orig.freq);
    if (!q_ok) printf("  Q error: %.1f%%\n",
                      100.0 * fabs(result.q - orig.q) / orig.q);
    if (!gain_ok) printf("  gain error: %.2f dB\n",
                         fabs(result.gain_db - orig.gain_db));
  }
}

int main(void) {
  double freqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 20000 };
  double qs[] = { 0.1, 0.5, 0.707, 1.0, 2.0, 5.0, 10.0 };
  double gains[] = { -18, -12, -6, -3, 0, 3, 6, 12, 18 };

  int n_freqs = sizeof(freqs) / sizeof(freqs[0]);
  int n_qs = sizeof(qs) / sizeof(qs[0]);
  int n_gains = sizeof(gains) / sizeof(gains[0]);

  printf("Testing biquad analysis...\n\n");

  for (int type = 0; type < BIQUAD_TYPE_COUNT; type++) {
    printf("Testing %s...\n", biquad_type_name(type));

    // Gain filter: only test gains, freq/Q are irrelevant
    if (type == BIQUAD_TYPE_GAIN) {
      for (int gi = 0; gi < n_gains; gi++) {
        test_filter(type, 1000, 0.707, gains[gi]);
      }
      continue;
    }

    // First-order filters: don't iterate over Q
    if (!biquad_type_uses_q(type)) {
      for (int fi = 0; fi < n_freqs; fi++) {
        if (biquad_type_uses_gain(type)) {
          for (int gi = 0; gi < n_gains; gi++) {
            // Skip 0dB shelf filters - indistinguishable from bypass
            if (gains[gi] == 0)
              continue;
            test_filter(type, freqs[fi], 0.707, gains[gi]);
          }
        } else {
          test_filter(type, freqs[fi], 0.707, 0);
        }
      }
      continue;
    }

    // Second-order filters: iterate over freq, Q, and optionally gain
    for (int fi = 0; fi < n_freqs; fi++) {
      for (int qi = 0; qi < n_qs; qi++) {
        if (biquad_type_uses_gain(type)) {
          for (int gi = 0; gi < n_gains; gi++) {
            // Skip 0dB shelf filters - indistinguishable from peaking
            if (gains[gi] == 0 &&
                (type == BIQUAD_TYPE_LOW_SHELF || type == BIQUAD_TYPE_HIGH_SHELF))
              continue;
            test_filter(type, freqs[fi], qs[qi], gains[gi]);
          }
        } else {
          test_filter(type, freqs[fi], qs[qi], 0);
        }
      }
    }
  }

  printf("\n========================================\n");
  printf("Results: %d tests, %d passed, %d failed\n",
         test_count, pass_count, fail_count);

  return fail_count > 0 ? 1 : 0;
}

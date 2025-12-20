// SPDX-FileCopyrightText: 2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <glib.h>

// Biquad filter types
typedef enum {
  BIQUAD_TYPE_PEAKING,
  BIQUAD_TYPE_LOW_SHELF,
  BIQUAD_TYPE_HIGH_SHELF,
  BIQUAD_TYPE_LOWPASS,
  BIQUAD_TYPE_HIGHPASS,
  BIQUAD_TYPE_BANDPASS,
  BIQUAD_TYPE_NOTCH,
  BIQUAD_TYPE_COUNT
} BiquadFilterType;

// Filter parameters
struct biquad_params {
  BiquadFilterType type;
  double freq;      // Frequency in Hz (20-20000)
  double q;         // Q factor (0.1-10)
  double gain_db;   // Gain in dB for peaking/shelving (-18 to +18)
};

// Biquad coefficients (normalized so a0=1)
struct biquad_coeffs {
  double b0, b1, b2;  // numerator
  double a1, a2;      // denominator (a0 implicit = 1)
};

// Fixed-point conversion constants
#define BIQUAD_FIXED_POINT_SHIFT 28
#define BIQUAD_FIXED_POINT_SCALE (1L << BIQUAD_FIXED_POINT_SHIFT)  // 268435456

// Calculate biquad coefficients from parameters
void biquad_calculate(
  const struct biquad_params *params,
  double sample_rate,
  struct biquad_coeffs *coeffs
);

// Convert coefficients to fixed-point format for hardware
// Output: [b0, b1, b2, -a1, -a2] * 2^28
void biquad_to_fixed_point(
  const struct biquad_coeffs *coeffs,
  long fixed[5]
);

// Calculate frequency response magnitude in dB at given frequency
double biquad_response_db(
  const struct biquad_coeffs *coeffs,
  double freq,
  double sample_rate
);

// Get human-readable filter type name
const char *biquad_type_name(BiquadFilterType type);

// Check if filter type uses gain parameter
gboolean biquad_type_uses_gain(BiquadFilterType type);

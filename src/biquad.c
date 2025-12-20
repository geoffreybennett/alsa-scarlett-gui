// SPDX-FileCopyrightText: 2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include "biquad.h"

// Filter type names
static const char *filter_type_names[] = {
  "Peaking",
  "Low Shelf",
  "High Shelf",
  "Lowpass",
  "Highpass",
  "Bandpass",
  "Notch"
};

const char *biquad_type_name(BiquadFilterType type) {
  if (type < 0 || type >= BIQUAD_TYPE_COUNT)
    return "Unknown";
  return filter_type_names[type];
}

gboolean biquad_type_uses_gain(BiquadFilterType type) {
  return type == BIQUAD_TYPE_PEAKING ||
         type == BIQUAD_TYPE_LOW_SHELF ||
         type == BIQUAD_TYPE_HIGH_SHELF;
}

// Calculate biquad coefficients using Audio EQ Cookbook formulas
// Reference: https://www.w3.org/TR/audio-eq-cookbook/
void biquad_calculate(
  const struct biquad_params *params,
  double sample_rate,
  struct biquad_coeffs *coeffs
) {
  double w0 = 2.0 * M_PI * params->freq / sample_rate;
  double cos_w0 = cos(w0);
  double sin_w0 = sin(w0);
  double alpha = sin_w0 / (2.0 * params->q);

  // A = sqrt(10^(gain_db/20)) = 10^(gain_db/40)
  double A = pow(10.0, params->gain_db / 40.0);

  double b0, b1, b2, a0, a1, a2;

  switch (params->type) {
    case BIQUAD_TYPE_PEAKING:
      b0 =  1.0 + alpha * A;
      b1 = -2.0 * cos_w0;
      b2 =  1.0 - alpha * A;
      a0 =  1.0 + alpha / A;
      a1 = -2.0 * cos_w0;
      a2 =  1.0 - alpha / A;
      break;

    case BIQUAD_TYPE_LOW_SHELF: {
      double sqrt_A = sqrt(A);
      double sqrt_A_alpha = 2.0 * sqrt_A * alpha;
      b0 =      A * ((A + 1.0) - (A - 1.0) * cos_w0 + sqrt_A_alpha);
      b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cos_w0);
      b2 =      A * ((A + 1.0) - (A - 1.0) * cos_w0 - sqrt_A_alpha);
      a0 =           (A + 1.0) + (A - 1.0) * cos_w0 + sqrt_A_alpha;
      a1 =    -2.0 * ((A - 1.0) + (A + 1.0) * cos_w0);
      a2 =           (A + 1.0) + (A - 1.0) * cos_w0 - sqrt_A_alpha;
      break;
    }

    case BIQUAD_TYPE_HIGH_SHELF: {
      double sqrt_A = sqrt(A);
      double sqrt_A_alpha = 2.0 * sqrt_A * alpha;
      b0 =       A * ((A + 1.0) + (A - 1.0) * cos_w0 + sqrt_A_alpha);
      b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cos_w0);
      b2 =       A * ((A + 1.0) + (A - 1.0) * cos_w0 - sqrt_A_alpha);
      a0 =            (A + 1.0) - (A - 1.0) * cos_w0 + sqrt_A_alpha;
      a1 =      2.0 * ((A - 1.0) - (A + 1.0) * cos_w0);
      a2 =            (A + 1.0) - (A - 1.0) * cos_w0 - sqrt_A_alpha;
      break;
    }

    case BIQUAD_TYPE_LOWPASS:
      b0 = (1.0 - cos_w0) / 2.0;
      b1 =  1.0 - cos_w0;
      b2 = (1.0 - cos_w0) / 2.0;
      a0 =  1.0 + alpha;
      a1 = -2.0 * cos_w0;
      a2 =  1.0 - alpha;
      break;

    case BIQUAD_TYPE_HIGHPASS:
      b0 =  (1.0 + cos_w0) / 2.0;
      b1 = -(1.0 + cos_w0);
      b2 =  (1.0 + cos_w0) / 2.0;
      a0 =   1.0 + alpha;
      a1 =  -2.0 * cos_w0;
      a2 =   1.0 - alpha;
      break;

    case BIQUAD_TYPE_BANDPASS:
      // Constant skirt gain, peak gain = Q
      b0 =  alpha;
      b1 =  0.0;
      b2 = -alpha;
      a0 =  1.0 + alpha;
      a1 = -2.0 * cos_w0;
      a2 =  1.0 - alpha;
      break;

    case BIQUAD_TYPE_NOTCH:
      b0 =  1.0;
      b1 = -2.0 * cos_w0;
      b2 =  1.0;
      a0 =  1.0 + alpha;
      a1 = -2.0 * cos_w0;
      a2 =  1.0 - alpha;
      break;

    default:
      // Passthrough (unity gain)
      coeffs->b0 = 1.0;
      coeffs->b1 = 0.0;
      coeffs->b2 = 0.0;
      coeffs->a1 = 0.0;
      coeffs->a2 = 0.0;
      return;
  }

  // Normalize by a0
  coeffs->b0 = b0 / a0;
  coeffs->b1 = b1 / a0;
  coeffs->b2 = b2 / a0;
  coeffs->a1 = a1 / a0;
  coeffs->a2 = a2 / a0;
}

void biquad_to_fixed_point(
  const struct biquad_coeffs *coeffs,
  long fixed[5]
) {
  // Hardware expects: [b0, b1, b2, -a1, -a2] * 2^28
  fixed[0] = lround(coeffs->b0 * BIQUAD_FIXED_POINT_SCALE);
  fixed[1] = lround(coeffs->b1 * BIQUAD_FIXED_POINT_SCALE);
  fixed[2] = lround(coeffs->b2 * BIQUAD_FIXED_POINT_SCALE);
  fixed[3] = lround(-coeffs->a1 * BIQUAD_FIXED_POINT_SCALE);
  fixed[4] = lround(-coeffs->a2 * BIQUAD_FIXED_POINT_SCALE);
}

// Calculate frequency response magnitude in dB
// H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
// At frequency f: z = e^(j*2*pi*f/fs)
double biquad_response_db(
  const struct biquad_coeffs *coeffs,
  double freq,
  double sample_rate
) {
  double w = 2.0 * M_PI * freq / sample_rate;
  double cos_w = cos(w);
  double cos_2w = cos(2.0 * w);
  double sin_w = sin(w);
  double sin_2w = sin(2.0 * w);

  // Numerator: b0 + b1*e^(-jw) + b2*e^(-2jw)
  double num_real = coeffs->b0 + coeffs->b1 * cos_w + coeffs->b2 * cos_2w;
  double num_imag = -coeffs->b1 * sin_w - coeffs->b2 * sin_2w;

  // Denominator: 1 + a1*e^(-jw) + a2*e^(-2jw)
  double den_real = 1.0 + coeffs->a1 * cos_w + coeffs->a2 * cos_2w;
  double den_imag = -coeffs->a1 * sin_w - coeffs->a2 * sin_2w;

  // |H|^2 = |num|^2 / |den|^2
  double num_mag_sq = num_real * num_real + num_imag * num_imag;
  double den_mag_sq = den_real * den_real + den_imag * den_imag;

  if (den_mag_sq < 1e-20)
    return 0.0;

  double mag_sq = num_mag_sq / den_mag_sq;

  if (mag_sq < 1e-20)
    return -100.0;

  return 10.0 * log10(mag_sq);
}

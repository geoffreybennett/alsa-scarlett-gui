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
  "Notch",
  "Gain",
  "LP 6dB/oct",
  "HP 6dB/oct",
  "LS 6dB/oct",
  "HS 6dB/oct"
};

const char *biquad_type_name(BiquadFilterType type) {
  if (type < 0 || type >= BIQUAD_TYPE_COUNT)
    return "Unknown";
  return filter_type_names[type];
}

const char **biquad_get_type_names(void) {
  return filter_type_names;
}

gboolean biquad_type_uses_gain(BiquadFilterType type) {
  return type == BIQUAD_TYPE_PEAKING ||
         type == BIQUAD_TYPE_LOW_SHELF ||
         type == BIQUAD_TYPE_HIGH_SHELF ||
         type == BIQUAD_TYPE_GAIN ||
         type == BIQUAD_TYPE_LOW_SHELF_1 ||
         type == BIQUAD_TYPE_HIGH_SHELF_1;
}

gboolean biquad_type_uses_q(BiquadFilterType type) {
  // First-order filters and gain don't use Q
  return type != BIQUAD_TYPE_GAIN &&
         type != BIQUAD_TYPE_LOWPASS_1 &&
         type != BIQUAD_TYPE_HIGHPASS_1 &&
         type != BIQUAD_TYPE_LOW_SHELF_1 &&
         type != BIQUAD_TYPE_HIGH_SHELF_1;
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

    case BIQUAD_TYPE_GAIN:
      // Pure gain, no frequency dependence
      // gain_db uses 20*log10 scale (not 40 like peaking A parameter)
      coeffs->b0 = pow(10.0, params->gain_db / 20.0);
      coeffs->b1 = 0.0;
      coeffs->b2 = 0.0;
      coeffs->a1 = 0.0;
      coeffs->a2 = 0.0;
      return;

    case BIQUAD_TYPE_LOWPASS_1: {
      // First-order lowpass (6 dB/octave)
      double K = tan(M_PI * params->freq / sample_rate);
      double norm = 1.0 / (1.0 + K);
      coeffs->b0 = K * norm;
      coeffs->b1 = K * norm;
      coeffs->b2 = 0.0;
      coeffs->a1 = (K - 1.0) * norm;
      coeffs->a2 = 0.0;
      return;
    }

    case BIQUAD_TYPE_HIGHPASS_1: {
      // First-order highpass (6 dB/octave)
      double K = tan(M_PI * params->freq / sample_rate);
      double norm = 1.0 / (1.0 + K);
      coeffs->b0 = norm;
      coeffs->b1 = -norm;
      coeffs->b2 = 0.0;
      coeffs->a1 = (K - 1.0) * norm;
      coeffs->a2 = 0.0;
      return;
    }

    case BIQUAD_TYPE_LOW_SHELF_1: {
      // First-order low shelf
      // Ensure exact relationship: b0 - b1 = 1 - a1 (Nyquist gain = 1)
      double K = tan(M_PI * params->freq / sample_rate);
      double V = pow(10.0, fabs(params->gain_db) / 20.0);
      if (params->gain_db >= 0) {
        // Boost: a1 = (K-1)/(1+K)
        double denom = 1.0 + K;
        coeffs->a1 = (K - 1.0) / denom;
        double nyq_diff = 1.0 - coeffs->a1;  // Exact: b0 - b1 = nyq_diff
        double dc_sum = 2.0 * V * K / denom; // b0 + b1
        coeffs->b0 = (dc_sum + nyq_diff) / 2.0;
        coeffs->b1 = (dc_sum - nyq_diff) / 2.0;
      } else {
        // Cut: a1 = (V*K-1)/(1+V*K)
        double denom = 1.0 + V * K;
        coeffs->a1 = (V * K - 1.0) / denom;
        double nyq_diff = 1.0 - coeffs->a1;  // Exact: b0 - b1 = nyq_diff
        double dc_sum = 2.0 * K / denom;     // b0 + b1
        coeffs->b0 = (dc_sum + nyq_diff) / 2.0;
        coeffs->b1 = (dc_sum - nyq_diff) / 2.0;
      }
      coeffs->b2 = 0.0;
      coeffs->a2 = 0.0;
      return;
    }

    case BIQUAD_TYPE_HIGH_SHELF_1: {
      // First-order high shelf
      // Ensure exact relationship: b0 + b1 = 1 + a1 (DC gain = 1)
      double K = tan(M_PI * params->freq / sample_rate);
      double V = pow(10.0, fabs(params->gain_db) / 20.0);
      if (params->gain_db >= 0) {
        // Boost: a1 = (K-1)/(1+K)
        double denom = 1.0 + K;
        coeffs->a1 = (K - 1.0) / denom;
        double dc_sum = 1.0 + coeffs->a1;    // Exact: b0 + b1 = dc_sum
        double nyq_diff = 2.0 * V / denom;   // b0 - b1
        coeffs->b0 = (dc_sum + nyq_diff) / 2.0;
        coeffs->b1 = (dc_sum - nyq_diff) / 2.0;
      } else {
        // Cut: a1 = (K-V)/(V+K)
        double denom = V + K;
        coeffs->a1 = (K - V) / denom;
        double dc_sum = 1.0 + coeffs->a1;    // Exact: b0 + b1 = dc_sum
        double nyq_diff = 2.0 / denom;       // b0 - b1
        coeffs->b0 = (dc_sum + nyq_diff) / 2.0;
        coeffs->b1 = (dc_sum - nyq_diff) / 2.0;
      }
      coeffs->b2 = 0.0;
      coeffs->a2 = 0.0;
      return;
    }

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

void biquad_from_fixed_point(
  const long fixed[5],
  struct biquad_coeffs *coeffs
) {
  // Hardware format: [b0, b1, b2, -a1, -a2] * 2^28
  coeffs->b0 = (double)fixed[0] / BIQUAD_FIXED_POINT_SCALE;
  coeffs->b1 = (double)fixed[1] / BIQUAD_FIXED_POINT_SCALE;
  coeffs->b2 = (double)fixed[2] / BIQUAD_FIXED_POINT_SCALE;
  coeffs->a1 = -(double)fixed[3] / BIQUAD_FIXED_POINT_SCALE;
  coeffs->a2 = -(double)fixed[4] / BIQUAD_FIXED_POINT_SCALE;
}

// Tolerance for fixed-point round-trip (2^-28 ≈ 3.7e-9, allow some margin)
#define COEFF_TOL 1e-6

// Helper to check if two values are approximately equal
static gboolean approx_eq(double a, double b, double tol) {
  return fabs(a - b) < tol;
}

// Check coefficient equality with fixed-point tolerance
static gboolean coeff_eq(double a, double b) {
  return fabs(a - b) < COEFF_TOL;
}

// Analyze coefficients to determine filter parameters
gboolean biquad_analyze(
  const struct biquad_coeffs *coeffs,
  double sample_rate,
  struct biquad_params *params
) {
  double b0 = coeffs->b0;
  double b1 = coeffs->b1;
  double b2 = coeffs->b2;
  double a1 = coeffs->a1;
  double a2 = coeffs->a2;

  // Check for pure gain filter (b1, b2, a1, a2 all zero, b0 is gain)
  if (approx_eq(b1, 0.0, 0.001) && approx_eq(b2, 0.0, 0.001) &&
      approx_eq(a1, 0.0, 0.001) && approx_eq(a2, 0.0, 0.001) &&
      b0 > 0.0) {
    params->type = BIQUAD_TYPE_GAIN;
    params->freq = 1000.0;  // Arbitrary, not used
    params->q = 0.707;      // Arbitrary, not used
    params->gain_db = 20.0 * log10(b0);
    params->gain_db = CLAMP(params->gain_db,
                            -GAIN_DB_LIMIT, GAIN_DB_LIMIT);
    return TRUE;
  }

  // Check for first-order filters (b2 = 0, a2 = 0)
  if (approx_eq(b2, 0.0, 0.001) && approx_eq(a2, 0.0, 0.001)) {
    // First-order filter: H(z) = (b0 + b1*z^-1) / (1 + a1*z^-1)
    // K = (1 + a1) / (1 - a1) = tan(pi * fc / fs)
    double K = (1.0 + a1) / (1.0 - a1);
    if (K < 0.001) K = 0.001;
    double freq = atan(K) * sample_rate / M_PI;
    if (freq < 20.0) freq = 20.0;
    if (freq > 20000.0) freq = 20000.0;
    params->freq = freq;
    params->q = 0.707;  // Not used for first-order

    // First-order lowpass: b0 = b1
    if (coeff_eq(b0, b1)) {
      params->type = BIQUAD_TYPE_LOWPASS_1;
      params->gain_db = 0.0;
      return TRUE;
    }

    // First-order highpass: b0 = -b1
    if (coeff_eq(b0, -b1)) {
      params->type = BIQUAD_TYPE_HIGHPASS_1;
      params->gain_db = 0.0;
      return TRUE;
    }

    // Low shelf: b0 - b1 = 1 - a1 (Nyquist gain = 1)
    if (coeff_eq(b0 - b1, 1.0 - a1)) {
      params->type = BIQUAD_TYPE_LOW_SHELF_1;
      double dc_gain = (b0 + b1) / (1.0 + a1);
      params->gain_db = 20.0 * log10(fabs(dc_gain));
      if (params->gain_db < -24.0) params->gain_db = -24.0;
      if (params->gain_db > 24.0) params->gain_db = 24.0;

      // Recover K: boost uses a1 = (K-1)/(K+1), cut uses a1 = (V*K-1)/(V*K+1)
      double V = pow(10.0, fabs(params->gain_db) / 20.0);
      double K_term = (1.0 + a1) / (1.0 - a1);
      double K = (params->gain_db >= 0) ? K_term : K_term / V;
      if (K > 0)
        params->freq = atan(K) * sample_rate / M_PI;
      if (params->freq < 20.0) params->freq = 20.0;
      if (params->freq > 20000.0) params->freq = 20000.0;
      return TRUE;
    }

    // High shelf: b0 + b1 = 1 + a1 (DC gain = 1)
    if (coeff_eq(b0 + b1, 1.0 + a1)) {
      params->type = BIQUAD_TYPE_HIGH_SHELF_1;
      double nyq_gain = (b0 - b1) / (1.0 - a1);
      params->gain_db = 20.0 * log10(fabs(nyq_gain));
      if (params->gain_db < -24.0) params->gain_db = -24.0;
      if (params->gain_db > 24.0) params->gain_db = 24.0;

      // Recover K: boost uses a1 = (K-1)/(K+1), cut uses a1 = (K-V)/(K+V)
      double V = pow(10.0, fabs(params->gain_db) / 20.0);
      double K_term = (1.0 + a1) / (1.0 - a1);
      double K = (params->gain_db >= 0) ? K_term : K_term * V;
      if (K > 0)
        params->freq = atan(K) * sample_rate / M_PI;
      if (params->freq < 20.0) params->freq = 20.0;
      if (params->freq > 20000.0) params->freq = 20000.0;
      return TRUE;
    }

    // Unknown first-order, default to lowpass
    params->type = BIQUAD_TYPE_LOWPASS_1;
    params->gain_db = 0.0;
    return TRUE;
  }

  // For normalized biquad coefficients, a1_norm = a1/a0, so we need to
  // recover a0 to get the original cos_w0 = -a1/2.
  //
  // For all filter types: a0 = 2/(1+a2)
  // (derived from a2_norm = (1-x)/(1+x) where x=alpha or alpha/A)
  double a0 = 2.0 / (1.0 + a2);
  double cos_w0 = -a1 * a0 / 2.0;

  // Clamp cos_w0 to valid range
  if (cos_w0 < -1.0) cos_w0 = -1.0;
  if (cos_w0 > 1.0) cos_w0 = 1.0;

  double w0 = acos(cos_w0);
  double sin_w0 = sin(w0);
  double freq = w0 * sample_rate / (2.0 * M_PI);

  // Clamp frequency to valid range
  if (freq < 20.0) freq = 20.0;
  if (freq > 20000.0) freq = 20000.0;

  params->freq = freq;
  params->gain_db = 0.0;

  // Check DC and Nyquist gains early to detect shelf filters
  // For low shelf: H(DC) = A, H(Nyquist) = 1
  // For high shelf: H(DC) = 1, H(Nyquist) = A
  double dc_gain = (b0 + b1 + b2) / (1.0 + a1 + a2);
  double nyq_gain = (b0 - b1 + b2) / (1.0 - a1 + a2);

  // Low shelf: DC gain significantly different from 1, Nyquist ≈ 1
  // Exclude highpass (dc_gain ≈ 0) which would otherwise match with extreme cut
  if (fabs(dc_gain - 1.0) > 0.05 && fabs(dc_gain) > 0.01 &&
      approx_eq(nyq_gain, 1.0, 0.1)) {
    params->type = BIQUAD_TYPE_LOW_SHELF;
    // dc_gain = A² from cookbook, so gain_db = 20*log10(A²) = 40*log10(A)
    params->gain_db = 20.0 * log10(fabs(dc_gain));
    params->gain_db = CLAMP(params->gain_db,
                            -GAIN_DB_LIMIT, GAIN_DB_LIMIT);

    // For frequency formula, we need actual A = sqrt(dc_gain)
    double A = sqrt(fabs(dc_gain));

    // Derive cos_w0 from a1 and A for low shelf
    // The formula: num/den gives -cos_w0, so negate num
    double num = -(a1 * (A + 1) + (1 + a2) * (A - 1));
    double den = a1 * (A - 1) + (1 + a2) * (A + 1);
    double shelf_cos_w0 = 0.0;
    if (fabs(den) > 0.001) {
      shelf_cos_w0 = num / den;
      if (shelf_cos_w0 < -1.0) shelf_cos_w0 = -1.0;
      if (shelf_cos_w0 > 1.0) shelf_cos_w0 = 1.0;
      double shelf_w0 = acos(shelf_cos_w0);
      params->freq = shelf_w0 * sample_rate / (2.0 * M_PI);
    }

    // Extract Q from: (1-a2)/(1+a2) = sqrt(A)*sin_w0 / [Q * ((A+1)+(A-1)*cos_w0)]
    double shelf_sin_w0 = sqrt(1.0 - shelf_cos_w0 * shelf_cos_w0);
    double x = (A + 1) + (A - 1) * shelf_cos_w0;
    double y_over_x = (1.0 - a2) / (1.0 + a2);
    if (fabs(y_over_x * x) > 1e-10) {
      params->q = sqrt(A) * shelf_sin_w0 / (y_over_x * x);
    } else {
      params->q = 0.707;
    }
    if (params->q < 0.1) params->q = 0.1;
    if (params->q > 10.0) params->q = 10.0;
    return TRUE;
  }

  // High shelf: Nyquist gain significantly different from 1, DC ≈ 1
  // Exclude lowpass (nyq_gain ≈ 0) which would otherwise match with extreme cut
  if (fabs(nyq_gain - 1.0) > 0.05 && fabs(nyq_gain) > 0.01 &&
      approx_eq(dc_gain, 1.0, 0.1)) {
    params->type = BIQUAD_TYPE_HIGH_SHELF;
    // nyq_gain = A² from cookbook, so gain_db = 20*log10(A²) = 40*log10(A)
    params->gain_db = 20.0 * log10(fabs(nyq_gain));
    params->gain_db = CLAMP(params->gain_db,
                            -GAIN_DB_LIMIT, GAIN_DB_LIMIT);

    // For frequency formula, we need actual A = sqrt(nyq_gain)
    double A = sqrt(fabs(nyq_gain));

    // Derive cos_w0 from a1 and A for high shelf
    double num = a1 * (A + 1) - (1 + a2) * (A - 1);
    double den = a1 * (A - 1) - (1 + a2) * (A + 1);
    double shelf_cos_w0 = 0.0;
    if (fabs(den) > 0.001) {
      shelf_cos_w0 = num / den;
      if (shelf_cos_w0 < -1.0) shelf_cos_w0 = -1.0;
      if (shelf_cos_w0 > 1.0) shelf_cos_w0 = 1.0;
      double shelf_w0 = acos(shelf_cos_w0);
      params->freq = shelf_w0 * sample_rate / (2.0 * M_PI);
    }

    // Extract Q: for high shelf, x = (A+1) - (A-1)*cos_w0
    double shelf_sin_w0 = sqrt(1.0 - shelf_cos_w0 * shelf_cos_w0);
    double x = (A + 1) - (A - 1) * shelf_cos_w0;
    double y_over_x = (1.0 - a2) / (1.0 + a2);
    if (fabs(y_over_x * x) > 1e-10) {
      params->q = sqrt(A) * shelf_sin_w0 / (y_over_x * x);
    } else {
      params->q = 0.707;
    }
    if (params->q < 0.1) params->q = 0.1;
    if (params->q > 10.0) params->q = 10.0;

    return TRUE;
  }

  // Bandpass: b1 = 0, b0 = -b2
  if (coeff_eq(b1, 0.0) && coeff_eq(b0, -b2)) {
    params->type = BIQUAD_TYPE_BANDPASS;
    // alpha from normalized a2: a2_n = (1-alpha)/(1+alpha)
    double alpha = (1.0 - a2) / (1.0 + a2);
    if (alpha > 1e-10)
      params->q = sin_w0 / (2.0 * alpha);
    else
      params->q = 1.0;
    if (params->q < 0.1) params->q = 0.1;
    if (params->q > 10.0) params->q = 10.0;
    return TRUE;
  }

  // Peaking: b1 = a1 (both equal -2*cos_w0), but b0 ≠ b2
  if (coeff_eq(b1, a1) && !coeff_eq(b0, b2)) {
    params->type = BIQUAD_TYPE_PEAKING;
    // For peaking: b0 = 1 + alpha*A, b2 = 1 - alpha*A (before norm)
    // After norm by a0 = 1 + alpha/A:
    //   b0_n = (1 + alpha*A) / (1 + alpha/A)
    //   b2_n = (1 - alpha*A) / (1 + alpha/A)
    //   a2_n = (1 - alpha/A) / (1 + alpha/A)
    //
    // From a2: alpha/A = (1 - a2) / (1 + a2)
    // From b0-b2: alpha*A * 2 / a0 = b0 - b2
    //   so alpha*A = (b0 - b2) * (1 + alpha/A) / 2
    //              = (b0 - b2) * (1 + (1-a2)/(1+a2)) / 2
    //              = (b0 - b2) * (2/(1+a2)) / 2
    //              = (b0 - b2) / (1 + a2)

    // Direct computation without intermediate divisions:
    // A² = (b0 - b2) / (1 - a2)
    // alpha² = (b0 - b2) * (1 - a2) / (1 + a2)²
    double b_diff = b0 - b2;
    double a_diff = 1.0 - a2;

    double A_sq = b_diff / a_diff;
    double A = (A_sq > 0 && isfinite(A_sq)) ? sqrt(A_sq) : 1.0;

    double alpha_sq = b_diff * a_diff / ((1.0 + a2) * (1.0 + a2));
    double alpha = (alpha_sq > 0) ? sqrt(alpha_sq) : 0.001;

    params->gain_db = 40.0 * log10(A);
    params->gain_db = CLAMP(params->gain_db,
                            -GAIN_DB_LIMIT, GAIN_DB_LIMIT);

    if (alpha > 1e-10)
      params->q = sin_w0 / (2.0 * alpha);
    else
      params->q = 1.0;
    if (params->q < 0.1) params->q = 0.1;
    if (params->q > 10.0) params->q = 10.0;
    return TRUE;
  }

  // Notch: b0 = b2, b1 = a1
  if (coeff_eq(b0, b2) && coeff_eq(b1, a1)) {
    params->type = BIQUAD_TYPE_NOTCH;
    double alpha = (1.0 - a2) / (1.0 + a2);
    if (alpha > 1e-10)
      params->q = sin_w0 / (2.0 * alpha);
    else
      params->q = 1.0;
    if (params->q < 0.1) params->q = 0.1;
    if (params->q > 10.0) params->q = 10.0;
    return TRUE;
  }

  // Lowpass: b0 = b2, b1 = 2*b0
  if (coeff_eq(b0, b2) && coeff_eq(b1, 2.0 * b0)) {
    params->type = BIQUAD_TYPE_LOWPASS;
    double alpha = (1.0 - a2) / (1.0 + a2);
    if (alpha > 1e-10)
      params->q = sin_w0 / (2.0 * alpha);
    else
      params->q = 0.707;
    if (params->q < 0.1) params->q = 0.1;
    if (params->q > 10.0) params->q = 10.0;
    return TRUE;
  }

  // Highpass: b0 = b2, b1 = -2*b0
  if (coeff_eq(b0, b2) && coeff_eq(b1, -2.0 * b0)) {
    params->type = BIQUAD_TYPE_HIGHPASS;
    double alpha = (1.0 - a2) / (1.0 + a2);
    if (alpha > 1e-10)
      params->q = sin_w0 / (2.0 * alpha);
    else
      params->q = 0.707;
    if (params->q < 0.1) params->q = 0.1;
    if (params->q > 10.0) params->q = 10.0;
    return TRUE;
  }

  // Default to gain with 0dB if we can't identify (equivalent to bypass)
  params->type = BIQUAD_TYPE_GAIN;
  params->freq = 1000.0;
  params->q = 0.707;
  params->gain_db = 0.0;
  return TRUE;
}

// SPDX-FileCopyrightText: 2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <alsa/asoundlib.h>
#include <math.h>

static double db_to_linear(double db) {
  if (db <= SND_CTL_TLV_DB_GAIN_MUTE)
    return 0.0;
  return pow(10.0, db / 20.0);
}

static double linear_to_db(double linear) {
  if (linear <= 0.0)
    return SND_CTL_TLV_DB_GAIN_MUTE;
  return 20.0 * log10(linear);
}

int cdb_to_linear_value(
  int cdb, int min_val, int max_val, int min_cdb, int max_cdb
) {
  if (cdb <= min_cdb)
    return min_val;
  if (cdb >= max_cdb)
    return max_val;

  // Convert centidB to dB
  double db = (double)cdb / 100.0;
  double max_db = (double)max_cdb / 100.0;

  // Convert dB relative to max_db to linear scale 0.0-1.0
  double linear = db_to_linear(db - max_db);

  // Scale to full ALSA range
  double scaled = linear * (double)max_val;
  int value = (int)round(scaled);
  if (value < min_val)
    return min_val;
  if (value > max_val)
    return max_val;
  return value;
}

int linear_value_to_cdb(
  int value, int min_val, int max_val, int min_cdb, int max_cdb
) {
  if (value <= min_val)
    return min_cdb;
  if (value >= max_val)
    return max_cdb;

  // Convert to 0.0-1.0 linear scale
  double linear = (double)value / (double)max_val;
  double max_db = (double)max_cdb / 100.0;

  // Convert to dB relative to max_db and back to centidB
  int cdb = (int)round((linear_to_db(linear) + max_db) * 100.0);
  if (cdb < min_cdb)
    return min_cdb;
  if (cdb > max_cdb)
    return max_cdb;
  return cdb;
}

double linear_value_to_db(
  int value, int min_val, int max_val, int min_db, int max_db
) {
  if (value <= min_val)
    return min_db;
  if (value >= max_val)
    return max_db;

  // Convert to 0.0-1.0 linear scale
  double linear = (double)value / (double)max_val;

  // Convert to dB relative to max_db
  double db = linear_to_db(linear) + max_db;
  if (db < min_db)
    return min_db;
  if (db > max_db)
    return max_db;
  return db;
}

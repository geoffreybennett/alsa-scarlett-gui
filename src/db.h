// SPDX-FileCopyrightText: 2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

int cdb_to_linear_value(
  int cdb, int min_val, int max_val, int min_cdb, int max_cdb
);

int linear_value_to_cdb(
  int value, int min_val, int max_val, int min_cdb, int max_cdb
);

double linear_value_to_db(
  int value, int min_val, int max_val, int min_db, int max_db
);

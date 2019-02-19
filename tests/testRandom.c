/*******************************************************************************
 * This file is part of SWIFT.
 * Copyright (C) 2019 Matthieu Schaller (schaller@strw.leidenuniv.nl)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

/* Config parameters. */
#include "../config.h"

#include <fenv.h>

/* Local headers. */
#include "swift.h"

int main(int argc, char* argv[]) {

  /* Initialize CPU frequency, this also starts time. */
  unsigned long long cpufreq = 0;
  clocks_set_cpufreq(cpufreq);

/* Choke on FPEs */
#ifdef HAVE_FE_ENABLE_EXCEPT
  feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#endif

  /* Get some randomness going */
  const int seed = time(NULL);
  message("Seed = %d", seed);
  srand(seed);

  

  /* Time-step size */
  const int time_bin = 29;

  /* Try a few different values for the ID */
  for (int i = 0; i < 20; ++i) {

    const long long id = rand() * (1LL << 31) + rand();
    const integertime_t increment = (1LL << time_bin);

    message("Testing id=%lld time_bin=%d", id, time_bin);
    char buffer[32];
    snprintf(buffer, sizeof(char)*32, "fileII%i.txt", i);

    FILE *fp;
    fp = fopen(buffer,"w");

    double total = 0., total2 = 0.;
    int count = 0;

    /* Pearson correlation variables */
    double sum_previous_current = 0.;
    double previous = 0.;
    
    message("Max nr timesteps = %lld",max_nr_timesteps);

    /* Check that the numbers are uniform over the full-range of useful
     * time-steps */
    for (integertime_t ti_current = 0LL; ti_current < max_nr_timesteps;
         ti_current += increment) {

      ti_current += increment;

      const double r =
          random_unit_interval(id, ti_current, random_number_star_formation);

      total += r;
      total2 += r * r;
      count++;
      const unsigned int test = 127LL*(ti_current - 1LL) + 124429LL;
      fprintf(fp, "%f   %lld    %lld\n", r, (test) % 1514917LL, ti_current );

      /* For the pearson correlation */
      sum_previous_current += r * previous;
      previous = r;
    }

    fclose(fp);
    const double mean = total / (double)count;
    const double var = total2 / (double)count - mean * mean;

    double mean_xy = sum_previous_current / ( (double)count -1.f);
    double correlation = (mean_xy-mean*mean)/var;
    message("Correlation = %f", correlation);

    /* Verify that the mean and variance match the expected values for a uniform
     * distribution */
    if ((fabs(mean - 0.5) / 0.5 > 2e-4) ||
        (fabs(var - 1. / 12.) / (1. / 12.) > 1e-3) ||
        (fabs(correlation) > 3e-4)) {
      message("Test failed!");
      message("Result:    count=%d mean=%f var=%f, correlation=%f", count, mean, var, correlation);
      message("Expected:  count=%d mean=%f var=%f, correlation=%f", count, 0.5f, 1. / 12., 0.);
      return 1;
    }
  }

  return 0;
}

/*******************************************************************************
 * This file is part of SWIFT.
 * Copyright (c) 2013 Pedro Gonnet (pedro.gonnet@durham.ac.uk)
 *                    Matthieu Schaller (matthieu.schaller@durham.ac.uk)
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
#ifndef SWIFT_MULTI_SOFTENING_GRAVITY_IACT_H
#define SWIFT_MULTI_SOFTENING_GRAVITY_IACT_H

/* Includes. */
#include "kernel_gravity.h"
#include "kernel_long_gravity.h"
#include "multipole.h"

/* Standard headers */
#include <float.h>

/**
 * @brief Computes the intensity of the force at a point generated by a
 * point-mass.
 *
 * The returned quantity needs to be multiplied by the distance vector to obtain
 * the force vector.
 *
 * @param r2 Square of the distance to the point-mass.
 * @param h2 Square of the softening length.
 * @param h_inv Inverse of the softening length.
 * @param h_inv3 Cube of the inverse of the softening length.
 * @param mass Mass of the point-mass.
 * @param f_ij (return) The force intensity.
 * @param pot_ij (return) The potential.
 */
__attribute__((always_inline, nonnull)) INLINE static void
runner_iact_grav_pp_full(const MyFloat r2, const MyFloat h2, const MyFloat h_inv,
                         const MyFloat h_inv3, const MyFloat mass,
                         MyFloat *restrict f_ij, MyFloat *restrict pot_ij) {

  /* Get the inverse distance */
  const MyFloat r_inv = 1. / sqrt(r2 + FLT_MIN);

  /* Should we soften ? */
  if (r2 >= h2) {

    /* Get Newtonian gravity */
    *f_ij = mass * r_inv * r_inv * r_inv;
    *pot_ij = -mass * r_inv;

  } else {

    const double r = r2 * r_inv;
    const double ui = r * h_inv;
    double W_f_ij, W_pot_ij;
    kernel_grav_eval_force_double(ui, &W_f_ij);
    kernel_grav_eval_pot_double(ui, &W_pot_ij);

    /* Get softened gravity */
    *f_ij = mass * h_inv3 * W_f_ij;
    *pot_ij = mass * h_inv * W_pot_ij;
  }
}

/**
 * @brief Computes the intensity of the force at a point generated by a
 * point-mass truncated for long-distance periodicity.
 *
 * The returned quantity needs to be multiplied by the distance vector to obtain
 * the force vector.
 *
 * @param r2 Square of the distance to the point-mass.
 * @param h2 Square of the softening length.
 * @param h_inv Inverse of the softening length.
 * @param h_inv3 Cube of the inverse of the softening length.
 * @param mass Mass of the point-mass.
 * @param r_s_inv Inverse of the mesh smoothing scale.
 * @param f_ij (return) The force intensity.
 * @param pot_ij (return) The potential.
 */
__attribute__((always_inline, nonnull)) INLINE static void
runner_iact_grav_pp_truncated(const MyFloat r2, const MyFloat h2, const MyFloat h_inv,
                              const MyFloat h_inv3, const MyFloat mass,
                              const float r_s_inv, MyFloat *restrict f_ij,
                              MyFloat *restrict pot_ij) {

  /* Get the inverse distance */
  const MyFloat r_inv = 1. / sqrt(r2 + FLT_MIN);
  const MyFloat r = r2 * r_inv;

  /* Should we soften ? */
  if (r2 >= h2) {

    /* Get Newtonian gravity */
    *f_ij = mass * r_inv * r_inv * r_inv;
    *pot_ij = -mass * r_inv;

  } else {

    const float ui = (float)(r * h_inv);
    const float W_f_ij = kernel_grav_force_eval(ui);
    const float W_pot_ij = kernel_grav_pot_eval(ui);

    /* Get softened gravity */
    *f_ij = mass * h_inv3 * W_f_ij;
    *pot_ij = mass * h_inv * W_pot_ij;
  }

  /* Get long-range correction */
  const float u_lr = r * r_s_inv;
  float corr_f_lr, corr_pot_lr;
  kernel_long_grav_eval(u_lr, &corr_f_lr, &corr_pot_lr);
  *f_ij *= corr_f_lr;
  *pot_ij *= corr_pot_lr;
}

/**
 * @brief Computes the forces at a point generated by a multipole.
 *
 * @param r_x x-component of the distance vector to the multipole.
 * @param r_y y-component of the distance vector to the multipole.
 * @param r_z z-component of the distance vector to the multipole.
 * @param r2 Square of the distance vector to the multipole.
 * @param h The softening length.
 * @param h_inv Inverse of the softening length.
 * @param m The multipole.
 * @param f_x (return) The x-component of the acceleration.
 * @param f_y (return) The y-component of the acceleration.
 * @param f_z (return) The z-component of the acceleration.
 * @param pot (return) The potential.
 */
__attribute__((always_inline, nonnull)) INLINE static void
runner_iact_grav_pm_full(const float r_x, const float r_y, const float r_z,
                         const float r2, const float h, const float h_inv,
                         const struct multipole *m, float *restrict f_x,
                         float *restrict f_y, float *restrict f_z,
                         float *restrict pot) {

  /* Use the M2P kernel */
  struct reduced_grav_tensor l;
  l.F_000 = 0.f;
  l.F_100 = 0.f;
  l.F_010 = 0.f;
  l.F_001 = 0.f;

  gravity_M2P(m, r_x, r_y, r_z, r2, h, /*periodic=*/0, /*rs_inv=*/0.f, &l);

  /* Write back */
  *pot = l.F_000;
  *f_x = l.F_100;
  *f_y = l.F_010;
  *f_z = l.F_001;
}

/**
 * @brief Computes the forces at a point generated by a multipole, truncated for
 * long-range periodicity.
 *
 * @param r_x x-component of the distance vector to the multipole.
 * @param r_y y-component of the distance vector to the multipole.
 * @param r_z z-component of the distance vector to the multipole.
 * @param r2 Square of the distance vector to the multipole.
 * @param h The softening length.
 * @param h_inv Inverse of the softening length.
 * @param r_s_inv The inverse of the gravity mesh-smoothing scale.
 * @param m The multipole.
 * @param f_x (return) The x-component of the acceleration.
 * @param f_y (return) The y-component of the acceleration.
 * @param f_z (return) The z-component of the acceleration.
 * @param pot (return) The potential.
 */
__attribute__((always_inline, nonnull)) INLINE static void
runner_iact_grav_pm_truncated(const float r_x, const float r_y, const float r_z,
                              const float r2, const float h, const float h_inv,
                              const float r_s_inv, const struct multipole *m,
                              float *restrict f_x, float *restrict f_y,
                              float *restrict f_z, float *restrict pot) {

  /* Use the M2P kernel */
  struct reduced_grav_tensor l;
  l.F_000 = 0.f;
  l.F_100 = 0.f;
  l.F_010 = 0.f;
  l.F_001 = 0.f;

  gravity_M2P(m, r_x, r_y, r_z, r2, h, /*periodic=*/1, r_s_inv, &l);

  /* Write back */
  *pot = l.F_000;
  *f_x = l.F_100;
  *f_y = l.F_010;
  *f_z = l.F_001;
}

#endif /* SWIFT_MULTI_SOFTENING_GRAVITY_IACT_H */

/*******************************************************************************
 * This file is part of SWIFT.
 * Coypright (c) 2015 Matthieu Schaller (matthieu.schaller@durham.ac.uk)
 *               2016, 2017 Bert Vandenbroucke (bert.vandenbroucke@gmail.com)
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
#ifndef SWIFT_GIZMO_MFV_HYDRO_H
#define SWIFT_GIZMO_MFV_HYDRO_H

#include "adiabatic_index.h"
#include "approx_math.h"
#include "cosmology.h"
#include "equation_of_state.h"
#include "hydro_gradients.h"
#include "hydro_properties.h"
#include "hydro_space.h"
#include "hydro_unphysical.h"
#include "hydro_velocities.h"
#include "minmax.h"
#include "riemann.h"

#include <float.h>

//#define GIZMO_LLOYD_ITERATION

/**
 * @brief Computes the hydro time-step of a given particle
 *
 * @param p Pointer to the particle data.
 * @param xp Pointer to the extended particle data.
 * @param hydro_properties Pointer to the hydro parameters.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float hydro_compute_timestep(
    const struct part* restrict p, const struct xpart* restrict xp,
    const struct hydro_props* restrict hydro_properties,
    const struct cosmology* restrict cosmo) {

  const float CFL_condition = hydro_properties->CFL_condition;

#ifdef GIZMO_LLOYD_ITERATION
  return CFL_condition;
#endif

  /* v_full is the actual velocity of the particle, primitives.v is its
     hydrodynamical velocity. The time step depends on the relative difference
     of the two. */
  float vrel[3];
  vrel[0] = p->primitives.v[0] - xp->v_full[0];
  vrel[1] = p->primitives.v[1] - xp->v_full[1];
  vrel[2] = p->primitives.v[2] - xp->v_full[2];
  float vmax =
      sqrtf(vrel[0] * vrel[0] + vrel[1] * vrel[1] + vrel[2] * vrel[2]) +
      sqrtf(hydro_gamma * p->primitives.P / p->primitives.rho);
  vmax = max(vmax, p->timestepvars.vmax);

  // MATTHIEU: Bert is this correct? Do we need more cosmology terms here?
  const float psize = powf(p->geometry.volume / hydro_dimension_unit_sphere,
                           hydro_dimension_inv);
  float dt = FLT_MAX;
  if (vmax > 0.) {
    dt = psize / vmax;
  }
  return cosmo->a * cosmo->a * CFL_condition * dt;
}

/**
 * @brief Does some extra hydro operations once the actual physical time step
 * for the particle is known.
 *
 * This method is no longer used, as Gizmo is now unaware of the actual particle
 * time step.
 *
 * @param p The particle to act upon.
 * @param dt Physical time step of the particle during the next step.
 */
__attribute__((always_inline)) INLINE static void hydro_timestep_extra(
    struct part* p, float dt) {

#ifdef SWIFT_DEBUG_CHECKS
  if (dt == 0.) {
    error("Zero time step assigned to particle!");
  }

  if (dt != dt) {
    error("NaN time step assigned to particle!");
  }
#endif
}

/**
 * @brief Initialises the particles for the first time
 *
 * This function is called only once just after the ICs have been
 * read in to do some conversions.
 *
 * In this case, we copy the particle velocities into the corresponding
 * primitive variable field. We do this because the particle velocities in GIZMO
 * can be independent of the actual fluid velocity. The latter is stored as a
 * primitive variable and integrated using the linear momentum, a conserved
 * variable.
 *
 * @param p The particle to act upon
 * @param xp The extended particle data to act upon
 */
__attribute__((always_inline)) INLINE static void hydro_first_init_part(
    struct part* p, struct xpart* xp) {

  const float mass = p->conserved.mass;

  p->time_bin = 0;
  p->wakeup = time_bin_not_awake;

  p->primitives.v[0] = p->v[0];
  p->primitives.v[1] = p->v[1];
  p->primitives.v[2] = p->v[2];

  /* we can already initialize the momentum */
  p->conserved.momentum[0] = mass * p->primitives.v[0];
  p->conserved.momentum[1] = mass * p->primitives.v[1];
  p->conserved.momentum[2] = mass * p->primitives.v[2];

/* and the thermal energy */
/* remember that we store the total thermal energy, not the specific thermal
   energy (as in Gadget) */
#if defined(EOS_ISOTHERMAL_GAS)
  /* this overwrites the internal energy from the initial condition file
   * Note that we call the EoS function just to get the constant u here. */
  p->conserved.energy = mass * gas_internal_energy_from_entropy(0.f, 0.f);
#else
  p->conserved.energy *= mass;
#endif

#ifdef GIZMO_TOTAL_ENERGY
  /* add the total kinetic energy */
  p->conserved.energy += 0.5f * (p->conserved.momentum[0] * p->primitives.v[0] +
                                 p->conserved.momentum[1] * p->primitives.v[1] +
                                 p->conserved.momentum[2] * p->primitives.v[2]);
#endif

#ifdef GIZMO_LLOYD_ITERATION
  /* overwrite all variables to make sure they have safe values */
  p->primitives.rho = 1.;
  p->primitives.v[0] = 0.;
  p->primitives.v[1] = 0.;
  p->primitives.v[2] = 0.;
  p->primitives.P = 1.;

  p->conserved.mass = 1.;
  p->conserved.momentum[0] = 0.;
  p->conserved.momentum[1] = 0.;
  p->conserved.momentum[2] = 0.;
  p->conserved.energy = 1.;

  p->v[0] = 0.;
  p->v[1] = 0.;
  p->v[2] = 0.;
#endif

  /* initialize the particle velocity based on the primitive fluid velocity */
  hydro_velocities_init(p, xp);

  /* ignore accelerations present in the initial condition */
  p->a_hydro[0] = 0.0f;
  p->a_hydro[1] = 0.0f;
  p->a_hydro[2] = 0.0f;

  /* we cannot initialize wcorr in init_part, as init_part gets called every
     time the density loop is repeated, and the whole point of storing wcorr
     is to have a way of remembering that we need more neighbours for this
     particle */
  p->density.wcorr = 1.0f;
}

/**
 * @brief Prepares a particle for the volume calculation.
 *
 * Simply makes sure all necessary variables are initialized to zero.
 *
 * @param p The particle to act upon
 * @param hs #hydro_space containing hydro specific space information.
 */
__attribute__((always_inline)) INLINE static void hydro_init_part(
    struct part* p, const struct hydro_space* hs) {

  p->density.wcount = 0.0f;
  p->density.wcount_dh = 0.0f;
  p->geometry.volume = 0.0f;
  p->geometry.matrix_E[0][0] = 0.0f;
  p->geometry.matrix_E[0][1] = 0.0f;
  p->geometry.matrix_E[0][2] = 0.0f;
  p->geometry.matrix_E[1][0] = 0.0f;
  p->geometry.matrix_E[1][1] = 0.0f;
  p->geometry.matrix_E[1][2] = 0.0f;
  p->geometry.matrix_E[2][0] = 0.0f;
  p->geometry.matrix_E[2][1] = 0.0f;
  p->geometry.matrix_E[2][2] = 0.0f;
  p->geometry.centroid[0] = 0.0f;
  p->geometry.centroid[1] = 0.0f;
  p->geometry.centroid[2] = 0.0f;
}

/**
 * @brief Finishes the volume calculation.
 *
 * Multiplies the density and number of neighbours by the appropiate constants
 * and adds the self-contribution term. Calculates the volume and uses it to
 * update the primitive variables (based on the conserved variables). The latter
 * should only be done for active particles. This is okay, since this method is
 * only called for active particles.
 *
 * Multiplies the components of the matrix E with the appropriate constants and
 * inverts it. Initializes the variables used during the gradient loop. This
 * cannot be done in hydro_prepare_force, since that method is called for all
 * particles, and not just the active ones. If we would initialize the
 * variables there, gradients for passive particles would be zero, while we
 * actually use the old gradients in the flux calculation between active and
 * passive particles.
 *
 * @param p The particle to act upon.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static void hydro_end_density(
    struct part* restrict p, const struct cosmology* cosmo) {

  /* Some smoothing length multiples. */
  const float h = p->h;
  const float ih = 1.0f / h;
  const float ihdim = pow_dimension(ih);
  const float ihdim_plus_one = ihdim * ih;

  /* Final operation on the density. */
  p->density.wcount += kernel_root;
  p->density.wcount *= ihdim;

  p->density.wcount_dh -= hydro_dimension * kernel_root;
  p->density.wcount_dh *= ihdim_plus_one;

  /* Final operation on the geometry. */
  /* we multiply with the smoothing kernel normalization ih3 and calculate the
   * volume */
  const float volume = 1.f / (ihdim * (p->geometry.volume + kernel_root));
  p->geometry.volume = volume;

  /* we multiply with the smoothing kernel normalization */
  p->geometry.matrix_E[0][0] = ihdim * p->geometry.matrix_E[0][0];
  p->geometry.matrix_E[0][1] = ihdim * p->geometry.matrix_E[0][1];
  p->geometry.matrix_E[0][2] = ihdim * p->geometry.matrix_E[0][2];
  p->geometry.matrix_E[1][0] = ihdim * p->geometry.matrix_E[1][0];
  p->geometry.matrix_E[1][1] = ihdim * p->geometry.matrix_E[1][1];
  p->geometry.matrix_E[1][2] = ihdim * p->geometry.matrix_E[1][2];
  p->geometry.matrix_E[2][0] = ihdim * p->geometry.matrix_E[2][0];
  p->geometry.matrix_E[2][1] = ihdim * p->geometry.matrix_E[2][1];
  p->geometry.matrix_E[2][2] = ihdim * p->geometry.matrix_E[2][2];

  p->geometry.centroid[0] *= kernel_norm;
  p->geometry.centroid[1] *= kernel_norm;
  p->geometry.centroid[2] *= kernel_norm;

  p->geometry.centroid[0] /= p->density.wcount;
  p->geometry.centroid[1] /= p->density.wcount;
  p->geometry.centroid[2] /= p->density.wcount;

  /* Check the condition number to see if we have a stable geometry. */
  float condition_number_E = 0.0f;
  int i, j;
  for (i = 0; i < 3; ++i) {
    for (j = 0; j < 3; ++j) {
      condition_number_E +=
          p->geometry.matrix_E[i][j] * p->geometry.matrix_E[i][j];
    }
  }

  invert_dimension_by_dimension_matrix(p->geometry.matrix_E);

  float condition_number_Einv = 0.0f;
  for (i = 0; i < 3; ++i) {
    for (j = 0; j < 3; ++j) {
      condition_number_Einv +=
          p->geometry.matrix_E[i][j] * p->geometry.matrix_E[i][j];
    }
  }

  float condition_number =
      hydro_dimension_inv * sqrtf(condition_number_E * condition_number_Einv);

  if (condition_number > const_gizmo_max_condition_number &&
      p->density.wcorr > const_gizmo_min_wcorr) {
#ifdef GIZMO_PATHOLOGICAL_ERROR
    error("Condition number larger than %g (%g)!",
          const_gizmo_max_condition_number, condition_number);
#endif
#ifdef GIZMO_PATHOLOGICAL_WARNING
    message("Condition number too large: %g (> %g, p->id: %llu)!",
            condition_number, const_gizmo_max_condition_number, p->id);
#endif
    /* add a correction to the number of neighbours for this particle */
    p->density.wcorr *= const_gizmo_w_correction_factor;
  }

  hydro_gradients_init(p);

  /* compute primitive variables */
  /* eqns (3)-(5) */
  const float m = p->conserved.mass;

#ifdef SWIFT_DEBUG_CHECKS
  if (m < 0.) {
    error("Mass is negative!");
  }

  if (volume == 0.) {
    error("Volume is 0!");
  }
#endif

  // MATTHIEU: Bert is this correct? Do we need cosmology terms here?
  float momentum[3];
  momentum[0] = p->conserved.momentum[0];
  momentum[1] = p->conserved.momentum[1];
  momentum[2] = p->conserved.momentum[2];
  p->primitives.rho = m / volume;
  if (m == 0.) {
    p->primitives.v[0] = 0.;
    p->primitives.v[1] = 0.;
    p->primitives.v[2] = 0.;
  } else {
    p->primitives.v[0] = momentum[0] / m;
    p->primitives.v[1] = momentum[1] / m;
    p->primitives.v[2] = momentum[2] / m;
  }

#ifdef EOS_ISOTHERMAL_GAS
  /* although the pressure is not formally used anywhere if an isothermal eos
     has been selected, we still make sure it is set to the correct value */
  p->primitives.P = gas_pressure_from_internal_energy(p->primitives.rho, 0.);
#else

  float energy = p->conserved.energy;

#ifdef GIZMO_TOTAL_ENERGY
  /* subtract the kinetic energy; we want the thermal energy */
  energy -= 0.5f * (momentum[0] * p->primitives.v[0] +
                    momentum[1] * p->primitives.v[1] +
                    momentum[2] * p->primitives.v[2]);
#endif

  /* energy contains the total thermal energy, we want the specific energy.
     this is why we divide by the volume, and not by the density */
  p->primitives.P = hydro_gamma_minus_one * energy / volume;
#endif

  /* sanity checks */
  gizmo_check_physical_quantities("density", "pressure", p->primitives.rho,
                                  p->primitives.v[0], p->primitives.v[1],
                                  p->primitives.v[2], p->primitives.P);

#ifdef GIZMO_LLOYD_ITERATION
  /* overwrite primitive variables to make sure they still have safe values */
  p->primitives.rho = 1.;
  p->primitives.v[0] = 0.;
  p->primitives.v[1] = 0.;
  p->primitives.v[2] = 0.;
  p->primitives.P = 1.;
#endif

  /* Add a correction factor to wcount (to force a neighbour number increase if
     the geometry matrix is close to singular) */
  p->density.wcount *= p->density.wcorr;
  p->density.wcount_dh *= p->density.wcorr;
}

/**
 * @brief Sets all particle fields to sensible values when the #part has 0 ngbs.
 *
 * @param p The particle to act upon
 * @param xp The extended particle data to act upon
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static void hydro_part_has_no_neighbours(
    struct part* restrict p, struct xpart* restrict xp,
    const struct cosmology* cosmo) {

  /* Some smoothing length multiples. */
  const float h = p->h;
  const float h_inv = 1.0f / h;                 /* 1/h */
  const float h_inv_dim = pow_dimension(h_inv); /* 1/h^d */

  /* Re-set problematic values */
  p->density.wcount = kernel_root * h_inv_dim;
  p->density.wcount_dh = 0.f;
  p->geometry.volume = 1.0f;
  p->geometry.matrix_E[0][0] = 1.0f;
  p->geometry.matrix_E[0][1] = 0.0f;
  p->geometry.matrix_E[0][2] = 0.0f;
  p->geometry.matrix_E[1][0] = 0.0f;
  p->geometry.matrix_E[1][1] = 1.0f;
  p->geometry.matrix_E[1][2] = 0.0f;
  p->geometry.matrix_E[2][0] = 0.0f;
  p->geometry.matrix_E[2][1] = 0.0f;
  p->geometry.matrix_E[2][2] = 1.0f;
  /* centroid is relative w.r.t. particle position */
  /* by setting the centroid to 0.0f, we make sure no velocity correction is
     applied */
  p->geometry.centroid[0] = 0.0f;
  p->geometry.centroid[1] = 0.0f;
  p->geometry.centroid[2] = 0.0f;
}

/**
 * @brief Prepare a particle for the gradient calculation.
 *
 * This function is called after the density loop and before the gradient loop.
 *
 * We use it to set the physical timestep for the particle and to copy the
 * actual velocities, which we need to boost our interfaces during the flux
 * calculation. We also initialize the variables used for the time step
 * calculation.
 *
 * @param p The particle to act upon.
 * @param xp The extended particle data to act upon.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static void hydro_prepare_gradient(
    struct part* restrict p, struct xpart* restrict xp,
    const struct cosmology* cosmo) {

  /* Initialize time step criterion variables */
  p->timestepvars.vmax = 0.;

  // MATTHIEU: Bert is this correct? Do we need cosmology terms here?

  /* Set the actual velocity of the particle */
  hydro_velocities_prepare_force(p, xp);
}

/**
 * @brief Finishes the gradient calculation.
 *
 * Just a wrapper around hydro_gradients_finalize, which can be an empty method,
 * in which case no gradients are used.
 *
 * @param p The particle to act upon.
 */
__attribute__((always_inline)) INLINE static void hydro_end_gradient(
    struct part* p) {

  hydro_gradients_finalize(p);

#ifdef GIZMO_LLOYD_ITERATION
  /* reset the gradients to zero, as we don't want them */
  hydro_gradients_init(p);
#endif
}

/**
 * @brief Prepare a particle for the force calculation.
 *
 * This function is called in the ghost task to convert some quantities coming
 * from the density loop over neighbours into quantities ready to be used in the
 * force loop over neighbours. Quantities are typically read from the density
 * sub-structure and written to the force sub-structure.
 * Examples of calculations done here include the calculation of viscosity term
 * constants, thermal conduction terms, hydro conversions, etc.
 *
 * @param p The particle to act upon
 * @param xp The extended particle data to act upon
 * @param cosmo The current cosmological model.
 * @param hydro_props Hydrodynamic properties.
 * @param dt_alpha The time-step used to evolve non-cosmological quantities such
 *                 as the artificial viscosity.
 */
__attribute__((always_inline)) INLINE static void hydro_prepare_force(
    struct part* restrict p, struct xpart* restrict xp,
    const struct cosmology* cosmo, const struct hydro_props* hydro_props,
    const float dt_alpha) {

  /* Initialise values that are used in the force loop */
  p->gravity.mflux[0] = 0.0f;
  p->gravity.mflux[1] = 0.0f;
  p->gravity.mflux[2] = 0.0f;

  p->conserved.flux.mass = 0.0f;
  p->conserved.flux.momentum[0] = 0.0f;
  p->conserved.flux.momentum[1] = 0.0f;
  p->conserved.flux.momentum[2] = 0.0f;
  p->conserved.flux.energy = 0.0f;
}

/**
 * @brief Reset acceleration fields of a particle
 *
 * This is actually not necessary for GIZMO, since we just set the accelerations
 * after the flux calculation.
 *
 * @param p The particle to act upon.
 */
__attribute__((always_inline)) INLINE static void hydro_reset_acceleration(
    struct part* p) {

  /* Reset the acceleration. */
  p->a_hydro[0] = 0.0f;
  p->a_hydro[1] = 0.0f;
  p->a_hydro[2] = 0.0f;

  /* Reset the time derivatives. */
  p->force.h_dt = 0.0f;
}

/**
 * @brief Resets the variables that are required for a gradient calculation.
 *
 * This function is called after hydro_prepare_gradient.
 *
 * @param p The particle to act upon.
 * @param xp The extended particle data to act upon.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static void hydro_reset_gradient(
    struct part* restrict p) {}

/**
 * @brief Sets the values to be predicted in the drifts to their values at a
 * kick time
 *
 * @param p The particle.
 * @param xp The extended data of this particle.
 */
__attribute__((always_inline)) INLINE static void hydro_reset_predicted_values(
    struct part* restrict p, const struct xpart* restrict xp) {}

/**
 * @brief Converts the hydrodynamic variables from the initial condition file to
 * conserved variables that can be used during the integration
 *
 * We no longer do this, as the mass needs to be provided in the initial
 * condition file, and the mass alone is enough to initialize all conserved
 * variables. This is now done in hydro_first_init_part.
 *
 * @param p The particle to act upon.
 */
__attribute__((always_inline)) INLINE static void hydro_convert_quantities(
    struct part* p, struct xpart* xp, const struct cosmology* cosmo,
    const struct hydro_props* hydro_props) {

  p->conserved.energy /= cosmo->a_factor_internal_energy;
}

/**
 * @brief Extra operations to be done during the drift
 *
 * @param p Particle to act upon.
 * @param xp The extended particle data to act upon.
 * @param dt_drift The drift time-step for positions.
 * @param dt_therm The drift time-step for thermal quantities.
 */
__attribute__((always_inline)) INLINE static void hydro_predict_extra(
    struct part* p, struct xpart* xp, float dt_drift, float dt_therm) {

#ifdef GIZMO_LLOYD_ITERATION
  return;
#endif

  const float h_inv = 1.0f / p->h;

  /* Predict smoothing length */
  const float w1 = p->force.h_dt * h_inv * dt_drift;
  float h_corr;
  if (fabsf(w1) < 0.2f)
    h_corr = approx_expf(w1); /* 4th order expansion of exp(w) */
  else
    h_corr = expf(w1);

  /* Limit the smoothing length correction (and make sure it is always
     positive). */
  if (h_corr < 2.0f && h_corr > 0.) {
    p->h *= h_corr;
  }

  /* drift the primitive variables based on the old fluxes */
  if (p->geometry.volume > 0.) {
    p->primitives.rho += p->conserved.flux.mass * dt_therm / p->geometry.volume;
  }

  if (p->conserved.mass > 0.) {
    p->primitives.v[0] +=
        p->conserved.flux.momentum[0] * dt_therm / p->conserved.mass;
    p->primitives.v[1] +=
        p->conserved.flux.momentum[1] * dt_therm / p->conserved.mass;
    p->primitives.v[2] +=
        p->conserved.flux.momentum[2] * dt_therm / p->conserved.mass;

#if !defined(EOS_ISOTHERMAL_GAS)
#ifdef GIZMO_TOTAL_ENERGY
    const float Etot =
        p->conserved.energy + p->conserved.flux.energy * dt_therm;
    const float v2 = (p->primitives.v[0] * p->primitives.v[0] +
                      p->primitives.v[1] * p->primitives.v[1] +
                      p->primitives.v[2] * p->primitives.v[2]);
    const float u = (Etot / p->conserved.mass - 0.5 * v2);
#else
    const float u =
        (p->conserved.energy + p->conserved.flux.energy * dt_therm) /
        p->conserved.mass;
#endif
    p->primitives.P = hydro_gamma_minus_one * u * p->primitives.rho;
#endif
  }

  /* we use a sneaky way to get the gravitational contribution to the
     velocity update */
  p->primitives.v[0] += p->v[0] - xp->v_full[0];
  p->primitives.v[1] += p->v[1] - xp->v_full[1];
  p->primitives.v[2] += p->v[2] - xp->v_full[2];

#ifdef SWIFT_DEBUG_CHECKS
  if (p->h <= 0.) {
    error("Zero or negative smoothing length (%g)!", p->h);
  }
#endif

  gizmo_check_physical_quantities("density", "pressure", p->primitives.rho,
                                  p->primitives.v[0], p->primitives.v[1],
                                  p->primitives.v[2], p->primitives.P);
}

/**
 * @brief Set the particle acceleration after the flux loop
 *
 * We use the new conserved variables to calculate the new velocity of the
 * particle, and use that to derive the change of the velocity over the particle
 * time step.
 *
 * If the particle time step is zero, we set the accelerations to zero. This
 * should only happen at the start of the simulation.
 *
 * @param p Particle to act upon.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static void hydro_end_force(
    struct part* p, const struct cosmology* cosmo) {

  /* set the variables that are used to drift the primitive variables */

  // MATTHIEU: Bert is this correct? Do we need cosmology terms here?
  hydro_velocities_end_force(p);
}

/**
 * @brief Extra operations done during the kick
 *
 * @param p Particle to act upon.
 * @param xp Extended particle data to act upon.
 * @param dt_therm Thermal energy time-step @f$\frac{dt}{a^2}@f$.
 * @param dt_grav Gravity time-step @f$\frac{dt}{a}@f$.
 * @param dt_hydro Hydro acceleration time-step
 * @f$\frac{dt}{a^{3(\gamma{}-1)}}@f$.
 * @param dt_kick_corr Gravity correction time-step @f$adt@f$.
 * @param cosmo Cosmology.
 * @param hydro_props Additional hydro properties.
 */
__attribute__((always_inline)) INLINE static void hydro_kick_extra(
    struct part* p, struct xpart* xp, float dt_therm, float dt_grav,
    float dt_hydro, float dt_kick_corr, const struct cosmology* cosmo,
    const struct hydro_props* hydro_props) {

  float a_grav[3];

  /* Add gravity. We only do this if we have gravity activated. */
  if (p->gpart) {
    /* Retrieve the current value of the gravitational acceleration from the
       gpart. We are only allowed to do this because this is the kick. We still
       need to check whether gpart exists though.*/
    a_grav[0] = p->gpart->a_grav[0];
    a_grav[1] = p->gpart->a_grav[1];
    a_grav[2] = p->gpart->a_grav[2];

#ifdef GIZMO_TOTAL_ENERGY
    p->conserved.energy += dt_grav * (p->conserved.momentum[0] * a_grav[0] +
                                      p->conserved.momentum[1] * a_grav[1] +
                                      p->conserved.momentum[2] * a_grav[2]);
#endif

    /* Kick the momentum for half a time step */
    /* Note that this also affects the particle movement, as the velocity for
       the particles is set after this. */
    p->conserved.momentum[0] += p->conserved.mass * a_grav[0] * dt_grav;
    p->conserved.momentum[1] += p->conserved.mass * a_grav[1] * dt_grav;
    p->conserved.momentum[2] += p->conserved.mass * a_grav[2] * dt_grav;

    p->conserved.energy -=
        0.5f * dt_kick_corr *
        (p->gravity.mflux[0] * a_grav[0] + p->gravity.mflux[1] * a_grav[1] +
         p->gravity.mflux[2] * a_grav[2]);
  }

  /* Update conserved variables. */
  p->conserved.mass += p->conserved.flux.mass * dt_therm;
  p->conserved.momentum[0] += p->conserved.flux.momentum[0] * dt_therm;
  p->conserved.momentum[1] += p->conserved.flux.momentum[1] * dt_therm;
  p->conserved.momentum[2] += p->conserved.flux.momentum[2] * dt_therm;
#if defined(EOS_ISOTHERMAL_GAS)
  /* We use the EoS equation in a sneaky way here just to get the constant u */
  p->conserved.energy =
      p->conserved.mass * gas_internal_energy_from_entropy(0.f, 0.f);
#else
  p->conserved.energy += p->conserved.flux.energy * dt_therm;
#endif

#ifndef HYDRO_GAMMA_5_3
  const float Pcorr = (dt_hydro - dt_therm) * p->geometry.volume;
  p->conserved.momentum[0] -= Pcorr * p->primitives.gradients.P[0];
  p->conserved.momentum[1] -= Pcorr * p->primitives.gradients.P[1];
  p->conserved.momentum[2] -= Pcorr * p->primitives.gradients.P[2];
#ifdef GIZMO_TOTAL_ENERGY
  p->conserved.energy -=
      Pcorr * (p->primitives.v[0] * p->primitives.gradients.P[0] +
               p->primitives.v[1] * p->primitives.gradients.P[1] +
               p->primitives.v[2] * p->primitives.gradients.P[2]);
#endif
#endif

  /* Apply the minimal energy limit */
  const float min_energy =
      hydro_props->minimal_internal_energy * cosmo->a_factor_internal_energy;
  if (p->conserved.energy < min_energy * p->conserved.mass) {
    p->conserved.energy = min_energy * p->conserved.mass;
    p->conserved.flux.energy = 0.f;
  }

  gizmo_check_physical_quantities(
      "mass", "energy", p->conserved.mass, p->conserved.momentum[0],
      p->conserved.momentum[1], p->conserved.momentum[2], p->conserved.energy);

#ifdef SWIFT_DEBUG_CHECKS
  /* Note that this check will only have effect if no GIZMO_UNPHYSICAL option
     was selected. */
  if (p->conserved.mass < 0.) {
    error(
        "Negative mass after conserved variables update (mass: %g, dmass: %g)!",
        p->conserved.mass, p->conserved.flux.mass);
  }

  if (p->conserved.energy < 0.) {
    error(
        "Negative energy after conserved variables update (energy: %g, "
        "denergy: %g)!",
        p->conserved.energy, p->conserved.flux.energy);
  }
#endif

  if (p->gpart) {
    /* Make sure the gpart knows the mass has changed. */
    p->gpart->mass = p->conserved.mass;
  }

  hydro_velocities_set(p, xp);

#ifdef GIZMO_LLOYD_ITERATION
  /* reset conserved variables to safe values */
  p->conserved.mass = 1.;
  p->conserved.momentum[0] = 0.;
  p->conserved.momentum[1] = 0.;
  p->conserved.momentum[2] = 0.;
  p->conserved.energy = 1.;

  /* set the particle velocities to the Lloyd velocities */
  /* note that centroid is the relative position of the centroid w.r.t. the
     particle position (position - centroid) */
  xp->v_full[0] = -p->geometry.centroid[0] / p->force.dt;
  xp->v_full[1] = -p->geometry.centroid[1] / p->force.dt;
  xp->v_full[2] = -p->geometry.centroid[2] / p->force.dt;
  p->v[0] = xp->v_full[0];
  p->v[1] = xp->v_full[1];
  p->v[2] = xp->v_full[2];
#endif

  /* reset wcorr */
  p->density.wcorr = 1.0f;
}

/**
 * @brief Returns the comoving internal energy of a particle
 *
 * @param p The particle of interest.
 */
__attribute__((always_inline)) INLINE static float
hydro_get_comoving_internal_energy(const struct part* restrict p) {

  if (p->primitives.rho > 0.)
    return gas_internal_energy_from_pressure(p->primitives.rho,
                                             p->primitives.P);
  else
    return 0.;
}

/**
 * @brief Returns the physical internal energy of a particle
 *
 * @param p The particle of interest.
 * @param xp The extended data of the particle of interest.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float
hydro_get_physical_internal_energy(const struct part* restrict p,
                                   const struct xpart* restrict xp,
                                   const struct cosmology* cosmo) {

  return cosmo->a_factor_internal_energy *
         hydro_get_comoving_internal_energy(p);
}

/**
 * @brief Returns the physical internal energy of a particle
 *
 * @param p The particle of interest.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float
hydro_get_drifted_physical_internal_energy(const struct part* restrict p,
                                           const struct cosmology* cosmo) {

  return hydro_get_physical_internal_energy(p, /*xp=*/NULL, cosmo);
}

/**
 * @brief Returns the comoving entropy of a particle
 *
 * @param p The particle of interest.
 */
__attribute__((always_inline)) INLINE static float hydro_get_comoving_entropy(
    const struct part* restrict p) {

  if (p->primitives.rho > 0.) {
    return gas_entropy_from_pressure(p->primitives.rho, p->primitives.P);
  } else {
    return 0.;
  }
}

/**
 * @brief Returns the physical internal energy of a particle
 *
 * @param p The particle of interest.
 * @param xp The extended data of the particle of interest.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float hydro_get_physical_entropy(
    const struct part* restrict p, const struct xpart* restrict xp,
    const struct cosmology* cosmo) {

  /* Note: no cosmological conversion required here with our choice of
   * coordinates. */
  return hydro_get_comoving_entropy(p);
}

/**
 * @brief Returns the physical internal energy of a particle
 *
 * @param p The particle of interest.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float
hydro_get_drifted_physical_entropy(const struct part* restrict p,
                                   const struct cosmology* cosmo) {

  /* Note: no cosmological conversion required here with our choice of
   * coordinates. */
  return hydro_get_comoving_entropy(p);
}

/**
 * @brief Returns the sound speed of a particle
 *
 * @param p The particle of interest.
 */
__attribute__((always_inline)) INLINE static float
hydro_get_comoving_soundspeed(const struct part* restrict p) {

  if (p->primitives.rho > 0.)
    return gas_soundspeed_from_pressure(p->primitives.rho, p->primitives.P);
  else
    return 0.;
}

/**
 * @brief Returns the physical sound speed of a particle
 *
 * @param p The particle of interest.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float
hydro_get_physical_soundspeed(const struct part* restrict p,
                              const struct cosmology* cosmo) {

  return cosmo->a_factor_sound_speed * hydro_get_comoving_soundspeed(p);
}

/**
 * @brief Returns the comoving pressure of a particle
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float hydro_get_comoving_pressure(
    const struct part* restrict p) {

  return p->primitives.P;
}

/**
 * @brief Returns the comoving pressure of a particle
 *
 * @param p The particle of interest.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float hydro_get_physical_pressure(
    const struct part* restrict p, const struct cosmology* cosmo) {

  return cosmo->a_factor_pressure * p->primitives.P;
}

/**
 * @brief Returns the mass of a particle
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float hydro_get_mass(
    const struct part* restrict p) {

  return p->conserved.mass;
}

/**
 * @brief Sets the mass of a particle
 *
 * @param p The particle of interest
 * @param m The mass to set.
 */
__attribute__((always_inline)) INLINE static void hydro_set_mass(
    struct part* restrict p, float m) {

  p->conserved.mass = m;
}

/**
 * @brief Returns the velocities drifted to the current time of a particle.
 *
 * @param p The particle of interest
 * @param xp The extended data of the particle.
 * @param dt_kick_hydro The time (for hydro accelerations) since the last kick.
 * @param dt_kick_grav The time (for gravity accelerations) since the last kick.
 * @param v (return) The velocities at the current time.
 */
__attribute__((always_inline)) INLINE static void hydro_get_drifted_velocities(
    const struct part* restrict p, const struct xpart* xp, float dt_kick_hydro,
    float dt_kick_grav, float v[3]) {

  if (p->conserved.mass > 0.) {
    v[0] = p->primitives.v[0] +
           p->conserved.flux.momentum[0] * dt_kick_hydro / p->conserved.mass;
    v[1] = p->primitives.v[1] +
           p->conserved.flux.momentum[1] * dt_kick_hydro / p->conserved.mass;
    v[2] = p->primitives.v[2] +
           p->conserved.flux.momentum[2] * dt_kick_hydro / p->conserved.mass;
  } else {
    v[0] = p->primitives.v[0];
    v[1] = p->primitives.v[1];
    v[2] = p->primitives.v[2];
  }

  // MATTHIEU: Bert is this correct?
  v[0] += xp->a_grav[0] * dt_kick_grav;
  v[1] += xp->a_grav[1] * dt_kick_grav;
  v[2] += xp->a_grav[2] * dt_kick_grav;
}

/**
 * @brief Returns the comoving density of a particle
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float hydro_get_comoving_density(
    const struct part* restrict p) {

  return p->primitives.rho;
}

/**
 * @brief Returns the physical density of a particle
 *
 * @param p The particle of interest
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float hydro_get_physical_density(
    const struct part* restrict p, const struct cosmology* cosmo) {

  return cosmo->a3_inv * p->primitives.rho;
}

/**
 * @brief Modifies the thermal state of a particle to the imposed internal
 * energy
 *
 * This overrides the current state of the particle but does *not* change its
 * time-derivatives
 *
 * @param p The particle
 * @param u The new internal energy
 */
__attribute__((always_inline)) INLINE static void hydro_set_internal_energy(
    struct part* restrict p, float u) {

  /* conserved.energy is NOT the specific energy (u), but the total thermal
     energy (u*m) */
  p->conserved.energy = u * p->conserved.mass;
#ifdef GIZMO_TOTAL_ENERGY
  /* add the kinetic energy */
  p->conserved.energy += 0.5f * p->conserved.mass *
                         (p->conserved.momentum[0] * p->primitives.v[0] +
                          p->conserved.momentum[1] * p->primitives.v[1] +
                          p->conserved.momentum[2] * p->primitives.v[2]);
#endif
  p->primitives.P = hydro_gamma_minus_one * p->primitives.rho * u;
}

/**
 * @brief Modifies the thermal state of a particle to the imposed entropy
 *
 * This overrides the current state of the particle but does *not* change its
 * time-derivatives
 *
 * @param p The particle
 * @param S The new entropy
 */
__attribute__((always_inline)) INLINE static void hydro_set_entropy(
    struct part* restrict p, float S) {

  p->conserved.energy = S * pow_gamma_minus_one(p->primitives.rho) *
                        hydro_one_over_gamma_minus_one * p->conserved.mass;
#ifdef GIZMO_TOTAL_ENERGY
  /* add the kinetic energy */
  p->conserved.energy += 0.5f * p->conserved.mass *
                         (p->conserved.momentum[0] * p->primitives.v[0] +
                          p->conserved.momentum[1] * p->primitives.v[1] +
                          p->conserved.momentum[2] * p->primitives.v[2]);
#endif
  p->primitives.P = S * pow_gamma(p->primitives.rho);
}

/**
 * @brief Overwrite the initial internal energy of a particle.
 *
 * Note that in the cases where the thermodynamic variable is not
 * internal energy but gets converted later, we must overwrite that
 * field. The conversion to the actual variable happens later after
 * the initial fake time-step.
 *
 * @param p The #part to write to.
 * @param u_init The new initial internal energy.
 */
__attribute__((always_inline)) INLINE static void
hydro_set_init_internal_energy(struct part* p, float u_init) {

  p->conserved.energy = u_init * p->conserved.mass;
#ifdef GIZMO_TOTAL_ENERGY
  /* add the kinetic energy */
  p->conserved.energy += 0.5f * p->conserved.mass *
                         (p->conserved.momentum[0] * p->primitives.v[0] +
                          p->conserved.momentum[1] * p->primitives.v[1] +
                          p->conserved.momentum[2] * p->primitives.v[2]);
#endif
  p->primitives.P = hydro_gamma_minus_one * p->primitives.rho * u_init;
}

#endif /* SWIFT_GIZMO_MFV_HYDRO_H */

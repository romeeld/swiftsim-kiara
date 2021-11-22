/*******************************************************************************
 * This file is part of SWIFT.
 * Copyright (c) 2016 Matthieu Schaller (matthieu.schaller@durham.ac.uk)
 *               2018   Jacob Kegerreis (jacob.kegerreis@durham.ac.uk).
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
#ifndef SWIFT_PLANETARY_HYDRO_H
#define SWIFT_PLANETARY_HYDRO_H

/**
 * @file Planetary/hydro.h
 * @brief Minimal conservative implementation of SPH (Non-neighbour loop
 * equations) with multiple materials.
 *
 * The thermal variable is the internal energy (u). Simple constant
 * viscosity term with the Balsara (1995) switch (optional).
 * No thermal conduction term is implemented.
 *
 * This corresponds to equations (43), (44), (45), (101), (103)  and (104) with
 * \f$\beta=3\f$ and \f$\alpha_u=0\f$ of Price, D., Journal of Computational
 * Physics, 2012, Volume 231, Issue 3, pp. 759-794.
 */

#include "adiabatic_index.h"
#include "approx_math.h"
#include "cosmology.h"
#include "debug.h"
#include "dimension.h"
#include "entropy_floor.h"
#include "equation_of_state.h"
#include "hydro_parameters.h"
#include "hydro_properties.h"
#include "hydro_space.h"
#include "kernel_hydro.h"
#include "minmax.h"
#include "math.h"
#include <stdio.h>
/*
 * Note: Define PLANETARY_SPH_NO_BALSARA to disable the Balsara (1995) switch
 * for the artificial viscosity and use the vanilla Monaghan (1992) instead.
 * i.e. compile with:  make CFLAGS=-DPLANETARY_SPH_NO_BALSARA
 */

/**
 * @brief Returns the comoving internal energy of a particle at the last
 * time the particle was kicked.
 *
 * For implementations where the main thermodynamic variable
 * is not internal energy, this function computes the internal
 * energy from the thermodynamic variable.
 *
 * @param p The particle of interest
 * @param xp The extended data of the particle of interest.
 */
__attribute__((always_inline)) INLINE static float
hydro_get_comoving_internal_energy(const struct part *restrict p,
                                   const struct xpart *restrict xp) {

  return xp->u_full;
}

/**
 * @brief Returns the physical internal energy of a particle at the last
 * time the particle was kicked.
 *
 * For implementations where the main thermodynamic variable
 * is not internal energy, this function computes the internal
 * energy from the thermodynamic variable and converts it to
 * physical coordinates.
 *
 * @param p The particle of interest.
 * @param xp The extended data of the particle of interest.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float
hydro_get_physical_internal_energy(const struct part *restrict p,
                                   const struct xpart *restrict xp,
                                   const struct cosmology *cosmo) {

  return xp->u_full * cosmo->a_factor_internal_energy;
}

/**
 * @brief Returns the comoving internal energy of a particle drifted to the
 * current time.
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float
hydro_get_drifted_comoving_internal_energy(const struct part *restrict p) {

  return p->u;
}

/**
 * @brief Returns the physical internal energy of a particle drifted to the
 * current time.
 *
 * @param p The particle of interest.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float
hydro_get_drifted_physical_internal_energy(const struct part *restrict p,
                                           const struct cosmology *cosmo) {

  return p->u * cosmo->a_factor_internal_energy;
}

/**
 * @brief Returns the comoving pressure of a particle
 *
 * Computes the pressure based on the particle's properties.
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float hydro_get_comoving_pressure(
    const struct part *restrict p) {

  return gas_pressure_from_internal_energy(p->rho, p->u, p->mat_id);
}

/**
 * @brief Returns the physical pressure of a particle
 *
 * Computes the pressure based on the particle's properties and
 * convert it to physical coordinates.
 *
 * @param p The particle of interest
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float hydro_get_physical_pressure(
    const struct part *restrict p, const struct cosmology *cosmo) {

  return cosmo->a_factor_pressure *
         gas_pressure_from_internal_energy(p->rho, p->u, p->mat_id);
}

/**
 * @brief Returns the comoving entropy of a particle
 *
 * For implementations where the main thermodynamic variable
 * is not entropy, this function computes the entropy from
 * the thermodynamic variable.
 *
 * @param p The particle of interest
 * @param xp The extended data of the particle of interest.
 */
__attribute__((always_inline)) INLINE static float hydro_get_comoving_entropy(
    const struct part *restrict p, const struct xpart *restrict xp) {

  return gas_entropy_from_internal_energy(p->rho, xp->u_full, p->mat_id);
}

/**
 * @brief Returns the physical entropy of a particle
 *
 * For implementations where the main thermodynamic variable
 * is not entropy, this function computes the entropy from
 * the thermodynamic variable and converts it to
 * physical coordinates.
 *
 * @param p The particle of interest
 * @param xp The extended data of the particle of interest.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float hydro_get_physical_entropy(
    const struct part *restrict p, const struct xpart *restrict xp,
    const struct cosmology *cosmo) {

  /* Note: no cosmological conversion required here with our choice of
   * coordinates. */
  return gas_entropy_from_internal_energy(p->rho, xp->u_full, p->mat_id);
}

/**
 * @brief Returns the comoving entropy of a particle drifted to the
 * current time.
 *
 * @param p The particle of interest.
 */
__attribute__((always_inline)) INLINE static float
hydro_get_drifted_comoving_entropy(const struct part *restrict p) {

  return gas_entropy_from_internal_energy(p->rho, p->u, p->mat_id);
}

/**
 * @brief Returns the physical entropy of a particle drifted to the
 * current time.
 *
 * @param p The particle of interest.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float
hydro_get_drifted_physical_entropy(const struct part *restrict p,
                                   const struct cosmology *cosmo) {

  /* Note: no cosmological conversion required here with our choice of
   * coordinates. */
  return gas_entropy_from_internal_energy(p->rho, p->u, p->mat_id);
}

/**
 * @brief Returns the comoving sound speed of a particle
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float
hydro_get_comoving_soundspeed(const struct part *restrict p) {

  return p->force.soundspeed;
}

/**
 * @brief Returns the physical sound speed of a particle
 *
 * @param p The particle of interest
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float
hydro_get_physical_soundspeed(const struct part *restrict p,
                              const struct cosmology *cosmo) {

  return cosmo->a_factor_sound_speed * p->force.soundspeed;
}

/**
 * @brief Returns the comoving density of a particle
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float hydro_get_comoving_density(
    const struct part *restrict p) {

  return p->rho;
}

/**
 * @brief Returns the comoving density of a particle.
 *
 * @param p The particle of interest
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float hydro_get_physical_density(
    const struct part *restrict p, const struct cosmology *cosmo) {

  return cosmo->a3_inv * p->rho;
}

/**
 * @brief Returns the mass of a particle
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float hydro_get_mass(
    const struct part *restrict p) {

  return p->mass;
}

/**
 * @brief Sets the mass of a particle
 *
 * @param p The particle of interest
 * @param m The mass to set.
 */
__attribute__((always_inline)) INLINE static void hydro_set_mass(
    struct part *restrict p, float m) {

  p->mass = m;
}

/**
 * @brief Returns the time derivative of internal energy of a particle
 *
 * We assume a constant density.
 *
 * @param p The particle of interest
 */
__attribute__((always_inline)) INLINE static float
hydro_get_comoving_internal_energy_dt(const struct part *restrict p) {

  return p->u_dt;
}

/**
 * @brief Returns the time derivative of internal energy of a particle
 *
 * We assume a constant density.
 *
 * @param p The particle of interest
 * @param cosmo Cosmology data structure
 */
__attribute__((always_inline)) INLINE static float
hydro_get_physical_internal_energy_dt(const struct part *restrict p,
                                      const struct cosmology *cosmo) {

  return p->u_dt * cosmo->a_factor_internal_energy;
}

/**
 * @brief Returns the time derivative of internal energy of a particle
 *
 * We assume a constant density.
 *
 * @param p The particle of interest.
 * @param du_dt The new time derivative of the internal energy.
 */
__attribute__((always_inline)) INLINE static void
hydro_set_comoving_internal_energy_dt(struct part *restrict p, float du_dt) {

  p->u_dt = du_dt;
}

/**
 * @brief Returns the time derivative of internal energy of a particle
 *
 * We assume a constant density.
 *
 * @param p The particle of interest.
 * @param cosmo Cosmology data structure
 * @param du_dt The new time derivative of the internal energy.
 */
__attribute__((always_inline)) INLINE static void
hydro_set_physical_internal_energy_dt(struct part *restrict p,
                                      const struct cosmology *cosmo,
                                      float du_dt) {

  p->u_dt = du_dt / cosmo->a_factor_internal_energy;
}

/**
 * @brief Sets the physical entropy of a particle
 *
 * @param p The particle of interest.
 * @param xp The extended particle data.
 * @param cosmo Cosmology data structure
 * @param entropy The physical entropy
 */
__attribute__((always_inline)) INLINE static void hydro_set_physical_entropy(
    struct part *p, struct xpart *xp, const struct cosmology *cosmo,
    const float entropy) {

  /* Note there is no conversion from physical to comoving entropy */
  const float comoving_entropy = entropy;
  xp->u_full =
      gas_internal_energy_from_entropy(p->rho, comoving_entropy, p->mat_id);
}

/**
 * @brief Sets the physical internal energy of a particle
 *
 * @param p The particle of interest.
 * @param xp The extended particle data.
 * @param cosmo Cosmology data structure
 * @param u The physical internal energy
 */
__attribute__((always_inline)) INLINE static void
hydro_set_physical_internal_energy(struct part *p, struct xpart *xp,
                                   const struct cosmology *cosmo,
                                   const float u) {

  xp->u_full = u / cosmo->a_factor_internal_energy;
}

/**
 * @brief Sets the drifted physical internal energy of a particle
 *
 * @param p The particle of interest.
 * @param cosmo Cosmology data structure
 * @param u The physical internal energy
 */
__attribute__((always_inline)) INLINE static void
hydro_set_drifted_physical_internal_energy(struct part *p,
                                           const struct cosmology *cosmo,
                                           const float u) {

  p->u = u / cosmo->a_factor_internal_energy;

  /* Now recompute the extra quantities */

  /* Compute the sound speed */
  const float pressure =
      gas_pressure_from_internal_energy(p->rho, p->u, p->mat_id);
  const float soundspeed = hydro_get_comoving_soundspeed(p);

  /* Update variables. */
  p->force.pressure = pressure;
  p->force.soundspeed = soundspeed;

  p->force.v_sig = max(p->force.v_sig, 2.f * soundspeed);
}

/**
 * @brief Update the value of the viscosity alpha for the scheme.
 *
 * @param p the particle of interest
 * @param alpha the new value for the viscosity coefficient.
 */
__attribute__((always_inline)) INLINE static void hydro_set_viscosity_alpha(
    struct part *restrict p, float alpha) {
  /* This scheme has fixed alpha */
}

/**
 * @brief Update the value of the diffusive coefficients to the
 *        feedback reset value for the scheme.
 *
 * @param p the particle of interest
 */
__attribute__((always_inline)) INLINE static void
hydro_diffusive_feedback_reset(struct part *restrict p) {
  /* This scheme has fixed alpha */
}

/**
 * @brief Computes the hydro time-step of a given particle
 *
 * This function returns the time-step of a particle given its hydro-dynamical
 * state. A typical time-step calculation would be the use of the CFL condition.
 *
 * @param p Pointer to the particle data
 * @param xp Pointer to the extended particle data
 * @param hydro_properties The SPH parameters
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static float hydro_compute_timestep(
    const struct part *restrict p, const struct xpart *restrict xp,
    const struct hydro_props *restrict hydro_properties,
    const struct cosmology *restrict cosmo) {

  const float CFL_condition = hydro_properties->CFL_condition;

  /* CFL condition */
  const float dt_cfl = 2.f * kernel_gamma * CFL_condition * cosmo->a * p->h /
                       (cosmo->a_factor_sound_speed * p->force.v_sig);

  return dt_cfl;
}

/**
 * @brief Does some extra hydro operations once the actual physical time step
 * for the particle is known.
 *
 * @param p The particle to act upon.
 * @param dt Physical time step of the particle during the next step.
 */
__attribute__((always_inline)) INLINE static void hydro_timestep_extra(
    struct part *p, float dt) {}

/**
 * @brief Prepares a particle for the density calculation.
 *
 * Zeroes all the relevant arrays in preparation for the sums taking place in
 * the various density loop over neighbours. Typically, all fields of the
 * density sub-structure of a particle get zeroed in here.
 *
 * @param p The particle to act upon
 * @param hs #hydro_space containing hydro specific space information.
 */
__attribute__((always_inline)) INLINE static void hydro_init_part(
    struct part *restrict p, const struct hydro_space *hs) {

  p->density.wcount = 0.f;
  p->density.wcount_dh = 0.f;
  p->rho = 0.f;
  p->density.rho_dh = 0.f;
  p->density.div_v = 0.f;
  p->density.rot_v[0] = 0.f;
  p->density.rot_v[1] = 0.f;
  p->density.rot_v[2] = 0.f;
  p->weighted_wcount = 0.f;
  p->weighted_neighbour_wcount = 0.f;
  p->f_gdf = 0.f;
#ifdef PLANETARY_IMBALANCE
  p->P = 0.f;
  p->T = 0.f;
#endif

  #ifdef SWIFT_HYDRO_DENSITY_CHECKS
  p->N_density = 1; /* Self contribution */
  p->N_force = 0;
  p->N_density_exact = 0;
  p->N_force_exact = 0;
  p->rho_exact = 0.f;
  p->n_density = 0.f;
  p->n_density_exact = 0.f;
  p->n_force = 0.f;
  p->n_force_exact = 0.f;
  p->inhibited_exact = 0;
  p->limited_part = 0;
#endif

#ifdef PLANETARY_IMBALANCE
  p->sum_rij[0] = 0.f;
  p->sum_rij[1] = 0.f;
  p->sum_rij[2] = 0.f;
  p->I = 0.f;
  p->sum_wij = 0.f;
#endif
}

/**
 * @brief Finishes the density calculation.
 *
 * Multiplies the density and number of neighbours by the appropiate constants
 * and add the self-contribution term.
 * Additional quantities such as velocity gradients will also get the final
 * terms added to them here.
 *
 * Also adds/multiplies the cosmological terms if need be.
 *
 * @param p The particle to act upon
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static void hydro_end_density(
    struct part *restrict p, const struct cosmology *cosmo) {

  /* Some smoothing length multiples. */
  const float h = p->h;
  const float h_inv = 1.0f / h;                       /* 1/h */
  const float h_inv_dim = pow_dimension(h_inv);       /* 1/h^d */
  const float h_inv_dim_plus_one = h_inv_dim * h_inv; /* 1/h^(d+1) */

  /* Final operation on the density (add self-contribution). */
  p->rho += p->mass * kernel_root;
  p->density.rho_dh -= hydro_dimension * p->mass * kernel_root;
  p->density.wcount += kernel_root;
  p->density.wcount_dh -= hydro_dimension * kernel_root;

  /* Finish the calculation by inserting the missing h-factors */
  p->rho *= h_inv_dim;
  p->density.rho_dh *= h_inv_dim_plus_one;
  p->density.wcount *= h_inv_dim;
  p->density.wcount_dh *= h_inv_dim_plus_one;

  const float rho_inv = 1.f / p->rho;
  const float a_inv2 = cosmo->a2_inv;

  /* Finish calculation of the (physical) velocity curl components */
  p->density.rot_v[0] *= h_inv_dim_plus_one * a_inv2 * rho_inv;
  p->density.rot_v[1] *= h_inv_dim_plus_one * a_inv2 * rho_inv;
  p->density.rot_v[2] *= h_inv_dim_plus_one * a_inv2 * rho_inv;

  /* Finish calculation of the (physical) velocity divergence */
  p->density.div_v *= h_inv_dim_plus_one * a_inv2 * rho_inv;

#ifdef SWIFT_HYDRO_DENSITY_CHECKS
  p->n_density += kernel_root;
  p->n_density *= h_inv_dim;
#endif

#ifdef PLANETARY_IMBALANCE
  /* Final operation on sum_wij (add self-contribution) */
  p->sum_wij += kernel_root*p->mass; 

  /* Compute norm sum_rij */
  float sum_rij_norm = 0.f;
  sum_rij_norm += p->sum_rij[0] * p->sum_rij[0] * h_inv * h_inv / p->sum_wij / p->sum_wij;
  sum_rij_norm += p->sum_rij[1] * p->sum_rij[1] * h_inv * h_inv / p->sum_wij / p->sum_wij;
  sum_rij_norm += p->sum_rij[2] * p->sum_rij[2] * h_inv * h_inv / p->sum_wij / p->sum_wij;
  p->I = sqrtf(sum_rij_norm);
   
  /* Define alpha depending on kernel and eta=1.2348 */ 
#ifdef CUBIC_SPLINE_KERNEL
  const float alpha = 7.5f; // eta=1.2348
#endif
#ifdef WENDLAND_C6_KERNEL
  //const float alpha = 7.1f; // eta=1.2348
  const float alpha = 5.1;  // eta=2.2
#endif
  p->I *= alpha;
#endif
}

/**
 * @brief Sets all particle fields to sensible values when the #part has 0 ngbs.
 *
 * In the desperate case where a particle has no neighbours (likely because
 * of the h_max ceiling), set the particle fields to something sensible to avoid
 * NaNs in the next calculations.
 *
 * @param p The particle to act upon
 * @param xp The extended particle data to act upon
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static void hydro_part_has_no_neighbours(
    struct part *restrict p, struct xpart *restrict xp,
    const struct cosmology *cosmo) {

  /* Some smoothing length multiples. */
  const float h = p->h;
  const float h_inv = 1.0f / h;                 /* 1/h */
  const float h_inv_dim = pow_dimension(h_inv); /* 1/h^d */

  /* Re-set problematic values */
  p->rho = p->mass * kernel_root * h_inv_dim;
  p->density.wcount = kernel_root * h_inv_dim;
  p->density.rho_dh = 0.f;
  p->density.wcount_dh = 0.f;
  p->density.div_v = 0.f;
  p->density.rot_v[0] = 0.f;
  p->density.rot_v[1] = 0.f;
  p->density.rot_v[2] = 0.f;

  /* Set the ratio f_gdf = 1 if particle has no neighbours */
  p->weighted_wcount = p->mass * kernel_root * h_inv_dim;
  p->weighted_neighbour_wcount = 1.f;

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
 * @param hydro_props Hydrodynamic properties.
 */
__attribute__((always_inline)) INLINE static void hydro_prepare_gradient(
    struct part *restrict p, struct xpart *restrict xp,
    const struct cosmology *cosmo, const struct hydro_props *hydro_props) {

#ifdef PLANETARY_IMBALANCE
  /* Initialize kernel averages to 0 */
  p->sum_wij_exp_T = 0.f;
  p->sum_wij_exp_P = 0.f;
  p->sum_wij_exp = 0.f;

  /* Compute the pressure */
  const float pressure =
      gas_pressure_from_internal_energy(p->rho, p->u, p->mat_id);
      
  /* Compute the temperature */
  const float temperature =
      gas_temperature_from_internal_energy(p->rho, p->u, p->mat_id);

  p->P = pressure;
  p->T = temperature;
#endif
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
    struct part *restrict p) {

}

/**
 * @brief Finishes the gradient calculation.
 *
 * Just a wrapper around hydro_gradients_finalize, which can be an empty method,
 * in which case no gradients are used.
 *
 * This method also initializes the force loop variables.
 *
 * @param p The particle to act upon.
 */
__attribute__((always_inline)) INLINE static void hydro_end_gradient(
    struct part *p) {

  /* Ensure f_gdf = 1 when having two or more particles overlaping */
  if (p->weighted_wcount == 0.f || p->weighted_neighbour_wcount == 0.f){
    p->f_gdf = 1.f;
  } else {
    /* Compute f_gdf normally*/
    p->f_gdf = p->weighted_wcount / (p->weighted_neighbour_wcount * p->rho);
  }

#ifdef PLANETARY_IMBALANCE
  /* Add self contribution to kernel averages*/
  p->sum_wij_exp += kernel_root * expf(-p->I*p->I);
  p->sum_wij_exp_P += p->P * kernel_root * expf(-p->I*p->I);
  p->sum_wij_exp_T += p->T * kernel_root * expf(-p->I*p->I);

  /* compute minimum SPH quantities */
  const float h = p->h;
  const float h_inv = 1.0f / h;                 /* 1/h */
  const float h_inv_dim = pow_dimension(h_inv); /* 1/h^d */
  const float rho_min = p->mass * kernel_root * h_inv_dim;
  const float P_min = gas_pressure_from_internal_energy(rho_min, p->u, p->mat_id);
  const float T_min = gas_temperature_from_internal_energy(rho_min, p->u, p->mat_id);
 
  /* Bullet proof */
  if (p->sum_wij_exp > 0.f && p->sum_wij_exp_P > 0.f && p->sum_wij_exp_T > 0.f){
	  /* End computation */
	  p->sum_wij_exp_P /= p->sum_wij_exp;
	  p->sum_wij_exp_T /= p->sum_wij_exp;
	  
	  /* Compute new P */ 
	  float P_new = 0.f;
	  P_new = expf(-p->I*p->I)*p->P + (1.f - expf(-p->I*p->I))*p->sum_wij_exp_P;

          /* Compute new T */
	  float T_new = 0.f;
	  T_new = expf(-p->I*p->I)*p->T + (1.f - expf(-p->I*p->I))*p->sum_wij_exp_T;
          
	  /* Compute new density */
	  float rho_new =
	      gas_density_from_pressure_and_temperature(P_new, T_new, p->mat_id);
	 
	  /* Ensure new density is not lower than minimum SPH density */
	  if (rho_new < rho_min){
	    p->rho = rho_min;
            p->P = P_min;
            p->T = T_min;
	  } else {
            p->rho = rho_new;
            p->P = gas_pressure_from_internal_energy(p->rho, p->u, p->mat_id);
            p->T = gas_temperature_from_internal_energy(p->rho, p->u, p->mat_id);
	  }
  } else { 
    const float P = gas_pressure_from_internal_energy(p->rho, p->u, p->mat_id);
    const float T = gas_temperature_from_internal_energy(p->rho, p->u, p->mat_id);
    p->P = P;
    p->T = T;
  }

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
    struct part *restrict p, struct xpart *restrict xp,
    const struct cosmology *cosmo, const struct hydro_props *hydro_props,
    const float dt_alpha) {

  const float fac_Balsara_eps = cosmo->a_factor_Balsara_eps;

  /* Compute the norm of the curl */
  const float curl_v = sqrtf(p->density.rot_v[0] * p->density.rot_v[0] +
                             p->density.rot_v[1] * p->density.rot_v[1] +
                             p->density.rot_v[2] * p->density.rot_v[2]);

  /* Compute the norm of div v including the Hubble flow term */
  const float div_physical_v = p->density.div_v + hydro_dimension * cosmo->H;
  const float abs_div_physical_v = fabsf(div_physical_v);

#ifdef PLANETARY_FIXED_ENTROPY
  /* Override the internal energy to satisfy the fixed entropy */
  p->u = gas_internal_energy_from_entropy(p->rho, p->s_fixed, p->mat_id);
  xp->u_full = p->u;
#endif
  
  /* Compute the sound speed */
  const float soundspeed =
      gas_soundspeed_from_internal_energy(p->rho, p->u, p->mat_id);

  /* Compute the "grad h" term  - Note here that we have \tilde{x}
   * as 1 as we use the local number density to find neighbours. This
   * introduces a j-component that is considered in the force loop,
   * meaning that this cached grad_h_term gives:
   *
   * f_ij = 1.f - grad_h_term_i / m_j */
  const float common_factor = p->h * hydro_dimension_inv / p->density.wcount;
  float grad_h_term;

  /* Ignore changing-kernel effects when h ~= h_max */
  if (p->h > 0.999f * hydro_props->h_max) {
    grad_h_term = 0.f;
  } else {
    grad_h_term = common_factor * p->density.rho_dh /
                  (1.f + common_factor * p->density.wcount_dh);
  }

  /* Compute the Balsara switch */
#ifdef PLANETARY_SPH_NO_BALSARA
  const float balsara = hydro_props->viscosity.alpha;
#else
  /* Pre-multiply in the AV factor; hydro_props are not passed to the iact
   * functions */
  float balsara; 
  if (abs_div_physical_v == 0.f){
  balsara = 0.f;
  } else {
  balsara = hydro_props->viscosity.alpha * abs_div_physical_v /
                        (abs_div_physical_v + curl_v +
                         0.0001f * fac_Balsara_eps * soundspeed / p->h);
  }
#endif

  /* Update variables. */
  p->force.f = grad_h_term;
#ifdef PLANETARY_IMBALANCE
  p->force.pressure = p->P;
#else 
  /* Compute the pressure */
  const float pressure =
    gas_pressure_from_internal_energy(p->rho, p->u, p->mat_id);
  p->force.pressure = pressure;
#endif
  p->force.soundspeed = soundspeed;
  p->force.balsara = balsara;
}

/**
 * @brief Reset acceleration fields of a particle
 *
 * Resets all hydro acceleration and time derivative fields in preparation
 * for the sums taking  place in the various force tasks.
 *
 * @param p The particle to act upon
 */
__attribute__((always_inline)) INLINE static void hydro_reset_acceleration(
    struct part *restrict p) {

  /* Reset the acceleration. */
  p->a_hydro[0] = 0.0f;
  p->a_hydro[1] = 0.0f;
  p->a_hydro[2] = 0.0f;

  /* Reset the time derivatives. */
  p->u_dt = 0.0f;
  p->force.h_dt = 0.0f;
  p->force.v_sig = p->force.soundspeed;
}

/**
 * @brief Sets the values to be predicted in the drifts to their values at a
 * kick time
 *
 * @param p The particle.
 * @param xp The extended data of this particle.
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static void hydro_reset_predicted_values(
    struct part *restrict p, const struct xpart *restrict xp,
    const struct cosmology *cosmo) {

  /* Re-set the predicted velocities */
  p->v[0] = xp->v_full[0];
  p->v[1] = xp->v_full[1];
  p->v[2] = xp->v_full[2];

  /* Re-set the internal energy */
  p->u = xp->u_full;

  /* Compute the pressure */
  const float pressure =
      gas_pressure_from_internal_energy(p->rho, p->u, p->mat_id);

  /* Compute the sound speed */
  const float soundspeed =
      gas_soundspeed_from_internal_energy(p->rho, p->u, p->mat_id);

  p->force.pressure = pressure;
  p->force.soundspeed = soundspeed;

  p->force.v_sig = max(p->force.v_sig, 2.f * soundspeed);
}

/**
 * @brief Predict additional particle fields forward in time when drifting
 *
 * Additional hydrodynamic quantites are drifted forward in time here. These
 * include thermal quantities (thermal energy or total energy or entropy, ...).
 *
 * Note the different time-step sizes used for the different quantities as they
 * include cosmological factors.
 *
 * @param p The particle.
 * @param xp The extended data of the particle.
 * @param dt_drift The drift time-step for positions.
 * @param dt_therm The drift time-step for thermal quantities.
 * @param cosmo The cosmological model.
 * @param hydro_props The constants used in the scheme
 * @param floor_props The properties of the entropy floor.
 */
__attribute__((always_inline)) INLINE static void hydro_predict_extra(
    struct part *restrict p, const struct xpart *restrict xp, float dt_drift,
    float dt_therm, const struct cosmology *cosmo,
    const struct hydro_props *hydro_props,
    const struct entropy_floor_properties *floor_props) {

  /* Predict the internal energy */
  p->u += p->u_dt * dt_therm;

  /* Check against absolute minimum */
  const float min_u =
      hydro_props->minimal_internal_energy / cosmo->a_factor_internal_energy;

  p->u = max(p->u, min_u);

  const float h_inv = 1.f / p->h;

  /* Predict smoothing length */
  const float w1 = p->force.h_dt * h_inv * dt_drift;
  if (fabsf(w1) < 0.2f)
    p->h *= approx_expf(w1); /* 4th order expansion of exp(w) */
  else
    p->h *= expf(w1);

  /* Predict density */
  const float w2 = -hydro_dimension * w1;
  if (fabsf(w2) < 0.2f)
    p->rho *= approx_expf(w2); /* 4th order expansion of exp(w) */
  else
    p->rho *= expf(w2);

  /* Compute the new pressure */
  const float pressure =
      gas_pressure_from_internal_energy(p->rho, p->u, p->mat_id);

  /* Compute the new sound speed */
  const float soundspeed =
      gas_soundspeed_from_internal_energy(p->rho, p->u, p->mat_id);

  p->force.pressure = pressure;
  p->force.soundspeed = soundspeed;

  p->force.v_sig = max(p->force.v_sig, 2.f * soundspeed);
}

/**
 * @brief Finishes the force calculation.
 *
 * Multiplies the force and accelerations by the appropiate constants
 * and add the self-contribution term. In most cases, there is little
 * to do here.
 *
 * Cosmological terms are also added/multiplied here.
 *
 * @param p The particle to act upon
 * @param cosmo The current cosmological model.
 */
__attribute__((always_inline)) INLINE static void hydro_end_force(
    struct part *restrict p, const struct cosmology *cosmo) {

    p->force.h_dt *= p->h * hydro_dimension_inv;
}

/**
 * @brief Kick the additional variables
 *
 * Additional hydrodynamic quantites are kicked forward in time here. These
 * include thermal quantities (thermal energy or total energy or entropy, ...).
 *
 * @param p The particle to act upon.
 * @param xp The particle extended data to act upon.
 * @param dt_therm The time-step for this kick (for thermodynamic quantities).
 * @param cosmo The cosmological model.
 * @param hydro_props The constants used in the scheme
 * @param floor_props The properties of the entropy floor.
 */
__attribute__((always_inline)) INLINE static void hydro_kick_extra(
    struct part *restrict p, struct xpart *restrict xp, float dt_therm,
    float dt_grav, float dt_hydro, float dt_kick_corr,
    const struct cosmology *cosmo, const struct hydro_props *hydro_props,
    const struct entropy_floor_properties *floor_props) {

  /* Integrate the internal energy forward in time */
  const float delta_u = p->u_dt * dt_therm;

  /* Do not decrease the energy by more than a factor of 2*/
  xp->u_full = max(xp->u_full + delta_u, 0.5f * xp->u_full);

  /* Check against absolute minimum */
  const float min_u =
      hydro_props->minimal_internal_energy / cosmo->a_factor_internal_energy;

  if (xp->u_full < min_u) {
    xp->u_full = min_u;
    p->u_dt = 0.f;
  }
}

/**
 * @brief Converts hydro quantity of a particle at the start of a run
 *
 * This function is called once at the end of the engine_init_particle()
 * routine (at the start of a calculation) after the densities of
 * particles have been computed.
 * This can be used to convert internal energy into entropy for instance.
 *
 * @param p The particle to act upon
 * @param xp The extended particle to act upon
 * @param cosmo The cosmological model.
 */
__attribute__((always_inline)) INLINE static void hydro_convert_quantities(
    struct part *restrict p, struct xpart *restrict xp,
    const struct cosmology *cosmo, const struct hydro_props *hydro_props) {

  /* Compute the pressure */
  const float pressure =
      gas_pressure_from_internal_energy(p->rho, p->u, p->mat_id);

  /* Compute the sound speed */
  const float soundspeed =
      gas_soundspeed_from_internal_energy(p->rho, p->u, p->mat_id);

  p->force.pressure = pressure;
  p->force.soundspeed = soundspeed;
}

/**
 * @brief Initialises the particles for the first time
 *
 * This function is called only once just after the ICs have been
 * read in to do some conversions or assignments between the particle
 * and extended particle fields.
 *
 * @param p The particle to act upon
 * @param xp The extended particle data to act upon
 */
__attribute__((always_inline)) INLINE static void hydro_first_init_part(
    struct part *restrict p, struct xpart *restrict xp) {

  p->time_bin = 0;
  xp->v_full[0] = p->v[0];
  xp->v_full[1] = p->v[1];
  xp->v_full[2] = p->v[2];
  xp->u_full = p->u;

  hydro_reset_acceleration(p);
  hydro_init_part(p, NULL);
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
hydro_set_init_internal_energy(struct part *p, float u_init) {

  p->u = u_init;
}

/**
 * @brief Operations performed when a particle gets removed from the
 * simulation volume.
 *
 * @param p The particle.
 * @param xp The extended particle data.
 * @param time The simulation time.
 */
__attribute__((always_inline)) INLINE static void hydro_remove_part(
    const struct part *p, const struct xpart *xp, const double time) {

  /* Print the particle info as csv to facilitate later analysis, e.g. with
   * grep '## Removed' -A 1 --no-group-separator output.txt > removed.txt
   */
  printf(
      "## Removed particle: "
      "id, x, y, z, vx, vy, vz, m, u, P, rho, h, mat_id, time \n"
      "%lld, %.7g, %.7g, %.7g, %.7g, %.7g, %.7g, "
      "%.7g, %.7g, %.7g, %.7g, %.7g, %d, %.7g \n",
      p->id, p->x[0], p->x[1], p->x[2], p->v[0], p->v[1], p->v[2], p->mass,
      p->u, p->force.pressure, p->rho, p->h, p->mat_id, time);
}

#endif /* SWIFT_PLANETARY_HYDRO_H */
/*******************************************************************************
 * This file is part of SWIFT.
 * Copyright (c) 2018 Matthieu Schaller (schaller@strw.leidenuniv.nl)
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
#ifndef SWIFT_SIMBA_FEEDBACK_IACT_H
#define SWIFT_SIMBA_FEEDBACK_IACT_H

/* Local includes */
#include "random.h"
#include "timestep_sync_part.h"
#include "tools.h"
#include "tracers.h"

/**
 * @brief Compute the mean DM velocity around a star. (non-symmetric).
 *
 * @param si First sparticle.
 * @param gj Second particle (not updated).
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_feedback_dm_vel_sum(struct spart *si,
                                       const struct gpart *gj,
                                       int *dm_ngb_N,
                                       float dm_mean_velocity[3]) {

  dm_mean_velocity[0] += gj->v_full[0];
  dm_mean_velocity[1] += gj->v_full[1];
  dm_mean_velocity[2] += gj->v_full[2];
  (*dm_ngb_N)++;
}

/**
 * @brief Compute the DM velocity dispersion around a star. (non-symmetric).
 *
 * @param si First sparticle.
 * @param gj Second particle.
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_feedback_dm_vel_disp(struct spart *si,
                                        const struct gpart *gj,
                                        const float dm_mean_velocity[3]) {

  if (si->feedback_data.dm_ngb_N <= 0) return;
  si->feedback_data.dm_vel_diff2[0] += (gj->v_full[0] - dm_mean_velocity[0]) *
                                       (gj->v_full[0] - dm_mean_velocity[0]);
  si->feedback_data.dm_vel_diff2[1] += (gj->v_full[1] - dm_mean_velocity[1]) *
                                       (gj->v_full[1] - dm_mean_velocity[1]);
  si->feedback_data.dm_vel_diff2[2] += (gj->v_full[2] - dm_mean_velocity[2]) *
                                       (gj->v_full[2] - dm_mean_velocity[2]);
}

/**
 * @brief Density interaction between two particles (non-symmetric).
 *
 * @param r2 Comoving square distance between the two particles.
 * @param dx Comoving vector separating both particles (pi - pj).
 * @param hi Comoving smoothing-length of particle i.
 * @param hj Comoving smoothing-length of particle j.
 * @param si First sparticle.
 * @param pj Second particle (not updated).
 * @param xpj Extra particle data (not updated).
 * @param cosmo The cosmological model.
 * @param fb_props Properties of the feedback scheme.
 * @param ti_current Current integer time value
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_feedback_density(const float r2, const float dx[3],
                                    const float hi, const float hj,
                                    struct spart *si, const struct part *pj,
                                    const struct xpart *xpj,
                                    const struct cosmology *cosmo,
                                    const struct feedback_props *fb_props,
                                    const integertime_t ti_current) {

  /* Ignore wind in density computation */
  if (pj->feedback_data.decoupling_delay_time > 0.f) return;

  /* Get the gas mass. */
  const float mj = hydro_get_mass(pj);

  /* Get r. */
  const float r = sqrtf(r2);

  /* Compute the kernel function */
  const float hi_inv = 1.0f / hi;
  const float ui = r * hi_inv;
  float wi;
  kernel_eval(ui, &wi);

  /* We found a neighbour! */
  si->feedback_data.to_collect.ngb_N++;

  /* Add mass of pj to neighbour mass of si  */
  si->feedback_data.to_collect.ngb_mass += mj;

  /* Update counter of total (integer) neighbours */
  si->feedback_data.to_collect.num_ngbs++;

  /* Contribution to the star's surrounding gas density */
  si->feedback_data.to_collect.ngb_rho += mj * wi;

  const float Zj = chemistry_get_total_metal_mass_fraction_for_feedback(pj);

  /* Contribution to the star's surrounding metallicity (metal mass fraction */
  si->feedback_data.to_collect.ngb_Z += mj * Zj * wi;

  /* Add contribution of pj to normalisation of density weighted fraction
   * which determines how much mass to distribute to neighbouring
   * gas particles */
  const float rho = hydro_get_comoving_density(pj);
  if (rho != 0.f)
    si->feedback_data.to_collect.enrichment_weight_inv += wi / rho;

}

/**
 * @brief Feedback interaction between two particles (non-symmetric).
 * Used for updating properties of gas particles neighbouring a star particle
 *
 * @param r2 Comoving square distance between the two particles.
 * @param dx Comoving vector separating both particles (si - pj).
 * @param hi Comoving smoothing-length of particle i.
 * @param hj Comoving smoothing-length of particle j.
 * @param si First (star) particle (not updated).
 * @param pj Second (gas) particle.
 * @param xpj Extra particle data
 * @param cosmo The cosmological model.
 * @param fb_props Properties of the feedback scheme.
 * @param ti_current Current integer time used value for seeding random number
 * generator
 */
__attribute__((always_inline)) INLINE static void
runner_iact_nonsym_feedback_apply(
    const float r2, const float dx[3], const float hi, const float hj,
    const struct spart *si, struct part *pj, struct xpart *xpj,
    const struct cosmology *cosmo, const struct hydro_props *hydro_props,
    const struct feedback_props *fb_props, const integertime_t ti_current) {

#ifdef SWIFT_DEBUG_CHECKS
  if (si->count_since_last_enrichment != 0 && engine_current_step > 0)
    error("Computing feedback from a star that should not");
#endif

  /* Ignore decoupled particles */
  if (pj->feedback_data.decoupling_delay_time > 0.f) return;

  /* Get r. */
  const float r = sqrtf(r2);

  /* Compute the kernel function */
  const float hi_inv = 1.0f / hi;
  const float ui = r * hi_inv;
  float wi;
  kernel_eval(ui, &wi);

  /* Gas particle density */
  const float rho_j = hydro_get_comoving_density(pj);

  /* Compute weighting for distributing feedback quantities */
  float Omega_frac;
  if (rho_j != 0.f) {
    Omega_frac = si->feedback_data.to_distribute.enrichment_weight * wi / rho_j;
  } else {
    Omega_frac = 0.f;
  }

#ifdef SWIFT_DEBUG_CHECKS
  if (Omega_frac < 0. || Omega_frac > 1.01)
    error(
        "Invalid fraction of material to distribute for star ID=%lld "
        "Omega_frac=%e count since last enrich=%d",
        si->id, Omega_frac, si->count_since_last_enrichment);
#endif

  /* Update particle mass */
  const double current_mass = hydro_get_mass(pj);
  const double delta_mass = si->feedback_data.to_distribute.mass * Omega_frac;
  const double new_mass = current_mass + delta_mass;

  hydro_set_mass(pj, new_mass);

  /* Inverse of the new mass */
  const double new_mass_inv = 1. / new_mass;

  /* Update total metallicity */
  const double current_metal_mass_total =
      pj->chemistry_data.metal_mass_fraction_total * current_mass;
  const double delta_metal_mass_total =
      si->feedback_data.to_distribute.total_metal_mass * Omega_frac;
  const double new_metal_mass_total =
      current_metal_mass_total + delta_metal_mass_total;

  pj->chemistry_data.metal_mass_fraction_total =
      new_metal_mass_total * new_mass_inv;

  /* Update mass fraction of each tracked element  */
  for (int elem = 0; elem < chemistry_element_count; elem++) {
    const double current_metal_mass =
        pj->chemistry_data.metal_mass_fraction[elem] * current_mass;
    const double delta_metal_mass =
        si->feedback_data.to_distribute.metal_mass[elem] * Omega_frac;
    const double new_metal_mass = current_metal_mass + delta_metal_mass;

    pj->chemistry_data.metal_mass_fraction[elem] =
        new_metal_mass * new_mass_inv;
  }

  /* Update iron mass fraction from SNIa  */
  const double current_iron_from_SNIa_mass =
      pj->chemistry_data.iron_mass_fraction_from_SNIa * current_mass;
  const double delta_iron_from_SNIa_mass =
      si->feedback_data.to_distribute.Fe_mass_from_SNIa * Omega_frac;
  const double new_iron_from_SNIa_mass =
      current_iron_from_SNIa_mass + delta_iron_from_SNIa_mass;

  pj->chemistry_data.iron_mass_fraction_from_SNIa =
      new_iron_from_SNIa_mass * new_mass_inv;

  /* Update mass from SNIa  */
  const double delta_mass_from_SNIa =
      si->feedback_data.to_distribute.mass_from_SNIa * Omega_frac;

  pj->chemistry_data.mass_from_SNIa += delta_mass_from_SNIa;

  /* Update metal mass fraction from SNIa */
  const double current_metal_mass_from_SNIa =
      pj->chemistry_data.metal_mass_fraction_from_SNIa * current_mass;
  const double delta_metal_mass_from_SNIa =
      si->feedback_data.to_distribute.metal_mass_from_SNIa * Omega_frac;
  const double new_metal_mass_from_SNIa =
      current_metal_mass_from_SNIa + delta_metal_mass_from_SNIa;

  pj->chemistry_data.metal_mass_fraction_from_SNIa =
      new_metal_mass_from_SNIa * new_mass_inv;

  /* Update mass from SNII  */
  const double delta_mass_from_SNII =
      si->feedback_data.to_distribute.mass_from_SNII * Omega_frac;

  pj->chemistry_data.mass_from_SNII += delta_mass_from_SNII;

  /* Update metal mass fraction from SNII */
  const double current_metal_mass_from_SNII =
      pj->chemistry_data.metal_mass_fraction_from_SNII * current_mass;
  const double delta_metal_mass_from_SNII =
      si->feedback_data.to_distribute.metal_mass_from_SNII * Omega_frac;
  const double new_metal_mass_from_SNII =
      current_metal_mass_from_SNII + delta_metal_mass_from_SNII;

  pj->chemistry_data.metal_mass_fraction_from_SNII =
      new_metal_mass_from_SNII * new_mass_inv;

  /* Update mass from AGB  */
  const double delta_mass_from_AGB =
      si->feedback_data.to_distribute.mass_from_AGB * Omega_frac;

  pj->chemistry_data.mass_from_AGB += delta_mass_from_AGB;

  /* Update metal mass fraction from AGB */
  const double current_metal_mass_from_AGB =
      pj->chemistry_data.metal_mass_fraction_from_AGB * current_mass;
  const double delta_metal_mass_from_AGB =
      si->feedback_data.to_distribute.metal_mass_from_AGB * Omega_frac;
  const double new_metal_mass_from_AGB =
      current_metal_mass_from_AGB + delta_metal_mass_from_AGB;

  pj->chemistry_data.metal_mass_fraction_from_AGB =
      new_metal_mass_from_AGB * new_mass_inv;

  /* Compute the current kinetic energy */
  const double current_v2 = xpj->v_full[0] * xpj->v_full[0] +
                            xpj->v_full[1] * xpj->v_full[1] +
                            xpj->v_full[2] * xpj->v_full[2];
  const double current_kinetic_energy_gas =
      0.5 * cosmo->a2_inv * current_mass * current_v2;

  /* Compute the current thermal energy */
  const double current_thermal_energy =
      current_mass * hydro_get_physical_internal_energy(pj, xpj, cosmo);

  /* Apply conservation of momentum */

  /* Update velocity following change in gas mass */
  xpj->v_full[0] *= current_mass * new_mass_inv;
  xpj->v_full[1] *= current_mass * new_mass_inv;
  xpj->v_full[2] *= current_mass * new_mass_inv;

  /* Update velocity following addition of mass with different momentum */
  xpj->v_full[0] += delta_mass * new_mass_inv * si->v[0];
  xpj->v_full[1] += delta_mass * new_mass_inv * si->v[1];
  xpj->v_full[2] += delta_mass * new_mass_inv * si->v[2];

  /* Compute the new kinetic energy */
  const double new_v2 = xpj->v_full[0] * xpj->v_full[0] +
                        xpj->v_full[1] * xpj->v_full[1] +
                        xpj->v_full[2] * xpj->v_full[2];
  const double new_kinetic_energy_gas = 0.5 * cosmo->a2_inv * new_mass * new_v2;

  /* Energy injected
   * (thermal SNIa + kinetic energy of ejecta + kinetic energy of star) */
  const double injected_energy =
      si->feedback_data.to_distribute.energy * Omega_frac;

  /* Apply energy conservation to recover the new thermal energy of the gas
   *
   * Note: in some specific cases the new_thermal_energy could be lower
   * than the current_thermal_energy, this is mainly the case if the change
   * in mass is relatively small and the velocity vectors between both the
   * gas particle and the star particle have a small angle. */
  double new_thermal_energy = current_kinetic_energy_gas +
                              current_thermal_energy + injected_energy -
                              new_kinetic_energy_gas;

  /* In rare configurations the new thermal energy could become negative.
   * We must prevent that even if that implies a slight violation of the
   * conservation of total energy.
   * The minimum energy (in units of energy not energy per mass) is
   * the total particle mass (including the mass to distribute) at the
   * minimal internal energy per unit mass */
  const double min_u = hydro_props->minimal_internal_energy * new_mass;

  new_thermal_energy = max(new_thermal_energy, min_u);

  /* Convert this to a specific thermal energy */
  const double u_new_enrich = new_thermal_energy * new_mass_inv;

  /* Do the energy injection. */
  hydro_set_physical_internal_energy(pj, xpj, cosmo, u_new_enrich);
  hydro_set_drifted_physical_internal_energy(pj, cosmo, u_new_enrich);

  /* Finally, SNII stochastic feedback */

  /* Get the total number of SNII thermal energy injections per stellar
   * particle at this time-step */
  const int N_of_SNII_thermal_energy_inj =
      si->feedback_data.to_distribute.SNII_num_of_thermal_energy_inj;

  /* Are we doing some SNII feedback? */
  if (N_of_SNII_thermal_energy_inj > 0) {

    const float rand_kick = random_unit_interval_two_IDs(
            si->id, pj->id, ti_current, random_number_stellar_feedback_1);

    message("V_KICK_PROB: prob=%g rand_kick=%g", 
            si->feedback_data.kick_probability,
            rand_kick);

    /* We already know the probability to kick, let's check if we do it now */
    if (rand_kick < si->feedback_data.kick_probability) {

      /* Compute new energy of this particle */

      const double u_init = hydro_get_physical_internal_energy(pj, xpj, cosmo);
      const float delta_u = si->feedback_data.to_distribute.SNII_delta_u;
      const float thermal_frac = fb_props->SNII_fthermal;
      const float kinetic_frac = fb_props->SNII_fkinetic;

      /* Need delta_u * cosmo->a2_inv to have energy in physical units */
      const double u_new = u_init + (delta_u * cosmo->a2_inv) * thermal_frac;

      /* Inject energy into the particle */
      hydro_set_physical_internal_energy(pj, xpj, cosmo, u_new);
      hydro_set_drifted_physical_internal_energy(pj, cosmo, u_new);

      /* Kick particle with SNII energy */

      const double v_kick = sqrtf(2.0 * kinetic_frac * delta_u);

      /* Direction of kick: a x v */
      const double dir[3] = {
          pj->gpart->a_grav[1] * pj->v[2] - pj->gpart->a_grav[2] * pj->v[1],
          pj->gpart->a_grav[2] * pj->v[0] - pj->gpart->a_grav[0] * pj->v[2],
          pj->gpart->a_grav[0] * pj->v[1] - pj->gpart->a_grav[1] * pj->v[0]
      };
      const double norm = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);

      /* Random number to choose to kick the particle */
      const double rand_dir = random_unit_interval(
          pj->id, ti_current, random_number_stellar_feedback_2);
      const int dirsign = rand_dir > 0.5 ? 1 : -1;
      const double prefactor = v_kick * dirsign / norm;

      pj->v[0] += prefactor * dir[0];
      pj->v[1] += prefactor * dir[1];
      pj->v[2] += prefactor * dir[2];

      /* Impose maximal viscosity */
      hydro_diffusive_feedback_reset(pj);

      /* Mark this particle has having been heated/kicked by feedback */
      tracers_after_feedback(xpj);

      /* Set delay time */
      pj->feedback_data.decoupling_delay_time = fb_props->Wind_decoupling_time_factor *
            cosmology_get_time_since_big_bang(cosmo, cosmo->a);;

      pj->feedback_data.number_of_times_decoupled++;

      /* Immediately set hydro acceleration to zero */
      hydro_reset_acceleration(pj);

      /* Update the signal velocity of the particle based on the velocity kick */
      hydro_set_v_sig_based_on_velocity_kick(pj, cosmo, v_kick * cosmo->a_inv);

      /* Synchronize the particle on the timeline */
      timestep_sync_part(pj);

      message(
          "V_KICK: z=%g  sp->id=%lld  pj->id=%lld f_E=%g  sigDM=%g km/s  tdelay=%g  "
          "v_kick=%g km/s",
          cosmo->z, si->id, pj->id, si->f_E, 
          si->feedback_data.dm_vel_disp_1d * cosmo->a_inv / fb_props->kms_to_internal,
          pj->feedback_data.decoupling_delay_time, v_kick * cosmo->a_inv / fb_props->kms_to_internal);

    }
  }
}

#endif /* SWIFT_SIMBA_FEEDBACK_IACT_H */

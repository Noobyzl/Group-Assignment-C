/* impact.c - Module 4 (M4) environmental and economic impact
 *
 * EEEB1014 Solar Planner, Theme E
 *
 * Two small calculations, deliberately kept simple and defensive:
 *
 *   1) CO2 avoided  (kg/year) = solar kWh that replace grid kWh * 0.758
 *   2) Savings      (RM/year) = min(generated kWh, annual demand kWh) * tariff
 *
 * Assumptions (state these in the report):
 *   - Every kWh the array produces is used on site or replaces a grid kWh
 *     1:1. Time-of-use, weekend/holiday patterns and battery losses are not
 *     modelled (annual totals only).
 *   - There is no export credit: generation above annual demand earns
 *     nothing, so both savings and avoided CO2 are capped at demand.
 *   - 0.758 kg CO2/kWh is the grid emission factor given in the brief.
 */
#include "common.h"
#include "impact.h"

#include <math.h>

double co2_avoided_kg(double kwh)
{
    /* Never return a negative mass of CO2 and never let a NaN or infinity
     * escape into the report. */
    if (!isfinite(kwh) || kwh <= 0.0)
        return (0.0);
    return (kwh * CO2_KG_PER_KWH);
}

double saving_rm(double gen_kwh, double use_kwh, double tariff)
{
    double used_kwh;

    if (!isfinite(gen_kwh) || !isfinite(use_kwh) || !isfinite(tariff))
        return (0.0);
    /* Zero or negative demand / tariff cannot give a meaningful saving. */
    if (gen_kwh <= 0.0 || use_kwh <= 0.0 || tariff <= 0.0)
        return (0.0);
    /* No export credit: only the part of the generation that the user would
     * otherwise have bought from the grid is worth money. */
    if (gen_kwh < use_kwh)
        used_kwh = gen_kwh;
    else
        used_kwh = use_kwh;
    return (used_kwh * tariff);
}

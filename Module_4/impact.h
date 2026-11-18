/* impact.h - Module 4 (M4) environmental and economic impact
 *
 * EEEB1014 Solar Planner, Theme E
 *
 * This header only DECLARES the two functions. The code is in impact.c.
 * Both functions are also declared in common.h (the group contract), so any
 * module that includes common.h can call them; including impact.h documents
 * that this file is the owner of those functions.
 */

#ifndef IMPACT_H
#define IMPACT_H

/* Avoided CO2 in kg for a quantity of grid-displacing solar energy in kWh.
 * Uses the group constant CO2_KG_PER_KWH (0.758 kg CO2 per kWh).
 * Returns 0.0 for zero, negative or non-finite input.                        */
double co2_avoided_kg(double kwh);

/* Annual money saved in RM.
 *   saving = min(gen_kwh, use_kwh) * tariff
 * The min() is the "no export credit" rule: solar energy that exceeds the
 * annual consumption is assumed to be wasted, not sold back to the grid.
 * Returns 0.0 if any input is zero, negative or not a finite number.         */
double saving_rm(double gen_kwh, double use_kwh, double tariff);

#endif /* IMPACT_H */

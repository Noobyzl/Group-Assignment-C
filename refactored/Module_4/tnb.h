/* tnb.h - TNB Domestic General Tariff, minimum charge, RM/W cost and bill-based savings
 * (flowchart steps 4 and 5).
 *
 * Source: https://www.mytnb.com.my/tariff/index.html (rates effective 1 July 2025,
 * AFA as shown for Nov 2026, read 2026-11-18). Update the values below if TNB changes them.
 *
 * Monthly bill for k kWh (Domestic General, <= 1500 kWh):
 *   energy + capacity + network charges on ALL kWh        (27.03 + 4.55 + 12.85 sen/kWh)
 *   (+10 sen/kWh energy charge on all kWh when k > 1500)
 *   - Energy Efficiency Incentive (k <= 1000): band rate x all kWh (table in tnb.c)
 *   + retail charge RM 10 and AFA, both waived when k <= 800
 *   + 8 % service tax on the part above 800 kWh (simplified, see tnb.c)
 *   + 1.6 % KWTBB when k > 300
 * Simplifications: standard 30-day bill, no ToU, no rebates, no stamp duty. */
#ifndef TNB_H
#define TNB_H

#include "../common.h"

#define TNB_ENERGY_RM_KWH     0.2703
#define TNB_CAPACITY_RM_KWH   0.0455
#define TNB_NETWORK_RM_KWH    0.1285
#define TNB_HIGH_USE_KWH      1500.0   /* above this, +10 sen/kWh energy charge on all kWh */
#define TNB_HIGH_USE_EXTRA    0.1000
#define TNB_RETAIL_RM         10.00    /* RM/month, waived at <= TNB_PROTECT_KWH */
#define TNB_AFA_RM_KWH        0.0360   /* Nov 2026: +3.60 sen/kWh, waived at <= TNB_PROTECT_KWH */
#define TNB_PROTECT_KWH       800.0    /* 800 kWh protection level, valid 1 Sep - 31 Dec 2026 */
#define TNB_SERVICE_TAX       0.08     /* only on usage above TNB_PROTECT_KWH */
#define TNB_KWTBB_RATE        0.016    /* on all customers except domestic <= 300 kWh */
#define TNB_KWTBB_FREE_KWH    300.0
#define TNB_INCENTIVE_MAX_KWH 1000.0   /* Energy Efficiency Incentive only up to this usage */

/* Flowchart constants (not from the TNB page). */
#define TNB_MIN_CHARGE_RM     3.00   /* minimum monthly bill (flowchart: RM 3/mo) */
#define INSTALL_COST_RM_PER_W 4.00   /* constant installed cost (flowchart: RM 4.00/W) */
#define ELIGIBLE_MIN_BILL_RM  200.0  /* flowchart gate: monthly bill >= RM 200 */

double tnb_energy_charge(double kwh);          /* full TNB bill for a month's kWh (no RM 3 floor) */
double tnb_monthly_bill(double kwh);           /* same, but never below TNB_MIN_CHARGE_RM */
double tnb_kwh_from_bill(double bill_rm);      /* inverse of tnb_energy_charge (bill rises with kWh) */

/* Yearly bill saving (RM) from a kWp array, month by month. Each month the solar
 * kWh reduce the kWh bought (never below 0), the bill is re-priced with the
 * tariff above and the RM 3 floor, and the saving is old bill minus new bill, so
 * the total can never exceed what the user was paying. bill_rm is the bill the user typed
 * (the "before" price; a bill inside a tariff jump converts to a kWh that costs slightly less);
 * pass 0 to use the re-priced kWh instead. */
double tnb_annual_saving(const Dataset *d, double kwp, double monthly_kwh, double bill_rm);

/* Re-prices rows out[0..n-1] (already filled by sweep_panels) with that saving,
 * then recomputes payback and verdict. */
void tnb_apply_savings(const Dataset *d, const SystemConfig *cfg, SimResult out[], int n);

#endif

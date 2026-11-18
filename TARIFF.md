# Tariff and cost assumptions used by the planner

Source: TNB tariff page, https://www.mytnb.com.my/tariff/index.html (read 2026-11-18).
Code: `Module_4/tnb.h` (values) and `Module_4/tnb.c` (calculation).

## TNB Domestic General Tariff (effective 1 July 2025)

Usage of 1,500 kWh/month or less:

| Charge | Rate |
|---|---|
| Energy charge, all kWh | 27.03 sen/kWh |
| Capacity charge, all kWh | 4.55 sen/kWh |
| Network charge, all kWh | 12.85 sen/kWh |
| Retail charge | RM 10.00/month (waived at 800 kWh or less) |

Usage above 1,500 kWh/month: energy charge becomes 37.03 sen/kWh (+10 sen on all kWh); other charges unchanged.

### Energy Efficiency Incentive (credit on ALL kWh, by the band the monthly total falls in; usage up to 1,000 kWh only)

| Monthly kWh | Credit (sen/kWh) | Monthly kWh | Credit (sen/kWh) |
|---|---|---|---|
| 1 - 200 | 25.0 | 551 - 600 | 9.0 |
| 201 - 250 | 24.5 | 601 - 650 | 7.5 |
| 251 - 300 | 22.5 | 651 - 700 | 5.5 |
| 301 - 350 | 21.0 | 701 - 750 | 4.5 |
| 351 - 400 | 17.0 | 751 - 800 | 4.0 |
| 401 - 450 | 14.5 | 801 - 850 | 2.5 |
| 451 - 500 | 12.0 | 851 - 900 | 1.0 |
| 501 - 550 | 10.5 | 901 - 1,000 | 0.5 |

### Other charges

| Item | Rule used |
|---|---|
| AFA (fuel cost adjustment) | +3.60 sen/kWh (Nov 2026 rate); waived at 800 kWh or less |
| 800 kWh protection level | Valid 1 Sep - 31 Dec 2026 (reverts to 600 kWh afterwards) |
| KWTBB | 1.6% of the bill when usage is above 300 kWh |
| Service tax | 8% on the kWh above 800 only (simplified) |

## Flowchart constants (not from the TNB page)

| Item | Value |
|---|---|
| Eligibility gate | Monthly bill of RM 200 or more |
| Minimum monthly bill after solar | RM 3.00 |
| Installed cost | RM 4.00 per watt (rated W x RM 4.00 per panel) |

## Not modelled

Time-of-use pricing, rebates, stamp duty, billing periods other than a standard 30-day month, export credit (NEM). The AFA rate changes monthly, so update `TNB_AFA_RM_KWH` in `tnb.h` when it does.

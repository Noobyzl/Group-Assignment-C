# Refactor notes

This folder is a refactored copy of the project. The original code in the parent folder is untouched.

## Rules applied
- Output uses only `printf` / `fprintf`. `puts` and `snprintf` are gone, and so is the `va_list` helper in the report.
  The rule was read as covering output only, so these are unchanged: input (`fgets`, `getchar`, `fgetc`, `strtol`,
  `strtod`), and string helpers (`strncpy` in `panels.c`, bounded `strlen`/`memcpy` for the CSV file paths in
  `report_csv.c`).
- `sscanf` is gone. The timestamp search in `search.c` now uses `scanf` on stdin. `scanf`/`fscanf` read streams, not
  strings, so the loader (`loader_helper.c`), which parses text already in memory, uses `strtod` for numbers and small
  hand-written readers for the four timestamp formats instead. Hex numbers such as `0x1A` are still rejected.
- No function is longer than 50 lines (signature to closing brace).
- No file is longer than 300 lines.

## Behaviour
Output, exit codes and `output/report.txt` match the original for 21 scripted runs (default, ineligible,
roof too small, budget too low, declining at every "Continue?" prompt, invalid input, EOF at several points, all
five search options, a user-supplied dataset, and datasets that trigger both coverage warnings).
The optional CSV export (`export_csvs_with_config`, never called by `main`) was checked separately with a small
driver and writes byte-identical files.
The one intentional difference: a dataset path longer than about 230 characters is no longer cut off in the
"User-supplied dataset ..." line.

## File changes
| Original | Now |
|---|---|
| `Module_1/loader.c` (459 lines) | `loader.c` (reading, public API), `loader_row.c` (one line -> record), `loader_order.c` (sort / duplicates); `loader_shared.h` holds the shared struct and limits |
| `Module_1/panels.c` | `load_panels` split with `is_panel_header` and `store_panel` |
| `Module_2/Yield.c` | time-step code moved to `TimeStep.c`; `time_step_hours` split into `tally_gap` and `most_common_gap` |
| `Module_2/Fault.c` | `fault_scan` split into `flag_bad_rows` and `estimate_loss` |
| `Module_5/main.c` (351 lines, `main` 182 lines) | `main.c` (one `stage_*` function per flowchart step) and `input.c` / `input.h` (prompts) |
| `Module_5/report.c` (462 lines) | `report.c` (state, writing order), `report_print.c` (text of each part), `report_csv.c` (CSV export), `coverage.c` (dataset coverage check), `report_internal.h` |
| `Module_5/search.c` | `puts` -> `printf`; payback text printed with `print_payback()` |

## API changes (anything that calls these needs updating)
- `dataset_coverage_warning(d, buf, size)` is now `dataset_coverage_warning(d, &value)` and returns a kind;
  print it with `print_coverage_warning(out, kind, value)`.
- `set_report_metadata()` stores the pointers it is given (they must stay valid for the whole run).
- New: `set_report_source_file(path)` and `print_payback(out, years)`.
- The `tests/` folder is not copied here and still targets the original API.

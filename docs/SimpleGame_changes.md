# SimpleGame Changes Needed

No outstanding SimpleGame changes are currently required.

The previously documented callback-type, transform-aware input-mapping,
text-measurement, wrapping-row, and HUD-inheritance requests are implemented in
SimpleGame. Ravenest now uses the native `setInputTransform()` path and the
measured `Row`/`Text` layout APIs directly.

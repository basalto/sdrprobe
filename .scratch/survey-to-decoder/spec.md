# From "something is there" to "here is what it says"

The survey is this program's spine: it walks a band and reports what is on it.
`Inspect` is how a reader acts on that -- it takes the selected candidate,
asks the band plan what lives at that frequency, and opens the decoder for it.

It knows two of the four technologies this program decodes. GSM and ADS-B were
wired when the button was written; LTE arrived afterwards and was not, and FM
arrived after that. So the survey can find a carrier at 94.4 MHz, name it
"FM broadcasting", and offer no way to hear it -- while a view that would
decode its RDS and play its audio sits one tab away, reachable only by typing
the frequency back in by hand.

The band plan already carries the mapping (`enum band_plan_decoder`), and
`BAND_PLAN_LTE` exists and is used. `BAND_PLAN_FM` does not exist: band II is
in the plan as a label with no decoder behind it, one of 64 allocations that
resolve to `BAND_PLAN_NONE`.

This is wiring, not new capability. Everything it reaches already works.

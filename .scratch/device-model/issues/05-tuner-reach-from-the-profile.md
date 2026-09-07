# 05 - What the tuner reaches comes from the profile

Status: ready-for-agent
Blocked by: 02

`SURVEY_TUNER_LOWER_HZ` and `SURVEY_TUNER_UPPER_HZ` are an R820T's 24 MHz and
1766 MHz, and `check-survey-bands` asserts reach in **both** directions --
nothing offered is out of reach, nothing reachable is left off. That check is
correct and it will fail on a new device by design, which is the right
behaviour and the reason this is its own ticket.

A B210-class device is **not a superset**: it reaches 6 GHz and opens 2.4 GHz
ISM, 3.5 GHz n78 and 5 GHz, and it loses everything below about 70 MHz --
including the 25 MHz clock fundamental, though its third harmonic at
75.0005 MHz that `carrier_75000_bare.bin` holds stays reachable.

## What to build

The two constants come from the profile. `survey_bands_for()` takes it.
`check-survey-bands` runs its both-directions assertion against **each** of at
least two profiles, which is what turns it from a device-specific check into a
property.

`docs/band-surveys.md` and the band-plan table gain the allocations above
1766 MHz that were previously unreachable and therefore untested.

## Acceptance criteria

- `check-survey-bands` passes for an RTL profile and a wideband profile.
- No allocation is offered that the active profile cannot tune.
- `check-pipelines` unchanged.

## Not in scope

- Decoding anything new up there. `docs/what-is-on-air.md` gets the entries
  when there is a receiver to measure them with.

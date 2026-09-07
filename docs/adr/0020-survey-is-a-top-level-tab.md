# Survey is a top-level tab

## Status

accepted

## Context and decision

ADR-0008 introduced Scope and Decode as top-level tabs and originally placed
the band survey among Scope's views. That grouped two different workflows:
Scope changes how the current tuning is inspected, while Survey retunes across
a frequency range, compares the result with receiving setup history, and may repeat the
sweep to observe change. Under Scope it also inherited numbered view choices
that did not describe anything in the survey workflow.

The top-level tabs are therefore **Survey, Scope, and Decode**, with Survey as
the first tab and the opening screen. Survey remains a subdomain of Probe: tab
placement organizes workflows and does not define a bounded context.

## Considered options

- **Keep Survey under Scope** minimizes top-level navigation but conflates a
  retuning workflow with alternate presentations of one tuning.
- **Make Survey a separate mode or window** isolates it more strongly, but
  duplicates navigation and receiver ownership without introducing a separate
  domain language.

## Consequences

- Scope's numbered choices remain only the views of the current tuning.
- Survey owns its range, site, antenna, history, and repeated-sweep workflow
  without inheriting Scope view controls.
- Calibration and Settings remain global overlays as decided by ADR-0008.
# Capture waterfall artifact

## Feature description

Every signal capture recorded by sdrprobe should be accompanied by an
annotated PNG waterfall generated from the completed raw I/Q, and its sidecar
JSON should identify that image by a relative sibling filename.

The image is a durable description of the capture, not a screenshot of the
window that happened to be open while recording. Given the same capture,
sample metadata and rendering parameters, generation must produce the same
raster independently of window size, active view or GPU state. It includes a
time axis, frequency axis and level scale so the image remains interpretable
beside the raw bytes.

The sidecar field is optional when reading old or reconstructed captures.
Newly recorded captures name the image only after the image has been written
successfully; a failed image export must not leave a dangling path that claims
an artifact exists.

## Decisions

- PNG is the artifact format.
- The image is an annotated capture waterfall, not a live UI screenshot.
- The image is a sibling of the `.bin` and `.json` files.
- The sidecar stores a relative filename, so the three files can move as one
  capture set.
- Rendering starts from the completed raw capture and its recorded metadata.
- The capture's whole duration is represented. A fixed documented raster size
  reduces or aggregates rows when the capture contains more time slices than
  the image can display.
- Image generation does not run while holding the acquisition recording mutex
  and does not delay delivery of receiver blocks.

## Plan

1. Establish a presentation-free waterfall raster and metadata contract, then
   expose it through a command that can generate the PNG for an existing
  capture and record the relative path in its sidecar. The same module owns
  live history append, retune overlap and time reduction; GPU and PNG output
  are its two adapters.
2. Add recording-finalization notification so automatic and early-stop
   recordings invoke the same generator after the `.bin` is closed, with
   failures reported without corrupting the capture or sidecar.
3. Verify the raster numerically without a window, inspect an annotated PNG,
   and exercise the assembled recording path over file playback.

## Constraints

- Preserve the raw-I/Q rendering seam in ADR-0005.
- Keep image geometry and colour decisions reachable without a window under
  ADR-0012.
- Do not make the acquisition worker own presentation or PNG encoding.
- Do not infer sample format, full scale, tuning or rate from defaults when
  the capture states them.
- A capture with short blocks remains marked non-contiguous; generating an
  image does not repair or conceal gaps.
- Existing sidecars without the image field continue to parse unchanged.
- Do not leave row stride, retune movement or time reduction as knowledge that
  both the Scope view and artifact generator must learn.

## Tickets

1. `issues/01-existing-capture-to-waterfall-artifact.md`
2. `issues/02-recording-produces-the-artifact.md`

Tickets are ordered by dependency.

# One way to look at a frequency axis

## What is asked for

Every chart with a frequency along the bottom should behave the same way:

- drag a region with the mouse to select it,
- Left and Right move the range,
- a key puts it back,
- and the range is shown in the header as a start and an end frequency, which
  can be typed into and which follow a dragged region.

The centre frequency currently lives in the Settings panel and should move to
that header.

## What already exists

The survey has all of it. `src/survey_window.h` is the arithmetic -- a `data`
range that exists and a `view` range on screen, with clamp, zoom, pan and
reset -- and it is plain doubles with no raylib, no receiver and no
application state, checked by `tests/survey_window_test.c`.

Nothing about it is specific to the survey except its name. That is the
foundation and it does not need writing.

## What does not exist

The five other charts with a frequency axis draw the whole of what the
receiver is delivering and offer no way to look at part of it: the Scope's
spectrum and waterfall, and the waterfalls in the GSM, LTE and FM views and
the calibration overlay.

## The one thing that needs deciding, not assuming

In the survey, the range **is** the thing: it is what a sweep will cover, and
narrowing it narrows the sweep. On the Scope views it is not. What arrives is
whatever the tuner is centred on across whatever the sample rate is, and a
selected region can mean either of two quite different things:

- **draw less of what is already here**, which is a zoom and costs nothing; or
- **go and get that instead**, which is a retune and changes what is received.

They are not interchangeable. A user dragging a box around a signal 200 kHz
wide almost certainly means the second -- they want to look at it properly --
but a zoom that silently retunes is a zoom that cannot be undone by zooming
out, because the samples either side are gone.

The arrangement this settles on: **the header's two fields are the truth**.
They say what part of the spectrum is on screen. Dragging sets them. Typing
sets them. When what they ask for lies inside what the receiver is already
delivering it is a zoom; when it does not, it is a retune. Reset puts them
back to the full received span. That way one pair of numbers explains the
screen, and the difference between zooming and retuning is a consequence of
what was asked for rather than a mode to be in.

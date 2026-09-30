# Original Strategic UI – Stage 6.4a

## Global Czech text encoding fix

The Czech text problem was not a Research-screen problem.  The source tree contains
Unicode literals while the original bitmap font ultimately expects CP895.  The runtime
conversion (`wstring2stringCP895`) was already shared, but MSVC was allowed to decode
source files using the machine's legacy source code page.  That could damage accented
characters before they ever reached the CP895 converter, leading to labels such as
`Pen ze`, `V zkum` or corrupted category captions.

The Visual Studio project now enables `/utf-8` for **all six build configurations**
(Debug/Release/Release static, Win32/x64).  Therefore every source literal reaches wxWidgets
as the intended Unicode text and the existing single CP895 conversion path can render it
with the original Spellcross bitmap font.

This is deliberately a project-wide fix, not another per-screen replacement.  Research,
Info, Resources, Statistics, Buy, Units, Hierarchy and future restored screens all use the
same compiler/text pipeline.

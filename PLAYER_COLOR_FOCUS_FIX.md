# Player-color focus-halo patch

## Cause

After all GUI controls are drawn, `UpdateFrame_UI` calls the focus renderer at
`004A16F0` for the currently focused control. For most control types, that function
draws six successively larger outline rectangles around the control.

The rectangles use hardcoded palette indices:

```text
31, 28, 24, 19, 13, 6
```

Each iteration expands the rectangle by one pixel. A focused 20x20 `Color%d` control
therefore receives a six-pixel multicolor halo which extends into neighboring player
rows. Under the active game palette, those indices create the apparent highlight and
color-remapping defect.

## Runtime patch

The Patch Loader redirects the two calls to the focus renderer made by `UpdateFrame_UI`:

```text
004A947B  primary focused control
004A9505  linked focused control
```

The replacement suppresses the halo only when all of the following are true:

- Control type is 6 (GAF image gadget).
- Control dimensions are 20x20.
- Control name matches `Color%d`.

Every other control is passed to the original focus renderer at `004A16F0`. The player
color image, selected frame number, click handling, and palette indices are unchanged.

The installer validates each enabled call instruction and the focus-renderer entry
signature. It refuses to apply an enabled hook if another executable version or patch
has changed its required locations.

## INI controls

The two call-site hooks can be controlled independently in the embedded
`res/patches.ini` file. Both remain enabled by default:

```ini
[Settings]
PlayerColorFocusPrimary=Yes
PlayerColorFocusLinked=Yes
```

Set either value to `No` to leave that call site completely untouched. To turn off
the entire player-color focus fix, set both values to `No` and rebuild `dplayx.dll`.
Disabled call sites are neither signature-checked nor patched during startup.

## Build

From the Patch Loader directory with a 32-bit MinGW-w64 toolchain in `PATH`:

```powershell
mingw32-make WINDRES=windres STRIP=strip
```

The Visual Studio project includes `color_focus_fix.c` and `color_focus_fix.h`.

## Test checklist

- Enter Skirmish setup and click every visible `Color%d` control.
- Confirm no multicolor outline appears around the selected color.
- Confirm the selected frame remains inside its normal 20x20 control.
- Confirm the rows above and below do not change when focus moves.
- Confirm keyboard/mouse focus highlights remain present on ordinary buttons, edit
  fields, and other non-color controls.
- Repeat in Multiplayer setup.

`TotalA.exe` is never modified on disk.

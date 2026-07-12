<h1 align="center">
  <img src="zenonia3_java/resources/res/drawable/ui_title_logo5.png" alt="Zenonia 3 Logo" width="350"><br>
  Zenonia 3 · PSVita Port
</h1>
<p align="center">
  <a href="#setup-instructions-for-players">How to install</a> •
  <a href="#controls">Controls</a> •
  <a href="#faq">FAQ</a> •
  <a href="#known-issues">Known Issues</a> •
  <a href="#build-instructions-for-developers">How to compile</a> •
  <a href="#credits">Credits</a> •
  <a href="#license">License</a>
</p>

This repository contains a loader of **the Android release of Zenonia 3**,
based on the [Android SO Loader by TheFloW][gtasa]. The loader provides
a tailored, minimalistic Android-like environment to run the official ARMv6
game executable on the PS Vita.

Disclaimer
----------------

**ZENONIA 3** is a registered trademark of Gamevil. The work
presented in this repository is not "official" or produced or sanctioned by
the owner(s) of the aforementioned trademark or any other registered trademark
mentioned in this repository.

This software does not contain the original code, executables, assets, or
other non-redistributable parts of the original game product. The authors of 
this work do not promote or condone piracy in any way. To launch and play
the game on their PS Vita device, users must possess their own legally obtained
copy of the game in form of an .apk file.

Setup Instructions (For Players)
----------------

In order to properly install the game, you'll have to follow these steps
precisely:

- (Recommended) Make sure that you are either on 3.60 enso or 3.65 enso firmware
version. Other versions may work too, but no support are provided for them! If
you experience any issues apart from described in the
<a href="#known-issues">Known Issues</a> section, please upgrade or downgrade
your firmware before asking for support.

- Install or update [kubridge][kubridge] and [FdFix][fdfix] by copying
`kubridge.skprx` and `fd_fix.skprx` to your taiHEN plugins folder
(usually `ur0:tai`) and adding two entries to your `config.txt` under `*KERNEL`:

```
  *KERNEL
  ur0:tai/kubridge.skprx
  ur0:tai/fd_fix.skprx
```

- Make sure you have `libshacccg.suprx` in the `ur0:/data/` folder on your
console. If you don't, use [ShaRKBR33D][shrkbrd] to get it quickly and easily.

- <u>Legally</u> obtain your copy of Zenonia 3 in a form
of an `.apk` file. Make sure that your game is the correct supported version.

    - If you have it installed on your phone, you can 
        [get all the required files directly from it][unpack-on-phone]
        or by using any APK extractor you can find on Google Play.

- Open the `.apk` with any zip explorer (like [7-Zip](https://www.7-zip.org/))
and extract all folders from the `.apk` into `ux0:data/zenonia3` on your Vita.
Example of correct resulting path: `ux0:data/zenonia3/lib/armeabi/libzenonia3.so`

- Install `Zenonia3.vpk` (from [Releases][latest-release]).


Controls
-----------------

|       Button        | Action                                  |
|:-------------------:|:----------------------------------------|
|      ![dpadh]       | Move Left / Right                       |
|      ![dpadv]       | Move Up / Down                          |
|      ![cross]       | Action / Attack / OK                    |
|      ![circl]       | Back / Cancel / Menu                    |
|      ![squar]       | Map / Quick Item                        |
|      ![trian]       | Skip / Special                          |
|      ![trigl]       | Save                                    |
| ![start] + ![selec] | Quit Game                               |

FAQ
----------------

❓ **As soon as I run the app, I get "An error has occured in the following
application. (C2-12828-1)" message. What to do?**<br>
❕ Most likely, it means that after updating/installing the kubridge plugin, you
didn't reboot your console. Reboot is required after any modifications in kernel
plugins.

Another possibility is that you are using some conflicting or too heavy plugins
and kubridge can not run correctly. Try disabling everything but the most
necessary *kernel* plugins, reboot, and try again.

If the problem persists after reboot, please [post a new issue][issue].

❓ **Any other question?**<br>
❕ Check the following "Known Issues" section. If nothing there looks like the
problem you're having, please [post a new issue][issue], and I'll try my best
to help you.

Known Issues
----------------

- **Invisible Text**: In-game text and UI text are currently invisible. The font rendering engine (GFA stubs) requires a custom bitmap/TrueType implementation that is still pending.
- **Menu I/O Load**: The menu re-reads certain assets (like `Title.pzx`) from the disk continuously on input events, which might cause slight loading delays.

Build Instructions (For Developers)
----------------

In order to build the loader, you'll need a [vitasdk](https://github.com/vitasdk)
build fully compiled with softfp usage. The easiest way to obtain one is
following the instructions on https://vitasdk.org/ while replacing the URL in
this command:
```bash
git clone https://github.com/vitasdk/vdpm
```
Like this:
```bash
git clone https://github.com/vitasdk-softfp/vdpm
```

All the required libraries should get installed automatically if you follow the
installation process from https://vitasdk.org/.

After all these requirements are met, you can compile the loader with the
following commands:

```bash
cmake -Bbuild .
cmake --build build
```

Also note that this CMakeLists has two "convenience targets". While developing,
I highly recommed using them, like this:
```bash
cmake --build build --target send # Build, upload eboot.bin and run (requires vitacompanion)
cmake --build build --target dump # Fetch latest coredump and parse
```

For more information and build options, read the [CMakeLists.txt](CMakeLists.txt).

Credits
----------------

- [Andy "The FloW" Nguyen][flow] for the original .so loader.
- [Rinnegatamante][rinne] for VitaGL and lots of help with understanding and
debugging the loader.
- [Bythos][bythos] for the new kubridge with exceptions handling support and
short vector VFP emulation and code generation.

License
----------------

This software may be modified and distributed under the terms of
the MIT license. See the [LICENSE](LICENSE) file for details.

[cross]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/cross.svg "Cross"
[circl]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/circle.svg "Circle"
[squar]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/square.svg "Square"
[trian]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/triangle.svg "Triangle"
[joysl]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/joystick-left.svg "Left Joystick"
[joysr]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/joystick-right.svg "Left Joystick"
[dpadh]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/dpad-left-right.svg "D-Pad Left/Right"
[dpadv]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/dpad-top-down.svg "D-Pad Up/Down"
[selec]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/dpad-select.svg "Select"
[start]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/dpad-start.svg "Start"
[trigl]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/trigger-left.svg "Left Trigger"
[trigr]: https://raw.githubusercontent.com/v-atamanenko/sdl2sand/master/img/trigger-right.svg "Right Trigger"

[gtasa]: https://github.com/TheOfficialFloW/gtasa_vita
[kubridge]: https://github.com/bythos14/kubridge/releases/
[fdfix]: https://github.com/TheOfficialFloW/FdFix/releases/
[unpack-on-phone]: https://stackoverflow.com/questions/11012976/how-do-i-get-the-apk-of-an-installed-app-without-root-access
[shrkbrd]: https://github.com/Rinnegatamante/ShaRKBR33D/releases/latest
[latest-release]: https://github.com/USER/zenonia3-vita/releases/latest
[issue]: https://github.com/USER/zenonia3-vita/issues/new

[flow]: https://github.com/TheOfficialFloW/
[rinne]: https://github.com/Rinnegatamante/
[bythos]: https://github.com/bythos14/

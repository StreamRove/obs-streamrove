# StreamRove for OBS

An OBS Studio dock that signs in to your StreamRove account, lets you pick a
stream, points OBS at that stream's ingest and shows what StreamRove is doing
with it: the destinations it fans out to, live health, and the encoder settings
it recommends for the platforms attached.

OBS sends **one** stream. StreamRove sends it to every connected channel.

## What the dock does

| Control | Effect |
|---|---|
| Server / API key / **Connect** | Signs in with a StreamRove developer key (`mux_…`). Create one under *Account settings → API keys*. The key authenticates as you, so the dock sees exactly the streams your dashboard shows. |
| Stream picker | Lists the streams of your active workspace. The choice is remembered. |
| **Use this stream in OBS** | Sets OBS's stream output to *Custom* with the stream's RTMPS ingest and key — the same thing Settings → Stream does, without copying anything by hand. |
| **Go Live / Stop Streaming** | Starts or stops OBS's stream output. If the ingest was not applied yet, it is applied first. |
| Destinations | Each attached channel with a checkbox: unticking sends `PATCH /streams/{id}/destinations/{destId} {"enabled": false}`, the same call the dashboard makes. |
| Health | Polled every 10 s while live: overall status, score, and the latest explained event. |
| Recommended | The platform-derived encoder recommendation from `/streams/{id}/output-profile`. Shown, not applied — OBS's own Settings → Output stays yours. |
| **Open** | Opens the stream page on StreamRove in your browser. |

Settings live in OBS's plugin config directory (`obs_module_config_path`,
e.g. `~/.config/obs-studio/plugin_config/obs-streamrove/settings.json`). The API
key is stored there as typed, the same way OBS stores stream keys; revoke it
from the StreamRove dashboard if the machine changes hands.

## Install

Downloads are on the [releases page](https://github.com/StreamRove/obs-streamrove/releases).
Quit OBS before installing.

**Windows.** Run `obs-streamrove-<version>-windows-x64-Installer.exe`. Windows asks
for an administrator password, because OBS reads plugins from a machine-wide
directory rather than your own profile. It appears in *Settings → Apps* for
removal. The installer is not code signed yet, so SmartScreen shows a warning on
first run: choose *More info*, then *Run anyway*. Prefer to place the files
yourself? Take the `.zip` instead and extract the `obs-streamrove` folder into
`C:\ProgramData\obs-studio\plugins\`. Note that this is *not* `%APPDATA%`: on
Windows, unlike macOS and Linux, OBS does not look in the user profile at all.

*Portable OBS is the exception.* A portable install reads neither of those
directories, only its own folder, and it wants a different layout. Skip the
installer, take the `.zip`, and place the two pieces by hand:
`obs-streamrove.dll` into `<obs folder>\obs-plugins\64bit\`, and the contents of
`data` into `<obs folder>\data\obs-plugins\obs-streamrove\`.

**macOS.** Open `obs-streamrove-<version>-macos-universal.pkg`. It is not notarized
yet, so Gatekeeper blocks a double click: right-click the file, choose *Open*, then
*Open* again. The `.tar.xz` is the manual route, with `obs-streamrove.plugin` going
into `~/Library/Application Support/obs-studio/plugins/`.

**Linux.** Install the `.deb`, or use the build instructions below.

Start OBS afterwards and open the dock from the *Docks* menu.

## Requirements

* OBS Studio **30.0 or newer** (uses `obs_frontend_add_dock_by_id`).
* Windows x64, macOS (universal), or Linux x86_64.
* Network access to your StreamRove server over HTTPS.

## Build

This tree is the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
with the plugin's sources in `src/`, so every template workflow applies as is.
The GitHub Actions in `.github/` expect this directory to be the **root of its
own repository** (they read `buildspec.json` from the repo root); publish it as
`obs-streamrove` rather than building from inside the monorepo.

### Linux (Ubuntu 24.04)

```sh
sudo apt install cmake g++ ninja-build pkg-config libobs-dev qt6-base-dev libgl-dev libcurl4-openssl-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_FRONTEND_API=ON -DENABLE_QT=ON
cmake --build build
# install for the current user:
mkdir -p ~/.config/obs-studio/plugins/obs-streamrove/bin/64bit ~/.config/obs-studio/plugins/obs-streamrove/data
cp build/obs-streamrove.so ~/.config/obs-studio/plugins/obs-streamrove/bin/64bit/
cp -r data/locale ~/.config/obs-studio/plugins/obs-streamrove/data/
```

The same steps run inside a throwaway container if you do not want the
toolchain on your machine:

```sh
docker run --rm -v "$PWD":/src -v obs-build:/build ubuntu:24.04 bash -c '
  apt-get update -qq && apt-get install -y -qq --no-install-recommends cmake g++ ninja-build pkg-config \
    libobs-dev qt6-base-dev libgl-dev libcurl4-openssl-dev >/dev/null &&
  cmake -S /src -B /build/b -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_FRONTEND_API=ON -DENABLE_QT=ON &&
  cmake --build /build/b && cp /build/b/obs-streamrove.so /src/'
```

### macOS and Windows

Use the template presets; they download the pinned OBS sources and prebuilt
dependencies named in `buildspec.json`:

```sh
cmake --preset macos          # Xcode; then: cmake --build --preset macos
cmake --preset windows-x64    # Visual Studio 2022; then: cmake --build --preset windows-x64
```

Or push a tag and let CI build all three platforms (see *Releasing*).

## Releasing

1. Set the version in `buildspec.json` (`"version": "0.1.0"`).
2. Commit, tag exactly that version (`git tag 0.1.0 && git push --tags`).
3. The `push.yaml` workflow builds Windows, macOS and Ubuntu packages and opens a
   **draft** GitHub Release with them attached. Review, then publish. Windows gets
   both a `.zip` and an Inno Setup installer built from
   `cmake/windows/resources/installer-Windows.iss.in`; CMake generates the compiled
   script into the build directory and `Package-Windows.ps1` runs ISCC over it.
   Building the installer locally needs Inno Setup 6.3 or newer on the machine.
4. macOS: without the signing secrets the `.pkg` is unsigned and Gatekeeper
   blocks it for users. Add the eight `MACOS_*` repository secrets the template
   documents (Developer ID Application + Installer certificates, notarization
   Apple ID and app-specific password) and the workflow signs and notarizes.
5. Windows builds are unsigned by default; SmartScreen warns on first run.
   A code-signing certificate can be added to `Package-Windows.ps1` later.

## Getting listed inside OBS

Two routes, independent of each other:

* **OBS Forums → Resources.** The plugin itself is listed under
  [OBS Studio Plugins](https://obsproject.com/forum/resources/), with the GitHub
  Release page as the download. New versions are posted as updates to the same
  resource.
* **OBS's built-in service list.** A pull request to
  [obsproject/obs-studio](https://github.com/obsproject/obs-studio) adds StreamRove
  to `plugins/rtmp-services/data/services.json`, so it appears under
  *Settings → Stream → Service* without any plugin. `docs/obs-services-entry.json`
  is the entry to submit; the server it names is the anycast ingest StreamRove
  streams use, and the key comes from the stream's page on StreamRove.

## License

GPL-2.0-or-later, like OBS Studio and the template.

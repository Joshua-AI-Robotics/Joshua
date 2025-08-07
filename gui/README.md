### Project Joshua GUI — Qt6 setup and Bazel build

#### 1) Install Qt6 on Linux (Ubuntu/Debian)
- Update packages:
  ```bash
  sudo apt-get update && sudo apt-get upgrade -y
  ```
- Install Qt6 development packages (provides headers and moc at /usr/lib/qt6/libexec/moc):
  ```bash
  sudo apt-get install -y qt6-base-dev qt6-base-dev-tools libgl1-mesa-dev
  ```
- Optional (Qt Creator via Qt Online Installer): follow Stanford’s guide to install a desktop Qt 6.x kit if you prefer the Qt installer/Creator workflow: [Install Qt Creator on Linux](https://web.stanford.edu/dept/cs_edu/resources/qt/install-linux).

Quick verification
- Check moc exists:
  ```bash
  test -x /usr/lib/qt6/libexec/moc && echo "moc OK" || echo "moc missing"
  ```
- Check headers exist:
  ```bash
  ls /usr/include/x86_64-linux-gnu/qt6 >/dev/null && echo "Qt6 headers OK"
  ```

#### 2) Build and run with Bazel
From the repo root:
- Build:
  ```bash
  bazel build //gui:gui_app
  ```
- Run:
  ```bash
  bazel run //gui:gui_app
  ```

#### 3) How the Bazel build works (no custom scripts, MOC auto)
- System Qt6 via @local_deps
  - `MODULE.bazel` registers a repository rule from `system_deps.bzl` that symlinks `/usr` into an external repo `@local_deps` and exposes Qt as Bazel `cc_library` targets: `@local_deps//:qt6_core`, `qt6_gui`, `qt6_widgets`.
  - These targets provide the include paths (e.g., `/usr/include/x86_64-linux-gnu/qt6/...`) and link flags (e.g., `-lQt6Widgets`).
- Automatic MOC generation
  - `gui/qt_moc.bzl` defines a `qt_moc` rule that runs Qt’s `moc` and generates `*.moc.cc` from headers with `Q_OBJECT`.
  - Default moc path is `/usr/lib/qt6/libexec/moc`. If your system differs, update `moc_path` in `gui/BUILD`:
    ```python
    qt_moc(
      name = "moc_sources",
      hdrs = ["main_window.h", "control_panel.h", "status_panel.h"],
      moc_path = "/custom/path/to/moc",
    )
    ```
- Targets in `gui/BUILD`
  - `qt_moc("moc_sources", ...)` emits generated `.moc.cc` files.
  - `cc_library("gui_lib", ...)` compiles sources plus `:moc_sources`, and depends on `@local_deps//:qt6_widgets`.
  - `cc_binary("gui_app", ...)` is the runnable GUI.

#### 4) Troubleshooting
- moc not found: `sudo apt-get install -y qt6-base-dev qt6-base-dev-tools`, or set `moc_path` to the correct executable.
- Missing Qt headers: ensure `/usr/include/x86_64-linux-gnu/qt6` exists (use the apt install above). Some distros package additional Qt modules separately.
- Link errors: confirm the `qt6_*` dev packages are present; the build links dynamically against system Qt6 libs.

Reference
- Stanford guide (Qt installer/Creator overview for Linux): [Install Qt Creator on Linux](https://web.stanford.edu/dept/cs_edu/resources/qt/install-linux)

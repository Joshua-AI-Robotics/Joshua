# Third-Party Notices

Joshua source code in this repository (excluding third-party components listed
below) is licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE)
and [NOTICE](NOTICE).

This project includes third-party software. Each component is licensed under its
own terms. Refer to the respective project sites for full and up-to-date license
texts.

## Dependencies and Licenses (summary)

- ROS2 (Robot Operating System 2): Apache-2.0 — https://docs.ros.org/en/humble/
- Protocol Buffers: BSD-3-Clause — https://developers.google.com/protocol-buffers
- Bazel: Apache-2.0 — https://bazel.build/
- Qt6: LGPL-3.0 — https://www.qt.io/
- OpenCV: Apache-2.0 — https://opencv.org/
- Abseil C++: Apache-2.0
- Boost.Asio: BSL-1.0 — https://www.boost.org/LICENSE_1_0.txt
- gflags: BSD-3-Clause
- glog: BSD-3-Clause
- PyTorch: BSD-3-Clause — https://pytorch.org/
- Transformers (Hugging Face): Apache-2.0 — https://huggingface.co/transformers/
- NumPy: BSD-3-Clause — https://numpy.org/
- Pillow (PIL): HPND
- Pandas: BSD-3-Clause — https://pandas.pydata.org/
- PyArrow: Apache-2.0 — https://arrow.apache.org/
- libevdev: MIT — https://www.freedesktop.org/wiki/Software/libevdev/

## Qt6 (LGPL-3.0)

The Joshua Control Panel (`joshua_control_panel/`) uses Qt6. When you distribute
binaries that link to Qt (including Docker images that bundle Qt libraries),
LGPL obligations apply (for example: allow users to relink or replace Qt,
provide license notices, and obtain Qt source as required by LGPL-3.0). Prefer
dynamic linking to system Qt packages where possible. See
https://www.qt.io/licensing/

## Notes

- Some licenses require attribution or inclusion of their license text in distributions.
- LGPL components (e.g., Qt6) have additional obligations for dynamic vs. static linking.
- Always verify current license terms in upstream repositories.

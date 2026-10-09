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
- SOEM (Simple Open EtherCAT Master) v2.0.0: GPLv3 or commercial license — https://github.com/OpenEtherCATsociety/SOEM/tree/v2.0.0

## SOEM

Joshua uses the SOEM C library for EtherCAT communication through the `@soem`
Bazel dependency, pinned to v2.0.0 in [MODULE.bazel](MODULE.bazel).

Upstream copyright notices:

- Copyright (C) 2005-2025 Speciaal Machinefabriek Ketels v.o.f.
- Copyright (C) 2005-2025 Arthur Ketels
- Copyright (C) 2009-2025 RT-Labs AB, Sweden

SOEM v2.0.0 is dual-licensed under the [GNU General Public License, version 3](https://www.gnu.org/licenses/gpl-3.0.html)
or a commercial license available from RT-Labs. See the
[upstream v2.0.0 license notice](https://github.com/OpenEtherCATsociety/SOEM/blob/v2.0.0/LICENSE.md)
for the applicable terms and commercial licensing contact.

## Notes

- Some licenses require attribution or inclusion of their license text in distributions.
- Always verify current license terms in upstream repositories.

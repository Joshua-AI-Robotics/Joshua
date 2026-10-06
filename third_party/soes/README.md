# SOES (firmware slave stack)

Pinned to `6ef7b9415e936c061ad30626ca00004bdde2ac62` from
[OpenEtherCATsociety/SOES](https://github.com/OpenEtherCATsociety/SOES).
This is **not** SOEM, the host master library.

SOES is GPLv2 with the upstream linking exception. Keep its LICENSE and source
available when distributing firmware containing it; Joshua's Apache license does
not relicense SOES. The TI SDK and PRU firmware remain external, separately
licensed dependencies of the AM243 port.

`upload_size.patch` makes the storage passed to SOES's `size_t *` upload hooks
actually `size_t`, avoiding an out-of-bounds access on 64-bit native builds.
The same patch is applied by Bazel and the AM243 firmware build. Joshua's compiler
shim and fixed JW mailbox/PDO settings live in `firmware/common/soes`.

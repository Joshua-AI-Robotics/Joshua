# AM243 UART JW firmware

This profile patches the TI 401 demo to add a software-only JW UART endpoint.
It uses the shared complete-frame endpoint and a nonblocking UART adapter;
channel servicing continues while a reply is pending. UART commands do not
drive physical motors on AM243. The inherited TI-demo process data is a separate
legacy profile, not the JW EtherCAT protocol.

Build explicitly with the Industrial Communications SDK 09.00.00.03:

```bash
firmware/am243/joshua_dual_transport/scripts/build.sh
```

The build produces `out/am243_dual_transport_jw.release.appimage`. Flashing
is a deliberate operation; follow `firmware/README.md`. Native adapter tests use
TI SDK fakes and do not access UART hardware.

## Opt-in JW EtherCAT profile

Set `JOSHUA_ETHERCAT_PROFILE=jw`, `JOSHUA_COMM_WATCHDOG_US` and
`JOSHUA_TARGET_WATCHDOG_US` explicitly before running `scripts/build.sh`. Both
watchdog values must be 10000..999999999 microseconds. This selects the shared
JW descriptor, session, CoE mailbox and correlated 80-byte PDO profile through
the TI stack. Its channel remains software-only. The artifact is
`out/am243_ethercat_jw.release.appimage`, distinct from the UART/TI-demo artifact.

Native tests use the production firmware profile and command handler to cover
retained replies, cross-plane IDs, duplicate handling, watchdog expiry,
operational-state loss and host interoperability. They do not qualify real
EtherCAT timing or physical motor outputs.

TODO: retain this TI-stack JW profile until the separate SOES replacement has
completed AM243 hardware qualification and endurance testing, then review its
retirement.

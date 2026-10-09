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

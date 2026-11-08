# Poulsbo USB device controller

Experimental port of Intel's GPL-2.0-only `iusbc` driver to the ReRead kernel,
`5.10.256-antix.1-486-smp`. It binds **PCI 8086:8118**, not Intel's unrelated
Langwell or EG20T controllers. Revision `reader2` has working USB Ethernet on
the physical Reader with Fedora: carrier, DHCP, HTTP file transfers and SSH.
Other hosts and sleep/reconnect behavior need further testing.

## Source

Recovered from Ubuntu's official
[linux_2.6.24-32.107.diff.gz](https://old-releases.ubuntu.com/ubuntu/pool/main/l/linux/linux_2.6.24-32.107.diff.gz),
SHA-256 `e9e8aa66337c6239d0d06e4293469f9767258548d35fd305e951e282bd75d342`.
The outer distribution diff adds
`debian/binary-custom.d/lpia/patchset/0017-poulsbo_USBC.patch`. That nested patch
contains `drivers/usb/gadget/iusbc.c` (3120 lines) and `iusbc.h` (213 lines).
Strip the outer `+` prefix, then extract the additions for those two new files.
Copyright Intel Corporation, 2006-07. See [COPYING](COPYING).

The recovered source identifies itself as `2.0.0.32L.0010`; the Reader's stock
module is **newer**, `2.3.0.32L.0014`. This is not recovered source for that exact
stock binary. Its remaining hardware fixes may matter. Preserve that distinction
when debugging.

## Port changes

- Modern UDC core registration and start/stop callbacks replace private gadget
  driver registration exports; the core owns function binding/unbinding.
- Endpoint capabilities and maximum packet limits support modern gadget
  endpoint selection. The original two bulk and one interrupt data endpoints
  are retained.
- Current DMA pool/mapping APIs, a 32-bit DMA mask and mapping-error handling.
- GET_STATUS uses a controller-owned request instead of requiring patched
  function drivers. Class/vendor requests go directly to the function driver.
- Allow request queueing while DMA is paused during completion callbacks,
  matching the newer stock module. Ethernet reuses callbacks for receive
  requests and link notifications; the original early `ep->stopped` rejection
  prevented this.
- Interrupt/tasklet teardown, probe failure cleanup, and sleep without an
  attached function are adapted to the newer lifecycle.
- Remove obsolete buffer-allocation callbacks and obsolete includes; disable
  per-transfer debug logging.

`antix/gadget.py` builds this against matching antiX headers. Its helper programs
are rebuilt for the build machine; the module itself remains i386. Module and
compiler outputs live under ignored `build/antix/` during normal image builds.

## First physical test

The first physical image enumerated on Fedora as CDC Ethernet with the correct
host MAC, but reported no carrier and received no packets, even with a temporary
manual host address. That host profile was removed after testing. Revision
`reader2` fixes completion-callback queueing. The physical retest established
carrier, DHCP, HTTP and SSH; a host profile set to link-local also needed to be
changed to automatic/DHCP addressing.

Install the generated ReRead image, then connect **mini-B** to a computer
with a data cable. The expected result is a USB Ethernet device, host address
`192.168.7.2`, and the Reader at `192.168.7.1`. Linux is the first host target;
Windows RNDIS driver selection and macOS enumeration need separate verification.

The screen reports controller-load/no-controller failures or the web address.
The address means the interface was configured locally, **not** that the host
successfully enumerated or received its DHCP lease. Check the computer too.
Logs: `/var/log/reader/usb.log`, `web.log`, `ssh.log`, and kernel `dmesg`.
Test reconnect and sleep/wake after initial file transfers work.

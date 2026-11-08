# USB Ethernet bring-up, 2026-11-07

First image (`iusbc` revision `reader1`) enumerates on the owner's Fedora host:

- USB `0525:a4a2`, high speed, manufacturer identifies Linux 5.10 with `iusbc`.
- Host driver `cdc_ether`, interface `enp0s20f0u3`, MAC `02:52:44:52:00:02`.
- ECM configuration selected, data-interface alternate setting 1.
- Host reports `NO-CARRIER`; no DHCP address or received Ethernet packets.
- Reader screen displays its configured address, `192.168.7.1`.

A temporary, unsaved NetworkManager profile assigned the host `192.168.7.2/24`,
with no default route and IPv6 disabled. Activation succeeded despite missing
carrier. HTTP timed out; ARP resolution failed, with zero RX packets. This
rules out merely lacking a host IP address as a sufficient explanation. It
does not prove the exact controller failure. The temporary profile was then
deactivated and deleted; Wi-Fi remained the normal default route.

## Completion-callback queueing fix

`done()` removes the completed request, sets `ep->stopped = 1`, drops the lock,
invokes the function's completion callback, then restores `ep->stopped`.
The imported old source's `iusbc_queue()` immediately rejected a stopped
endpoint. This prevented callbacks from queueing their next request even though
the later queue code explicitly supports deferring DMA while stopped. IRQ
handlers restart queued requests after completion returns.

The newer stock module's `iusbc_queue()` has no equivalent early stopped-endpoint
rejection. Static comparison used the stock module SHA-256
`3497d5e25a670018f9578c3dbef945e62b47a300383acd462eb0a264a658b412`.
REA/Hopper evidence:

- `ev_2ecc4ebfaf613e9d13f671eae888d84dc53a33458c5d748b1d7fa796423b97d3`: stock `iusbc_queue`.
- `ev_473ddcc7c2adc189a398f37ef4edb3808bba8de70130eb1d727790bf262f201e`: stock `iusbc_ep_enable`.
- `ev_593823580dfc89588ae84b87d4dc1ae42d45f27cc5d2cff7e626bcdbd21fc112`: stock `iusbc_probe`.

Snapshot: ignored `analysis/inventory/iusbc.rea-snapshot.json`. These are static
observations, not traces of the Reader running. Hopper's public API does not
provide complete function-body ranges, and its pseudocode can misrepresent
register arguments and relocations; comparisons use the control-flow checks.

Linux's [ECM notification callback](https://github.com/torvalds/linux/blob/v5.10/drivers/usb/gadget/function/f_ecm.c)
queues subsequent notifications, and its
[Ethernet receive callback](https://github.com/torvalds/linux/blob/v5.10/drivers/usb/gadget/function/u_ether.c)
resubmits receive requests. Revision `reader2` removes the premature rejection;
existing endpoint-descriptor, disconnected-device and suspended-device checks
remain. Build against the matching kernel headers passed. Its effect on carrier,
DHCP and traffic needs a physical retest.

An initial DMA-mode suspicion was **not established**: the stock module also
selects its mode from the capability flag. No DMA-mode change was made.

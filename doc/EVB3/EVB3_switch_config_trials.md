# FS S5850-32S2Q: settings to try against the silent frame loss (2026-10-16)

For whoever runs the EVB switch. Background and the CLI forms that are known to work on
this unit are in `ai_skills/evb_switch_cli.md`; the loss history is in
`EVB3_software_status_registers.md` (bit 1) and the memory notes.

## What we know

- FS S5850-32S2Q, FSOS 7.4.5 (2021), factory-bare config plus two static MAC entries.
- About 1 in 2000 to 5000 of the largest frames (1508 B, 186 words) vanish inside the switch,
  both directions, uncounted: port in = sender's TxCount, port out = in - 1, every error,
  discard, policer and per-queue drop counter at 0.
- Ruled out: frame length (1024 B max still lost), frame spacing (20x gap, same rate), the
  nonstandard runt padding (fixed 2026-10-14, same rate), static MAC entries, storm control,
  policers, EFD, ACL/QoS (none configured), cut-through (already off), flow control (off).
- A Cisco switch in the same spot: no loss. A direct cable: no loss.

So it is this switch's forwarding behaviour, not a counter we have not found. The trials
below are the knobs that change how the forwarding path handles our frames. Change ONE at a
time, run the standard check (TX off both DTCs, `clear counters`, one run, TX off, compare
in/out per direction and 0x9174 resend counts), then `write` only if it helped.

## Why this traffic is unusual to a switch

Our frames are IEEE 802.3 length-field frames with no EtherType, no VLAN tag, no IP, from a
MAC the switch has never seen a protocol from, at a sustained near-line-rate burst of
max-size frames with the minimum gap. Most switch firmware is tested with EtherType frames
and IP. Anything in the forwarding path that looks past the MAC header (classification,
snooping, learning-rate limits) is on untested ground with our frames.

## Trials, in order of how likely they are to matter

### 1. Hardware MAC learning (most likely)

Default is software learning: the CPU learns source MACs from copies of frames. On a
near-line-rate burst the learn path can be the one thing that touches every frame, and a
learn-queue overflow or a learn/age race on a frame in flight is exactly the kind of drop
that lands in no counter. Static entries did not fix it, which fits: the CPU still sees the
frame for learning even when the entry is static.

```
configure terminal
 mac-address-table hardware-learning enable
end
show running-config | include hardware-learning
```
Undo: `no mac-address-table hardware-learning enable`. No reload needed per the manual.

### 2. Stop learning entirely on VLAN 1

Our two MACs are static. Turning learning off removes the learn path from the frame's way
altogether (and the aging path: `show mac address-table` was empty between runs, meaning
dynamic entries were being aged and relearned under traffic before the static entries).

```
configure terminal
 vlan database
 vlan 1
 vlan 1 mac learning disable
 exit
end
show vlan-security
```
Undo: `vlan 1 mac learning enable`. Keep the two static entries in place for this.

### 3. Cut-through ON (opposite of the current setting)

It is store-and-forward now. Cut-through changes the egress path and the buffer handling for
large frames. Needs a reload. Try it with the DTCs idle.

```
configure terminal
 cut-through-forwarding enable 10G-40G-100G
end
write
reload
show cut-through-forwarding mode
```
Undo: `no cut-through-forwarding enable`, `write`, `reload`.

### 4. Block unknown-unicast flooding on both DTC ports

The MC0 flood queue on eth-0-32 carried 300 million 116-byte idle frames over the switch's
life: our frames were being flooded as unknown-unicast for long stretches. With static
entries that should be gone, but the port-block removes the flood path from consideration
and forces a table hit or a drop that the MC queue counters would show.

```
configure terminal
 interface eth-0-31
  port-block unknown-unicast
 interface eth-0-32
  port-block unknown-unicast
end
show interface eth-0-31 | include blocked
```
Undo: `no port-block unknown-unicast` on each port. If the loss rate CHANGES (up or down)
the flood path is involved; if the DTCs stop talking, the static entries are not being hit
and that is itself the finding.

### 5. Port isolation / spanning tree / snooping off on the two ports

Each of these can drop or divert a frame without a port counter. All default on or auto.

```
configure terminal
 no spanning-tree enable
 interface eth-0-31
  spanning-tree port disable
  no lldp enable
 interface eth-0-32
  spanning-tree port disable
  no lldp enable
 no ip igmp snooping          (if the form is rejected: igmp snooping disable, or per vlan 1)
end
```
Undo with the `no`/enable forms. STP should already be forwarding on both; this removes the
BPDU-guard / loopback-detection path (`show errdisable detect` lists bpduloop and
loopback-detection as Enabled by default).

### 6. Aging time to maximum

Only matters if trial 2 is not possible. The default is 300 s with a 300 to 600 s window;
set it to the maximum so no entry ages during a run.

```
configure terminal
 mac-address-table aging-time 1000000
end
show mac address-table aging-time
```
(If 1000000 is out of range the error message gives the limit.)

### 7. Egress queue depth for the DTC ports

Per-queue drop counters read 0, so this is low probability, but it is cheap. Raise the tail
drop limit on the queue our frames use (queue 0, untagged, priority 0) on both ports.

```
configure terminal
 class-map type traffic-class tc0
  match traffic-class 0
  exit
 policy-map type traffic-class evb
  class type traffic-class tc0
   queue-limit 8000
   exit
  exit
 interface eth-0-31
  service-policy type traffic-class evb
 interface eth-0-32
  service-policy type traffic-class evb
end
show qos interface eth-0-31 statistics queue
```
Undo: `no service-policy type traffic-class evb` on each port. Units are 288-byte cells.

### 8. Firmware

7.4.5 is from 2021. FS has published 7.4.6, 7.4.8 and 7.4.12 for S5850 models; the release
notes seen so far list unicast forwarding fixes (MLAG, VXLAN split-horizon) but nothing
matching ours. Worth asking FS support for the newest 32S2Q image and its notes before
spending more bench time: a one-line "known unicast dropped under line-rate burst" fix would
end this. The support case should quote: FSOS 7.4.5, HW 2.0, two static MAC entries, 802.3
length-field frames, max 1508 B, loss ~1/3000 of max-size frames both directions, all
counters 0, Cisco in the same spot clean.

### 9. Port mirror for FS support (diagnostic, not a fix)

Mirror eth-0-31 egress to a spare up port with a capture box. Counters say the frame enters
and never leaves; a capture at the egress mirror point tells FS whether the frame reached the
egress queue. `monitor session 1 source interface eth-0-31 tx` / `destination interface
eth-0-N` (check with `monitor ?`).

## What would make it a firmware (DTC) item after all

None of the above is a DTC change. If trial 1 or 2 stops the loss, the switch was losing
frames in its learn path and the DTC was fine. If nothing helps and the Cisco stays clean, the
answer is the Cisco, and the retransmission layer stays as insurance.

## Things that will NOT help (already tested or not applicable)

Static MAC entries (done, no change), MTU (9600 already), flow control (off; turning it on
would only matter if a queue overflowed, and none did), FEC (off, link is clean), policer /
storm control (none configured), frame size and spacing on the DTC side (tested).

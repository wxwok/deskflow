# Per-monitor layout

Deskflow normally treats each computer as one rectangle, the bounding box of its monitors, and switches only on the outside of that rectangle. A monitor edge that sits inside the box, because another monitor of the same computer sticks out further, can never switch: the operating system will not move the pointer into the empty part of the box.

`section: layout` opts into a shared virtual desktop. Each computer reports the rectangles of its monitors. The server places each computer's group with an origin. The pointer switches when it hits a monitor edge that is not continued by another monitor of the same computer and that overlaps a monitor of a different computer.

## Migration

- No `section: layout` means the neighbour links behave exactly as before.
- A layout section replaces pointer switching. The links remain in the file and in the grid UI, and are ignored for the pointer while the layout is present.
- Hot keys (`switchToScreen`, `switchInDirection`) are unchanged.
- A peer that speaks protocol 1.8 or older is one rectangle, the `DINF` shape, placed at that computer's origin.
- The server configuration dialog writes the section only when "Use monitor layout" is checked. The checkbox is off by default.
- An external config file is used as written. Put `section: layout` in that file. The arrangement view edits the internal config.
- Computers named by the layout are added if they are not already known, so a layout-only file can be read. Link lines are still parsed first and still need the computer to be registered in the general settings (the GUI does this when it saves).

## Protocol 1.9

The minor version becomes 9. Hello negotiates `min(client, server)`. Peers that settle below 9 never send or expect the new message.

After every `DINF`, a 1.9 client sends:

```
DMON <count:int16>
repeated count times: <name:string> <x:int32> <y:int32> <width:int32> <height:int32>
```

Coordinates are pixels in that computer's own space. The rectangle is half-open. `count` is at most 64. Non-positive sizes are ignored. `DINF` is still acknowledged with `CIAK`; `DMON` has no separate ack. The primary computer's monitors come from the local platform, not from this message.

The application version is intentionally not bumped here. That stays with the maintainers when the protocol change is accepted.

## What the server does

Connected computers that appear in the layout are placed at their configured origins. Live monitor rectangles replace the saved ones; the origin stays. A disconnected computer is not a switch target. Saved rectangles are the fallback when a connected computer has not reported any.

On the primary computer the pointer is still inside a monitor. The edge pixel (the jump zone, one pixel on current platforms) of a switchable edge is a hit. On a secondary computer the server integrates relative motion. A point that lands inside any monitor of that computer stays there, including when the segment crosses a gap between two of its own monitors. Leaving onto another computer carries the overshoot inward on the target, then clamps into that monitor.

Corner locking still uses the bounding box. Switch delay and double-tap still apply.

## Wayland, X11, Windows, macOS

Wayland input capture only notices an edge that has a pointer barrier, and a barrier must lie on the outside of the union of the zones and on a single zone. The portal backend places barriers on the free parts of each zone edge (the parts no other zone touches, which includes concave notches) that overlap a switch edge. If the portal coordinates cannot be matched to those edges, it falls back to every free edge; a dead edge is captured and then released. Legacy mode, used when the layout is empty, still barriers only the outer bounding-box sides that have a neighbour.

X11, Windows, and macOS report pointer motion. The server treats the edge pixel as the switch. The Windows hook is told about a bounding-box side only when a switch edge lies on that extreme, so it does not trap the cursor on a dead outer edge. Interior edges are seen because the hook forwards every move and the compositor clamps the cursor to the monitor.

## Arrangement view

The Monitor layout tab drags each client as one group. The server stays where it is. While dragging, a monitor edge within about 12 screen pixels of another computer's monitor edge snaps to it. The axes are independent: opposite edges meet with no gap (left against right, top against bottom), and parallel edges line up (top against top, and the same for the other sides). The nearest edge wins. A placement that would overlap another computer is moved to the nearest snapped position that only touches. Hold Alt or Shift while dragging to ignore the magnet. The origin boxes set an exact position, and the arrow keys nudge by 1 px, or by 10 px with Shift; hold Alt to nudge without snapping. Orange marks the edges that will switch the pointer.

## Example

`docs/examples/monitor-layout.conf` places sur3 to the left of the server's lower-left monitors and a Windows laptop to the right of the lower built-in panel. See that file for the coordinates.

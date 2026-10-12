# Pan/tilt camera head v2 — ESP32-S3-CAM + 2× MG90S + 2× HLK-LD2410C

No STM32 box any more. The whole thing is: a base plate with the pan servo in a turret, a pan
head that carries the tilt servo, a pivot column and the two radar holders, and a camera pod
with the ESP32-S3-CAM lying inside it. Everything is wired to the ESP32 board in the pod.

Print file: **`out/pantilt_cam_v2_A1mini.3mf`** (Bambu Studio, two A1 mini plates, no supports).
Plain STLs are in `out/*.stl` as well, and `out/pantilt_cam_v2_generic.3mf` is a plain 3MF for
other slicers.

![assembled](out/fig1_assembled_iso.png)
![exploded](out/fig5_exploded.png)

## What's in the 3MF

| plate | part | print orientation | notes |
|---|---|---|---|
| 1 | 1 Base plate + pan turret | as is, plate down | 100 × 80 × 4 plate, 4× Ø4.5 holes with Ø9 counterbores (M4 / #8 screws), Ø51 turret, pan-servo pocket, lead tunnel out the side at plate level |
| 1 | 3 Camera pod | lens face down | 77 × 36 × 35 box, ESP32-S3-CAM lies inside with the lens looking out the front |
| 1 | 4 Pod back cover | outer face down | snap tabs top/bottom, pry notches at both ends, three wire notches on the bottom edge |
| 1 | 5 Pivot pin | standing on its head | Ø4.8 × 14 shank. Print 2–3, they are tiny |
| 2 | 2 Pan head | plate down | yoke plate + spline hub + tilt-servo wall + pin column + two LD2410C holders at ±45° |

All parts are designed for **0.2 mm layers, 0.4 nozzle, 3 walls, 15–20 % infill, PLA or PETG, no
supports**. The only "bridges" are small (ring groove 4 mm, lead tunnel 9 mm, servo window 13 mm,
USB opening 25 mm) and every horizontal hole is a teardrop. A brim on the pan head is a good idea
because the radar wings are long and thin.

## Bill of materials

- 1× ESP32-S3-CAM (the 68 × 29 mm dual USB-C board, OV3660 on the 21 mm ribbon)
- 2× MG90S (SG90 footprint). **They must be positional 180° servos, not "360°/continuous
  rotation" ones** — see the warning below. Each kit's 2 ear screws + 1 centre screw are used.
- 2× HLK-LD2410C with the 5-pin header soldered (pins on the component side)
- female–female Dupont jumper wires (3 per servo lead extension if needed, 4–5 per radar: VCC, GND, TX, RX, OUT)
- 4× M4 (or #8 wood) screws for the base plate
- foam tape or a 1 mm thermal pad for the ESP32 module, a dab of hot glue for the camera head
- USB-C cable into the OTG port for power (2 A supply; the two servos can pull >1 A when they stall)

## How it goes together (in this order)

1. **Pan servo into the base.** Drop the MG90S into the turret pocket, shaft end towards the tunnel
   side. Push its lead through the slot in the pocket's end wall and out of the tunnel at plate
   level (the JR plug fits through the 9 × 4.5 mm tunnel). Two kit ear screws into the slotted
   pilot holes.
2. **Pan head onto the spline.** Press the hub's Ø4.7 socket onto the spline (ream to 4.8 if it is
   really tight), then the kit centre screw down through the hole in the hub boss. Do this *before*
   the pod is on — the pod sits over the hub afterwards. The turret ring runs inside a groove on the
   underside of the head with 1 mm radial clearance; it is a wobble limiter and dust shield, not a
   bearing.
3. **Radars.** Each LD2410C slides down into its holder from the top, antenna side (the side with the
   two square copper patches) facing **out**, header edge at the **bottom**, so the Dupont housings on
   the pins lie flat on the plate pointing back towards the middle. Keep them there with a drop of
   hot glue on the top edge if you want.
4. **Board into the pod.** Stick the camera head onto the ESP32 module can with foam tape so the lens
   barrel goes through the Ø8 hole in the front wall (there is a 9.4 × 9.4 recess for the square head).
   Lay the board in with the component side towards the front wall — the module rests against the
   wall (put the thermal pad there), the USB-C ports face the opening in the end wall, header pins
   point at the cover. Plug the servo and radar leads onto the pins, run them out of the bottom wire
   notches, snap the cover on (the posts inside the cover press the board's free corners).
5. **Pod onto the head.** Lower the pod between the wall and the column with the big boss on the
   servo-wall side. Push the pivot pin through the column into the small boss on the pod's USB end.
6. **Tilt servo last, from outside.** Slide the MG90S through the window in the wall, shaft first, so
   its spline enters the pod's socket. The ear screws go through the ears into the slotted pilot
   holes in the wall. The centre screw for this one goes in from *inside the pod* through the Ø2.3
   hole in the end wall, so do it before you close the cover for the last time (or skip it; the
   press fit plus the pin on the other side hold the pod fine).
7. Screw the base plate down, plug the USB-C cable into the OTG port, done.

Pan servo lead comes out of the turret at plate level and has to climb up to the pod: leave a
100 mm service loop so the head can pan ±90° without pulling on it.

## Motion limits

- Tilt: the pod clears the head plate and the radar holders at **every** tilt angle (swept with the
  checker from −90° to +90°).
- With a USB-C cable plugged in, looking **down** is limited to about +60° before the plug touches
  the pivot column. Looking up is unlimited.
- Pan: mechanically unlimited, but the leads are not — keep the pan range to ±90° in firmware.

## About the "360°" MG90S

A micro servo sold as "360°" is almost always a *continuous rotation* servo: the pulse width sets
speed and direction, not position, and it has no idea where it is. That is useless for pointing a
camera — the tilt would just fall over. If yours spin freely when you send them a fixed pulse,
swap them for ordinary 180° MG90S (identical case, identical mounting, same kit screws; this model
does not change). If they do hold a position, ignore this paragraph.

## Wiring

Everything plugs onto the ESP32-S3-CAM's header pins on the back of the board:

- Servo leads: brown → GND, red → 5V, orange → a free GPIO per servo (two PWM outputs).
- Each LD2410C: VCC → 5V, GND → GND, TX → an ESP32 RX pin, RX → an ESP32 TX pin (256000 baud), or
  just OUT → a GPIO if you only need "someone is there".
- Which GPIOs are free depends on the camera pin map for this board (see `camera_pins.h` in the
  repo for the DVP pins to avoid). G1, G2, G21, G47, G14, G13 are the usual candidates on this
  board; G19/G20 are the native USB pair, so leave those alone.

## Regenerating / changing dimensions

Everything comes from `gen_pantilt_v2.py` (one parameter dict `P` at the top).

```
pip install manifold3d trimesh shapely scipy numpy pillow lxml
python3 gen_pantilt_v2.py    # STLs in out/
python3 check_v2.py          # interference, component clearance, tilt sweep, overhang report
python3 render_v2.py         # figures
python3 pack_3mf.py          # Bambu 3MF (A1 mini) + generic 3MF
```

Key numbers if you need to tweak the fit:

| what | value |
|---|---|
| servo pocket / window | 23.4 × 12.9 (MG90S case 22.8 × 12.2) |
| ear screw pilots | 1.8 mm wide slots, 2.4 long, on 28.0 mm centres |
| spline sockets | Ø4.7 × 5 deep, Ø13 × 0.8 dome relief at the entrance, Ø2.3 centre-screw hole |
| head underside above the pan ear seat | 8.5 mm (MG90S dome top ≈ 8.6, spline 9.0 → 12.4) |
| tilt axis | 38 mm above the head plate top, 5 mm behind the pod's depth centre |
| pod cavity | 72 × 31 × 29.5 (board 68 × 29 incl. module overhang, 24 mm behind the PCB for Dupont housings) |
| radar holder grooves | 2.0 mm wide, 1.2 mm deep each side (PCB 1.0–1.6) |
| pivot pin | Ø4.8 in a Ø4.9 column hole (friction) and Ø5.2 pod hole (free) |
| turret ring / head groove | Ø47/51 ring, 1.0 mm radial clearance, 1.5 mm engagement |

# Pan/tilt camera head v2.1 — ESP32-S3-CAM + 2× MG90S + 2× HLK-LD2410C, 360° pan on a hollow post

No STM32 box. Five printed parts plus a pinion and a C-clip: a base plate with a **fixed hollow
post** (wires go up through it), a pan head that carries the pan servo, the tilt servo, a pivot
column and the two radar holders, and a camera pod with the ESP32-S3-CAM lying inside it. The pan
servo rides on the head and walks a 40-tooth pinion round a 20-tooth gear that is part of the post,
so a normal 180° servo turns the head a **full 360°** and the only wires that cross the pan joint
are the two power wires inside the post. Every lead from the pod leaves through slots in the back
cover, behind the swing.

Print file: **`out/pantilt_cam_v2_A1mini.3mf`** (Bambu Studio, two A1 mini plates, no supports).
Plain STLs are in `out/*.stl`, `out/*_print_oriented.stl` are the same parts already laid flat,
and `out/pantilt_cam_v2_generic.3mf` is a plain 3MF for other slicers.

![assembled](out/fig1_assembled_iso.png)
![rear](out/fig2_assembled_rear.png)
![exploded](out/fig5_exploded.png)

## What's in the 3MF

| plate | part | print orientation | notes |
|---|---|---|---|
| 1 | 1 Base plate + hollow post | as is | 100 × 80 × 4 plate, 4× Ø4.5 holes with Ø9 counterbores (M4 / #8 screws). Post Ø13 with a Ø9 bore, fixed 20T gear, thrust collar, C-clip groove, side wire slot at the top, cable notch at the foot |
| 1 | 3 Camera pod | lens face down | 77 × 36 × 35 box, ESP32-S3-CAM lies inside, lens looks out the front |
| 1 | 4 Pod back cover | outer face down | snap tabs top/bottom, pry notches at both ends, **three 14 × 3.6 wire slots through the back face** |
| 1 | 6 Pan pinion 40T | screw side down | module 1, Ø42, 7 mm thick, Ø4.7 spline socket on top, screw-head recess underneath |
| 1 | 5 Pivot pin | standing on its head | Ø4.8 × 14 shank. Print 2–3 |
| 1 | 7 Post C-clip | flat | 1.5 mm thick, snaps sideways into the post groove. Print 2–3 |
| 2 | 2 Pan head | plate down | yoke plate + hub bushing + pan-servo pad/window + tilt-servo wall + pin column + two LD2410C holders at ±45° |

Settings: **0.2 mm layers, 0.4 nozzle, 3 walls, 15–20 % infill, PLA or PETG, no supports**. Bridges
are short (6 mm cable slot, 13 mm servo window, 25 mm USB opening); every horizontal hole is a
teardrop. Brim on the pan head (the radar wings are long and thin) and on the pinion.

Sanity: every part is a closed manifold, no part touches another in the assembled position, the
pod clears the plate, post and radar holders at every tilt angle from −90° to +90°, the pinion
meshes with the post gear at every pan angle with 0.19 mm backlash, and nothing crosses the A1
mini's 180 × 180 bed (`check_v2.py`, `out/check_report.txt`).

## Bill of materials

- 1× ESP32-S3-CAM (the 68 × 29 mm dual USB-C board, OV3660 on the 21 mm ribbon)
- 2× MG90S (SG90 footprint). Positional 180° servos — see the note on "360°" servos below. Each kit's
  2 ear screws + 1 centre screw are used (ear screws should be the usual 2 × 8 mm; longer ones are
  fine, the pinion has clearance grooves for their tips).
- 2× HLK-LD2410C with the 5-pin header soldered (pins on the component side)
- female–female Dupont jumper wires: 3 per servo if its lead needs extending, 4–5 per radar (VCC, GND, TX, RX, OUT), 2 for power
- a 5 V supply lead: a USB-C or USB-A cable with the device end cut off (red = 5 V, black = GND), or
  a 5 V adapter lead. 2 A; the two servos can pull over 1 A when they stall.
- 4× M4 (or #8 wood) screws for the base plate
- foam tape or a 1 mm thermal pad for the ESP32 module, a dab of hot glue for the camera head, a
  cable tie

## How it goes together

1. **Power lead into the base.** Feed the 5 V lead in through the notch at the foot of the post (or
   up through the hole under the post if you are mounting the plate on a box) and up the bore until
   it comes out of the slot at the top. Leave about 120 mm sticking out of the top. Tie it off at the
   notch.
2. **Pan servo onto the head.** Drop the MG90S into the window in the raised pad at the back of the
   head, shaft **down** through the plate, ears on the pad, two kit ear screws into the slotted pilot
   holes. Press the pinion onto the spline from below (Ø4.7 socket — ream to 4.8 if it is really
   tight) and put the kit centre screw up through the pinion from underneath.
3. **Head onto the post.** Set the servo to its centre position (90°). Lower the head over the post
   with the pinion teeth going into the post gear — any tooth, the head is re-seated to "forward"
   by lifting it off and dropping it back one tooth over. The head rests on the collar; snap the
   C-clip into the groove just above the hub from the side. The clip is what keeps the head on if
   you mount the thing upside down.
4. **Radars.** Each LD2410C slides down into its holder from the top, antenna side (the side with the
   two square copper patches) facing **out**, header edge at the **bottom**, so the Dupont housings on
   the pins lie flat on the plate pointing back towards the middle. Keep them there with a drop of
   hot glue on the top edge if you want.
5. **Board into the pod.** Stick the camera head onto the ESP32 module can with foam tape so the lens
   barrel goes through the Ø8 hole in the front wall (there is a 9.4 × 9.4 recess for the square head).
   Lay the board in with the component side towards the front wall — the module rests against the
   wall (put the thermal pad there), the USB-C ports face the opening in the end wall, header pins
   point at the cover. Plug every lead onto the pins, pass the free ends out through the three slots
   in the cover's back face, snap the cover on (the posts inside the cover press the board's free
   corners; pry it off again at the notches on the ends).
6. **Pod onto the head.** Lower the pod between the wall and the column with the big boss on the
   servo-wall side. Push the pivot pin through the column into the small boss on the pod's USB end.
7. **Tilt servo last, from outside.** Slide the MG90S through the window in the wall, shaft first, so
   its spline enters the pod's socket; two ear screws into the slotted pilot holes in the wall. Its
   centre screw goes in from *inside the pod* through the Ø2.3 hole in the end wall, so do that
   before the cover goes on for the last time (or skip it; the press fit plus the pin on the other
   side hold the pod fine).
8. Screw the base plate down, connect the 5 V lead to the ESP32's 5V and GND pins (through a cover
   slot), done. The TTL and OTG ports stay reachable through the end-wall opening for flashing.

## Wiring

Everything plugs onto the ESP32-S3-CAM's header pins on the back of the board, and all of it rides
on the head, so only the 5 V/GND pair passes through the post:

- 5 V lead from the post → ESP32 **5V** and **GND** pins (this powers the board, both servos and both
  radars from the pins; the USB ports are only for flashing).
- Pan servo lead (on the head, next to the pod): brown → GND, red → 5V, orange → a PWM GPIO.
- Tilt servo lead (comes out of the top of the servo on the wall): same, second PWM GPIO.
- Each LD2410C: VCC → 5V, GND → GND, TX → an ESP32 RX pin, RX → an ESP32 TX pin (256000 baud), or
  just OUT → a GPIO if you only need "someone is there".
- Which GPIOs are free depends on the camera pin map for this board (see `camera_pins.h` in the
  repo for the DVP pins to avoid). G1, G2, G21, G47, G14, G13 are the usual candidates; G19/G20 are
  the native USB pair, leave those alone.

Lead lengths: the pan servo and the power lead come out of the head's back area a few cm from the
cover slots; the tilt servo lead comes out of the top of the servo on the left wall; the radar leads
run along the plate from the front corners. 150 mm jumpers cover everything with slack for the
tilt.

## Motion

- **Pan**: 2:1 overdrive, so servo 0°→180° = head 0°→360°. The head turns once; the power pair twists
  inside the post by one turn, which a flexible lead does without complaint. Do not drive a
  continuous-rotation servo round and round — the wire would wind up.
- **Tilt**: the pod clears the plate, the post and the radar holders at every angle. With a USB-C
  cable plugged in for flashing, looking **down** is limited to about +60° before the plug touches
  the pivot column; unplugged there is no limit.
- Pan accuracy is the servo's accuracy times two (a 1° servo dead band is 2° at the head), fine for
  a 68° lens. The hub rides on the Ø13 post with 0.2 mm clearance over 12.5 mm of bushing, so the
  head does not wobble on the servo spline the way a direct-drive pan does.

## About the "360°" MG90S

A micro servo sold as "360°" is almost always a *continuous rotation* servo: the pulse width sets
speed and direction, not position. That cannot point a camera on its own — the tilt would just
fall over and the pan would have no idea where it is. If yours spin freely when you send them a
fixed pulse, use ordinary 180° MG90S for both axes (identical case, identical mounting, same kit
screws; this model does not change). If you really want to use a continuous servo for pan, the
mechanics here work with it (the 2:1 gearing just becomes a speed ratio) but you need to add a
home switch or a magnetic encoder on the post and keep the rotation to ±180° in firmware so the
power lead does not wind up.

## Regenerating / changing dimensions

Everything comes from `gen_pantilt_v2.py` (one parameter dict `P` at the top).

```
pip install manifold3d trimesh shapely scipy numpy pillow lxml
python3 gen_pantilt_v2.py    # STLs in out/
python3 check_v2.py          # interference, component clearance, pan + tilt sweeps, overhang report
python3 render_v2.py         # figures
python3 pack_3mf.py          # Bambu 3MF (A1 mini) + generic 3MF + print-oriented STLs
```

Key numbers if you need to tweak the fit:

| what | value |
|---|---|
| servo pocket / window | 23.4 × 12.9 (MG90S case 22.8 × 12.2) |
| ear screw pilots | 1.8 mm wide slots, 2.4 long, on 28.0 mm centres |
| spline sockets | Ø4.7, 5 deep (pod) / 4 deep (pinion), Ø2.3 centre-screw hole |
| pan drive | module 1, 20T on the post / 40T pinion, centre distance 30, 0.2 mm backlash per gear |
| post | Ø13 OD, Ø9 bore, head bore Ø13.4, C-clip groove Ø10.6 |
| tilt axis | 45 mm above the head plate top, 5 mm behind the pod's depth centre |
| pod cavity | 72 × 31 × 29.5 (board 68 × 29 incl. module overhang, 24 mm behind the PCB for Dupont housings) |
| radar holder grooves | 2.0 mm wide, 1.2 mm deep each side (PCB 1.0–1.6) |
| pivot pin | Ø4.8 in a Ø4.9 column hole (friction) and Ø5.2 pod hole (free) |

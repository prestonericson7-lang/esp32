# Red-team notes — pan/tilt head v2.3

Everything below was checked against the generator, the checker output and the figures on this
branch. "Fixed" means the change is in the current 3MF; "open" means it is a real limit you should
know about.

## Pan drive (the part that was called crude, and was)

| item | v2.1 | now | status |
|---|---|---|---|
| gear module / teeth | m1.0, 20T sun / 40T pinion, 0.5 mm tip land, spiky | m1.5, 28T/28T, 20° involute, 2.21 mm thick at pitch, 0.97 mm tip land, root fillets 0.45, tip rounding 0.2 | fixed |
| contact ratio | not computed | 1.57 | fixed |
| backlash at the head | not computed | 1.0° total (5 mm at 300 mm range) | open, acceptable for a 68° lens |
| tooth stress at servo stall | – | 2 MPa Lewis bending vs ~50 MPa PLA | fine |
| pan bearing | Ø13 PLA post, 12.5 mm bushing, head sliding on a Ø17.5 collar | Ø30 journal, Ø30.4 bore, 10.5 mm long, head on a Ø30–38 flat ring + 2 mm washer | fixed |
| friction on the ring vs servo torque | – | 0.15 kg·cm vs 1.8 kg·cm | fine |
| mesh phase | hand-set 4.5° | computed from the servo's tooth-line position (`pinion_phase_deg`) | fixed |
| mesh check | pan 0 only | pan 0–360 in 15° steps, 0.18 mm minimum tooth clearance, 0 interference | fixed |
| sun tooth bottoms | hanging over the cone | cone top at the tip radius, fully supported | fixed |
| clip retention | one groove | groove 0.3 mm above the 6 mm hub with the washer fitted; snap-on spread 1.5 mm over 30 mm arms | fixed |
| ear-screw tips under the pan servo | would have hit the pinion | 2 mm pad keeps 8 mm screws inside the plate, clearance grooves in the pinion for longer ones | fixed |
| 1:1 and servo type | 2:1 overdrive assumed a 180° positional servo | 1:1: 360° positional gives 360°, 180° gives 180°, continuous spins at servo speed and needs the reed-switch home (pockets provided) | open: depends on which servos you actually have |

## Board mounting

| item | before | now | status |
|---|---|---|---|
| location of the board | pinched by three posts, lens barrel in a Ø8 hole, 2 mm end play | two pegs through the board's real mounting holes (Ø3.1, 24.2 apart, 3.0 from the USB edge, measured off the photo at ±0.3 mm), one round, one flattened so a spacing error cannot bind; cover posts clamp over the pegs | fixed |
| lens hole vs camera | Ø8 for a Ø6.4 barrel, 0.8 mm margin | Ø9, 1.3 mm margin; the camera is glued where the hole is | fixed |
| USB-C ports | 11.6 apart assumed | 10.6 measured; end-wall opening covers both plugs; locating blocks stop 1.4 mm short of the end wall | fixed |
| back-side TF slot | unknown, posts could have landed on it | posts land on the mounting-hole annuli only | fixed |
| envelope check with your Meshy model | – | body, pins, ports clear; only its mid-board camera column, missing holes and overlong headers touch | checked |

## Head geometry and printing

- Pod bottom is 70 mm above the plate at level tilt and 59 mm at the worst tilt; post top is 16 mm: wires have the room you asked for.
- Wall 101 × 28 × 4 mm with 75 mm outboard gussets, column 94 mm with a 70 mm rear gusset: tip deflection under 0.5 N is ~0.3 mm. The print is 105 mm tall on plate 2; use a brim.
- Overhang scan per part: only short bridges remain (13 mm cable notch, 13 mm servo window, 25 mm USB opening) and teardrop holes. No supports.
- Base STL is byte-identical to the one you printed (hash checked after every change).

## Still open / your call

1. **Servo type.** If your "360°" MG90S are continuous-rotation, tilt needs a positional servo, full stop. Pan works but needs homing.
2. **Radar boards are held by friction only** in their slots: one drop of hot glue on the top edge, especially if the unit hangs upside down.
3. **MG90S duty cycle.** Continuous 24/7 movement will cook the plastic potentiometer and the motor; park the servos (no PWM) when nothing is detected and let the radars wake them.
4. **PLA creep.** For a unit that runs warm all year, print the head and gears in PETG.
5. **Backlash** of about 1° at the head is inherent to printed spur gears at this size; the camera's 68° field of view hides it.

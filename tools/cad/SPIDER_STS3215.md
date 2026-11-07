# Four-leg STS3215 prototype

A compact, four-leg robot with hip yaw, hip pitch, and knee pitch on every leg.
The design uses twelve **12 V STS3215-family servos**, purchased metal horns,
and printable structural parts. All four legs share the same parts; they are
rotated around the body rather than requiring mirrored STL sets.

This is a fit-and-simulation prototype. The motor is a simplified dimensional
envelope, the payload is a 150 g placeholder, printed mass is estimated, and
motor inertia is approximated by a uniform envelope. No load rating, fatigue
life, walking controller, or electrical system has been qualified.
The reported mass excludes fasteners, inserts and cables; replace the payload
placeholder and weigh assembled links before using the dynamics for hardware.

## Reproduce and inspect

From the Joshua root, after [CAD setup](README.md):

```bash
./scripts/run_robotcad.sh --robot spider --create
./scripts/run_robotcad.sh --robot spider --verify-exports
./scripts/run_robotcad.sh --robot spider --check-cad
./scripts/run_robotcad.sh --robot spider --convert-mujoco
./scripts/run_robotcad.sh --robot spider --package
./scripts/run_robotcad.sh --robot spider
./scripts/run_robotcad.sh --robot spider --mujoco
```

The source dimensions and assumptions live in [spider_sts3215.json](spider_sts3215.json).
Outputs are under `output/robotcad/spider_sts3215/`, including the editable CAD,
individual printable STLs, URDF and meshes, manifests, and MuJoCo scenes.
Creation refuses to replace an existing CAD file. Preserve edits in a new
directory before rebuilding. Generated files remain Git-ignored.

`mujoco/scene.xml` has a floating base and gravity. `mujoco/inspection.xml` fixes
the body above the floor for articulation inspection. The neutral stance is
zero in all twelve control channels; hip and knee mounting origins include the
20° and 55° design bends. These are CAD reference angles, not calibrated native
servo positions. In FreeCAD the robot's **JointValues** are in degrees; MuJoCo
actuator control values are radians relative to those reference angles.

## Printable inventory

Every supplied STL is oriented, placed at Z=0, checked for closed surfaces,
and checked against a 180 × 180 mm XY bed. Supports may still be necessary.
Use `print_manifest.csv` for exact mesh extents and orientation notes.

| Part | Installed quantity | Material |
|---|---:|---|
| Chassis, 170 × 160 × 4 mm | 1 | PETG prototype |
| Electronics deck | 1 | PETG prototype |
| Deck standoff | 4 | PETG prototype |
| Servo cradle | 12 | PETG prototype |
| Servo clamp bar | 12 | PETG prototype |
| Coxa carrier | 4 | PETG prototype |
| Femur carrier | 4 | PETG prototype |
| Tibia | 4 | PETG prototype |
| Soft foot cap | 4 | TPU |
| Servo footprint gauge | 1 optional | PETG prototype |
| Horn adapter coupon | 1 optional | PETG prototype |

The kit has **46 installed printed parts**, plus two optional fit coupons.
Start with the coupons and one cradle/clamp/leg set. The source recipe suggests
0.2 mm layers, six walls, and 40% infill as initial prototype settings. Verify
your material, layer adhesion, dimensional accuracy, and bracket stiffness;
these settings do not establish a structural rating. The largest part leaves
only 5 mm per side in one dimension for a brim on a nominal 180 mm bed.

The cradle and clamp print flat. Offset coxa/femur carriers are oriented on
their side and may need supports under flanges. The tibia has a broad flat face.
The TPU cap has a flat annular socket face; inspect the small internal bridge
above its socket in the slicer. Remove supports and check all bores before fit.

## Purchased hardware

`bom.csv` contains counts and notes. Nominal fastener lengths must be checked
against the actual horn thickness, insert style, and printed material.

| Item | Quantity | Interface |
|---|---:|---|
| STS3215 12 V-family servo | 12 | Confirm actual case and cable outlet |
| Compatible metal servo horn | 12 | 25T / nominal 5.9 mm spline, four M3 holes on 14 mm PCD |
| Manufacturer-matched horn retention screw | 12 | Use the correct length for the motor/horn |
| M3 × 6 horn-to-print screws | 48 | Four per adapter; check engagement and clearance |
| M3 × 12 cradle mounting screws | 48 | Four per cradle |
| M3 cradle mounting nuts | 48 | Accessible from rear of mounting plate |
| M3 × 10 clamp-bar screws | 24 | Two per bar |
| M3 heat-set inserts | 24 | Match the 4.1 mm pilot / nominal 6 mm pocket |
| M3 threaded rod, nominal 65 mm | 4 | Deck/chassis tie rods |
| M3 tie-rod nuts and washers | 8 each | Two ends of each tie rod |
| Thin rubber liner stock | For 12 motors | Fill nominal 0.35 mm case clearance; test compression |
| Small hook-and-loop payload straps | 2 | Through electronics-deck slots |

Matching TTL bus leads, controller, power distribution/protection, and a suitable
battery or supply are separate choices. The green CAD box is a payload envelope,
not a selected battery or electronics assembly. Motors, horns, fasteners,
inserts, liners, and electronics are purchased; structural brackets and feet are
printable. Do not print spline teeth or substitute printed horns in this recipe.

## Fit gates before printing the full kit

1. Measure the actual case. The reference envelope is 45.22 × 24.72 × 35 mm,
   with the shaft 10.11 mm from the front edge in the vendor drawing. Nominal
   case clearance is 0.35 mm on each side. The gauge tests the footprint only.
2. Fit one cradle, liner and clamp. Cable plugs must pass freely through the
   open ends and back windows. The clamp must retain the case without crushing
   it; insert and liner dimensions vary by supplier and print process.
3. Fit the horn coupon. Check the four M3 / 14 mm PCD holes, central screw
   access, hub diameter, shaft engagement, and the nominal 6 mm printed-adapter
   plane relative to the case face. Change the source dimensions if necessary.
4. Verify screw engagement by measurement. No screw should bottom into the
   motor or foul rotation. Use the horn retention screw specified for the actual
   supplied hub, rather than inferring its length from the CAD envelope.
5. Fit one complete leg and move it manually through its intended range. Check
   brackets, case corners, screw heads, inserts, cable loops and toe retention.
   The nominal toe has 0.1 mm radial interference in TPU; adjust for your printer.

## Mechanical assembly order

1. Install inserts in the cradle posts, keeping the motor cavity and cable
   window clear. Test one clamp with liner stock before repeating twelve times.
2. Bolt four yaw cradles onto the chassis. Motor tails point toward the body
   center. The four axes are at X=±52 mm and Y=±47 mm. Route cables through the
   matched windows while the rear faces remain accessible.
3. Attach one coxa carrier to each yaw horn using four M3 screws. The central
   opening provides access to the motor's own horn retention screw.
4. Bolt a pitch-motor cradle to each coxa carrier's offset plate. Keep its
   connector accessible. The pitch axis is 40 mm radially outward and 10 mm
   above the yaw axis in the coxa frame.
5. Attach the femur carrier to the pitch horn, then mount its knee cradle.
   The knee motor tail points back toward the hip. The axis spacing is 100 mm.
6. Attach the tibia to the knee horn. Fit the TPU cap over the 8 mm-diameter,
   6 mm-long toe spigot. Check retention; the interference fit is a prototype
   assumption and is not a qualified retention method.
7. Install the four central deck standoffs and tie rods, then the payload deck.
   Keep wiring out of joints and mounting interfaces. Fit the actual payload
   only after checking its size, mass, attachment and electrical requirements.
8. Compare against the CAD assembly. Names `fl`, `fr`, `rl`, `rr` mean front-left,
   front-right, rear-left, rear-right. Body +X is forward, +Y is left, +Z is up.
   Clock horns to the intended reference stance using a separately verified
   calibration process; these tools do not power, configure or move motors.

## What the checks establish

- The STL surfaces are closed and fit the nominal bed in their supplied pose.
- URDF link frames agree with the CAD frames, and inertia tensors are positive
  and physically admissible under the stated mass assumptions.
- CAD clearance checks sample neutral and each joint's limits individually.
  The small intended TPU/spigot interference is reported separately.
- MuJoCo checks all twelve position drives while suspended, then a five-second
  floating-base settling example with torque limits enforced.

These checks are not a continuous multi-joint collision sweep, cable routing
validation, FEA, real-motor thermal model, fatigue test, payload rating or walking
test. Motor-bearing side loads and horn/bracket stiffness still need mechanical
review. The contact model omits some thin bracket details; simulation stability
does not prove that printed parts will support the same loads.

## Mechanical and motor references

- [Waveshare ST3215 family specifications](https://docs.waveshare.com/ST3215_Servo)
  distinguish the 12 V and 7.4 V versions. The 12 V reference rated torque is
  10 kgf·cm, approximately 0.981 N·m; the prototype's 0.65 N·m simulation limit
  is a design assumption, not a measured continuous operating limit.
- [Vendor mechanical resources](https://docs.waveshare.com/ST3215_Servo/Resources-And-Documents)
  provide the family STEP and 2D drawings. The downloaded PDF's title block says
  SCS215 even though the vendor distributes it under ST3215; verify your motor.
- [Feetech family datasheet](https://www.feetech.cn/Data/feetechrc/upload/file/20200611/6372749961523760249976542.pdf)
  supplies the metal horn's four-M3 / 14 mm PCD drawing. This is a **7.4 V**
  datasheet and is not used for 12 V electrical limits.

Downloaded references, motor envelopes and simulation values are distinct from
a measurement of the user's actual motor and supplied horn.

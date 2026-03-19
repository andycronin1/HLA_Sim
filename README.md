# HLA Soldier Federate (RPR FOM / MAK RTI)

This project now uses the **RPR FOM v2.0 (HLA 1516-2010)** model so it can interoperate with VR-Forces on MAK RTI.

## What This Sim Publishes/Subscribes

The federate publishes and subscribes this RPR object class:

- `HLAobjectRoot.BaseEntity.PhysicalEntity.Lifeform.Human`

It updates these inherited RPR attributes:

- `EntityType`
- `EntityIdentifier`
- `Spatial`
- `ForceIdentifier`
- `Marking`

This lets VR-Forces discover this entity as an RPR lifeform when both are in the same federation and using compatible FOM modules.

For close-range engagements, the federate also publishes these RPR interactions:

- `HLAinteractionRoot.WeaponFire`
- `HLAinteractionRoot.MunitionDetonation`

It also subscribes to incoming `HLAinteractionRoot.MunitionDetonation` interactions,
applies local health/damage, and publishes `DamageState` (when available in the joined
RPR class) so destroyed state can be visualized by peers.

## RPR FOM In This Repo

The VR-Forces RPR module has been copied into this repository at:

- `foms/RPR_FOM_v2.0_1516-2010.xml`

By default, the federate loads that file.

## Requirements

- MAK RTI (for example, MAK RTI 5.x)
- C++17 compiler
- CMake 3.16+

You also need `MAK_RTI_HOME` set in your environment.

## Build (Windows)

```powershell
mkdir build
cd build
cmake -S ..
cmake --build .
```

## Run

Run from the `build` directory:

```powershell
.\Debug\SoldierFederate.exe Alpha
```

Start additional instances with unique names:

```powershell
.\Debug\SoldierFederate.exe Bravo
.\Debug\SoldierFederate.exe Charlie
```

## Interop Settings

Set these environment variables so this sim matches your VR-Forces session:

- `FEDERATION_NAME`: federation execution name (default: `SoldierFederation`)
- `RPR_FOM_PATH`: optional override for RPR FOM path
- `RPR_JOIN_WITH_ADDITIONAL_FOM`: optional (`1/true`) to attempt modular FOM merge on join of an existing federation (default: disabled)
- `RPR_SITE_ID`: optional Site ID for `EntityIdentifier` (default: `1`)
- `RPR_APPLICATION_ID`: optional Application ID (default: `1`)
- `RPR_ENTITY_NUMBER`: optional Entity Number (default: hash from federate name)
- `RPR_FORCE_ID`: optional force ID (default: `1` = Friendly)

Example:

```powershell
$env:FEDERATION_NAME = "MyVrForcesFederation"
.\Debug\SoldierFederate.exe Alpha
```

## Logging

Runtime logs are written to:

- `build/logs/<FederateName>_<timestamp>.log`

Fatal startup/runtime exceptions are also appended to:

- `build/logs/fatal.log`

## Notes

- The previous custom `Soldier` class path has been removed from the sim logic.
- Engagement events are now emitted using standard RPR `WeaponFire` and `MunitionDetonation` interactions.
- This federate now uses HLA encoding helper classes for RPR records/variant records, instead of raw host-endian memory copies.
- If VR-Forces still does not display the entity, check that both federates use the same federation name and the same RPR FOM module set.

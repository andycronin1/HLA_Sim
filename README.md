# HLA Soldier Federate (MAK RTI)

This project provides a **basic HLA 1516e federate** that can interact with the **MAK RTI** (e.g., MAK RTI 5.0.1) to simulate **soldiers** that can **move**, **fire**, and **kill** each other.

> ⚠️ This project is a template/skeleton. You will need a valid MAK RTI installation (and appropriate licensing) to build and run.

---

## ✅ Features

- Registers a `Soldier` object class with attributes like position and health
- Publishes a `FireWeapon` interaction to apply damage to other soldiers
- Subscribes to updates about all soldiers in the federation
- Performs a simple tick loop where each soldier may fire at others

---

## 🧩 Requirements

- MAK RTI (e.g., **MAK RTI 5.0.1**) installed
- C++17 compatible compiler (MSVC or GCC/Clang)
- CMake (>= 3.16)

> Tip: Set `MAK_RTI_HOME` to your MAK RTI install directory (e.g. `C:\MAK\makRti5.0.1`). This has been set permanently in the system environment variables.

---

## 🚀 Build and Run (Windows)

1. Ensure environment variables are set (already done permanently):

   The `MAK_RTI_HOME` is set to `C:\MAK\makRti5.0.1`.

2. Build:

```powershell
mkdir build
cd build
cmake -S ..
cmake --build .
```

3. Run (from build folder):

```powershell
.\Debug\SoldierFederate.exe
```

---

## 🧠 How it works

- On startup, the federate creates or joins a federation execution called `SoldierFederation`.
- It registers a `Soldier` object and periodically updates its position + health.
- It listens for `FireWeapon` interactions and applies damage.

> This template is intentionally minimal so you can extend it with synthetic terrain, better targeting, and AI behaviors.

---

## 🛠 Customization

- Add new attributes (e.g., `weaponType`, `ammoCount`)
- Add more interactions (e.g., `SuppressiveFire`, `MedEvac`)
- Integrate with a rendering engine or VR-Forces scenario

---

## 📌 Notes

- This code targets HLA 1516e. If you're using HLA 1.3, you will need to adapt the APIs.
- The RTI libraries and headers are not included in this repo.

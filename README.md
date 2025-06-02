# 🐟 ESP32-C Fish Feeder Notifier

An **ESP32-C project** that sends a notification (via LED + MQTT) at **7:00 AM** and **7:00 PM** — a friendly reminder to feed my little fish.

## 🧠 Project Goal

The goal is to create a simple and autonomous system that:

- Blinks an LED at 7:00 AM and 7:00 PM to remind me to feed my fish.
- Lets me **acknowledge** the notification by pressing a button (fish is fed).
- Synchronizes time using **NTP** via Wi-Fi.
- Calculates the time remaining before the next notification.
- Enters **deep sleep mode** to minimize power consumption between reminders.

## 🧰 Features

- 🔔 LED notification twice a day
- 🔘 Button to acknowledge feeding
- 🌐 NTP time sync over Wi-Fi
- 😴 Deep sleep to preserve battery
- 📊 Sends stats to an MQTT broker (so I can track how often I forget to feed my fish)
- 🕓 Handles early feeding: if I feed my fish before the scheduled time (e.g., at 4:00 PM instead of 7:00 PM), the system will skip the upcoming notification and go straight back to sleep until the next one (in this case, 7:00 AM the next day).
- ⚠️ Resilient to failures: gracefully handles Wi-Fi, NTP, or MQTT connection issues without crashing or getting stuck.


## 🌱 Why this project?

This project is a fun way to explore:

- ESP32 deep sleep mode
- GPIO behavior during deep sleep
- NTP time synchronization
- Sending MQTT messages from an embedded device

## 📷 Preview

*(optional image or schematic here)*

## 📦 Requirements

- ESP32-C
- Wi-Fi access
- MQTT broker (optional, for stats)
- A heart big enough for one small fish ❤️

---

> Built with love for my fish, and to improve my skills as a fish dad 🐠

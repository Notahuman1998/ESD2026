SmartHike Watch OS Documentation

🌟 Overview & Features

The SmartHike Watch OS is a custom-built, gesture-driven smartwatch interface designed for the LilyGo T-Watch 2020 V2. It acts as a standalone fitness tracker and an IoT node that pushes live telemetry to a backend server via MQTT.

Key Features:

Intuitive Gesture UI: Swipe left/right to switch apps, swipe up/down to scroll through menus, and double-tap the screen to instantly lock/sleep.

Dynamic Activity Tracking: Choose between 5 preset activities (Cycling, Walking, Running, Snowboarding, Hiking) to apply the correct MET (Metabolic Equivalent of Task) values for calorie calculation.

Live Metrics Dashboard: Displays real-time step count (via BMA423 accelerometer), distance traveled, and calories burned.

GPS Telemetry: Live readout of Altitude and Speed via the internal GPS module.

Haptic Feedback: The internal vibration motor provides tactile confirmation for button presses, input errors, and a massive celebratory buzz when daily step goals are met.

Secure Lock Screen: Features an auto-timeout sleep mode, a wake-up clock face, and an optional 4-digit PIN keypad to secure the watch.

On-Device Customization: Users can adjust their body weight, screen brightness, screen timeout duration, step goals, and hardware clock time directly from the watch.

📱 User Workflow

Wake & Unlock: Tap the screen to wake the watch. The clock face appears. Tap or swipe to unlock. If the Passcode is enabled, the user must input the correct 4-digit PIN.

Navigation: The OS is split into 3 main horizontal pages. Swiping Left or Right transitions smoothly between them.

Starting a Workout: - On Page 0, tap an activity from the menu (e.g., "Hiking").

The live dashboard appears. Tap START HIKE.

The watch begins tracking steps, calculating distance via GPS, and estimating calories. A background loop starts publishing this data to the Raspberry Pi over Wi-Fi.

Sleep: The user can double-tap the screen at any time to put the watch to sleep and save battery. Background tracking continues while asleep.

🗺️ Page Structure & Hierarchy

The UI is built on a state-machine architecture consisting of 3 main pages and a scrolling sub-menu system.

🔒 Lock Screen (State ID 99)

State 0: Clock Face

State 1: PIN Keypad (Displays typed numbers)

🏠 Page 0: Main Dashboard

View A: Activity Selection Menu (5 Options)

View B: Live Workout Dashboard (Time, Steps, Distance, Calories, Start/Stop Toggle)

⚙️ Page 1: Features Menu (Vertical Scrollable List)

Sub-page 1: All-Time Totals (Cumulative lifetime stats)

Sub-page 2: Battery Status (Visual battery icon, Percentage, Voltage, Internal Temp)

Sub-page 3: Set Step Goal (+/- controls)

Sub-page 4: Altitude & Speed (Live GPS radar)

Sub-page 5: Set Clock Time (Adjust hardware RTC Hour/Minute)

Sub-page 6: Screen Timeout (Adjust auto-sleep duration in seconds)

Sub-page 7: Passcode Lock (Toggle ON/OFF, Set new 4-digit PIN)

Sub-page 8: Screen Brightness (Adjust backlight from 10% to 100%)

👤 Page 2: User Profile

Displays static Username.

Interactive Weight (kg) adjustment for accurate calorie tracking.

🧮 Calorie Calculation Formula

Calories are calculated dynamically based on the duration of the activity, the user's weight, and the intensity of the specific sport chosen.

The standard physiological formula is:

Calories per minute = (MET * 3.5 * weight in kg) / 200

Because the watch calculates data continuously in seconds using millis(), we adapted the formula by dividing the denominator by 60 (200 * 60 = 12000):

elapsed_hike_time_sec = (millis() - hike_start_time) / 1000;
current_hike_calories = (elapsed_hike_time_sec * current_met * 3.5 * user_weight) / 12000.0;


MET (Metabolic Equivalent) Values Used:

Cycling: 9.5

Walking: 3.8

Running: 9.8

Snowboarding: 7.0

Hiking: 6.0

🧩 Code Structure

To keep the main loop fast and responsive, the code is heavily modularized:

Global Variables: Holds application state (current_page, current_sub_page, is_locked, is_activity_selected).

UI Rendering Functions: Every specific screen has its own drawing function (e.g., drawPageMain(), drawSubPagePasscode()). They accept a boolean isStatic to ensure static text/layouts are only drawn once upon page transition, preventing screen flicker. Dynamic data (like the clock ticking) is drawn continuously.

Data Tracking (Background): Placed after the touch logic in the main loop(). The GPS continuously reads Serial data, and if a hike is active, the BMA423 step counter and calorie math update non-stop, even if the screen is asleep.

MQTT Publisher: Waits for step counter interrupts. If a step is taken, it bundles all metrics (Date, Activity, Steps, Dist, Cals, Lat, Lng) into a payload and publishes it to test/topic.

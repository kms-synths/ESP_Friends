# ESP Friends
I hate ESPs. Useless WiFi/BLE features. I don't know how to use them to their maximum potential. So, why I should not make something interesting with a bunch of them for my Eurorack?

## ESPF-01 - oscilloscope
Based on Scope-O-Matic (github.com/josbouten/Scope-O-Matic/), which was based on the Poor-Man-Oscilloscope project. The original has its own flaws, such as: Arduino Nano as an MCU, meaningless voltage ranges, an imprecise input attenuation stage, etc.

In the ESP version, I discarded the old voltage ranges because they didn't work anyway, except for the A50V mode: since it has no differences from A5V, I just renamed it to "Auto" and called it a day. Then I added the modes that made sense to me: 3.3V (since it's the ESP32 power supply), 5V (since it's the usual CV range in Eurorack), ±5V or 10V (since it's the audio signal standard in Eurorack), and ±12V or 24V (since you can't achieve more than 24Vpp in Eurorack).

I put a lot of work into developing a decent attenuation stage: first, the signal goes through a voltage divider with (ideally) a 0.108(3) ratio, which scales the signal down from 24Vpp (worst case) to 2.6Vpp. 2.6V was selected since the ESP32 ADC has a dead zone above that threshold. I selected 43k and 5.1k resistors plus a 5k trimpot for the divider stage, which has (ideally) a 0.106029 input-to-output ratio and an estimated Vout of about 2.545Vpp. After the divider, the signal goes through an inverting amplifier with a gain of -1, which centers the signal around 1.2–1.3V. Then I implemented an LPF with an approximate -28dB slope and 18,500Hz cutoff for the FFT spectrum analyzer (still in development). Right before the ESP32, I placed BAT54S diodes for overvoltage protection (why didn't I place them before the MCP6002? No space + I can change the MCP6002 but not the ESP32).

For activating calibration mode press SEL and Hold at the same time.

## ESPF-01 - MIDI to CV over BLE

soon(tm)

# FootHoled
```
______          _   _   _       _          _ 
|  ___|        | | | | | |     | |        | |
| |_ ___   ___ | |_| |_| | ___ | | ___  __| |
|  _/ _ \ / _ \| __|  _  |/ _ \| |/ _ \/ _` |
| || (_) | (_) | |_| | | | (_) | |  __/ (_| |
\_| \___/ \___/ \__\_| |_/\___/|_|\___|\__,_|
```

Open hardware ESP32 cyber platform featuring GPS, rechargeable battery power, external RF support, SAO expansion, and extensible firmware.

An open-source portable cybersecurity platform built around the ESP32.

FootHoled is designed to be a modular handheld platform for wireless security research, GPS-enabled reconnaissance, and hardware experimentation.
or you can think of it as 
a location-aware, privacy-conscious reconnaissance platform that correlates observations from multiple sources, can be physically and programmatically expanded, and integrates with a desktop analysis environment.

## Current Features

- ESP32-based platform
- XXXX Cheap Yellow Display (CYD) - scrapped for waveshare 3.5" 320x480 IPS w/ESP32 S3
- GPS support - yes, going with different GPS board with helical stubby antenna
- Rechargeable dual 18650 battery system - this remains
- External Wi-Fi antenna - at least one external wifi antenna, possibly 2
- Battery monitoring - currently have led status lights indicating charge level and charge status - adding monitoring to interface to show usage and calculate remaining battery
- SAO expansion - scrapping this
- Open hardware - of course
- 3D printable enclosure - staying with own designs for 3d printed enclosure, these files will be available as soon as the hardware/software is all tested


## Planned Features

- Smart battery validation
- Modular SAO ecosystem
- Plugin architecture
- Custom PCB
- FootHoled OS


## DEFCON 34 Update: Project is incomplete at the moment, but the idea and desire to complete it are strong, it will just have to be post con. Whether you randomly found one of my stickers or if we spoke and I gave you one - keep an eye out over the next 4-6 weeks. In my rush to get it done before DC, I failed but after speaking with a few people, I am encouraged to see it through. I will return to the FootHoled development when I get home next week. Stay Hydrated and HACK THE PLANET!

## POST DEFCON UPDATE --- Ok, I've had a chance to look everything over and take a step back. The original idea was great for a "3 weeks until Defcon, can I complete it" but it lacked thinking some of it through. Why reinvent the wheel? In light of my most recent research, I'm taking this project in a slightly different direction. I am changing it to be a modular device, meaning any piece can be replaced or upgraded later. I am completely changing the hardware list. Moving up to a 3.5" screen and an ESP32 S3 with added GPIO options. It will have a 12 pin connector for add on modules later. It will have GPS with stubby antenna, 2 wifi cards, a magnetometer, accelerometer, IR receiver and transmitter, sub 1 Ghz, NFC and more. First I'm starting with the basics and the UI for it. As that progresses I will add more and more functionality until its ready for download. No idea what the case design for this revision will look like, but having 16MB to work in will make my life easier. Having a slightly larger, capacitive touch screen doesn't hurt either.  With the project change I will publish anew but will keep this in a historical branch for personal inspirations sake. Updated current features list to reflect known changes. UI will likely be LVGL based. Buckle up, Buttercup.  

## Yet another update - this project is on hold as I try to tackle another project before DC35, FAFO - Freq Around, Find Out. Details coming soon, waiting on slow boat from China so development on that can resume near the end of October. The plan is to have 100 units for sale at DC35. 

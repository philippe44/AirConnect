# AirCast: Send audio to Chromecast using AirPlay

=============================================

Works for Windows, Linux (x86, x64 and ARM and Sparc) and MacOS

No dependency except openSSL that every Linux box alread has. For Windows, copy all the DLLs

Take any computer on your network, launch the app and 30s after your CC devices will appear as AirPlay targets

Use -h to view options. A config file to fine tweaking can be generated, see -i and -I options (doc to come later)

When starting playback, there is lag of 10s (working on that)

=============================================

if you want to recompile, you'll need

https://github.com/nanopb/nanopb

https://github.com/akheron/jansson

https://github.com/philippe44/mDNS-SD

https://github.com/philippe44/TinySVCmDNS

needs libFLAC as well


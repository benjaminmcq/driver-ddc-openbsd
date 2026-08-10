NOTICE: This driver is still WIP and isn't in the OpenBSD tree yet.

PRs are accepted as well as feedback

This driver is ported from here: https://gitlab.com/ddcci-driver-linux/ddcci-driver-linux

I plan to get this into the source tree when it's finished likely under the directory sys/dev/i2c as well as a utility in ports to control it.

The planned architecture in question:
don't provide generic ioctl access from userland straight to the connector or anything, really.
this driver's purpose is to open a handle at /dev/ddc* and accepts commands controlling the monitor.

compilation instructions not given

NOTE FOR DEV BRANCH:
THIS BRANCH IS STRICTLY UNTESTED

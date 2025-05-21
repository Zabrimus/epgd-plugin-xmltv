# epgd-plugin-xmltv
xmltv loader for epgd.

## First and important patch of vdr-epg-daemon
epgd defines an external id used in channel-map.conf with maximum length of 10.
For all other existing plugins the length is sufficient. But for this plugin i decided to use 
the xmltv id as external id to prevent a mapping of xmltv id to some shorter value (maintance nightmare) 
or to calculate a shorter value using the xmltv id (not readable anymore in channel-map.conf).

A patch exists in directory `patches` which needs to be applied either in the directory `/etc/epgd` 
or in the source directory of vdr-epg-daemon `config`.
The patch extends the external id with length 10 to maximum length 50. No recompile is needed, only a restart of epgd.

### Altenative solution for the xmltv id
Instead of patching vdr-epg-daemon it is possible to modify the xmltv epg XML file. The XML file contains entries
like ```channel="very_long_xml_tv_id.de"```. After creating or downloading the XML file, just before the epg daemon 
read this file, the ```channel``` can be modified - if possible. 
But currently this plugin does not provide helper scripts for this task.
A sample integration can be done e.g. in the script get_epgdata.sh (see below).


## Build
Clone this repository into the "PLUGIN" directory of your epgd source and build.

## Configuration in /etc/epgd/epgd.conf

```
# ---------------
# xmltv plugin
# ---------------
# script which will be called to get xmltv epg data.
# the epg data needs to be written to the file configured
# in xmltv.input.
# This parameter is optional. If not set another mechanism is
# needed to update the xmltv epg data, like a cron job or whatever.
xmltv.getdata = /usr/local/bin/get_epgdata.sh

xmltv.input = /var/epgd/guide.xml
#xmltv.input = http://10.183.229.232:3000/guide.xml
```

The value of xmltv.getdata is optional, but if defined the configured 
script will be called to get the newest xmltv epg data. You need a useful and
working grabber.
The value of xmltv.getdata points to the xmltv XML epg data file, which will be imported in epgd.

### Sample of /usr/local/bin/get_epgdata.sh
I personally use the iptv-org grabber (see https://github.com/iptv-org/epg) with a channels.conf 
created with eismann (see https://github.com/Zabrimus/eismann).
The script itself is really simple:
```
#!/bin/bash

export PATH=/root/.nvm/versions/node/v24.0.2/bin:/usr/bin:/bin

cd /usr/local/src/epg
npm run grab --- --days=14 --channels=/var/epgd/channels.conf --output /var/epgd/guide.xml

# if a transformation of the channel is necessary, this is a good place to add this task.

exit 0
```

### Sample of channel-map.conf
```
// Das Erste HD
vdr:000:0:0 = C-1-1051-10301
xmltv:DasErste.de:1 = C-1-1051-10301

// ZDF HD
vdr:000:0:0 = C-1-1079-11110
xmltv:ZDF.de:1 = C-1-1079-11110

// ZDFinfo HD
vdr:000:0:0 = C-1-1079-11170
xmltv:ZDFinfo.de:1 = C-1-1079-11170
```
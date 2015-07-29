# osmi-water
Read openstreetmap data and extract water objects, indicate logical and tagging errors. Output is sqlite. Designed for OSM Inspector (http://tools.geofabrik.de/osmi).

Software is not in use now, but tested with the whole osm planet. Written in c++, it uses the new osmium framework (https://github.com/osmcode/libosmium). The source code can be used as an example how to use the libosmium.

The programm is develeped during my internship at the geofabrik. If you see some improvements, please let me know.

## Map File

'water.map' ist the layer configuration file for the fileserver. If you like to set up a mapserver (http://mapserver.org), take the file. Just the paths for the sqlite file must be mached.

## Lizenz

The software is available under BSD License (http://www.linfo.org/bsdlicense.html)

## Author
Nathanael Lang

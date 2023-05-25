#!/bin/bash

set -e

function usage() {
    echo "detector-init.sh [-n nimages] [-v simplon_version] <hostname>"
    exit
}

# parse keyword args
N_IMAGES=2
VERSION=1.8.0
while [ $# -gt 0 ]; do
    case $1 in
	-n)
	    N_IMAGES=$2
	    shift 2
	    ;;
	-v)
	    VERSION=$2
	    shift 2
	    ;;
	*)
	    break;
	    ;;
    esac
done

# parse positional args
[ $# -eq 1 ] || usage
DCU=$1


#curl -X PUT http://$DCU/detector/api/$VERSION/config/trigger_mode -d '{"value":"exte"}'
#curl -X PUT http://$DCU/detector/api/$VERSION/config/nimages -d "{\"value\":1}"
#curl -X PUT http://$DCU/detector/api/$VERSION/config/compression -d '{"value":"lz4"}'
#curl -X PUT http://$DCU/detector/api/$VERSION/config/omega_start -d '{"value":0.0}'
#curl -X PUT http://$DCU/detector/api/$VERSION/config/omega_increment -d '{"value":90.0}'

curl -X PUT http://$DCU/stream/api/$VERSION/command/initialize
curl -X PUT http://$DCU/stream/api/$VERSION/config/mode -d '{"value":"enabled"}'
curl -X PUT http://$DCU/stream/api/$VERSION/config/header_detail -d '{"value":"basic"}'

#curl -X PUT http://$DCU/detector/api/$VERSION/command/arm
#curl -X PUT http://$DCU/detector/api/$VERSION/command/trigger

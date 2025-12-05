#!/bin/bash
#
# Download ostserver debian file
# Hardcoded repo 
#
set -e
echo $1
str='https://gitlab.ostserver.fr/api/v4'
if [[ "$1" == "$str" ]]
then 
    echo "Download ostserver from gitlab.ostserver.fr" 
    wget https://gitlab.ostserver.fr/api/v4/projects/1/packages/generic/latest/latest/ostserver_latest_amd64.deb 
else 
    echo "Download  ostserver from gitlab.com" 
    wget https://gitlab.com/api/v4/projects/76294902/packages/generic/latest/latest/ostserver_latest_amd64.deb 
fi
 

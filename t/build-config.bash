#!/bin/bash

read -p "Enter AS/400 Host: " host
read -p "Enter AS/400 Username: " user
read -sp "Enter AS/400 Password: " pass

cat << EOF > config.h
#define AS400_HOST	"$host"
#define AS400_USER	"$user"
#define AS400_PASS	"$pass"
#define AS400_VERBOSITY	"99"
#define ZS_PATH		"../zs"
EOF

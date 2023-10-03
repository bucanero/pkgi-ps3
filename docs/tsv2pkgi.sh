#!/bin/sh
TSV_FILE="$1"
TYPE="${2:-0}"
DATABASE_FILE="$3"
DATABASE_FN="pkgi"
DATABASE_EXT="txt"
DATABASE_TYPE=""
case "$TYPE" in
  [0-9]) TYPE_ID="$2";;
  Uknown | none | NONE) TYPE_ID=0;;
  Game | game | GAME) TYPE_ID=1 && DATABASE_TYPE='_games';;
  dlc|DLC) TYPE_ID=2 && DATABASE_TYPE='_dlcs';;
  Theme | theme | THEME) TYPE_ID=3 && DATABASE_TYPE='_themes';;
  Avatar | avatar | AVATAR) TYPE_ID=4 && DATABASE_TYPE='_avatars';;
  Demo | DEMO | demo) TYPE_ID=5 && DATABASE_TYPE='_demos';;
  Manager | manager | MANAGER) TYPE_ID=6 && DATABASE_TYPE='_managers';;
  Emulator | emulator | EMULATOR) TYPE_ID=7 && DATABASE_TYPE='_emulators';;
  App | app | APP)  TYPE_ID=8 && DATABASE_TYPE='_apps';;
  Tool |tool | TOOL) TYPE_ID=9 && DATABASE_TYPE='_tools';;
  *) TYPE_ID=0;;
esac

[ -z "$DATABASE_FILE"] && DATABASE_FILE="${DATABASE_FN}${DATABASE_TYPE}.${DATABASE_EXT}"

tail -n +2 "$TSV_FILE" | awk -F '	' '{print $6","'"$TYPE_ID"'","$3",,"$5","$4","$9","$10}' > "$DATABASE_FILE"

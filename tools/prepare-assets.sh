#!/usr/bin/env bash
# Copia y optimiza el arte de la tienda desde la web de Esteban's Forge y los proyectos 3DS.
# Uso: tools/prepare-assets.sh        (requiere ffmpeg, ffprobe, cwebp y dwebp)
#
# Los iconos usan el arte HD de cada proyecto (el mismo diseño que su icono 48x48 de 3DS) a 192x192.
# Mutiny solo tiene pixel art de 48x48: se amplía x4 sin suavizado.
# Las capturas de la web vienen como pantalla superior + inferior lado a lado (720x240 a 1x o 2x).
# Se separan en shots/NN-top.webp (800x480) y shots/NN-bottom.webp (640x480): el doble de la
# resolución de 3DS, para que se vean nítidas con el zoom del prototipo.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SITE="${FORGE_SITE:-$ROOT/../../../../02 WEB/PORTFOLIO/github/estebanpdn.github.io/public/assets}"
PROJ="${FORGE_PROJECTS:-$ROOT/../../PROJECTS}"
OUT="$ROOT/assets"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

for tool in ffmpeg ffprobe cwebp dwebp; do  # ffmpeg needs libx264 + aac for the trailers
  command -v "$tool" >/dev/null || { echo "Falta $tool" >&2; exit 1; }
done

# Decodifica cualquier imagen a PNG para que ffmpeg la pueda recortar y escalar.
to_png() {
  local src="$1" out="$TMP/src-$RANDOM.png"
  case "$src" in
    *.webp) dwebp -quiet "$src" -o "$out" ;;
    *) ffmpeg -loglevel error -y -i "$src" -frames:v 1 "$out" ;;
  esac
  echo "$out"
}

# encode <origen> <destino.webp> <filtro ffmpeg> <calidad|lossless>
encode() {
  local src="$1" dst="$2" vf="$3" quality="$4" png
  png="$(to_png "$src")"
  ffmpeg -loglevel error -y -i "$png" -vf "$vf" -frames:v 1 "$TMP/frame.png"
  mkdir -p "$(dirname "$dst")"
  if [[ "$quality" == lossless ]]; then
    cwebp -quiet -lossless -z 9 "$TMP/frame.png" -o "$dst"
  else
    cwebp -quiet -q "$quality" -alpha_q 100 -m 6 "$TMP/frame.png" -o "$dst"
  fi
}

icon_smooth() { encode "$2" "$OUT/ports/$1/icon.webp" "crop=min(iw\,ih):min(iw\,ih),scale=192:192:flags=lanczos" 90; }
icon_pixel() { encode "$2" "$OUT/ports/$1/icon.webp" "scale=192:192:flags=neighbor" lossless; }
logo() { encode "$2" "$OUT/ports/$1/logo.webp" "${3:-scale='min(720,iw)':-2:flags=lanczos}" 90; }
art() { encode "$2" "$OUT/ports/$1/${4:-art}.webp" "${3:-scale=800:-2:flags=lanczos}" 82; }

# shot <id> <número> <captura>
shot() {
  local id="$1" n="$2" src="$3" png w h s dir="$OUT/ports/$1/shots"
  png="$(to_png "$src")"
  IFS=, read -r w h < <(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 "$png")
  mkdir -p "$dir"
  if (( w * 240 == h * 720 )); then
    s=$(( h / 240 ))
    ffmpeg -loglevel error -y -i "$png" -vf "crop=$((400 * s)):$h:0:0,scale=800:480:flags=neighbor" "$TMP/top.png"
    ffmpeg -loglevel error -y -i "$png" -vf "crop=$((320 * s)):$h:$((400 * s)):0,scale=640:480:flags=neighbor" "$TMP/bottom.png"
    cwebp -quiet -q 86 -m 6 "$TMP/bottom.png" -o "$dir/$n-bottom.webp"
  elif (( w * 480 == h * 400 )); then
    s=$(( w / 400 ))
    ffmpeg -loglevel error -y -i "$png" -vf "crop=$w:$((240 * s)):0:0,scale=800:480:flags=neighbor" "$TMP/top.png"
    ffmpeg -loglevel error -y -i "$png" -vf "crop=$((320 * s)):$((240 * s)):$((40 * s)):$((240 * s)),scale=640:480:flags=neighbor" "$TMP/bottom.png"
    cwebp -quiet -q 86 -m 6 "$TMP/bottom.png" -o "$dir/$n-bottom.webp"
  else
    ffmpeg -loglevel error -y -i "$png" -vf "scale=800:480:flags=neighbor" "$TMP/top.png"
  fi
  cwebp -quiet -q 86 -m 6 "$TMP/top.png" -o "$dir/$n-top.webp"
}

mkdir -p "$OUT/brand" "$OUT/audio" "$OUT/fonts"

# Marca y sonidos de la intro de la web.
cp "$SITE/brand/estebans-forge-icon.png" "$OUT/brand/forge-icon.png"
cp "$SITE/audio/05-terminal.mp3" "$OUT/audio/intro-welcome.mp3"
cp "$SITE/audio/forge-entry.mp3" "$OUT/audio/intro-enter.mp3"

# Nunito y DM Sans (OFL), las mismas que usa la web.
cp "$SITE"/fonts/*.woff2 "$SITE"/fonts/dmsans.css "$SITE"/fonts/nunito.css \
  "$SITE"/fonts/dmsans-OFL.txt "$SITE"/fonts/nunito-OFL.txt "$OUT/fonts/"

CAP="$SITE/captures"

# A Link to the Past
icon_smooth alttp "$SITE/icons/alttp.webp"
logo alttp "$SITE/logos/a-link-to-the-past.png" "scale=-2:420:flags=lanczos"
art alttp "$SITE/artwork/a-link-to-the-past.png"
n=1; for f in 01-title 03-light-world 05-hyrule-castle 09-dark-world 16-inventory 19-randomizer; do
  shot alttp "$(printf %02d $n)" "$CAP/a-link-to-the-past/$f.webp"; n=$((n + 1)); done

# The Minish Cap
icon_smooth minish "$SITE/icons/minish.webp"
logo minish "$SITE/logos/the-minish-cap.png"
art minish "$SITE/artwork/the-minish-cap.png" "crop=iw:iw*0.62:0:ih*0.08,scale=800:-2:flags=lanczos"
n=1; for f in 01-title 03-picori-festival 04-minish-woods 05-minish-village 07-crenel-cave 10-map; do
  shot minish "$(printf %02d $n)" "$CAP/the-minish-cap/$f.webp"; n=$((n + 1)); done

# Legend of Doom
icon_smooth doom "$PROJ/LEGEND OF DOOM/assets/images/ICON_VARIANT.png"
logo doom "$PROJ/LEGEND OF DOOM/assets/images/BANNER.png"
art doom "$SITE/artwork/legend-of-doom.png"
n=1; for f in 01-title 03-hyrule 04-sword-cave 05-forest-enemy 11-automap; do
  shot doom "$(printf %02d $n)" "$CAP/legend-of-doom/$f.webp"; n=$((n + 1)); done

# Mario Kart 64
icon_smooth mk64 "$SITE/icons/mk64.webp"
logo mk64 "$SITE/logos/mario-kart-64-clean.png"
art mk64 "$SITE/artwork/mario-kart-64.png"
n=1; for f in 01-title 03-characters 05-luigi-raceway 06-koopa-troopa-beach 10-rainbow-road; do
  shot mk64 "$(printf %02d $n)" "$CAP/mario-kart-64/$f.webp"; n=$((n + 1)); done

# Star Fox / Starwing
icon_smooth starwing "$PROJ/STARFOX SNES/assets/banners/STARFOX_3D/source/starfox-icon-original.jpg"
logo starwing "$SITE/logos/starwing.png" "scale=512:-2:flags=lanczos"
art starwing "$SITE/artwork/starwing.png"
n=1; for f in 01-title 05-lylat-map 06-corneria 09-asteroids 10-venom; do
  shot starwing "$(printf %02d $n)" "$CAP/star-fox/$f.webp"; n=$((n + 1)); done

# Mutiny: logo del juego, decorado de la partida como fondo y capturas de la documentación.
icon_pixel mutiny "$PROJ/MUNITY/assets/3ds/icon.png"
logo mutiny "$PROJ/MUNITY/assets/ui/logo.png" "scale=632:216:flags=neighbor"
art mutiny "$PROJ/MUNITY/docs/images/screens/aim.png" "crop=800:480:0:0,scale=800:480:flags=neighbor"
n=1; for f in title aim weapons result poop; do
  shot mutiny "$(printf %02d $n)" "$PROJ/MUNITY/docs/images/screens/$f.png"; n=$((n + 1)); done

# Super Mario 64 DS
icon_smooth sm64ds "$PROJ/SM64DS/assets/icons/mario-yellow-approved.png"
logo sm64ds "$PROJ/SM64DS/assets/banners/approved-logo-v2/previews/front.png" "crop=1236:640:137:127,scale=720:-2:flags=lanczos"
# Fondo: el castillo con Mario, Luigi, Wario y Yoshi (tools/source/sm64ds-castle.png).
art sm64ds "$ROOT/tools/source/sm64ds-castle.png" "crop=1240:620:680:0,scale=800:-2:flags=lanczos"
shot sm64ds 01 "$PROJ/SM64DS/docs/images/build-24-pause.png"

# Dusklight
icon_smooth dusklight "$PROJ/DUSKLIGHT/icon.png"
logo dusklight "$SITE/logos/dusklight.png"
art dusklight "$SITE/dusklight/hyrule.webp"
art dusklight "$SITE/dusklight/link-epona.webp" "scale=-2:420:flags=lanczos" mid

# Worms 3D
icon_smooth worms "$PROJ/WORMS 3D/ICON.png"
logo worms "$SITE/worms/logo.webp"
art worms "$SITE/worms/wallpaper.webp"
n=1; for f in menu match bridge fortress target; do
  shot worms "$(printf %02d $n)" "$SITE/worms/captures/$f.webp"; n=$((n + 1)); done

# Red Viper Color: el mando de Virtual Boy en pixel art (de la web), con contorno blanco
# sobre un rojo oscuro. Se monta a su resolución real (72x72) y se amplía x3 sin suavizado.
mkdir -p "$OUT/ports/rvc"
ffmpeg -loglevel error -y -i "$SITE/red-viper/hardware-controller.png" \
  -f lavfi -i "gradients=s=72x72:c0=0xb3222f:c1=0x4d0910:type=radial:x0=36:y0=24:x1=36:y1=96:d=1" \
  -filter_complex "[0]crop=165:159:15:12,scale=55:53:flags=neighbor,format=rgba,pad=72:72:8:10:color=black@0[art];[art]split[a1][a2];[a1]alphaextract,dilation,format=gray[m];color=white:s=72x72,format=rgba[w];[w][m]alphamerge[outline];[1]format=rgba[bg];[bg][outline]overlay[b1];[b1][a2]overlay,scale=216:216:flags=neighbor" \
  -frames:v 1 "$TMP/rvc-icon.png"
cwebp -quiet -lossless "$TMP/rvc-icon.png" -o "$OUT/ports/rvc/icon.webp"
logo rvc "$SITE/red-viper/emblem.png" "scale=416:-2:flags=lanczos"
art rvc "$SITE/red-viper/pixel-tunnel.png"
n=1; for f in wario golf tennis jack; do
  shot rvc "$(printf %02d $n)" "$SITE/red-viper/captures/$f-screen.webp"; n=$((n + 1)); done

# --- Vídeos --------------------------------------------------------------------
# Tráilers (con audio) a 800 px y 30 fps, y clips de gameplay separados en pantalla
# superior e inferior para reproducirlos sincronizados en las dos pantallas.
VID="${FORGE_VIDEOS:-$ROOT/../../../../04 MEDIA/VIDEOS/projects}"

trailer() { # <id> <origen> <segundo del póster>
  local dir="$OUT/ports/$1/video"
  mkdir -p "$dir"
  if [[ ! "$dir/trailer.mp4" -nt "$2" ]]; then
    ffmpeg -loglevel error -y -i "$2" -vf "scale=800:-2:flags=lanczos,fps=30" -c:v libx264 -preset slow -crf 28 \
      -profile:v high -pix_fmt yuv420p -c:a aac -b:a 96k -movflags +faststart "$dir/trailer.mp4"
  fi
  ffmpeg -loglevel error -y -ss "$3" -i "$2" -frames:v 1 -vf "scale=800:-2:flags=lanczos" "$TMP/poster.png"
  cwebp -quiet -q 82 "$TMP/poster.png" -o "$dir/trailer.webp"
}

clip() { # <id> <nombre> <vídeo superior> <vídeo inferior>
  local dir="$OUT/ports/$1/video"
  mkdir -p "$dir"
  ffmpeg -loglevel error -y -i "$3" -c:v copy -an -movflags +faststart "$dir/$2-top.mp4"
  ffmpeg -loglevel error -y -i "$4" -c:v copy -an -movflags +faststart "$dir/$2-bottom.mp4"
  ffmpeg -loglevel error -y -ss 1 -i "$3" -frames:v 1 -vf "scale=800:480:flags=neighbor" "$TMP/poster.png"
  cwebp -quiet -q 84 "$TMP/poster.png" -o "$dir/$2.webp"
}

trailer alttp "$VID/alttp-v4/renders/alttp-3ds-v4.0-trailer-1080p.mp4" 20
trailer mk64 "$VID/mariokart/renders/mk64-3ds-v2.0-trailer-1080p.mp4" 20
clip alttp light-world "$CAP/a-link-to-the-past/v4-light-world-top.mp4" "$CAP/a-link-to-the-past/v4-light-world-bottom.mp4"
clip mk64 luigi-raceway "$CAP/mario-kart-64/v2-luigi-raceway-top.mp4" "$CAP/mario-kart-64/v2-luigi-raceway-bottom.mp4"
clip minish minish-woods "$CAP/the-minish-cap/04-minish-woods-top.mp4" "$CAP/the-minish-cap/04-minish-woods-bottom.mp4"
clip starwing corneria "$CAP/star-fox/07-corneria-city-top.mp4" "$CAP/star-fox/07-corneria-city-bottom.mp4"
clip doom forest "$CAP/legend-of-doom/05-forest-enemy-top.mp4" "$CAP/legend-of-doom/05-forest-enemy-bottom.mp4"

echo "Recursos listos en $OUT"
du -sh "$OUT"

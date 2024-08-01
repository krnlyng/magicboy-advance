#!/bin/sh

set -ex

if [ "$1" == "" ]; then
    echo "specify size"
    exit 1
fi

if [ "$2" == "" ]; then
    echo "specify font"
    exit 1
fi

if [ "$3" == "" ]; then
    echo "specify font name in c"
    exit 1
fi

if [ "$4" == "" ]; then
    echo "specify color"
    exit 1
fi

if [ "$5" == "" ]; then
    echo "specify top crop"
    exit 1
fi

if [ "$6" == "" ]; then
    echo "specify left crop"
    exit 1
fi

SIZE=$1
CROP=$5

LEFTCROP=$6

CROPARG="-crop $(($SIZE / 2))x$SIZE+$LEFTCROP+0"

for ((num=0;num<10;num++)); do
    magick -background white -fill black -font $2 -pointsize $SIZE -size "$(($SIZE / 2 + $LEFTCROP))"x$SIZE label:"$num" $CROPARG tmpNumber"$num".png
done
magick -background white -fill black -font $2 -pointsize $SIZE -size "$(($SIZE / 2 + $LEFTCROP))"x$SIZE label:"-" $CROPARG tmpNumberMinus.png
magick -background white -fill black -font $2 -pointsize $SIZE -size "$(($SIZE / 2 + $LEFTCROP))"x$SIZE label:"." $CROPARG tmpNumberDot.png

magick tmpNumber[0-9].png tmpNumberMinus.png tmpNumberDot.png -background black -alpha off -depth 4 +append $2-numbers-nocrop.png

magick $2-numbers-nocrop.png -crop "$(($SIZE * 12 / 2 + 12 * $LEFTCROP))"x$(($SIZE - $CROP))+0+$CROP $2-numbers.png

g++ `pkg-config --cflags Magick++` `pkg-config --libs Magick++` numbers.cpp -o get_c

./get_c $2-numbers.png $3 $4

rm $2-numbers-nocrop.png
rm $2-numbers.png
rm tmpNumber*.png
rm get_c


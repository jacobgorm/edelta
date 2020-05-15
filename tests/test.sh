#!/bin/sh

( cd .. && jam )

TIM="/usr/bin/time -f %U"
rm -f ../out/time/* ../out/size/*


for i in wget-ppc ; do 
#for i in user32 user32_prebbt user32_12 ; do 
#for i in user32_12 ; do 
#for i in vlad ; do 
#for i in vim-ppc ; do 
#for i in epiphany-ia32 ; do 
#for i in hello-ppc ; do 
#for i in linux-ppc ; do 
#for i in xpdf-ppc ; do 
#for i in hejmor ; do 
#for i in runclient-ppc ; do 

#for i in `ls|grep -v test`; do 
	(
	echo Testing $i
	timefile=../../out/time/$i
	sizefile=../../out/size/$i

	cd $i

	echo   edelta delta ------------------------

	rm -f res
	$TIM 2>>$timefile ../../build/edelta -q delta *_v[12] diff.edelta
	wc -c diff.edelta >>$sizefile

	echo   edelta patch ------------------------
	cat diff.edelta | ../../build/edelta -q patch *_v1 res
	cmp *_v2 res || echo patches incorrectly!!

#	echo   edelta LE ------------------------
#	$TIM 2>>$timefile ../../build/edelta -q -le delta *_v[12] diff.ledelta
#	wc -c diff.ledelta >>$sizefile
#
#	echo   edelta patch  LE --------------------
#	cat diff.ledelta | ../../build/edelta -q -le patch *_v1 res
#	cmp *_v2 res || echo LE patches incorrectly!!


	$TIM 2>>$timefile ~/bsdiff-4.3/bsdiff *_v[12] diff.bsdiff
	wc -c diff.bsdiff >>$sizefile

	#$TIM 2>>$timefile xdelta delta *_v[12] diff.xdelta
	#wc -c diff.xdelta >>$sizefile

	$TIM 2>>$timefile ~/zdelta-2.1/zdc *_v[12] diff.zdelta
	wc -c diff.zdelta >>$sizefile

	#cp *_v[2] whole
	#$TIM 2>>$timefile gzip -f whole
	#wc -c whole.gz >>$sizefile

	cp $sizefile size.tmp
	cat size.tmp|sed -e 's/ .*//' > $sizefile
	# ls -l diff* whole.gz

	#rm -f diff* whole.gz res

	echo
	)

done

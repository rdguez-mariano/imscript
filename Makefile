# compiler specific part (may be removed with minor damage)
#
ENABLE_GSL = yes
WFLAGS=
WFLAGS = -pedantic -Wall -Wextra -Wshadow -Wstrict-prototypes -Wno-unused -Wno-parentheses -Wno-sign-compare -Werror -Wno-error=format -Wno-error=overflow
WFLAGS = -pedantic -Wall -Wextra -Wshadow -Wstrict-prototypes -Wno-unused -Wno-sign-compare
WFLAGS=

OFLAGS = $(WFLAGS)
OFLAGS = -march=native -O3

SRCDIR = src
BINDIR = bin

SRCIIO = fftshift sterint plambda viewflow imprintf ntiply backflow unalpha imdim downsa flowarrows flowdiv fnorm imgstats qauto qeasy lrcat tbcat lk hs rgbcube iminfo setdim synflow vecstack ofc component faxpb faxpby iion flowgrad fillcorners colorflow lic deframe crosses crop angleplot closeup hrezoom upsa veco vecov vecov_lm flowinv ghisto shuntingyard rpc overpoints periodize rpcflow ransac genk cgi zeropad siftu pview homfilt rpchfilt rpc_errfilt uncrop maptp rpcparcheck rpc_errsingle rpc_errpair cline rpc_angpair rpc_curvpair chisto fftper srmatch croparound zoombil flowh harris lgblur rpc_eval cutrecombine cdeint imgerr amle elap imspread replicate disp_to_corr printmask colormesh colormeshh ijmesh elap3 sphereheights fmsrA elap_rec amle_rec palette radphar aronsson43 scheme_plap flowjac elap_recsep aronsson11 fill_bill fill_rect rpc_epicyl starfield morsi gharrows ipol_watermark fontu fontu2 cglap flownop pairsinp pairhom poisson_rec cgpois cgpois_rec isoricci lapbediag lapcolo simplest_inpainting lapbediag_sep cldmask plyflatten metatiler hview dither ditheru histeq8 thinpa_recsep really_simplest_inpainting bmms perms censust satproj mnehs mnehs_ms aff3d amle_recsep elevate_matches elevate_matcheshh pmba pmba2 frustumize ghough ghough2 tdip ihough2 alphadots posmax ppsboundary homdots awgn strt remove_small_cc distance tregistration tcregistration tvint shadowcast raddots simpois nnint bdint contihist graysing unshadow sfblur overflow ppsmooth inppairs mima utm autholes plyroads startracker homwarp tiffu fancy_evals rpc_warpab rpc_warpabt rpc_mnehs rpc_pm rpc_pmn bandhisto polygonify dveco dvecov_lm drawroads
SRCFFT = gblur fft dct blur lgblur2 lure lgblur3 testgblur lures dht
ifeq ($(ENABLE_GSL), yes)
	SRCGSL = paraflow minimize
endif

# swap next two lines for libraw support
IIOFLAGS = -ljpeg -ltiff -lpng -lm -lraw -lHalf -lIex -lIlmImf -fopenmp
IIOFLAGS = -ljpeg -ltiff -lpng -lm
FFTFLAGS = -lfftw3f
GSLFLAGS = -lgsl -lgslcblas

# swap next two lines for libraw support
IIO=../iio/contrib/cmake/libIIOLIB.a
IIO=$(SRCDIR)/iio.o


SRC = $(SRCIIO) $(SRCFFT) $(SRCGSL)
PROGRAMS = $(addprefix $(BINDIR)/,$(SRC) flow_ms rgfield rgfields rgfieldst elap2 flambda fancy_zoomout fancy_downsa fancy_crop)


.PHONY: default
default: $(PROGRAMS)

$(addprefix $(BINDIR)/,depend) : $(SRCDIR)/

$(addprefix $(BINDIR)/,$(SRCIIO)) : $(BINDIR)/% : $(SRCDIR)/%.c $(IIO)
	$(CC) $(CFLAGS) $(OFLAGS) $^ -o $@ $(IIOFLAGS)

$(addprefix $(BINDIR)/,$(SRCFFT)) : $(BINDIR)/% : $(SRCDIR)/%.c $(IIO)
	$(CC) $(CFLAGS) $(OFLAGS) $^ -o $@ $(IIOFLAGS) $(FFTFLAGS)

$(addprefix $(BINDIR)/,$(SRCGSL)) : $(BINDIR)/% : $(SRCDIR)/%.c $(IIO)
	$(CC) $(CFLAGS) $(OFLAGS) $^ -o $@ $(IIOFLAGS) $(GSLFLAGS)

$(SRCDIR)/iio.o : $(SRCDIR)/iio.c $(SRCDIR)/iio.h
	$(CC) $(CFLAGS) $(OFLAGS) -c $< -o $@

$(SRCDIR)/hs.o: $(SRCDIR)/hs.c
	$(CC) $(CFLAGS) $(OFLAGS) -DOMIT_MAIN -c $< -o $@

$(SRCDIR)/lk.o: $(SRCDIR)/lk.c
	$(CC) $(CFLAGS) $(OFLAGS) -DOMIT_MAIN -c $< -o $@

$(SRCDIR)/gblur.o: $(SRCDIR)/gblur.c
	$(CC) $(CFLAGS) $(OFLAGS) -DOMIT_GBLUR_MAIN -c $< -o $@

$(SRCDIR)/flow_ms.o: $(SRCDIR)/flow_ms.c
	$(CC) $(CFLAGS) $(OFLAGS) -c $< -o $@

$(SRCDIR)/flowarrows.o: $(SRCDIR)/flowarrows.c
	$(CC) $(CFLAGS) $(OFLAGS) -DOMIT_MAIN -c $< -o $@

#ifndef OMIT_BLUR_MAIN
#define MAIN_BLUR
#endif
$(SRCDIR)/distance.o: $(SRCDIR)/distance.c
	$(CC) $(CFLAGS) $(OFLAGS) -DOMIT_DISTANCE_MAIN -c $< -o $@

$(BINDIR)/flow_ms: $(addprefix $(SRCDIR)/,flow_ms.c gblur.o hs.o lk.o iio.o)
	$(CC) $(CFLAGS) $(OFLAGS) -DUSE_MAINAPI $^ -o $@ $(IIOFLAGS) $(FFTFLAGS)

$(BINDIR)/rgfield: $(addprefix $(SRCDIR)/,rgfield.c gblur.o iio.o)
	$(CC) $(CFLAGS) $(OFLAGS) $^ -o $@ $(IIOFLAGS) $(FFTFLAGS)

$(BINDIR)/rgfields: $(addprefix $(SRCDIR)/,rgfields.c gblur.o iio.o)
	$(CC) $(CFLAGS) $(OFLAGS) $^ -o $@ $(IIOFLAGS) $(FFTFLAGS)

$(BINDIR)/rgfieldst: $(addprefix $(SRCDIR)/,rgfieldst.c gblur.o iio.o)
	$(CC) $(CFLAGS) $(OFLAGS) $^ -o $@ $(IIOFLAGS) $(FFTFLAGS)

$(BINDIR)/elap2: $(addprefix $(SRCDIR)/,elap2.c distance.o iio.o)
	$(CC) $(CFLAGS) $(OFLAGS) $^ -o $@ $(IIOFLAGS)

$(BINDIR)/flambda: $(addprefix $(SRCDIR)/,flambda.c fancy_image.o iio.o)
	$(CC) $(CFLAGS) $(OFLAGS) $^ -o $@ $(IIOFLAGS)

$(BINDIR)/fancy_zoomout: $(addprefix $(SRCDIR)/,fancy_zoomout.c fancy_image.o iio.o)
	$(CC) $(CFLAGS) $(OFLAGS) $^ -o $@ $(IIOFLAGS)

$(BINDIR)/fancy_downsa: $(addprefix $(SRCDIR)/,fancy_downsa.c fancy_image.o iio.o)
	$(CC) $(CFLAGS) $(OFLAGS) $^ -o $@ $(IIOFLAGS)

$(BINDIR)/fancy_crop: $(addprefix $(SRCDIR)/,fancy_crop.c fancy_image.o iio.o)
	$(CC) $(CFLAGS) $(OFLAGS) $^ -o $@ $(IIOFLAGS)

.PHONY: clean
clean:
	@rm -f $(PROGRAMS) $(SRCDIR)/*.o

.PHONY: zipdate
zipdate: clean
	(cd ..;tar --exclude-vcs -zchf imscript_`date +%Y_%m_%d_%H_%M`.tar.gz imscript)

.PHONY: zip
zip: clean
	(cd ..;tar --exclude-vcs -zchf imscript.tar.gz imscript)

include src/dependencies

src/plambda.c: src/random.c




test: $(BINDIR)/plambda $(BINDIR)/imprintf
	plambda zero:512x512 "rand rand rand rand rand 5 njoin" | plambda - "split del 0 >" | imprintf "%s\n" | grep -q 130945

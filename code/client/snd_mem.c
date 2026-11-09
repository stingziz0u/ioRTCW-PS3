/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/*****************************************************************************
 * name:		snd_mem.c
 *
 * desc:		sound caching
 *
 * $Archive: /MissionPack/code/client/snd_mem.c $
 *
 *****************************************************************************/

#include "snd_local.h"
#include "snd_codec.h"

#define DEF_COMSOUNDMEGS "32"

/*
===============================================================================

memory management

===============================================================================
*/

static	sndBuffer	*buffer = NULL;
static	sndBuffer	*freelist = NULL;
static	int inUse = 0;
static	int totalInUse = 0;

short *sfxScratchBuffer = NULL;
sfx_t *sfxScratchPointer = NULL;
int	   sfxScratchIndex = 0;

void	SND_free(sndBuffer *v) {
	*(sndBuffer **)v = freelist;
	freelist = (sndBuffer*)v;
	inUse += sizeof(sndBuffer);
}

#ifdef __PS3__
void S_AdpcmCacheForget( sndBuffer *chunk );    // snd_mix.c
#endif

sndBuffer*	SND_malloc(void) {
	sndBuffer *v;
redo:
	if (freelist == NULL) {
		S_FreeOldestSound();
		goto redo;
	}

	inUse -= sizeof(sndBuffer);
	totalInUse += sizeof(sndBuffer);

	v = freelist;
	freelist = *(sndBuffer **)freelist;
	v->next = NULL;
#ifdef __PS3__
	S_AdpcmCacheForget( v );    // reused: its decoded samples are stale
#endif
	return v;
}

void SND_setup(void) {
	sndBuffer *p, *q;
	cvar_t	*cv;
	int scs;

	cv = Cvar_Get( "com_soundMegs", DEF_COMSOUNDMEGS, CVAR_LATCH | CVAR_ARCHIVE );

	scs = (cv->integer*1536);

#ifdef __PS3__
	// Hard cap, not a default: com_soundMegs is CVAR_ARCHIVE, so a stale
	// config would beat the command line. The default (32) asks for ~100 MB,
	// and lv2 hangs instead of returning NULL when a malloc does not fit.
	if ( cv->integer > PS3_MAX_COMSOUNDMEGS ) {
		scs = PS3_MAX_COMSOUNDMEGS * 1536;
	}
	Com_Printf( "SND_setup: %i KB sound cache (com_soundMegs %i, PS3 cap %i)\n",
		(int)( ( scs * sizeof( sndBuffer ) ) / 1024 ), cv->integer, PS3_MAX_COMSOUNDMEGS );
#endif

	buffer = malloc(scs*sizeof(sndBuffer) );
#ifdef __PS3__
	if ( !buffer ) {
		Com_Error( ERR_FATAL, "SND_setup: out of memory for the sound cache" );
	}
#endif
	// allocate the stack based hunk allocator
	sfxScratchBuffer = malloc(SND_CHUNK_SIZE * sizeof(short) * 4);	//Hunk_Alloc(SND_CHUNK_SIZE * sizeof(short) * 4);
	sfxScratchPointer = NULL;

	inUse = scs*sizeof(sndBuffer);
	p = buffer;;
	q = p + scs;
	while (--q > p)
		*(sndBuffer **)q = q-1;
	
	*(sndBuffer **)q = NULL;
	freelist = p + scs - 1;
#ifdef __PS3__
	S_AdpcmCacheForget( NULL );
#endif

	Com_Printf("Sound memory manager started\n");
}

void SND_shutdown(void)
{
		free(sfxScratchBuffer);
		free(buffer);
}

#ifdef __PS3__
/*
Decimating (the mixer runs at 22050 Hz: a 44.1 kHz sound is halved): average
the source frames each output sample covers instead of keeping one of them,
which aliased the highs into a metallic hiss. span = 1 when not decimating.
*/
static int PS3_DecimSample16( const short *data, int srcsample, int channels, int j, int span, int total ) {
	int k, sum = 0, n = 0;

	for ( k = 0; k < span; k++ ) {
		int idx = srcsample + k * channels + j;
		if ( idx >= total ) {
			break;
		}
		sum += data[idx];
		n++;
	}
	return n ? sum / n : data[srcsample + j];
}
#define PS3_DECIM_SPAN( stepscale ) ( ( stepscale ) >= 1.5f ? (int)( ( stepscale ) + 0.5f ) : 1 )
#endif

/*
================
ResampleSfx

resample / decimate to the current source rate
================
*/
static int ResampleSfx( sfx_t *sfx, int channels, int inrate, int inwidth, int samples, byte *data, qboolean compressed ) {
	int		outcount;
	int		srcsample;
	float	stepscale;
	int		i, j;
	int		sample, samplefrac, fracstep;
	int			part;
	sndBuffer	*chunk;
	
	stepscale = (float)inrate / dma.speed;	// this is usually 0.5, 1, or 2

	outcount = samples / stepscale;

	srcsample = 0;
	samplefrac = 0;
	fracstep = stepscale * 256 * channels;
	chunk = sfx->soundData;

	for (i=0 ; i<outcount ; i++)
	{
		srcsample += samplefrac >> 8;
		samplefrac &= 255;
		samplefrac += fracstep;
		for (j=0 ; j<channels ; j++)
		{
			if( inwidth == 2 ) {
#ifdef __PS3__
				sample = PS3_DecimSample16( (short *)data, srcsample, channels, j, PS3_DECIM_SPAN( stepscale ), samples * channels );
#else
				sample = ( ((short *)data)[srcsample+j] );
#endif
			} else {
				sample = (unsigned int)( (unsigned char)(data[srcsample+j]) - 128) << 8;
			}
			part = (i*channels+j)&(SND_CHUNK_SIZE-1);
			if (part == 0) {
				sndBuffer	*newchunk;
				newchunk = SND_malloc();
				if (chunk == NULL) {
					sfx->soundData = newchunk;
				} else {
					chunk->next = newchunk;
				}
				chunk = newchunk;
			}

			chunk->sndChunk[part] = sample;
		}
	}

	return outcount;
}

/*
================
ResampleSfx

resample / decimate to the current source rate
================
*/
static int ResampleSfxRaw( short *sfx, int channels, int inrate, int inwidth, int samples, byte *data ) {
	int			outcount;
	int			srcsample;
	float		stepscale;
	int			i, j;
	int			sample, samplefrac, fracstep;
	
	stepscale = (float)inrate / dma.speed;	// this is usually 0.5, 1, or 2

	outcount = samples / stepscale;

	srcsample = 0;
	samplefrac = 0;
	fracstep = stepscale * 256 * channels;

	for (i=0 ; i<outcount ; i++)
	{
		srcsample += samplefrac >> 8;
		samplefrac &= 255;
		samplefrac += fracstep;
		for (j=0 ; j<channels ; j++)
		{
			if( inwidth == 2 ) {
#ifdef __PS3__
				// the wav codec already swapped the samples to host order
				// (S_ByteSwapRawSamples); swapping again on big-endian turned
				// every ADPCM sound into noise. ResampleSfx does not swap either.
				sample = PS3_DecimSample16( (short *)data, srcsample, channels, j, PS3_DECIM_SPAN( stepscale ), samples * channels );
#else
				sample = LittleShort ( ((short *)data)[srcsample+j] );
#endif
			} else {
				sample = (int)( (unsigned char)(data[srcsample+j]) - 128) << 8;
			}
			sfx[i*channels+j] = sample;
		}
	}
	return outcount;
}

//=============================================================================

/*
==============
S_LoadSound

The filename may be different than sfx->name in the case
of a forced fallback of a player specific sound
==============
*/
qboolean S_LoadSound( sfx_t *sfx )
{
	byte	*data;
	short	*samples;
	snd_info_t	info;
//	int		size;

	// player specific sounds are never directly loaded
	if ( sfx->soundName[0] == '*') {
		return qfalse;
	}

	// load it in
	data = S_CodecLoad(sfx->soundName, &info);
	if(!data)
		return qfalse;

	if ( info.width == 1 ) {
		Com_DPrintf(S_COLOR_YELLOW "WARNING: %s is a 8 bit audio file\n", sfx->soundName);
	}

#if defined( __PS3__ ) && defined( PS3_DIAG )
	// the mixer runs at 22050 Hz: say which sounds are not 22k mono
	if ( info.rate != dma.speed || info.channels != 1 ) {
		Com_Printf( "[snd] %s: %d Hz, %d ch, %d bit, %.1f s\n", sfx->soundName, info.rate,
					info.channels, info.width * 8, info.rate ? (float)info.samples / info.rate : 0.0f );
	}
#endif
	if ( info.rate != 22050 ) {
		Com_DPrintf(S_COLOR_YELLOW "WARNING: %s is not a 22kHz audio file\n", sfx->soundName);
	}

	samples = Hunk_AllocateTempMemory(info.channels * info.samples * sizeof(short) * 2);

	sfx->lastTimeUsed = Com_Milliseconds()+1;

	// each of these compression schemes works just fine
	// but the 16bit quality is much nicer and with a local
	// install assured we can rely upon the sound memory
	// manager to do the right thing for us and page
	// sound in as needed

#ifdef __PS3__
	// Long mono sounds (voices, ambient loops) go in as 4-bit ADPCM: four
	// times as many fit in the 18 MB cache (PS3_MAX_COMSOUNDMEGS). Without it
	// village2's ambient loops did not fit together and were re-read from the
	// HDD every frame (2 fps). Short effects stay 16-bit.
	if ( info.channels == 1 && info.width > 1 && info.samples >= 2 * info.rate ) {
		sfx->soundCompressed = qtrue;
	}
#endif
	if( info.channels == 1 && sfx->soundCompressed == qtrue) {
		sfx->soundCompressionMethod = 1;
		sfx->soundData = NULL;
		sfx->soundLength = ResampleSfxRaw( samples, info.channels, info.rate, info.width, info.samples, data + info.dataofs );
		S_AdpcmEncodeSound(sfx, samples);
#if 0
	} else if (info.channels == 1 && info.samples>(SND_CHUNK_SIZE*16) && info.width >1) {
		sfx->soundCompressionMethod = 3;
		sfx->soundData = NULL;
		sfx->soundLength = ResampleSfxRaw( samples, info.channels, info.rate, info.width, info.samples, (data + info.dataofs) );
		encodeMuLaw( sfx, samples);
	} else if (info.channels == 1 && info.samples>(SND_CHUNK_SIZE*6400) && info.width >1) {
		sfx->soundCompressionMethod = 2;
		sfx->soundData = NULL;
		sfx->soundLength = ResampleSfxRaw( samples, info.channels, info.rate, info.width, info.samples, (data + info.dataofs) );
		encodeWavelet( sfx, samples);
#endif
	} else {
		sfx->soundCompressionMethod = 0;
		sfx->soundData = NULL;
		sfx->soundLength = ResampleSfx( sfx, info.channels, info.rate, info.width, info.samples, data + info.dataofs, qfalse );
	}

	sfx->soundChannels = info.channels;
	
	Hunk_FreeTempMemory(samples);
	Hunk_FreeTempMemory(data);

	return qtrue;
}

void S_DisplayFreeMemory(void) {
	Com_Printf("%d bytes free sound buffer memory, %d total used\n", inUse, totalInUse);
}

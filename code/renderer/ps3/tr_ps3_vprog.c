/* tr_ps3_vprog.c -- r_ps3_vprog 1: the per-vertex stage work moves from the PPU to the RSX.
 *
 * From IoQuake3-PS3's ps3_renderer.c (Mayo1970, "RSX changes", V1-V5), adapted to iortcw's renderer.
 * A stage's rgbGen / alphaGen / tcGen / tcMods and a surface's deforms are folded into the uniforms of
 * q3_vp_gen (code/gl/shaders/q3_vp_gen.vcg), so ComputeColors, ComputeTexCoords and
 * RB_DeformTessGeometry stop running a loop over every vertex of every stage. ps3gl then reads the raw
 * tess arrays, and the later stages of a surface reuse the first stage's copy of them.
 *
 * Each helper mirrors tr_shade.c / tr_shade_calc.c. Anything the VP cannot reproduce returns qfalse,
 * and that stage (or surface) takes the PPU path, exactly as before. iortcw specifics:
 *   - tcMod swap is an affine map; RB_CalcDiffuseColor has no unlit-side ambient (see the .vcg)
 *   - the PPU keeps: alphaGen normalzfade, tcGen firerise env, fading entities (fadeStartTime),
 *     fire-rise deforms (negative wave frequency), r_primitives other than 2.
 * Back end only (render thread under r_smp). All state in one struct: TOC budget (see the Makefile). */

#include "../tr_local.h"
#include "../../gl/ps3gl_vprog.h"

static struct {
	/* the current surface's deforms: armed by RB_PS3_SurfaceDeforms, sent with every stage of it */
	qboolean deformActive;
	qboolean deformNormal;      /* a slot moves along tess.normal */
	float df[3][16];
} ps3vp;

/* EvalWaveForm (static in tr_shade_calc.c); qfalse on a bad func, which EvalWaveForm drops on */
static qboolean PS3_WaveValue( const waveForm_t *wf, float *out ) {
	const float *table;

	if ( wf->func == GF_NOISE ) {
		*out = wf->base + R_NoiseGet4f( 0, 0, 0, ( tess.shaderTime + wf->phase ) * wf->frequency ) * wf->amplitude;
		return qtrue;
	}
	switch ( wf->func ) {
	case GF_SIN:              table = tr.sinTable; break;
	case GF_TRIANGLE:         table = tr.triangleTable; break;
	case GF_SQUARE:           table = tr.squareTable; break;
	case GF_SAWTOOTH:         table = tr.sawToothTable; break;
	case GF_INVERSE_SAWTOOTH: table = tr.inverseSawToothTable; break;
	default:                  return qfalse;
	}
	*out = wf->base + table[( (int64_t)( ( wf->phase + tess.shaderTime * wf->frequency ) * FUNCTABLE_SIZE ) )
							& FUNCTABLE_MASK] * wf->amplitude;
	return qtrue;
}

/* q3_vp_gen's waveval() weights: b = (c, sin, square, triangle), c = (saw, invsaw, on) */
static qboolean PS3_WaveWeights( genFunc_t func, float *b, float *c ) {
	switch ( func ) {
	case GF_SIN:              b[1] = 1.0f; return qtrue;
	case GF_SQUARE:           b[2] = 1.0f; return qtrue;
	case GF_TRIANGLE:         b[3] = 1.0f; return qtrue;
	case GF_SAWTOOTH:         c[0] = 1.0f; return qtrue;
	case GF_INVERSE_SAWTOOTH: c[1] = 1.0f; return qtrue;
	default:                  return qfalse;   /* noise varies per vertex */
	}
}

/* RB_CalcDiffuseColor's inputs (iortcw: clamp(ambient + max(n . l, 0) * directed), alpha 255) */
static void PS3_DiffuseParams( const trRefEntity_t *ent, ps3gl_vparams_t *p ) {
	int i;

	p->csel[0] = 1.0f;
	for ( i = 0; i < 3; i++ ) {
		p->lamb[i] = ent->ambientLight[i] / 255.0f;
		p->ldir[i] = ent->lightDir[i];
		p->ldirc[i] = ent->directedLight[i] / 255.0f;
	}
}

/* COLOR0 channel = vertex * mul + add (+ the csel terms). Constants are bytes, as in ComputeColors, then b / 255. */
static qboolean PS3_ColorParams( const shaderStage_t *pStage, ps3gl_vparams_t *p ) {
	enum { K_CONST, K_VERTEX, K_ONE_MINUS_VERTEX } kind = K_CONST;
	const trRefEntity_t *ent = backEnd.currentEntity;
	float *mul = p->cmul, *add = p->cadd;
	byte k[4] = { 255, 255, 255, 255 };
	float scale = 1.0f, glow;
	int i, v;

	switch ( pStage->rgbGen ) {
	case CGEN_IDENTITY:
		break;
	case CGEN_EXACT_VERTEX:
		kind = K_VERTEX;
		break;
	case CGEN_VERTEX:
		kind = K_VERTEX;
		scale = tr.identityLight;
		break;
	case CGEN_ONE_MINUS_VERTEX:
		kind = K_ONE_MINUS_VERTEX;
		scale = tr.identityLight;
		break;
	case CGEN_CONST:
		Com_Memcpy( k, pStage->constantColor, 4 );
		break;
	case CGEN_FOG:
		if ( !tr.world ) {
			return qfalse;
		}
		Com_Memcpy( k, &tr.world->fogs[tess.fogNum].colorInt, 4 );
		break;
	case CGEN_WAVEFORM:
		/* RB_CalcWaveColor: identityLight scales every func but noise */
		if ( !PS3_WaveValue( &pStage->rgbWave, &glow ) ) {
			return qfalse;
		}
		if ( pStage->rgbWave.func != GF_NOISE ) {
			glow *= tr.identityLight;
		}
		glow = glow < 0 ? 0 : ( glow > 1 ? 1 : glow );
		k[0] = k[1] = k[2] = ri.ftol( 255 * glow );
		break;
	case CGEN_ENTITY:
	case CGEN_ONE_MINUS_ENTITY:
		if ( !ent ) {
			return qfalse;
		}
		for ( i = 0; i < 4; i++ ) {
			k[i] = ent->e.shaderRGBA[i];
			if ( pStage->rgbGen == CGEN_ONE_MINUS_ENTITY ) {
				k[i] = 255 - k[i];
			}
		}
		break;
	case CGEN_LIGHTING_DIFFUSE:
		/* rgb comes from the VP's lighting term, alpha is 255 */
		if ( !ent ) {
			return qfalse;
		}
		k[0] = k[1] = k[2] = 0;
		PS3_DiffuseParams( ent, p );
		break;
	default:            /* CGEN_IDENTITY_LIGHTING, CGEN_BAD: ComputeColors memsets all four bytes */
		k[0] = k[1] = k[2] = k[3] = tr.identityLightByte;
		break;
	}

	for ( i = 0; i < 4; i++ ) {
		if ( kind == K_CONST ) {
			mul[i] = 0.0f;
			add[i] = k[i] / 255.0f;
		} else if ( kind == K_VERTEX ) {
			mul[i] = i < 3 ? scale : 1.0f;  /* alpha is copied unscaled */
			add[i] = 0.0f;
		} else {
			mul[i] = -scale;
			add[i] = scale;
		}
	}

	switch ( pStage->alphaGen ) {
	case AGEN_SKIP:
		/* one-minus-vertex leaves the previous stage's alpha in svars, which the VP cannot see */
		if ( kind == K_ONE_MINUS_VERTEX ) {
			return qfalse;
		}
		break;
	case AGEN_IDENTITY:
		/* ComputeColors keeps the vertex alpha for rgbGen vertex at identityLight 1 (and identity's 255) */
		if ( pStage->rgbGen == CGEN_IDENTITY || ( pStage->rgbGen == CGEN_VERTEX && tr.identityLight == 1 ) ) {
			break;
		}
		mul[3] = 0.0f;
		add[3] = 1.0f;
		break;
	case AGEN_CONST:
		mul[3] = 0.0f;
		add[3] = pStage->constantColor[3] / 255.0f;
		break;
	case AGEN_WAVEFORM:
		/* RB_CalcWaveAlpha: clamped, then truncated to an int */
		if ( !PS3_WaveValue( &pStage->alphaWave, &glow ) ) {
			return qfalse;
		}
		glow = glow < 0 ? 0 : ( glow > 1 ? 1 : glow );
		v = 255 * glow;
		mul[3] = 0.0f;
		add[3] = v / 255.0f;
		break;
	case AGEN_ENTITY:
	case AGEN_ONE_MINUS_ENTITY:
		if ( !ent ) {
			return qfalse;
		}
		v = ent->e.shaderRGBA[3];
		if ( pStage->alphaGen == AGEN_ONE_MINUS_ENTITY ) {
			v = 255 - v;
		}
		mul[3] = 0.0f;
		add[3] = v / 255.0f;
		break;
	case AGEN_VERTEX:
		mul[3] = 1.0f;
		add[3] = 0.0f;
		break;
	case AGEN_ONE_MINUS_VERTEX:
		mul[3] = -1.0f;
		add[3] = 1.0f;
		break;
	case AGEN_LIGHTING_SPECULAR:
		mul[3] = add[3] = 0.0f;
		p->csel[1] = 1.0f;
		VectorCopy( backEnd.or.viewOrigin, p->vorg );
		break;
	case AGEN_PORTAL:
		/* the PPU path divides by portalRange */
		if ( tess.shader->portalRange <= 0 ) {
			return qfalse;
		}
		mul[3] = add[3] = 0.0f;
		p->csel[2] = 1.0f;
		VectorCopy( backEnd.viewParms.or.origin, p->porg );
		p->porg[3] = 1.0f / tess.shader->portalRange;
		break;
	default:            /* AGEN_NORMALZFADE (Ridah's per-vertex fade) stays on the PPU */
		return qfalse;
	}

	p->csel[3] = p->csel[0] + p->csel[1] + p->csel[2];
	return qtrue;
}

/* (s, t) -> (s * m00 + t * m10 + tx, s * m01 + t * m11 + ty), on rows s[8], t[8] over (st, lm, xyz, 1).
 * L, once a turb is seen, is the 2x2 that the turb offset still goes through. */
static void PS3_TexAffine( float *s, float *t, float *L, float m00, float m10, float tx, float m01, float m11, float ty ) {
	float ns, nt, l0, l1;
	int k;

	for ( k = 0; k < 8; k++ ) {
		ns = s[k] * m00 + t[k] * m10;
		nt = s[k] * m01 + t[k] * m11;
		s[k] = ns;
		t[k] = nt;
	}
	s[7] += tx;
	t[7] += ty;
	if ( L ) {
		l0 = L[0];
		l1 = L[1];
		L[0] = m00 * l0 + m10 * L[2];
		L[1] = m00 * l1 + m10 * L[3];
		L[2] = m01 * l0 + m11 * L[2];
		L[3] = m01 * l1 + m11 * L[3];
	}
}

/* RB_CalcScrollTexCoords: the offset wraps to [0, 1) in double */
static void PS3_TexScroll( float *s, float *t, const float speed[2] ) {
	double ds = speed[0] * tess.shaderTime, dt = speed[1] * tess.shaderTime;

	s[7] += ds - floor( ds );
	t[7] += dt - floor( dt );
}

/* tcGen as rows, then every tcMod composed onto them in shader order; one turb per bundle */
static qboolean PS3_BundleParams( const textureBundle_t *bundle, int b, ps3gl_vparams_t *p ) {
	float *s = p->tm[b], *t = p->tm[b] + 8, *L = NULL, q;
	double now;
	int i;

	switch ( bundle->tcGen ) {
	case TCGEN_IDENTITY:
		break;
	case TCGEN_TEXTURE:
		s[0] = 1.0f;
		t[1] = 1.0f;
		break;
	case TCGEN_LIGHTMAP:
		s[2] = 1.0f;
		t[3] = 1.0f;
		break;
	case TCGEN_VECTOR:
		VectorCopy( bundle->tcGenVectors[0], s + 4 );
		VectorCopy( bundle->tcGenVectors[1], t + 4 );
		break;
	case TCGEN_ENVIRONMENT_MAPPED:
		/* the VP's env coords take lm's place for this bundle */
		s[2] = 1.0f;
		t[3] = 1.0f;
		p->tsel[b] = 1.0f;
		VectorCopy( backEnd.or.viewOrigin, p->vorg );
		break;
	default:            /* fog, firerise env; TCGEN_BAD stops ComputeTexCoords */
		return qfalse;
	}

	for ( i = 0; i < bundle->numTexMods; i++ ) {
		const texModInfo_t *tmi = &bundle->texMods[i];

		switch ( tmi->type ) {
		case TMOD_NONE:
			i = TR_MAX_TEXMODS;
			break;
		case TMOD_SWAP:
			/* RB_CalcSwapTexCoords: (s, t) -> (t, 1 - s) */
			PS3_TexAffine( s, t, L, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f );
			break;
		case TMOD_TURBULENT:
			/* RB_CalcTurbulentTexCoords: func and base unused; a second turb stays on the PPU */
			if ( L ) {
				return qfalse;
			}
			L = p->turbm[b];
			L[0] = L[3] = 1.0f;
			p->tsel[2 + b] = 1.0f;
			now = tmi->wave.phase + tess.shaderTime * tmi->wave.frequency;
			p->turb[b][0] = tmi->wave.amplitude;
			p->turb[b][1] = now - floor( now );
			break;
		case TMOD_ENTITY_TRANSLATE:
			PS3_TexScroll( s, t, backEnd.currentEntity->e.shaderTexCoord );
			break;
		case TMOD_SCROLL:
			PS3_TexScroll( s, t, tmi->scroll );
			break;
		case TMOD_SCALE:
			PS3_TexAffine( s, t, L, tmi->scale[0], 0.0f, 0.0f, 0.0f, tmi->scale[1], 0.0f );
			break;
		case TMOD_STRETCH:
			if ( !PS3_WaveValue( &tmi->wave, &q ) || q == 0 ) {
				return qfalse;
			}
			q = 1.0f / q;
			PS3_TexAffine( s, t, L, q, 0.0f, 0.5f - 0.5f * q, 0.0f, q, 0.5f - 0.5f * q );
			break;
		case TMOD_TRANSFORM:
			PS3_TexAffine( s, t, L, tmi->matrix[0][0], tmi->matrix[1][0], tmi->translate[0],
						   tmi->matrix[0][1], tmi->matrix[1][1], tmi->translate[1] );
			break;
		case TMOD_ROTATE: {
			/* RB_CalcRotateTexCoords, sinTable lookup included */
			double degs = -tmi->rotateSpeed * tess.shaderTime;
			int64_t index = degs * ( FUNCTABLE_SIZE / 360.0f );
			float sinValue = tr.sinTable[index & FUNCTABLE_MASK];
			float cosValue = tr.sinTable[( index + FUNCTABLE_SIZE / 4 ) & FUNCTABLE_MASK];

			PS3_TexAffine( s, t, L, cosValue, -sinValue, 0.5 - 0.5 * cosValue + 0.5 * sinValue,
						   sinValue, cosValue, 0.5 - 0.5 * sinValue - 0.5 * cosValue );
			break;
		}
		default:        /* a bad type ComputeTexCoords drops on */
			return qfalse;
		}
	}
	return qtrue;
}

/* What every q3_vp_gen draw needs from the frame: plain glDrawElements and no fading entity */
static qboolean PS3_VProgUsable( void ) {
	if ( !ps3gl_VProgOn() || !r_ps3_vprog->integer ) {
		return qfalse;
	}
	/* R_DrawElements: 0 means 2 with glLockArraysEXT; strips go through glArrayElement */
	if ( r_primitives->integer != 0 && r_primitives->integer != 2 ) {
		return qfalse;
	}
	/* RB_IterateStagesGeneric's fade scales the svars colors after ComputeColors */
	if ( backEnd.currentEntity && backEnd.currentEntity->e.fadeStartTime ) {
		return qfalse;
	}
	return qtrue;
}

/* One stage, no side effects: the dry run for RB_PS3_SurfaceDeforms and the real one share it */
static qboolean PS3_StageParams( const shaderStage_t *pStage, ps3gl_vparams_t *p ) {
	Com_Memset( p, 0, sizeof( *p ) );
	/* greyscale and fog-modulated colors are per-vertex table work: they stay on the PPU */
	if ( r_greyscale->value || ( tess.fogNum && pStage->adjustColorsForFog != ACFF_NONE ) ) {
		return qfalse;
	}
	if ( !PS3_ColorParams( pStage, p ) || !PS3_BundleParams( &pStage->bundle[0], 0, p ) ) {
		return qfalse;
	}
	/* a single-texture stage's bundle 1 is TCGEN_BAD, and its TEXCOORD1 is never sampled */
	if ( pStage->bundle[1].image[0] && !PS3_BundleParams( &pStage->bundle[1], 1, p ) ) {
		return qfalse;
	}
	return qtrue;
}

static void PS3_SendParams( ps3gl_vparams_t *p ) {
	qboolean normal = p->csel[0] || p->csel[1] || p->tsel[0] || p->tsel[1];

	if ( ps3vp.deformActive ) {
		Com_Memcpy( p->df, ps3vp.df, sizeof( p->df ) );
		normal = normal || ps3vp.deformNormal;
	}
	p->color = tess.vertexColors;
	p->st = tess.texCoords[0][0];
	p->lm = tess.texCoords[0][1];
	p->tc_stride = sizeof( tess.texCoords[0] );
	p->normal = normal ? tess.normal : NULL;
	ps3gl_VertexParams( p );
}

/* RB_IterateStagesGeneric calls this in place of ComputeColors / ComputeTexCoords */
qboolean RB_PS3_StageParams( shaderStage_t *pStage ) {
	ps3gl_vparams_t p;

	if ( !PS3_VProgUsable() || !PS3_StageParams( pStage, &p ) ) {
		/* the PPU deformed nothing if the VP was meant to: see RB_PS3_SurfaceDeforms */
		return qfalse;
	}
	PS3_SendParams( &p );
	return qtrue;
}

/* RB_StageIteratorVertexLitTexture calls this in place of RB_CalcDiffuseColor: it binds raw st, no tcMods */
qboolean RB_PS3_VertexLitParams( void ) {
	ps3gl_vparams_t p;

	/* this iterator never deforms; the last generic surface's slots must not leak in */
	ps3vp.deformActive = qfalse;
	if ( !PS3_VProgUsable() || !backEnd.currentEntity ) {
		return qfalse;
	}
	Com_Memset( &p, 0, sizeof( p ) );
	PS3_DiffuseParams( backEnd.currentEntity, &p );
	p.csel[3] = 1.0f;
	p.cadd[3] = 1.0f;
	p.tm[0][0] = 1.0f;
	p.tm[0][9] = 1.0f;
	PS3_SendParams( &p );
	return qtrue;
}

/* RB_StageIteratorGeneric's call in place of RB_DeformTessGeometry. The VP deforms only if nothing on
 * the PPU reads tess.xyz / tess.normal afterwards and every stage goes through the VP. qfalse: the
 * caller deforms on the PPU as before. */
qboolean RB_PS3_SurfaceDeforms( void ) {
	const shader_t *shader = tess.shader;
	ps3gl_vparams_t p;
	int i, n = 0;

	ps3vp.deformActive = qfalse;
	ps3vp.deformNormal = qfalse;
	if ( !shader->numDeforms || !PS3_VProgUsable() ) {
		return qfalse;
	}
	/* dlights, the fog pass, shadows, the portal clip test, truform and the debug draws read the
	 * deformed arrays */
	if ( tess.dlightBits || tess.fogNum || r_shadows->integer == 2 || r_showtris->integer ||
		 r_shownormals->integer || backEnd.viewParms.isPortal || tess.ATI_tess ) {
		return qfalse;
	}

	Com_Memset( ps3vp.df, 0, sizeof( ps3vp.df ) );
	for ( i = 0; i < shader->numDeforms && n < 3; i++ ) {
		const deformStage_t *ds = &shader->deforms[i];
		float *a = ps3vp.df[n], *b = a + 4, *c = a + 8, *m = a + 12, scale;
		double now;

		switch ( ds->deformation ) {
		case DEFORM_NONE:
			continue;
		case DEFORM_WAVE:
			if ( ds->deformationWave.frequency < 0 ) {
				return qfalse;      /* Ridah's fire rise along the entity's up */
			}
			if ( ds->deformationWave.frequency == 0 ) {
				/* RB_CalcDeformVertexes: one scale for the whole surface */
				if ( !PS3_WaveValue( &ds->deformationWave, &scale ) ) {
					return qfalse;
				}
				a[0] = scale;
			} else {
				if ( !PS3_WaveWeights( ds->deformationWave.func, b, c ) ) {
					return qfalse;
				}
				a[0] = ds->deformationWave.base;
				a[1] = ds->deformationWave.amplitude;
				a[2] = ds->deformationSpread;
				now = ds->deformationWave.phase + tess.shaderTime * ds->deformationWave.frequency;
				b[0] = now - floor( now );
			}
			ps3vp.deformNormal = qtrue;
			break;
		case DEFORM_BULGE:
			/* RB_CalcBulgeVertexes: sinTable over st.s * width + now, one period per 2 pi */
			now = backEnd.refdef.time * 0.001 * ds->bulgeSpeed / ( 2 * M_PI );
			a[1] = ds->bulgeHeight;
			a[3] = ds->bulgeWidth / ( 2 * M_PI );
			b[0] = now - floor( now );
			b[1] = 1.0f;
			ps3vp.deformNormal = qtrue;
			break;
		case DEFORM_MOVE:
			if ( !PS3_WaveValue( &ds->deformationWave, &scale ) ) {
				return qfalse;
			}
			VectorScale( ds->moveVector, scale, m );
			break;
		default:        /* normals (per-vertex noise), autosprite, text, projection shadow */
			return qfalse;
		}
		c[2] = 1.0f;
		n++;
	}
	if ( i < shader->numDeforms ) {
		return qfalse;
	}

	for ( i = 0; i < MAX_SHADER_STAGES && tess.xstages[i]; i++ ) {
		if ( !PS3_StageParams( tess.xstages[i], &p ) ) {
			return qfalse;
		}
	}
	ps3vp.deformActive = n > 0;
	return ps3vp.deformActive;
}

